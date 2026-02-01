/*
 *  Hans - IP over ICMP
 *
 *  Optional congestion control. Stub: tracks sent/loss/RTT; rate is fixed when disabled.
 */

#include "congestion.h"

Congestion::Congestion()
    : enabled(false)
    , bytesSent(0)
    , packetsLost(0)
    , rttMs(0)
    , currentRateKbps(0)
{
}

void Congestion::reportSent(int bytes)
{
    bytesSent += bytes;
}

void Congestion::reportLoss()
{
    packetsLost++;
}

void Congestion::reportRttMs(int ms)
{
    rttMs = ms;
}

int Congestion::getCurrentRateKbps() const
{
    return currentRateKbps;
}

void Congestion::refill(Time now)
{
    (void)now;
}
