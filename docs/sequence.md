# Sequence and retransmission

## Overview

Optional sequence numbers and NACK-based retransmission can reduce the impact of packet loss. When enabled (see below), DATA packets carry a 4-byte sequence number; the receiver tracks the last seen sequence and sends TYPE_NACK when it detects a gap. The sender keeps a small send buffer and resends on NACK.

## Enabling

Set `SEQUENCE_ENABLED` to 1 in [src/config.h](src/config.h) and rebuild. When enabled:

- Data packets use TYPE_DATA_SEQ (10) with payload `[sequence uint32_t][data]`.
- Receiver sends TYPE_NACK (11) with a gap list when it sees a sequence gap.
- Sender keeps the last `SEND_BUF_SIZE` (default 64) packets and resends on NACK.

## Backward compatibility

Old peers that do not support TYPE_DATA_SEQ/TYPE_NACK ignore these packet types. New peers negotiate sequence support via handshake version (future) or by using TYPE_DATA_SEQ only when both ends are configured with SEQUENCE_ENABLED.

## Pacing

When using retransmission, enable pacing (`-R rate_kbps`) so resends do not burst and cause more loss.
