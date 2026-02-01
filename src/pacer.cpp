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

#include "pacer.h"

Pacer::Pacer()
    : enabled(false)
    , tokens(0)
    , refillRate(0)
    , burstBytes(0)
{
}

Pacer::Pacer(int rateKbps, int burstBytes_)
    : enabled(rateKbps > 0)
    , tokens(static_cast<double>(burstBytes_))
    , refillRate(rateKbps > 0 ? (rateKbps * 1000.0 / 8.0) / 1000.0 : 0) /* bytes/ms */
    , burstBytes(burstBytes_)
    , lastRefill(Time::now())
{
}

void Pacer::refill(Time now)
{
    if (!enabled)
        return;
    Time delta = now - lastRefill;
    if (delta < Time::ZERO)
    {
        lastRefill = now;
        return;
    }
    double ms = delta.getTimeval().tv_sec * 1000.0 + delta.getTimeval().tv_usec / 1000.0;
    tokens += refillRate * ms;
    if (tokens > burstBytes)
        tokens = burstBytes;
    lastRefill = now;
}

bool Pacer::allowSend(int payloadBytes)
{
    if (!enabled)
        return true;
    if (tokens >= payloadBytes)
    {
        tokens -= payloadBytes;
        return true;
    }
    return false;
}
