// -*- lsst-c++ -*-
/*
* Frontend Server Membership Function Implementations. 
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
 * @file membership.cc 
 *
 * @brief Frontend server membership function implementations.
 *
 * @ingroup frontend
 *
 * @author Michael Yao
 * Contact: myao2199 [at] seas [dot] upenn [dot] edu
 */
#ifndef FMEMBERSHIP_H
#define FMEMBERSHIP_H
#include <arpa/inet.h>
#include <ctime>
#include <map>
#include <string>
#include <vector>

#define HBINT 6
#define HBTIMEOUT 12
#define HBMAXMSGLEN 256
#define HB "HTBTG"
#define HBINC "HBINC"
#define HBDEC "HBDEC"

void *send_heartbeats(void *args);

bool is_server(std::string msg, std::string secret);

class HTTPServer {
  public:
    char ipaddr[INET_ADDRSTRLEN + 1];

    unsigned int port;

    HTTPServer(struct sockaddr_in *server);

    void heartbeat();

    bool isalive();

    bool match(struct sockaddr_in *server);

    void incload();

    void decload();

    unsigned int getload();

  private:
    volatile std::time_t last_heartbeat;
    unsigned int num_connections;
};


void handle_fserver_state(
  std::vector<HTTPServer> *servers,
  char *buf,
  struct sockaddr_in *src,
  std::string secret,
  bool verbose
);

std::string get_roundrobin_redirect(
  std::vector<HTTPServer> *servers, size_t counter
);

std::string fend_status(struct sockaddr_in flb);
#endif
