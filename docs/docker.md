# Running Hans in Docker

## Env and Python setup

**Option A – copy example and edit:** Copy [.env.example](../.env.example) to `.env` and set `HANS_PASSPHRASE`, `HANS_SERVER` (client), `HANS_NETWORK` (server).

**Option B – interactive script:** From the repo root, run the setup script to fill passphrase, server address, and tunnel network; it writes `.env` for docker-compose:

```bash
python scripts/setup_hans.py
```

Use `1`/`s` for server, `2`/`c` for client; you can choose to generate a random passphrase. Then build and run as below. See [scripts/README.md](scripts/README.md) for all options.

### Local testing (server and client on the same machine)

You use **one** `.env` file for both services. Docker Compose loads `.env` once; the server uses `HANS_NETWORK` and `HANS_PASSPHRASE`, the client uses `HANS_SERVER` and `HANS_PASSPHRASE`.

1. Copy `.env.example` to `.env` (or run `python scripts/setup_hans.py` and pick server then client if you want both in one go).
2. Set `HANS_SERVER=127.0.0.1` so the client talks to the server on localhost.
3. Start both:

   ```bash
   docker compose up -d
   ```

   That starts `hans-server` and `hans-client`; no need for two `.env` files or separate configs.

## Build

```bash
docker compose build
# or
docker build -t hans:latest .
```

## Throughput (iperf3 / bandwidth)

Compose is configured for better throughput: **server** uses `-B 524288,524288` (512 KiB socket buffers) and `-W 64` (server queue); **client** uses `-B 524288,524288` and `-w 20` (polls in advance). That should reduce retransmissions and improve bitrate compared to defaults.

On each **VPS host** (before or after starting containers), raise kernel socket limits so the larger buffers take effect:

```bash
sudo sysctl -w net.core.rmem_max=1048576
sudo sysctl -w net.core.wmem_max=1048576
```

To make these persistent: add the same lines to `/etc/sysctl.conf` or a file under `/etc/sysctl.d/`. See [docs/benchmark.md](benchmark.md) for iperf3 test steps and tuning.

## Running server and client separately

You can run **only the server**, **only the client**, or **both** on different hosts.

### With Docker Compose

- **Server only** (from repo root; uses `.env` for `HANS_NETWORK`, `HANS_PASSPHRASE`):
  ```bash
  docker compose up hans-server -d
  ```

- **Client only** (ensure `.env` has `HANS_SERVER` set to the server’s real IP or hostname):
  ```bash
  docker compose up hans-client -d
  ```

- **Both** (e.g. local test):
  ```bash
  docker compose up -d
  ```

To stop only one service:

```bash
docker compose stop hans-server
# or
docker compose stop hans-client
```

### With plain Docker (no Compose)

Use these when you are not using Compose or when running server and client on different machines. Replace `hans:latest` if you use another image name.

**Server only (IPv4):**

```bash
docker run -d --name hans-server \
  --cap-add=NET_RAW --cap-add=NET_ADMIN \
  --device=/dev/net/tun --network=host \
  hans:latest -s 10.0.0.0 -p YOUR_PASSPHRASE -f -d tun0
```

**Client only (IPv4):**

```bash
docker run -d --name hans-client \
  --cap-add=NET_RAW --cap-add=NET_ADMIN \
  --device=/dev/net/tun --network=host \
  hans:latest -c SERVER_IP -p YOUR_PASSPHRASE -f -d tun1
```

**Client only (IPv6):** use `-6` and the server’s IPv6 address or hostname:

```bash
docker run -d --name hans-client \
  --cap-add=NET_RAW --cap-add=NET_ADMIN \
  --device=/dev/net/tun --network=host \
  hans:latest -c SERVER_IPV6_OR_HOSTNAME -6 -p YOUR_PASSPHRASE -f -d tun1
```

Add options as needed (e.g. `-B 524288,524288`, `-R 80000`, `-w 20`, `-m 1500`). See [README](../README.md) for all options.

## Server (reference)

Run the server (listens on host network for ICMP):

```bash
docker compose up hans-server -d
# Or with custom passphrase and device (plain docker):
docker run --rm -d --name hans-server \
  --cap-add=NET_RAW --cap-add=NET_ADMIN --device=/dev/net/tun --network=host \
  hans:latest -s 10.0.0.0 -p mypass -f -d tun0
```

The server uses network `10.0.0.0/24` by default; tunnel device is `tun0` (or set via `-d`). Server listens on both IPv4 and IPv6 by default.

## Client (reference)

Run the client (connects to server’s real IP):

```bash
# With Compose (set HANS_SERVER in .env first)
docker compose up hans-client -d

# Plain docker – IPv4
docker run --rm -d --name hans-client \
  --cap-add=NET_RAW --cap-add=NET_ADMIN --device=/dev/net/tun --network=host \
  hans:latest -c 192.168.1.100 -p mypass -f -d tun1

# Plain docker – IPv6
docker run --rm -d --name hans-client \
  --cap-add=NET_RAW --cap-add=NET_ADMIN --device=/dev/net/tun --network=host \
  hans:latest -c 2001:db8::1 -6 -p mypass -f -d tun1
```

## Requirements

- **Capabilities:** `NET_RAW` (raw ICMP) and `NET_ADMIN` (TUN device).
- **TUN device:** The container must have access to `/dev/net/tun`. The compose file passes it from the host via `devices: - /dev/net/tun`. If you see *"could not create tunnel device: No such file or directory"*, the host has no TUN device (see below).
- **Network:** `network_mode: host` is used so the container shares the host network and can send/receive ICMP and create TUN devices. For non-host networking you would need to expose ICMP (not typical) and handle TUN differently.

### WSL (Windows Subsystem for Linux)

When running Docker from WSL2:

1. **Docker Desktop (WSL2 backend):** Containers run in Docker’s Linux VM, not inside WSL. That VM must have `/dev/net/tun`. If the error persists, try running hans **directly in WSL** (no Docker): install build deps and run `./hans` so it uses WSL’s kernel, which usually has TUN.
2. **Docker Engine inside WSL2:** The “host” is WSL2. Check that TUN exists:
   ```bash
   ls -l /dev/net/tun
   ```
   If it’s missing, load the module (if built as module): `sudo modprobe tun`. On some WSL2 kernels TUN is built-in and the node should exist; if not, you may need a WSL2 kernel with `CONFIG_TUN=m` or `=y`.
3. **Run without Docker:** From the repo in WSL, build with `make` and run e.g. `sudo ./hans -c <server> -p <passphrase> -f -d tun1`. That uses WSL’s `/dev/net/tun` and often works when Docker doesn’t.

## Testing IPv4 vs IPv6

- **IPv4 (default):** Compose client uses `HANS_SERVER` (e.g. `192.168.1.100` or `127.0.0.1`). No `-6` flag. Server and client communicate over IPv4.
- **IPv6:** Run the client with `-6` and the server’s IPv6 address. Compose does not pass `-6` by default; use plain `docker run` for the client with `-6` and `-c <server_ipv6>` (see “Client only (IPv6)” above). Ensure the server host has an IPv6 address and that ICMPv6 is allowed.

To test IPv4 only: start server and client without `-6`; ping over the tunnel (e.g. client gets `10.0.0.100`).  
To test IPv6 only: start server, then start client with `-6 -c <server_ipv6>`; ping over the tunnel the same way (tunnel payload is still IPv4).

## Healthcheck

To enable a simple healthcheck (ping over tunnel), uncomment the `healthcheck` block in `docker-compose.yml` for `hans-server` and ensure the client is up with tunnel IP `10.0.0.100` (or adjust the ping target).

## Plugging tunnel into host or other containers

With `network_mode: host`, the TUN device is created on the host. Routes and firewall rules on the host apply. To give another container access you would typically run without host network and use a different strategy (e.g. proxy or shared network namespace).
