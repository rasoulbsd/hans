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

#ifndef SERVER_H
#define SERVER_H

#include "worker.h"
#include "auth.h"

#include <map>
#include <queue>
#include <vector>
#include <list>
#include <set>
#include <string>
#include <cstring>
#include <netinet/in.h>

class Server : public Worker
{
public:
    Server(int tunnelMtu, const std::string *deviceName, const std::string &passphrase,
           uint32_t network, bool answerEcho, uid_t uid, gid_t gid, int pollTimeout,
           int maxBufferedPackets = 20, int recvBufSize = 256 * 1024, int sndBufSize = 256 * 1024, int rateKbps = 0);
    virtual ~Server();

    struct ClientConnectDataLegacy
    {
        uint8_t maxPolls;
        uint32_t desiredIp;
    };

    struct ClientConnectData
    {
        uint8_t version;
        uint8_t maxPolls;
        uint32_t desiredIp;
    };

    static const TunnelHeader::Magic magic;

protected:
    struct Packet
    {
        TunnelHeader::Type type;
        std::vector<char> data;
    };

    struct ClientData
    {
        enum State
        {
            STATE_NEW,
            STATE_CHALLENGE_SENT,
            STATE_ESTABLISHED
        };

        struct EchoId
        {
            EchoId(uint16_t id, uint16_t seq) { this->id = id; this->seq = seq; }

            uint16_t id;
            uint16_t seq;
        };

        uint32_t realIp;
        struct in6_addr realIp6;
        bool isV6;
        uint32_t tunnelIp;

        /* Per-flow queues (round-robin send for fairness); size = HANS_NUM_FLOW_QUEUES. If 1, single FIFO. */
        std::vector<std::queue<Packet> > pendingByFlow;
        int lastSentFlow;

        int maxPolls;
        /* Per-channel POLL queues for multiplexing; size = NUM_CHANNELS. Channel = echoId % NUM_CHANNELS. */
        std::vector<std::queue<EchoId> > pollIdsByChannel;
        int nextChannelToSend;
        Time lastActivity;

        State state;
        bool useHmac;

        Auth::Challenge challenge;
    };

    typedef std::list<ClientData> ClientList;
    typedef std::map<uint32_t, ClientList::iterator> ClientIpMap;
    struct In6AddrCompare {
        bool operator()(const struct in6_addr &a, const struct in6_addr &b) const {
            return memcmp(&a, &b, sizeof(a)) < 0;
        }
    };
    typedef std::map<struct in6_addr, ClientList::iterator, In6AddrCompare> ClientIp6Map;

    virtual bool handleEchoData(const TunnelHeader &header, int dataLength, uint32_t realIp, bool reply, uint16_t id, uint16_t seq);
    virtual bool handleEchoData6(const TunnelHeader &header, int dataLength, const struct in6_addr &realIp, bool reply, uint16_t id, uint16_t seq);
    virtual void handleTunData(int dataLength, uint32_t sourceIp, uint32_t destIp);
    virtual void handleTimeout();

    virtual void run();

    void serveTun(ClientData *client);

    void handleUnknownClient(const TunnelHeader &header, int dataLength, uint32_t realIp, uint16_t echoId, uint16_t echoSeq);
    void handleUnknownClient6(const TunnelHeader &header, int dataLength, const struct in6_addr &realIp, uint16_t echoId, uint16_t echoSeq);
    void removeClient(ClientData *client);

    void sendChallenge(ClientData *client);
    void checkChallenge(ClientData *client, int dataLength);
    void sendReset(ClientData *client);

    void sendEchoToClient(ClientData *client, TunnelHeader::Type type, int dataLength);

    void pollReceived(ClientData *client, uint16_t echoId, uint16_t echoSeq);

    bool getNextPollFromChannels(ClientData *client, uint16_t &outId, uint16_t &outSeq);
    bool getNextPollPeek(ClientData *client, uint16_t &outId, uint16_t &outSeq);

    uint32_t reserveTunnelIp(uint32_t desiredIp);
    void releaseTunnelIp(uint32_t tunnelIp);

    ClientData *getClientByTunnelIp(uint32_t ip);
    ClientData *getClientByRealIp(uint32_t ip);
    ClientData *getClientByRealIp6(const struct in6_addr &ip6);

    Auth auth;

    uint32_t network;
    std::set<uint32_t> usedIps;
    uint32_t latestAssignedIpOffset;

    Time pollTimeout;
    int maxBufferedPackets;

    ClientList clientList;
    ClientIpMap clientRealIpMap;
    ClientIp6Map clientRealIp6Map;
    ClientIpMap clientTunnelIpMap;
};

#endif
