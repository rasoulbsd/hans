/*
 *  Hans - IP over ICMP
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 */

#include "hmac.h"
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <cstring>
#include <string>

std::vector<char> Hmac::sign(const std::string &key, const char *data, size_t dataLen)
{
    std::vector<char> out(SHA256_SIZE);
    unsigned int len = 0;
    unsigned char *p = HMAC(EVP_sha256(), key.data(), key.size(),
                            reinterpret_cast<const unsigned char *>(data), dataLen,
                            reinterpret_cast<unsigned char *>(out.data()), &len);
    if (!p || len != SHA256_SIZE)
        out.clear();
    return out;
}

bool Hmac::verify(const std::string &key, const char *data, size_t dataLen,
                  const char *signature, size_t sigLen)
{
    if (sigLen != SHA256_SIZE)
        return false;
    std::vector<char> expected = sign(key, data, dataLen);
    if (expected.empty())
        return false;
    return memcmp(expected.data(), signature, SHA256_SIZE) == 0;
}
