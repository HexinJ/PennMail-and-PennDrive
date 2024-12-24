// -*- lsst-c++ -*-
/*
* HTTP Chunk Transfer Encoding Implementation
* See COPYRIGHT file at the top of the source tree.
*
* This product includes software developed by the
* CIS 505 Project Team 21 (https://github.com/CIS5550/fa24-cis5050-T21).
*
* This program is free software: you can redistribute it and/or modify it under
* the terms of the MIT License as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*/
#include <algorithm>
#include <cstring>
#include <errno.h>
#include <string>
#include <sstream>
#include <sys/socket.h>
#include "constants.h"
#include "stringutils.h"


bool endswith(std::string string, std::string suffix)
{
  if (suffix.size() > string.size())
    return false;
  return std::equal(suffix.rbegin(), suffix.rend(), string.rbegin());
}


std::string decToHex(size_t n)
{
  std::string hex;

  size_t rem = 0;
  while (n != 0) {
    rem = n % 16;
    hex.push_back(rem + 48 + (7 * (rem >= 10)));
    n = n / 16;
  }
  std::reverse(hex.begin(), hex.end());
  return hex;
}


size_t hexToDec(std::string hex)
{
  if (endswith(hex, CRLF)) {
    for (size_t i = 0; i < strlen(CRLF); i++)
      hex.pop_back();
  }
  size_t base = 1, n = 0;
  for (size_t i = hex.size() - 1; i >= 0; i--) {
    if (hex[i] >= '0' && hex[i] <= '9')
      n += (int(hex[i]) - 48) * base;
    else if (std::toupper(hex[i]) >= 'A' && std::toupper(hex[i]) <= 'F')
      n += (int(hex[i]) - 55) * base;
    else
      continue;
    base = base * 16;
  }
  return n;
}


std::string readline(int sock)
{
  int n;
  std::string line;
  char c = '\0';

  while ((n = recv(sock, &c, 1, 0)) > 0) {
    line.push_back(c);
    if (c != CRLF[0])
      continue;
    n = recv(sock, &c, 1, MSG_PEEK);
    if ((n > 0) && (c == CRLF[1])) {
      n = recv(sock, &c, 1, 0);
      line.push_back(c);
      break;
    }
  }
  return line;
}


void read_from_chunk_transfer(int sock, std::string payload)
{
  std::string buf;
  size_t expected_size = -1;
  while ((buf.append(readline(sock))) != CRLF) {
    if (expected_size < 0) {
      strip(&buf);
      expected_size = hexToDec(buf);
    } else if (expected_size + strlen(CRLF) != buf.length()) {
      perror("chunk_transfer_encoding");
    } else {
      for (size_t i = 0; i < strlen(CRLF); i++)
        buf.pop_back();
      payload.append(buf);
      buf.clear();
      expected_size = -1;
    }
  }
  if (expected_size != 0) {
    perror("chunk_transfer_encoding");
    exit(EXIT_FAILURE);
  }
}


std::ostringstream write_to_chunk_transfer(std::string data, size_t chunksize)
{
  std::ostringstream buf;
  size_t sz;
  while (data.length() > 0) {
    sz = MINF(chunksize, data.length());
    buf << decToHex(sz) << CRLF;
    buf << data.substr(0, sz) << CRLF;
    data.erase(0, sz);
  }
  buf << "0";
  return buf;
}
