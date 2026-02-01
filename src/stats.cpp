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

#include <inttypes.h>
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
    syslog(LOG_INFO, "stats: packets_sent=%" PRIu64 " packets_received=%" PRIu64 " bytes_sent=%" PRIu64 " bytes_received=%" PRIu64 " dropped_send_fail=%" PRIu64 " dropped_queue_full=%" PRIu64,
           packets_sent,
           packets_received,
           bytes_sent,
           bytes_received,
           packets_dropped_send_fail,
           packets_dropped_queue_full);
}
