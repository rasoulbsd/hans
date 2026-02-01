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

#ifndef PACER_H
#define PACER_H

#include "time.h"

class Pacer
{
public:
    Pacer(); /* disabled */
    Pacer(int rateKbps, int burstBytes = 4500);

    void refill(Time now);
    bool allowSend(int payloadBytes);

private:
    bool enabled;
    double tokens;       /* bytes */
    double refillRate;    /* bytes per millisecond */
    int burstBytes;
    Time lastRefill;
};

#endif
