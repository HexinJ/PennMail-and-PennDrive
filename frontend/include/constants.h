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

/**
 * @file constants
 *
 * @brief Contains all of the relevant constant values for the frontend server.
 *
 * @ingroup frontend
 *
 * @author Michael Yao
 * Contact: myao2199 [at] seas [dot] upenn [dot] edu
 */
#ifndef FSERVER_CONSTANTS_H
#define FSERVER_CONSTANTS_H

#define DEFAULT_PORT 2121
#define BACKLOG 20
#define MAX_CONCURRENT_CONNECTIONS 512
#define TEAMNO 21
#define AUTHORS "Ally Kim, Ben Jiang, Kathryn Chen, and Michael Yao"
#define PENNKEYS "{allykim, bjiang1, fdshfg, myao2199}"

#define CRLF "\r\n"
#define GET "GET "
#define HEAD "HEAD "
#define POST "POST "
#define DELE "DELETE"
#define OPTIONS "OPTIONS "
#define HELO "HELO"
#define OK "+OK"
#define ERR "-ERR"
#define EXTERNAL_OK "250"
#define CHUNK_SIZE 4096
#define COOKIE_LEN 128
#define BEND_PASS "passwd"

#define MAXF(x, y) (x > y ? x : y)
#define MINF(x, y) (x < y ? x : y)

#endif
