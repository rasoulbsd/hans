/*
 *  Hans - IP over ICMP
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 */

#ifndef HMAC_H
#define HMAC_H

#include <stddef.h>
#include <string>
#include <vector>

class Hmac
{
public:
    static const size_t SHA256_SIZE = 32;

    static std::vector<char> sign(const std::string &key, const char *data, size_t dataLen);
    static bool verify(const std::string &key, const char *data, size_t dataLen,
                       const char *signature, size_t sigLen);
};

#endif
