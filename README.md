Hans - IP over ICMP
===================

Hans tunnels IPv4 through ICMP echo packets (a “ping tunnel”). Useful when Internet access is firewalled but pings are allowed.

http://code.gerade.org/hans/

## Quick start

**Native (Linux):**

```bash
make
# Server (one host)
sudo ./hans -s 10.0.0.0 -p PASSPHRASE -f -d tun0
# Client (another host, or same for local test)
sudo ./hans -c SERVER_IP -p PASSPHRASE -f -d tun1
```

**Docker:** See [docs/docker.md](docs/docker.md). Build once, then run server and/or client separately:

```bash
docker compose build
docker compose up hans-server -d    # server only
docker compose up hans-client -d     # client only (set HANS_SERVER in .env)
```

## Command-line options

| Option | Description |
|--------|-------------|
| **Mode** | |
| `-c server` | Run as **client**. Connect to given server (IP or hostname). |
| `-s network` | Run as **server**. Use given network on tunnel (e.g. `10.0.0.0` → 10.0.0.0/24). |
| **Auth & identity** | |
| `-p passphrase` | Passphrase (required). |
| `-u username` | Drop privileges to this user after setup. |
| `-a ip` | (Client) Request this tunnel IP from the server. |
| **Tunnel** | |
| `-d device` | TUN device name (e.g. `tun0`, `tun1`). |
| `-m mtu` | MTU / max echo size (default 1500). Same on client and server. See [docs/mtu.md](docs/mtu.md). |
| **Server only** | |
| `-r` | Respond to ordinary pings in server mode. |
| **Client only** | |
| `-w polls` | Number of echo requests sent in advance (default 10). 0 disables polling. |
| `-i` | Change echo ID on every request (may help buggy routers). |
| `-q` | Change echo sequence on every request (may help buggy routers). |
| **Performance** | |
| `-B recv,snd` | Socket buffer sizes in bytes (e.g. `262144,262144`). Default 256 KiB each. |
| `-R rate` | Pacing: max send rate in Kbps (0 = disabled). |
| `-W packets` | (Server) Max buffered packets per client (default 20). |
| **IPv6** | |
| `-6` | (Client) Use IPv6 to reach server (AAAA / ICMPv6). |
| **Other** | |
| `-f` | Foreground (do not daemonize). |
| `-v` | Verbose / debug. |
| **Signals** | |
| `SIGUSR1` | Dump packet stats to syslog. |

**Examples:**

```bash
# Server
hans -s 10.0.0.0 -p mypass -f -d tun0
hans -s 10.0.0.0 -p mypass -f -d tun0 -W 64 -B 524288,524288

# Client (IPv4)
hans -c 192.168.1.100 -p mypass -f -d tun1
hans -c 192.168.1.100 -p mypass -f -d tun1 -w 20 -R 80000

# Client (IPv6)
hans -c server.example.com -6 -p mypass -f -d tun1
```

## Running server and client separately

- **Native:** Run the server on one host and the client on another (or same host for local test). No shared config; pass the same passphrase and ensure MTU matches.
- **Docker Compose:** Run only the service you need:
  - `docker compose up hans-server -d` — server only
  - `docker compose up hans-client -d` — client only (set `HANS_SERVER` in `.env` to the server’s IP)
- **Docker (no Compose):** Use `docker run` with `--cap-add=NET_RAW --cap-add=NET_ADMIN --device=/dev/net/tun --network=host`. Full examples: [docs/docker.md](docs/docker.md#running-server-and-client-separately).

## IPv4 and IPv6

- **IPv4 (default):** Client uses `-c SERVER_IP` (no `-6`). Server listens on IPv4; tunnel works over ICMP (IPv4).
- **IPv6:** Client uses `-6` and the server’s IPv6 address or hostname. The **control channel** (POLLs, DATA) then uses ICMPv6; the **tunnel payload** is still IPv4 (10.0.0.0/24). Server is dual-stack and accepts both IPv4 and IPv6 clients.

**How to test IPv6**

1. **Prerequisites:** Server host has a routable IPv6 address; ICMPv6 is allowed between client and server (firewall / security group).
2. **Native:** Start server as usual. Start client with `-6` and server IPv6 or hostname (with AAAA):
   ```bash
   hans -c 2001:db8::1 -6 -p mypass -f -d tun1
   # or: hans -c server.example.com -6 -p mypass -f -d tun1
   ```
3. **Docker:** Compose does not pass `-6` by default. Run the client with plain `docker run` and `-6`:
   ```bash
   docker run -d --name hans-client \
     --cap-add=NET_RAW --cap-add=NET_ADMIN \
     --device=/dev/net/tun --network=host \
     hans:latest -c SERVER_IPV6 -6 -p YOUR_PASSPHRASE -f -d tun1
   ```
4. **Verify:** Same as IPv4 — ping and iperf3 over the tunnel (e.g. `ping 10.0.0.100`, `iperf3 -c 10.0.0.100`). Tunnel addresses stay IPv4.

More: [docs/docker.md](docs/docker.md#testing-ipv4-vs-ipv6).

For **VPN / many users**: fairness and bandwidth are both important. See [docs/fairness-and-bandwidth.md](docs/fairness-and-bandwidth.md) for why throughput is limited (~137 Mbits/sec vs 1.6 Gbit/s), per-flow fairness (round-robin), tuning (`-w`/`-W`), and multiplexing/QUIC/KCP.

## Performance tuning on Ubuntu

- **Socket buffers:** Use `-B recv,snd` (bytes). Default is 256 KiB each. For higher throughput (e.g. 80+ Mbps), try `-B 524288,524288` (512 KiB). If you use larger values, raise system limits first:
  ```bash
  sudo sysctl -w net.core.rmem_max=1048576
  sudo sysctl -w net.core.wmem_max=1048576
  ```
- **Pacing:** Use `-R rate_kbps` to cap send rate and smooth bursts (e.g. `-R 80000` for 80 Mbps). Helps avoid kernel or middlebox drops under burst.
- **Server queue:** Use `-W packets` (server only) to allow more buffered packets per client. Default 20; increase (e.g. `-W 64`) if you see `dropped_queue_full` in stats.
- **Stats:** Send `SIGUSR1` to the hans process to dump packet counters to syslog: `kill -USR1 <pid>`.
- **ulimit:** If you run many FDs later (e.g. multiplexing), ensure `ulimit -n` is sufficient.
- **NIC offloads:** Leave on unless you are debugging; disabling can increase CPU use.

## Optional congestion control

A congestion module (see [src/congestion.h](src/congestion.h)) is provided as a stub: it can report sent bytes, loss, and RTT. When fully wired, it would drive pacing or rate (e.g. AIMD or token bucket with feedback). Off by default; enable via config or future `-C` option.

## Docker

Build once, then run **server and/or client separately** as needed. Full instructions: [docs/docker.md](docs/docker.md).

```bash
docker compose build
# Server only
docker compose up hans-server -d
# Client only (set HANS_SERVER in .env to server’s IP)
docker compose up hans-client -d
# Or both (e.g. local test)
docker compose up -d
```

Containers need `NET_RAW`, `NET_ADMIN`, `--device=/dev/net/tun`, and `--network=host`. See [docs/docker.md](docs/docker.md) for plain `docker run` examples and WSL notes.

## Authentication

- **Legacy (SHA1):** Old clients send a 5-byte connection request; server expects 20-byte SHA1 challenge response. Still supported.
- **HMAC-SHA256:** New clients send a 6-byte connection request with version 2; server expects 32-byte HMAC-SHA256(challenge) response. Enabled by default for new builds. Backward compatible with legacy servers (server accepts both 5- and 6-byte requests).

## IPv6 support

- **Client:** Use `-6` to connect to the server via IPv6 (ICMPv6). Server can be specified by IPv6 address or hostname (AAAA). Without `-6`, the client uses IPv4 (A record).
- **Server:** Listens on both IPv4 and IPv6 by default; accepts clients from either. Tunnel payload is still IPv4 (TUN device carries IPv4). On environments where the kernel does not support `IPV6_CHECKSUM` (e.g. some WSL/Docker setups), hans uses a userspace ICMPv6 checksum.

## Troubleshooting packet loss

1. **Check app-level counters**  
   Send `SIGUSR1` to the hans process and check syslog for `stats: ... dropped_send_fail=... dropped_queue_full=...`.  
   - High `dropped_send_fail`: kernel send buffer or pacing; increase `-B` or reduce rate.  
   - High `dropped_queue_full`: server has no poll ids (client not sending POLLs fast enough); increase `-W` or client `-w` (polls in advance).

2. **Kernel socket buffers**  
   Default raw ICMP buffers may be small. Use `-B recv,snd` and raise `net.core.rmem_max` / `net.core.wmem_max` if needed.

3. **Queue full (server→client heavy)**  
   Server can only send when the client has sent a POLL. If traffic is mostly server→client, increase client `-w` (e.g. 20) and server `-W` (e.g. 64).

4. **Pacing**  
   Enable `-R rate_kbps` to smooth bursts and avoid middlebox/kernel drops.

5. **MTU**  
   Use `-m mtu` to match path MTU (default 1500). If path MTU is smaller, reduce `-m` to avoid fragmentation. See [docs/mtu.md](docs/mtu.md) for typical values and path MTU discovery.

6. **Reproduce with netem**  
   Use `tc qdisc add dev eth0 root netem delay 20ms loss 1%` to simulate loss; run iperf3 over the tunnel and compare stats before/after. See [docs/benchmark.md](docs/benchmark.md).

## Changes and features (this fork)

Summary of additions and edits; see [CHANGES](CHANGES) for details.

- **Metrics:** Packet/byte counters and drop reasons; dump on `SIGUSR1`.
- **Socket buffers:** Configurable `-B recv,snd`; default 256 KiB.
- **Batching:** Batch receive on ICMP socket to reduce syscalls.
- **Pacing:** Optional `-R rate_kbps` token bucket.
- **Server queue:** `-W packets` (server); default 20.
- **IPv6:** Client `-6`; server dual-stack (IPv4 + IPv6). Userspace ICMPv6 checksum fallback when `IPV6_CHECKSUM` is unsupported (e.g. WSL/Docker).
- **Docker:** Dockerfile and docker-compose; run server and client separately; see [docs/docker.md](docs/docker.md).
- **Auth:** HMAC-SHA256 (version 2) with legacy SHA1 support.
- **MTU:** `-m mtu`; [docs/mtu.md](docs/mtu.md).
- **Multiplexing:** NUM_CHANNELS (default 4) with per-channel POLL queues; client sends maxPolls×num_channels POLLs for higher in-flight capacity and throughput. See [docs/multiplexing.md](docs/multiplexing.md).
- **Stubs/docs:** Sequence/retransmit ([docs/sequence.md](docs/sequence.md)), congestion ([src/congestion.h](src/congestion.h)).
