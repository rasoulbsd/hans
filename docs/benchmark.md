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

## Interpreting stats

- **dropped_send_fail:** `sendto()` failed or pacing denied send; increase socket buffers or reduce rate.
- **dropped_queue_full:** Server had no poll id and pending queue was full; increase `-W` or ensure client sends POLLs (e.g. use `-w 10` or higher).
