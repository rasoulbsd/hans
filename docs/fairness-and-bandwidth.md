# Fairness and bandwidth (VPN / many users)

This doc explains why throughput and fairness look the way they do, and what helps to get closer to your server’s capacity (e.g. 1.6 Gbit/s) and fair shares across users/streams.

## Why throughput is limited (~137 Mbits/sec vs 1.6 Gbit/s)

The hans tunnel is **POLL-based**: the client sends ICMP echo requests (POLLs), and the server can send **one data packet per POLL reply**. So:

- **In-flight packets** = number of POLLs the client has sent that the server hasn’t yet “used” for a reply.
- **Throughput** ≈ (in-flight packets) × (packet size) / RTT.

With `-w 20` (20 polls in advance) and RTT ~20 ms and 1500-byte packets:  
20 × 1500 × 8 / 0.02 ≈ **12 Mbits/sec** per “channel”. To reach 1.6 Gbit/s you’d need on the order of **100+ in-flight packets** or **multiple parallel channels** (multiplexing).

**What we already do:** Compose uses `-w 20` and `-W 64`. Increasing `-w` (e.g. 64 or 100) and `-W` (e.g. 128 or 200) gives more in-flight packets and can push throughput higher (and reduce retransmissions). Try:

- Client: `-w 64` or `-w 100`
- Server: `-W 128` or `-W 200`

Raise kernel socket limits (`net.core.rmem_max`, `net.core.wmem_max`) if you use larger `-B`.

## Why one stream gets 51 MB and another 7 MB (fairness)

There is **one FIFO queue per client**. All flows (e.g. 8 iperf3 streams) share that queue. Whichever flow’s packets sit at the front more often gets most of the send slots → uneven rates (51 MB vs 7 MB).

**What we do:** The server can use **per-flow queues** and **round-robin** when sending: parse the inner IP packet (5-tuple: src/dst IP, protocol, src/dst port), hash to a flow id, maintain one queue per flow (e.g. 16), and when a POLL arrives send from the next non-empty queue in round-robin order. That spreads send slots across flows and improves fairness (see [Per-flow fairness](#per-flow-fairness) below).

## Do QUIC, KCP, multiplexing help?

- **QUIC / KCP** – These are **transport** protocols. They run **over** the tunnel (your users’ traffic: e.g. browser QUIC to a server). Hans tunnels IP; it doesn’t replace TCP with QUIC inside the tunnel. So:
  - **QUIC/KCP over the VPN:** Can help with loss recovery and multi-stream behavior for that traffic; they don’t increase the tunnel’s raw capacity.
  - **QUIC/KCP inside hans:** Would mean redesigning the tunnel to use UDP and QUIC/KCP instead of ICMP; that’s a different project.

- **Multiplexing** – **Yes.** Multiple logical “channels” (e.g. several POLL/reply streams, each with its own in-flight window) multiply effective in-flight packets and can get you much closer to 1.6 Gbit/s. Each channel can also be assigned to a flow or user for fairness. See [docs/multiplexing.md](multiplexing.md). Full implementation is future work; the design and config stub (`NUM_CHANNELS`) are in place.

- **Per-flow fairness** – Implemented: round-robin across flow queues so multiple streams/users get more equal shares (see below).

## Per-flow fairness

When **per-flow fairness** is enabled (default in this fork), the server:

1. Parses the inner IP packet (IPv4 header; for TCP/UDP uses 5-tuple, else 3-tuple).
2. Hashes to a flow index (0 … N−1, N = 16 by default).
3. Keeps **N queues per client** (one per flow).
4. When a POLL arrives, sends from the **next non-empty queue in round-robin order** instead of always from the front of a single queue.

So multiple TCP streams (or multiple users’ traffic) share the tunnel more fairly. You should see less disparity (e.g. no 51 MB vs 7 MB) and more even per-stream rates.

Config: `HANS_NUM_FLOW_QUEUES` in [src/config.h](src/config.h) (default 16). Set to **1** to disable (single FIFO, original behavior). Rebuild after changing.

## Roadmap (short → long term)

1. **Tuning (now)** – Increase `-w` and `-W` in Compose or CLI for more in-flight packets and higher throughput. See [docs/docker.md](docker.md) and [docs/benchmark.md](benchmark.md).
2. **Per-flow fairness (done)** – Round-robin across flow queues for fairer multi-stream / multi-user behavior.
3. **Multiplexing (future)** – Multiple channels (e.g. 4–8) with separate POLL/reply streams to scale toward 1.6 Gbit/s and better multi-user capacity. See [docs/multiplexing.md](multiplexing.md).

For a **VPN with many users**, fairness and bandwidth both matter: use the current per-flow fairness and higher `-w`/`-W`; when you need more than ~100–200 Mbits/sec per client, multiplexing is the next step.
