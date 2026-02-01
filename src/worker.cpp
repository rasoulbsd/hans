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

#include "worker.h"
#include "tun.h"
#include "exception.h"
#include "config.h"

#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/select.h>
#include <grp.h>
#include <iostream>
#include <errno.h>

using std::cout;
using std::endl;

const int Worker::RECV_BATCH_MAX = HANS_RECV_BATCH_MAX;

Worker::TunnelHeader::Magic::Magic(const char *magic)
{
    memset(data, 0, sizeof(data));
    strncpy(data, magic, sizeof(data));
}

bool Worker::TunnelHeader::Magic::operator==(const Magic &other) const
{
    return memcmp(data, other.data, sizeof(data)) == 0;
}

bool Worker::TunnelHeader::Magic::operator!=(const Magic &other) const
{
    return memcmp(data, other.data, sizeof(data)) != 0;
}

Worker::Worker(int tunnelMtu, const std::string *deviceName, bool answerEcho,
               uid_t uid, gid_t gid,
               int recvBufSize, int sndBufSize, int rateKbps,
               bool useIPv4, bool useIPv6)
    : echo(useIPv4 ? new Echo(tunnelMtu + sizeof(TunnelHeader), recvBufSize, sndBufSize) : NULL),
      echo6(useIPv6 ? new Echo6(tunnelMtu + sizeof(TunnelHeader), recvBufSize, sndBufSize) : NULL),
      currentRecvFrom6(false),
      tun(deviceName, tunnelMtu),
      pacer(rateKbps > 0 ? rateKbps : 0, 4500)
{
    this->tunnelMtu = tunnelMtu;
    this->answerEcho = answerEcho;
    this->uid = uid;
    this->gid = gid;
    this->privilegesDropped = false;
}

Worker::~Worker()
{
    delete echo;
    echo = NULL;
    delete echo6;
    echo6 = NULL;
}

bool Worker::sendEcho(const TunnelHeader::Magic &magic, TunnelHeader::Type type,
                      int length, uint32_t realIp, bool reply, uint16_t id, uint16_t seq)
{
    if (!echo)
        return false;
    if (length > payloadBufferSize())
        throw Exception("packet too big");

    int totalLen = length + sizeof(TunnelHeader);
    if (!pacer.allowSend(totalLen))
    {
        stats.incDroppedSendFail();
        return false;
    }

    TunnelHeader *header = (TunnelHeader *)echo->sendPayloadBuffer();
    header->magic = magic;
    header->type = type;

    DEBUG_ONLY(
        cout << "sending: type " << type << ", length " << length
             << ", id " << id << ", seq " << seq << endl);

    if (!echo->send(totalLen, realIp, reply, id, seq))
    {
        stats.incDroppedSendFail();
        return false;
    }
    stats.incPacketsSent(totalLen);
    return true;
}

bool Worker::sendEcho6(const TunnelHeader::Magic &magic, TunnelHeader::Type type,
                       int length, const struct in6_addr &realIp, bool reply, uint16_t id, uint16_t seq)
{
    if (!echo6)
        return false;
    if (length > payloadBufferSize())
        throw Exception("packet too big");

    int totalLen = length + sizeof(TunnelHeader);
    if (!pacer.allowSend(totalLen))
    {
        stats.incDroppedSendFail();
        return false;
    }

    TunnelHeader *header = (TunnelHeader *)echo6->sendPayloadBuffer();
    header->magic = magic;
    header->type = type;

    if (!echo6->send(totalLen, realIp, reply, id, seq))
    {
        stats.incDroppedSendFail();
        return false;
    }
    stats.incPacketsSent(totalLen);
    return true;
}

void Worker::sendToTun(int length)
{
    tun.write(echoReceivePayloadBuffer(), length);
}

char *Worker::echoSendPayloadBuffer()
{
    if (echo)
        return echo->sendPayloadBuffer() + sizeof(TunnelHeader);
    if (echo6)
        return echo6->sendPayloadBuffer() + sizeof(TunnelHeader);
    return NULL;
}

char *Worker::echoSendPayloadBuffer6()
{
    return echo6 ? echo6->sendPayloadBuffer() + sizeof(TunnelHeader) : NULL;
}

void Worker::setTimeout(Time delta)
{
    nextTimeout = now + delta;
}

void Worker::run()
{
    now = Time::now();
    alive = true;

    int maxFd = tun.getFd();
    if (echo && echo->getFd() > maxFd)
        maxFd = echo->getFd();
    if (echo6 && echo6->getFd() > maxFd)
        maxFd = echo6->getFd();

    while (alive)
    {
        fd_set fs;
        Time timeout;

        FD_ZERO(&fs);
        FD_SET(tun.getFd(), &fs);
        if (echo)
            FD_SET(echo->getFd(), &fs);
        if (echo6)
            FD_SET(echo6->getFd(), &fs);

        if (nextTimeout != Time::ZERO)
        {
            timeout = nextTimeout - now;
            if (timeout < Time::ZERO)
                timeout = Time::ZERO;
        }

        // wait for data or timeout
        timeval *timeval = nextTimeout != Time::ZERO ? &timeout.getTimeval() : NULL;
        int result = select(maxFd + 1 , &fs, NULL, NULL, timeval);
        if (result == -1)
        {
            if (alive)
                throw Exception("select", true);
            else
                return;
        }
        now = Time::now();
        pacer.refill(now);

        // timeout
        if (result == 0)
        {
            nextTimeout = Time::ZERO;
            handleTimeout();
            continue;
        }

        // icmp data (batch read up to RECV_BATCH_MAX)
        if (echo && FD_ISSET(echo->getFd(), &fs))
        {
            int batchCount = 0;
            while (batchCount < RECV_BATCH_MAX)
            {
                bool reply;
                uint16_t id, seq;
                uint32_t ip;

                currentRecvFrom6 = false;
                int dataLength = echo->receive(ip, reply, id, seq);
                if (dataLength == -1)
                {
#ifndef WIN32
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        break;
#endif
                    break;
                }
                batchCount++;
                stats.incPacketsReceived(dataLength);
#ifdef WIN32
                break;
#endif
                bool valid = dataLength >= sizeof(TunnelHeader);

                if (valid)
                {
                    TunnelHeader *header = (TunnelHeader *)echo->receivePayloadBuffer();

                    DEBUG_ONLY(
                        cout << "received: type " << header->type
                             << ", length " << dataLength - sizeof(TunnelHeader)
                             << ", id " << id << ", seq " << seq << endl);

                    valid = handleEchoData(*header, dataLength - sizeof(TunnelHeader), ip, reply, id, seq);
                }

                if (!valid && !reply && answerEcho)
                {
                    memcpy(echo->sendPayloadBuffer(), echo->receivePayloadBuffer(), dataLength);
                    echo->send(dataLength, ip, true, id, seq);
                }
            }
        }

        if (echo6 && FD_ISSET(echo6->getFd(), &fs))
        {
            int batchCount = 0;
            while (batchCount < RECV_BATCH_MAX)
            {
                bool reply;
                uint16_t id, seq;
                struct in6_addr ip6;

                currentRecvFrom6 = true;
                int dataLength = echo6->receive(ip6, reply, id, seq);
                if (dataLength == -1)
                {
#ifndef WIN32
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        break;
#endif
                    break;
                }
                batchCount++;
                stats.incPacketsReceived(dataLength);
#ifdef WIN32
                break;
#endif
                bool valid = dataLength >= sizeof(TunnelHeader);

                if (valid)
                {
                    TunnelHeader *header = (TunnelHeader *)echo6->receivePayloadBuffer();

                    valid = handleEchoData6(*header, dataLength - sizeof(TunnelHeader), ip6, reply, id, seq);
                }

                if (!valid && !reply && answerEcho)
                {
                    memcpy(echo6->sendPayloadBuffer(), echo6->receivePayloadBuffer(), dataLength);
                    echo6->send(dataLength, ip6, true, id, seq);
                }
            }
        }

        // data from tun
        if (FD_ISSET(tun.getFd(), &fs))
        {
            uint32_t sourceIp, destIp;
            char *sendBuf = echoSendPayloadBuffer();
            if (!sendBuf)
                sendBuf = echoSendPayloadBuffer6();
            int dataLength = sendBuf ? tun.read(sendBuf, sourceIp, destIp) : -1;

            if (dataLength == 0)
                throw Exception("tunnel closed");

            if (dataLength != -1)
                handleTunData(dataLength, sourceIp, destIp);
        }
    }
}

void Worker::stop()
{
    alive = false;
}

void Worker::dropPrivileges()
{
    if (uid <= 0 || privilegesDropped)
        return;

#ifdef WIN32
    throw Exception("dropping privileges not supported");
#else
    syslog(LOG_INFO, "dropping privileges");

    if (setgroups(0, NULL) == -1)
        throw Exception("setgroups", true);

    if (setgid(gid) == -1)
        throw Exception("setgid", true);

    if (setuid(uid) == -1)
        throw Exception("setuid", true);

    privilegesDropped = true;
#endif
}

bool Worker::handleEchoData(const TunnelHeader &, int, uint32_t, bool, uint16_t, uint16_t)
{
    return true;
}

bool Worker::handleEchoData6(const TunnelHeader &, int, const struct in6_addr &, bool, uint16_t, uint16_t)
{
    return true;
}

void Worker::handleTunData(int, uint32_t, uint32_t) { }

void Worker::handleTimeout() { }

char *Worker::echoReceivePayloadBuffer()
{
    if (currentRecvFrom6 && echo6)
        return echo6->receivePayloadBuffer() + sizeof(TunnelHeader);
    if (echo)
        return echo->receivePayloadBuffer() + sizeof(TunnelHeader);
    return NULL;
}
