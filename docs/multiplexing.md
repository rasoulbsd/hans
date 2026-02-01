# Multiplexing for higher bandwidth

Multiple logical **channels** (POLL/reply streams) increase in-flight capacity and throughput. Each channel has its own POLL queue on the server; the server sends data round-robin across channels.

## How it works

- **Server:** Assigns each incoming POLL to a channel by `channel = echoId % NUM_CHANNELS`. Keeps one POLL queue per channel per client. When sending data, takes the next POLL from channels in round-robin order (`getNextPollFromChannels`).
- **Client:** Receives `num_channels` (1–255) in CONNECTION_ACCEPT (5-byte payload: 4 bytes tunnel IP + 1 byte num_channels). Sends **maxPolls × num_channels** POLLs initially (and one POLL per DATA received / per timeout), so all channels get POLLs.
- **Effect:** With NUM_CHANNELS=4 and client -w 20, the client sends 80 POLLs (20×4), so the server can have up to 80 in-flight packets (4× before). Throughput scales with in-flight packets, so multiplexing plus higher -w gets you closer to 1.6 Gbit/s.

## Configuration

- **NUM_CHANNELS** in [src/config.h](src/config.h): number of channels (default **4**). Set to **1** for original single-channel behavior. Rebuild after changing.
- **Protocol:** CONNECTION_ACCEPT may be 4 bytes (IP only, backward compatible) or 5 bytes (IP + num_channels). Old clients accept 4 bytes only; new clients accept 4 or 5 and use num_channels to send more POLLs.

## Compatibility

- **Server NUM_CHANNELS=1:** Sends 4-byte CONNECTION_ACCEPT; any client works.
- **Server NUM_CHANNELS>1:** Sends 5-byte CONNECTION_ACCEPT; new clients send maxPolls×numChannels POLLs; old clients (expect 4 bytes) may fail on CONNECTION_ACCEPT. Use NUM_CHANNELS=1 when talking to old clients.
- **Client:** Accepts 4 or 5 bytes; if 5, uses num_channels for initial POLL count.

## Ordering

Packets on the same channel are sent in order. Ordering across channels is not guaranteed (round-robin). For TCP over the tunnel this is fine; reordering is handled by TCP.

## Metrics

Per-channel stats (e.g. in SIGUSR1 dump) can be added later. For now, aggregate stats apply to all channels.
