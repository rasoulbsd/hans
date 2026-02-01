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

#ifndef ECHO6_H
#define ECHO6_H

#include <vector>
#include <stdint.h>
#include <netinet/in.h>

class Echo6
{
public:
    Echo6(int maxPayloadSize, int recvBufSize = 256 * 1024, int sndBufSize = 256 * 1024);
    ~Echo6();

    int getFd() { return fd; }

    bool send(int payloadLength, const struct in6_addr &realIp, bool reply, uint16_t id, uint16_t seq);
    int receive(struct in6_addr &realIp, bool &reply, uint16_t &id, uint16_t &seq);

    char *sendPayloadBuffer();
    char *receivePayloadBuffer();

    static int headerSize();
protected:
    struct Icmp6Header
    {
        uint8_t type;
        uint8_t code;
        uint16_t chksum;
        uint16_t id;
        uint16_t seq;
    }; // size = 8

    /* When IPV6_CHECKSUM setsockopt is unsupported (e.g. WSL/Docker), we fill checksum in userspace */
    bool kernelChecksum_;
    struct in6_addr cachedDest_;
    struct in6_addr cachedSrc_;
    bool cachedSrcValid_;

    static uint16_t icmp6Checksum(const struct in6_addr &src, const struct in6_addr &dst,
                                  const void *msg, size_t msgLen);
    bool getSourceForDest(const struct in6_addr &dest, struct in6_addr &srcOut);

    int fd;
    int bufferSize;
    std::vector<char> sendBuffer;
    std::vector<char> receiveBuffer;
};

#endif
