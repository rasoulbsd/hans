/*
 *  Hans - IP over ICMP
 *
 *  Optional congestion control / rate control module.
 *  Off by default; when enabled, provides rate feedback based on loss and optional RTT.
 */

#ifndef CONGESTION_H
#define CONGESTION_H

#include "time.h"
#include <stdint.h>

class Congestion
{
public:
    Congestion();
    void setEnabled(bool on) { enabled = on; }
    void reportSent(int bytes);
    void reportLoss();
    void reportRttMs(int ms);
    int getCurrentRateKbps() const;
    void refill(Time now);

private:
    bool enabled;
    uint64_t bytesSent;
    uint64_t packetsLost;
    int rttMs;
    int currentRateKbps;
    Time lastRefill;
};

#endif
