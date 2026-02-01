# Multiplexing for higher bandwidth

## Design

Multiple parallel "channels" (e.g. separate raw ICMP sockets or multiple id/seq streams) can increase throughput by spreading packets across channels. Packets are scheduled round-robin or by channel load; the receiver reassembles by channel id and optional per-channel sequence.

## Configuration

`NUM_CHANNELS` in [src/config.h](src/config.h) (default 1). When increased (e.g. 4), the implementation would:

- Use multiple Echo (and optionally Echo6) sockets or multiple id/seq pairs per socket.
- Round-robin outbound DATA packets across channels.
- Receive on all channel FDs and deliver in order per channel (or accept per-channel ordering).

## Metrics

Per-channel counters (packets_sent, packets_received, dropped) would be exposed in the stats dump (SIGUSR1) when multiplexing is enabled.

## Ordering

Per-channel ordering: packets on the same channel are delivered in order; ordering across channels is not guaranteed unless the implementation adds a reorder buffer.
