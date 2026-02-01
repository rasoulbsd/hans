# Benchmark and packet loss reproduction

This document describes how to reproduce and measure tunnel throughput and packet loss, and how to capture app-level and OS-level counters.

## Prerequisites

- Two Ubuntu machines or containers (server and client)
- `hans` built and installed (or run from build directory)
- `iperf3` installed on both ends
- Optional: `tc` (netem) for adding latency/loss; `netstat` or `ss` for OS stats

## Basic throughput test

1. **Start server** (as root or with CAP_NET_RAW):
   ```bash
   ./hans -s 10.0.0.0 -p passphrase -f -B 524288,524288
   ```
   Tunnel network will be 10.0.0.0/24; server tunnel IP 10.0.0.1, client will get e.g. 10.0.0.100.

2. **Start client** (pointing at server’s real IP):
   ```bash
   ./hans -c <SERVER_REAL_IP> -p passphrase -f -B 524288,524288
   ```

3. **Run iperf3** over the tunnel (use the client’s *tunnel* IP, e.g. 10.0.0.100):
   - On server: `iperf3 -s`
   - On client (or another host that can reach the tunnel): `iperf3 -c 10.0.0.100 -t 120 -P 4`
   - For **more even throughput** and fewer 0 KB/s streams, use **-P 2** or **-P 4** (see “Why 0 KB/s?” below). Single flow: `iperf3 -c 10.0.0.100 -t 120` (no -P).
   - For UDP at ~80 Mbps: `iperf3 -u -c 10.0.0.100 -b 80M -t 120`

4. **Dump app stats** (on server or client process):
   ```bash
   kill -USR1 <hans_pid>
   ```
   Check syslog for lines like:
   `stats: packets_sent=... packets_received=... bytes_sent=... bytes_received=... dropped_send_fail=... dropped_queue_full=...`

## With artificial loss (netem)

On the **server** or **client** host (on the interface used for ICMP):

```bash
# Add 20 ms delay and 1% loss
sudo tc qdisc add dev eth0 root netem delay 20ms loss 1%

# Run iperf3 as above, then inspect stats (SIGUSR1) and iperf3 loss report

# Remove netem
sudo tc qdisc del dev eth0 root
```

## OS-level counters

- **ICMP stats:** `netstat -s` (ICMP section) or `cat /proc/net/snmp` (Ip: InReceives, etc.).
- **Socket buffers:** After increasing with `-B`, you may need to raise system limits:
  ```bash
  # Optional, if -B values larger than default max
  sudo sysctl -w net.core.rmem_max=1048576
  sudo sysctl -w net.core.wmem_max=1048576
  ```

## Performance tuning options

- **Socket buffers:** `-B recv,snd` (e.g. `-B 524288,524288` for 512 KB).
- **Pacing:** `-R rate_kbps` to cap send rate (e.g. `-R 80000` for 80 Mbps).
- **Server queue:** `-W packets` (e.g. `-W 64`) to allow more buffered packets per client.

## Why do I see 0 KB/s on some iperf3 streams?

The tunnel is **one logical pipe** per client: the server has a single FIFO queue of packets to that client and sends **one packet per POLL reply**. With **8 parallel streams** (`-P 8`), all streams share that one queue. The kernel delivers segments from all TCP connections into the TUN; one connection often gets many segments in a row. So one stream’s packets can sit at the front of the queue and get most of the send slots, while others get almost none → you see one stream at ~40–50 Mbits/sec and several at 0 KB/s.

**What we do:** The default **recv batch size is 1** (one ICMP packet per `select()`), matching original hans / petrich/hans. That keeps processing interleaved across flows and avoids extreme 0 KB/s and “control socket has closed unexpectedly”. If you build with `HANS_RECV_BATCH_MAX=32` in `config.h` (or `-DHANS_RECV_BATCH_MAX=32`), throughput can increase but fairness drops and you may see 0 KB/s and iperf3 control-socket timeouts again.

**What you can do:**

- Use **fewer parallel streams**: `iperf3 -c 10.0.0.100 -t 30 -P 2` or `-P 4` for more even distribution.
- Or a **single flow**: `iperf3 -c 10.0.0.100 -t 30` (no `-P`) for one stream and predictable throughput.

Total bandwidth (SUM) is similar; with -P 2 or -P 4 the per-stream rates are less extreme.

## Interpreting stats

- **dropped_send_fail:** `sendto()` failed or pacing denied send; increase socket buffers or reduce rate.
- **dropped_queue_full:** Server had no poll id and pending queue was full; increase `-W` or ensure client sends POLLs (e.g. use `-w 10` or higher).
