# MTU handling

## Setting MTU

Use `-m mtu` to set the maximum echo packet size (the MTU of the path between client and server). This must match on both ends. Default is 1500 (Ethernet).

Example:
```bash
hans -c server -p passphrase -m 1500
hans -s 10.0.0.0 -p passphrase -m 1500
```

The program subtracts the ICMP + tunnel header overhead from `-m` to get the tunnel payload size. So with `-m 1500`, the tunnel payload is about 1500 - 28 (IP+ICMP) - 5 (TunnelHeader) = 1467 bytes.

## Typical values

- **1500** – Ethernet, most networks.
- **1492** – PPPoE.
- **9000** – Jumbo frames (if path supports it).

## Path MTU discovery

ICMP echo does not fragment; if the payload is larger than the path MTU, the packet may be dropped or you may get ICMP "fragmentation needed". To discover path MTU:

1. Use `ping -M do -s <size> <server>` (Linux) to find the largest size that works without fragmentation.
2. Set `-m` to that size (or slightly smaller to be safe).

Example:
```bash
ping -M do -s 1472 192.168.1.1   # 1472 + 28 (IP+ICMP) = 1500
```

If you see "Frag needed" or loss, reduce the size.

## Troubleshooting

- **Symptoms of MTU too large:** Packet loss, especially for larger transfers; connections that stall.
- **Fix:** Reduce `-m` (e.g. try 1400 or 1280) so the full packet fits the path MTU.
