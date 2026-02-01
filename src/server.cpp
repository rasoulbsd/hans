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

#include "server.h"
#include "client.h"
#include "config.h"
#include "utility.h"
#include "hmac.h"

#include <string.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <iostream>

using std::string;
using std::cout;
using std::endl;

#define FIRST_ASSIGNED_IP_OFFSET 100

const Worker::TunnelHeader::Magic Server::magic("hans");

/* Hash inner IP packet (5-tuple for TCP/UDP, else 3-tuple) to flow index 0..HANS_NUM_FLOW_QUEUES-1. */
static int getFlowIdFromPayload(const char *data, int dataLength)
{
    if (HANS_NUM_FLOW_QUEUES <= 1 || dataLength < 20)
        return 0;
    const unsigned char *p = (const unsigned char *)data;
    unsigned int hash = (unsigned int)p[12] << 24 | (unsigned int)p[13] << 16 | (unsigned int)p[14] << 8 | p[15];
    hash += ((unsigned int)p[16] << 24 | (unsigned int)p[17] << 16 | (unsigned int)p[18] << 8 | p[19]) * 31u;
    hash += (unsigned int)p[9] * 31u;
    if (dataLength >= 24 && (p[9] == 6 || p[9] == 17))
    {
        hash += ((unsigned int)p[20] << 8 | p[21]) * 31u;
        hash += ((unsigned int)p[22] << 8 | p[23]) * 31u;
    }
    return (int)(hash % (unsigned int)HANS_NUM_FLOW_QUEUES);
}

Server::Server(int tunnelMtu, const string *deviceName, const string &passphrase,
               uint32_t network, bool answerEcho, uid_t uid, gid_t gid, int pollTimeout,
               int maxBufferedPackets, int recvBufSize, int sndBufSize, int rateKbps)
    : Worker(tunnelMtu, deviceName, answerEcho, uid, gid, recvBufSize, sndBufSize, rateKbps, true, true), auth(passphrase)
{
    this->network = network & 0xffffff00;
    this->pollTimeout = pollTimeout;
    this->maxBufferedPackets = maxBufferedPackets > 0 ? maxBufferedPackets : 20;
    this->latestAssignedIpOffset = FIRST_ASSIGNED_IP_OFFSET - 1;

    tun.setIp(this->network + 1, this->network + 2);

    dropPrivileges();
}

Server::~Server()
{

}

void Server::handleUnknownClient(const TunnelHeader &header, int dataLength, uint32_t realIp, uint16_t echoId, uint16_t echoSeq)
{
    ClientData client;
    client.realIp = realIp;
    memset(&client.realIp6, 0, sizeof(client.realIp6));
    client.isV6 = false;
    client.useHmac = false;
    client.maxPolls = 1;

    pollReceived(&client, echoId, echoSeq);

    if (header.type != TunnelHeader::TYPE_CONNECTION_REQUEST ||
        (dataLength != sizeof(ClientConnectDataLegacy) && dataLength != sizeof(ClientConnectData)))
    {
        syslog(LOG_DEBUG, "invalid request (type %d) from %s", header.type,
               Utility::formatIp(realIp).c_str());
        sendReset(&client);
        return;
    }

    uint32_t desiredIp = 0;
    if (dataLength == sizeof(ClientConnectDataLegacy))
    {
        ClientConnectDataLegacy *connectData = (ClientConnectDataLegacy *)echoReceivePayloadBuffer();
        client.maxPolls = connectData->maxPolls;
        desiredIp = ntohl(connectData->desiredIp);
        client.useHmac = false;
    }
    else
    {
        ClientConnectData *connectData = (ClientConnectData *)echoReceivePayloadBuffer();
        client.maxPolls = connectData->maxPolls;
        desiredIp = ntohl(connectData->desiredIp);
        client.useHmac = (connectData->version >= 2);
    }
    client.state = ClientData::STATE_NEW;
    client.tunnelIp = reserveTunnelIp(desiredIp);

    syslog(LOG_DEBUG, "new client %s with tunnel address %s\n",
           Utility::formatIp(client.realIp).data(),
           Utility::formatIp(client.tunnelIp).data());

    if (client.tunnelIp != 0)
    {
        client.challenge = auth.generateChallenge(CHALLENGE_SIZE);
        sendChallenge(&client);

        // add client to list
        clientList.push_front(client);
        clientList.front().pendingByFlow.resize(HANS_NUM_FLOW_QUEUES);
        clientList.front().lastSentFlow = 0;
        clientRealIpMap[realIp] = clientList.begin();
        clientTunnelIpMap[client.tunnelIp] = clientList.begin();
    }
    else
    {
        syslog(LOG_WARNING, "server full");
        sendEchoToClient(&client, TunnelHeader::TYPE_SERVER_FULL, 0);
    }
}

void Server::handleUnknownClient6(const TunnelHeader &header, int dataLength, const struct in6_addr &realIp, uint16_t echoId, uint16_t echoSeq)
{
    ClientData client;
    client.realIp = 0;
    client.realIp6 = realIp;
    client.isV6 = true;
    client.useHmac = false;
    client.maxPolls = 1;

    pollReceived(&client, echoId, echoSeq);

    if (header.type != TunnelHeader::TYPE_CONNECTION_REQUEST ||
        (dataLength != sizeof(ClientConnectDataLegacy) && dataLength != sizeof(ClientConnectData)))
    {
        syslog(LOG_DEBUG, "invalid request (type %d) from %s", header.type, Utility::formatIp6(realIp).c_str());
        sendReset(&client);
        return;
    }

    uint32_t desiredIp6 = 0;
    if (dataLength == sizeof(ClientConnectDataLegacy))
    {
        ClientConnectDataLegacy *connectData = (ClientConnectDataLegacy *)echoReceivePayloadBuffer();
        client.maxPolls = connectData->maxPolls;
        desiredIp6 = ntohl(connectData->desiredIp);
        client.useHmac = false;
    }
    else
    {
        ClientConnectData *connectData = (ClientConnectData *)echoReceivePayloadBuffer();
        client.maxPolls = connectData->maxPolls;
        desiredIp6 = ntohl(connectData->desiredIp);
        client.useHmac = (connectData->version >= 2);
    }
    client.state = ClientData::STATE_NEW;
    client.tunnelIp = reserveTunnelIp(desiredIp6);

    syslog(LOG_DEBUG, "new IPv6 client %s with tunnel address %s\n",
           Utility::formatIp6(client.realIp6).data(),
           Utility::formatIp(client.tunnelIp).data());

    if (client.tunnelIp != 0)
    {
        client.challenge = auth.generateChallenge(CHALLENGE_SIZE);
        sendChallenge(&client);

        clientList.push_front(client);
        clientList.front().pendingByFlow.resize(HANS_NUM_FLOW_QUEUES);
        clientList.front().lastSentFlow = 0;
        clientRealIp6Map[realIp] = clientList.begin();
        clientTunnelIpMap[client.tunnelIp] = clientList.begin();
    }
    else
    {
        syslog(LOG_WARNING, "server full");
        sendEchoToClient(&client, TunnelHeader::TYPE_SERVER_FULL, 0);
    }
}

void Server::sendChallenge(ClientData *client)
{
    syslog(LOG_DEBUG, "sending authentication request to %s\n",
           Utility::formatIp(client->realIp).data());

    memcpy(echoSendPayloadBuffer(), &client->challenge[0], client->challenge.size());
    sendEchoToClient(client, TunnelHeader::TYPE_CHALLENGE, client->challenge.size());

    client->state = ClientData::STATE_CHALLENGE_SENT;
}

void Server::removeClient(ClientData *client)
{
    syslog(LOG_DEBUG, "removing client %s with tunnel ip %s\n",
           client->isV6 ? Utility::formatIp6(client->realIp6).data() : Utility::formatIp(client->realIp).data(),
           Utility::formatIp(client->tunnelIp).data());

    releaseTunnelIp(client->tunnelIp);

    ClientList::iterator it;
    if (client->isV6)
    {
        it = clientRealIp6Map[client->realIp6];
        clientRealIp6Map.erase(client->realIp6);
    }
    else
    {
        it = clientRealIpMap[client->realIp];
        clientRealIpMap.erase(client->realIp);
    }
    clientTunnelIpMap.erase(client->tunnelIp);
    clientList.erase(it);
}

void Server::checkChallenge(ClientData *client, int length)
{
    bool valid = false;
    if (client->useHmac)
    {
        if (length == (int)Hmac::SHA256_SIZE &&
            auth.verifyChallengeResponseHMAC(client->challenge, echoReceivePayloadBuffer(), length))
            valid = true;
    }
    else
    {
        Auth::Response rightResponse = auth.getResponse(client->challenge);
        if (length == (int)sizeof(Auth::Response) && memcmp(&rightResponse, echoReceivePayloadBuffer(), length) == 0)
            valid = true;
    }

    if (!valid)
    {
        syslog(LOG_DEBUG, "wrong challenge response from %s\n",
               client->isV6 ? Utility::formatIp6(client->realIp6).data() : Utility::formatIp(client->realIp).data());

        sendEchoToClient(client, TunnelHeader::TYPE_CHALLENGE_ERROR, 0);

        removeClient(client);
        return;
    }

    uint32_t *ip = (uint32_t *)echoSendPayloadBuffer();
    *ip = htonl(client->tunnelIp);

    sendEchoToClient(client, TunnelHeader::TYPE_CONNECTION_ACCEPT, sizeof(uint32_t));

    client->state = ClientData::STATE_ESTABLISHED;

    syslog(LOG_INFO, "connection established to %s",
           Utility::formatIp(client->realIp).data());
}

void Server::sendReset(ClientData *client)
{
    syslog(LOG_DEBUG, "sending reset to %s",
           Utility::formatIp(client->realIp).data());
    sendEchoToClient(client, TunnelHeader::TYPE_RESET_CONNECTION, 0);
}

bool Server::handleEchoData(const TunnelHeader &header, int dataLength, uint32_t realIp, bool reply, uint16_t id, uint16_t seq)
{
    if (reply)
        return false;

    if (header.magic != Client::magic)
        return false;

    ClientData *client = getClientByRealIp(realIp);
    if (client == NULL)
    {
        handleUnknownClient(header, dataLength, realIp, id, seq);
        return true;
    }

    pollReceived(client, id, seq);

    switch (header.type)
    {
        case TunnelHeader::TYPE_CONNECTION_REQUEST:
            if (client->state == ClientData::STATE_CHALLENGE_SENT)
            {
                sendChallenge(client);
                return true;
            }

            while (client->pollIds.size() > 1)
                client->pollIds.pop();

            syslog(LOG_DEBUG, "reconnecting %s", Utility::formatIp(realIp).data());
            sendReset(client);
            removeClient(client);
            return true;
        case TunnelHeader::TYPE_CHALLENGE_RESPONSE:
            if (client->state == ClientData::STATE_CHALLENGE_SENT)
            {
                checkChallenge(client, dataLength);
                return true;
            }
            break;
        case TunnelHeader::TYPE_DATA:
            if (client->state == ClientData::STATE_ESTABLISHED)
            {
                if (dataLength == 0)
                {
                    syslog(LOG_WARNING, "received empty data packet");
                    return true;
                }

                sendToTun(dataLength);
                return true;
            }
            break;
        case TunnelHeader::TYPE_POLL:
            return true;
        default:
            break;
    }

    syslog(LOG_DEBUG, "invalid packet from: %s, type: %d, state: %d",
           Utility::formatIp(realIp).data(), header.type, client->state);

    return true;
}

bool Server::handleEchoData6(const TunnelHeader &header, int dataLength, const struct in6_addr &realIp, bool reply, uint16_t id, uint16_t seq)
{
    if (reply)
        return false;

    if (header.magic != Client::magic)
        return false;

    ClientData *client = getClientByRealIp6(realIp);
    if (client == NULL)
    {
        handleUnknownClient6(header, dataLength, realIp, id, seq);
        return true;
    }

    pollReceived(client, id, seq);

    switch (header.type)
    {
        case TunnelHeader::TYPE_CONNECTION_REQUEST:
            if (client->state == ClientData::STATE_CHALLENGE_SENT)
            {
                sendChallenge(client);
                return true;
            }
            while (client->pollIds.size() > 1)
                client->pollIds.pop();
            syslog(LOG_DEBUG, "reconnecting %s", Utility::formatIp6(realIp).data());
            sendReset(client);
            removeClient(client);
            return true;
        case TunnelHeader::TYPE_CHALLENGE_RESPONSE:
            if (client->state == ClientData::STATE_CHALLENGE_SENT)
            {
                checkChallenge(client, dataLength);
                return true;
            }
            break;
        case TunnelHeader::TYPE_DATA:
            if (client->state == ClientData::STATE_ESTABLISHED)
            {
                if (dataLength == 0)
                {
                    syslog(LOG_WARNING, "received empty data packet");
                    return true;
                }
                sendToTun(dataLength);
                return true;
            }
            break;
        case TunnelHeader::TYPE_POLL:
            return true;
        default:
            break;
    }

    syslog(LOG_DEBUG, "invalid packet from: %s, type: %d, state: %d",
           Utility::formatIp6(realIp).data(), header.type, client->state);

    return true;
}

Server::ClientData *Server::getClientByTunnelIp(uint32_t ip)
{
    ClientIpMap::iterator it = clientTunnelIpMap.find(ip);
    if (it == clientTunnelIpMap.end())
        return NULL;

    return &*it->second;
}

Server::ClientData *Server::getClientByRealIp(uint32_t ip)
{
    ClientIpMap::iterator it = clientRealIpMap.find(ip);
    if (it == clientRealIpMap.end())
        return NULL;

    return &*it->second;
}

Server::ClientData *Server::getClientByRealIp6(const struct in6_addr &ip6)
{
    ClientIp6Map::iterator it = clientRealIp6Map.find(ip6);
    if (it == clientRealIp6Map.end())
        return NULL;

    return &*it->second;
}

void Server::handleTunData(int dataLength, uint32_t, uint32_t destIp)
{
    if (destIp == network + 255) // ignore broadcasts
        return;

    ClientData *client = getClientByTunnelIp(destIp);

    if (client == NULL)
    {
        syslog(LOG_DEBUG, "data received for unknown client %s\n",
               Utility::formatIp(destIp).data());
        return;
    }

    sendEchoToClient(client, TunnelHeader::TYPE_DATA, dataLength);
}

void Server::pollReceived(ClientData *client, uint16_t echoId, uint16_t echoSeq)
{
    unsigned int maxSavedPolls = client->maxPolls != 0 ? client->maxPolls : 1;

    client->pollIds.push(ClientData::EchoId(echoId, echoSeq));
    if (client->pollIds.size() > maxSavedPolls)
        client->pollIds.pop();
    DEBUG_ONLY(cout << "poll -> " << client->pollIds.size() << endl);

    const int N = (int)client->pendingByFlow.size();
    if (N > 0)
    {
        for (int i = 0; i < N; i++)
        {
            int q = (client->lastSentFlow + 1 + i) % N;
            if (client->pendingByFlow[q].size() > 0)
            {
                Packet &packet = client->pendingByFlow[q].front();
                memcpy(echoSendPayloadBuffer(), &packet.data[0], packet.data.size());
                client->pendingByFlow[q].pop();
                client->lastSentFlow = q;
                DEBUG_ONLY(cout << "pending packet: " << packet.data.size() << " bytes (flow " << q << ")\n");
                sendEchoToClient(client, packet.type, packet.data.size());
                break;
            }
        }
    }

    client->lastActivity = now;
}

void Server::sendEchoToClient(ClientData *client, TunnelHeader::Type type, int dataLength)
{
    if (client->maxPolls == 0)
    {
        if (client->isV6)
        {
            memcpy(echoSendPayloadBuffer6() - sizeof(TunnelHeader), echoSendPayloadBuffer() - sizeof(TunnelHeader), dataLength + sizeof(TunnelHeader));
            sendEcho6(magic, type, dataLength, client->realIp6, true, client->pollIds.front().id, client->pollIds.front().seq);
        }
        else
            sendEcho(magic, type, dataLength, client->realIp, true, client->pollIds.front().id, client->pollIds.front().seq);
        return;
    }

    if (client->pollIds.size() != 0)
    {
        ClientData::EchoId echoId = client->pollIds.front();
        client->pollIds.pop();

        DEBUG_ONLY(cout << "sending -> " << client->pollIds.size() << endl);
        if (client->isV6)
        {
            memcpy(echoSendPayloadBuffer6() - sizeof(TunnelHeader), echoSendPayloadBuffer() - sizeof(TunnelHeader), dataLength + sizeof(TunnelHeader));
            sendEcho6(magic, type, dataLength, client->realIp6, true, echoId.id, echoId.seq);
        }
        else
            sendEcho(magic, type, dataLength, client->realIp, true, echoId.id, echoId.seq);
        return;
    }

    const int N = (int)client->pendingByFlow.size();
    if (N <= 0)
        return;
    int flowId = (type == TunnelHeader::TYPE_DATA && N > 1)
        ? getFlowIdFromPayload(echoReceivePayloadBuffer(), dataLength) : 0;
    int maxPerFlow = (maxBufferedPackets > 0) ? (maxBufferedPackets + N - 1) / N : maxBufferedPackets;
    if (maxPerFlow < 1)
        maxPerFlow = 1;

    if ((int)client->pendingByFlow[flowId].size() >= maxPerFlow)
    {
        client->pendingByFlow[flowId].pop();
        stats.incDroppedQueueFull();
        syslog(LOG_WARNING, "packet to %s dropped (queue full, flow %d)",
               Utility::formatIp(client->tunnelIp).data(), flowId);
    }

    DEBUG_ONLY(cout << "packet queued: " << dataLength << " bytes (flow " << flowId << ")\n");

    client->pendingByFlow[flowId].push(Packet());
    Packet &packet = client->pendingByFlow[flowId].back();
    packet.type = type;
    packet.data.resize(dataLength);
    memcpy(&packet.data[0], echoReceivePayloadBuffer(), dataLength);
}

void Server::releaseTunnelIp(uint32_t tunnelIp)
{
    usedIps.erase(tunnelIp);
}

void Server::handleTimeout()
{
    ClientList::iterator it = clientList.begin();
    while (it != clientList.end())
    {
        ClientData &client = *it++;

        if (client.lastActivity + KEEP_ALIVE_INTERVAL * 2 < now)
        {
            syslog(LOG_DEBUG, "client %s timed out\n",
                   client.isV6 ? Utility::formatIp6(client.realIp6).data() : Utility::formatIp(client.realIp).data());
            removeClient(&client);
        }
    }

    setTimeout(KEEP_ALIVE_INTERVAL);
}

uint32_t Server::reserveTunnelIp(uint32_t desiredIp)
{
    if (desiredIp > network + 1 && desiredIp < network + 255 && !usedIps.count(desiredIp))
    {
        usedIps.insert(desiredIp);
        return desiredIp;
    }

    bool ipAvailable = false;

    for (int i = 0; i < 255 - FIRST_ASSIGNED_IP_OFFSET; i++)
    {
        latestAssignedIpOffset++;
        if (latestAssignedIpOffset == 255)
            latestAssignedIpOffset = FIRST_ASSIGNED_IP_OFFSET;

        if (!usedIps.count(network + latestAssignedIpOffset))
        {
            ipAvailable = true;
            break;
        }
    }

    if (!ipAvailable)
        return 0;

    usedIps.insert(network + latestAssignedIpOffset);
    return network + latestAssignedIpOffset;
}

void Server::run()
{
    setTimeout(KEEP_ALIVE_INTERVAL);

    Worker::run();
}
