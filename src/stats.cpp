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

#include "stats.h"

#include <syslog.h>

Stats::Stats()
    : packets_sent(0)
    , packets_received(0)
    , bytes_sent(0)
    , bytes_received(0)
    , packets_dropped_send_fail(0)
    , packets_dropped_queue_full(0)
{
}

void Stats::incPacketsSent(int bytes)
{
    packets_sent++;
    if (bytes > 0)
        bytes_sent += bytes;
}

void Stats::incPacketsReceived(int bytes)
{
    packets_received++;
    if (bytes > 0)
        bytes_received += bytes;
}

void Stats::incDroppedSendFail()
{
    packets_dropped_send_fail++;
}

void Stats::incDroppedQueueFull()
{
    packets_dropped_queue_full++;
}

void Stats::dumpToSyslog() const
{
    syslog(LOG_INFO, "stats: packets_sent=%llu packets_received=%llu bytes_sent=%llu bytes_received=%llu dropped_send_fail=%llu dropped_queue_full=%llu",
           (unsigned long long)packets_sent,
           (unsigned long long)packets_received,
           (unsigned long long)bytes_sent,
           (unsigned long long)bytes_received,
           (unsigned long long)packets_dropped_send_fail,
           (unsigned long long)packets_dropped_queue_full);
}
