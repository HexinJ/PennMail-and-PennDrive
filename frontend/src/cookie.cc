// -*- lsst-c++ -*-
/*
* Frontend Server Implementation
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
#include <random>
#include <string>
#include "constants.h"
#include "cookie.h"


std::string generate_cookie()
{
  std::mt19937 mt(time(NULL));
  int vocab_size = 62, c;
  std::string cookie;
  for (int i = 0; i < COOKIE_LEN; i++) {
    c = mt() % vocab_size;
    if (c < 10)
      cookie.push_back(48 + c);
    else if (c < 36)
      cookie.push_back(65 + (c - 10));
    else
      cookie.push_back(97 + (c - 36));
  }
  return cookie;
}
