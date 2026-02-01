/*
 *  Hans - IP over ICMP
 *  Copyright (C) 2009 Friedrich Schöller <hans@schoeller.se>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "echo6.h"
#include "exception.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip6.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <syslog.h>
#include <string.h>

#ifndef IPPROTO_IPV6
#define IPPROTO_IPV6 41
#endif

#ifndef IPPROTO_ICMPV6
#define IPPROTO_ICMPV6 58
#endif
#ifndef ICMP6_ECHO_REQUEST
#define ICMP6_ECHO_REQUEST 128
#endif
#ifndef ICMP6_ECHO_REPLY
#define ICMP6_ECHO_REPLY 129
#endif
#ifndef IPV6_CHECKSUM
#define IPV6_CHECKSUM 7
#endif

Echo6::Echo6(int maxPayloadSize, int recvBufSize, int sndBufSize)
{
    fd = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
    if (fd == -1)
        throw Exception("creating icmp6 socket", true);

    /* offset 2 = checksum field in ICMPv6 header; kernel fills it when supported */
    int csum_offset = 2;
    kernelChecksum_ = (setsockopt(fd, IPPROTO_IPV6, IPV6_CHECKSUM, &csum_offset, sizeof(csum_offset)) == 0);
    if (!kernelChecksum_)
        syslog(LOG_WARNING, "IPV6_CHECKSUM not supported (%s), using userspace checksum", strerror(errno));
    cachedSrcValid_ = false;

    if (recvBufSize > 0)
    {
        if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &recvBufSize, sizeof(recvBufSize)) == -1)
            syslog(LOG_WARNING, "SO_RCVBUF %d: %s", recvBufSize, strerror(errno));
    }
    if (sndBufSize > 0)
    {
        if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndBufSize, sizeof(sndBufSize)) == -1)
            syslog(LOG_WARNING, "SO_SNDBUF %d: %s", sndBufSize, strerror(errno));
    }

#ifdef WIN32
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
        syslog(LOG_WARNING, "O_NONBLOCK: %s", strerror(errno));
#endif

    bufferSize = maxPayloadSize + headerSize();
    sendBuffer.resize(bufferSize);
    receiveBuffer.resize(bufferSize);
}

Echo6::~Echo6()
{
    if (fd >= 0)
        close(fd);
}

int Echo6::headerSize()
{
    return sizeof(Icmp6Header);
}

/* One's complement sum for ICMPv6 checksum (RFC 2463): pseudo-header + ICMPv6 message */
uint16_t Echo6::icmp6Checksum(const struct in6_addr &src, const struct in6_addr &dst,
                              const void *msg, size_t msgLen)
{
    size_t i;
    const unsigned char *p;
    uint32_t sum = 0;

    /* Pseudo-header: src (16) + dst (16) + upper_layer_len (4) + zero (3) + next_header (1) */
    p = (const unsigned char *)&src;
    for (i = 0; i < 16; i += 2)
        sum += (uint32_t)((p[i] << 8) | p[i + 1]);
    p = (const unsigned char *)&dst;
    for (i = 0; i < 16; i += 2)
        sum += (uint32_t)((p[i] << 8) | p[i + 1]);
    sum += (uint32_t)(msgLen >> 16) + (uint32_t)(msgLen & 0xFFFF);
    sum += (uint32_t)IPPROTO_ICMPV6;

    p = (const unsigned char *)msg;
    for (i = 0; i + 1 < msgLen; i += 2)
        sum += (uint32_t)((p[i] << 8) | p[i + 1]);
    if (i < msgLen)
        sum += (uint32_t)(p[i] << 8);

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}

bool Echo6::getSourceForDest(const struct in6_addr &dest, struct in6_addr &srcOut)
{
    if (cachedSrcValid_ && memcmp(&cachedDest_, &dest, sizeof(dest)) == 0)
    {
        srcOut = cachedSrc_;
        return true;
    }
    int s = socket(AF_INET6, SOCK_DGRAM, 0);
    if (s == -1)
        return false;
    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = dest;
    addr.sin6_port = htons(80);
    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        close(s);
        return false;
    }
    struct sockaddr_in6 local;
    socklen_t len = sizeof(local);
    if (getsockname(s, (struct sockaddr *)&local, &len) != 0)
    {
        close(s);
        return false;
    }
    close(s);
    cachedDest_ = dest;
    cachedSrc_ = local.sin6_addr;
    cachedSrcValid_ = true;
    srcOut = cachedSrc_;
    return true;
}

bool Echo6::send(int payloadLength, const struct in6_addr &realIp, bool reply, uint16_t id, uint16_t seq)
{
    struct sockaddr_in6 target;
    memset(&target, 0, sizeof(target));
    target.sin6_family = AF_INET6;
    target.sin6_addr = realIp;

    if (payloadLength + sizeof(Icmp6Header) > bufferSize)
        throw Exception("packet too big");

    Icmp6Header *header = (Icmp6Header *)sendBuffer.data();
    header->type = reply ? ICMP6_ECHO_REPLY : ICMP6_ECHO_REQUEST;
    header->code = 0;
    header->id = htons(id);
    header->seq = htons(seq);
    header->chksum = 0;

    if (!kernelChecksum_)
    {
        struct in6_addr src;
        if (!getSourceForDest(realIp, src))
            return false;
        header->chksum = htons(icmp6Checksum(src, realIp, sendBuffer.data(), payloadLength + sizeof(Icmp6Header)));
    }

    int result = sendto(fd, sendBuffer.data(), payloadLength + sizeof(Icmp6Header), 0,
                        (struct sockaddr *)&target, sizeof(target));
    if (result == -1)
        return false;
    return true;
}

int Echo6::receive(struct in6_addr &realIp, bool &reply, uint16_t &id, uint16_t &seq)
{
    struct sockaddr_in6 source;
    socklen_t source_len = sizeof(source);

    int dataLength = recvfrom(fd, receiveBuffer.data(), bufferSize, 0,
                             (struct sockaddr *)&source, &source_len);
    if (dataLength == -1)
    {
#ifdef WIN32
        return -1;
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return -1;
        syslog(LOG_ERR, "error receiving icmp6 packet: %s", strerror(errno));
        return -1;
#endif
    }

    if (dataLength < (int)sizeof(Icmp6Header))
        return -1;

    Icmp6Header *header = (Icmp6Header *)receiveBuffer.data();
    if ((header->type != ICMP6_ECHO_REQUEST && header->type != ICMP6_ECHO_REPLY) || header->code != 0)
        return -1;

    realIp = source.sin6_addr;
    reply = header->type == ICMP6_ECHO_REPLY;
    id = ntohs(header->id);
    seq = ntohs(header->seq);

    return dataLength - sizeof(Icmp6Header);
}

char *Echo6::sendPayloadBuffer()
{
    return sendBuffer.data() + headerSize();
}

char *Echo6::receivePayloadBuffer()
{
    return receiveBuffer.data() + headerSize();
}
