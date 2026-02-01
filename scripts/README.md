# Hans setup scripts

## Env example

Copy [../.env.example](../.env.example) to `.env` and fill values, or use the Python script to generate `.env`:

```bash
cp .env.example .env
# Edit .env with your passphrase, server address, etc.
```

## Python setup (fill required values)

Use the Python script to fill in the required options and generate a `.env` file for docker-compose.

**Requirements:** Python 3.6+ (no extra packages).

**Interactive:**

```bash
# From repo root
python scripts/setup_hans.py
```

You will be prompted for:

- **Role:** `1` or `s` = server, `2` or `c` = client (default: 2)
- **Passphrase:** type your own, or choose “Generate random passphrase? [y/N]” to get a random one (shown once; save it)
- **Server address:** (client only; IP or hostname)
- **Tunnel network:** (server only; e.g. `10.0.0.0`)

The script writes `.env` in the current directory. Then run:

```bash
docker compose build
docker compose up hans-server -d   # or hans-client -d
```

**Role shortcuts:** `1` / `s` = server, `2` / `c` = client (short is easier than typing “server” or “client”).

**Random passphrase:**

```bash
# Generate random passphrase (shown once)
python scripts/setup_hans.py --random-pass
python scripts/setup_hans.py -r c -s 192.168.1.100 --random-pass

# Custom length (default 24)
python scripts/setup_hans.py -r s --random-pass --pass-length 32
```

**Non-interactive (e.g. CI):**

```bash
python scripts/setup_hans.py -r c -s 192.168.1.100 -p mypass -o .env
python scripts/setup_hans.py -r s -n 10.0.0.0 -p mypass -o .env
```

**Options:**

- `-r`, `--role` – `1`/`s`/server or `2`/`c`/client
- `-p`, `--passphrase` – passphrase (avoid in shell history)
- `--random-pass` – generate random passphrase (shown once)
- `--pass-length N` – length of random passphrase (default 24)
- `-s`, `--server` – server address (client only)
- `-n`, `--network` – tunnel network for server (default 10.0.0.0)
- `-o`, `--env` – output file (default .env)
- `--dry-run` – print variables only, do not write `.env`

**Security:** Do not commit `.env` if it contains your real passphrase. `.env` is in `.gitignore`.
