# Build stage
FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    make \
    libssl-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN make clean 2>/dev/null || true
RUN make

# Runtime stage
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
    iproute2 \
    net-tools \
    iputils-ping \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /src/hans /usr/local/bin/hans

# Need NET_RAW for raw ICMP; NET_ADMIN for TUN
# Run as root for TUN device setup (or use --cap-add=NET_ADMIN,NET_RAW)
ENTRYPOINT ["/usr/local/bin/hans"]
