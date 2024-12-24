// -*- lsst-c++ -*-
/*
* Frontend Load Balancer Implementation
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
#include <arpa/inet.h>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include "constants.h"
#include "ioutils.h"
#include "membership.h"
#include "message.h"


std::vector<HTTPServer> frontend_servers;


std::string get_status();


int main(int argc, char *argv[])
{
  bool verbose;
  std::string config_fn;
  parse_cli(argc, argv, NULL, &verbose, &config_fn, NULL);

  struct sockaddr_in lb;
  std::string fsecret;
  load_config(config_fn, &lb, &fsecret);

  int pubsock, prisock;
  char ipaddr[INET_ADDRSTRLEN + 1] = { '\0' };
  inet_ntop(lb.sin_family, &lb.sin_addr, ipaddr, INET_ADDRSTRLEN);
  bind_socket_and_listen(htons(lb.sin_port), ipaddr, &pubsock, &prisock);

  setup_sigint_handler();

  std::string CRLFCRLF;
  CRLFCRLF.append(CRLF);
  CRLFCRLF.append(CRLF);

  struct sockaddr_in inaddr, src;
  socklen_t inaddrlen = sizeof(inaddr);
  char buf[HBMAXMSGLEN + 1] = { '\0' };
  int fd, retval;
  fd_set r;
  std::string response;
  size_t ctr;
  while (!is_shutting_down) {
    FD_ZERO(&r);
    FD_SET(pubsock, &r);
    FD_SET(prisock, &r);

    if (select(MAXF(pubsock, prisock) + 1, &r, NULL, NULL, NULL) < -1) {
      perror("select");
      exit(EXIT_FAILURE);
    }

    if (FD_ISSET(prisock, &r)) {
      retval = recvfrom(
        prisock, buf, HBMAXMSGLEN, 0, (struct sockaddr *)&src, &inaddrlen
      );
      if (retval < 0) {
        perror("recvfrom");
        exit(EXIT_FAILURE);
      }
      handle_fserver_state(&frontend_servers, buf, &src, fsecret, verbose);
      continue;
    }

    if ((fd = accept(pubsock, (struct sockaddr *)&inaddr, &inaddrlen)) <= 0)
      continue;

    if (do_read(fd, CRLF).rfind("GET passwd null null", 0) == 0) {
      do_write(fd, get_status());
      continue;
    }
    std::string redirect = get_roundrobin_redirect(&frontend_servers, ctr++);
    if (redirect.length() == 0)
      response.append("HTTP/1.1 503 Service Unavailable");
    else {
      response.append("HTTP/1.1 301 Moved Permanently");
      response.append(CRLF);
      response.append("Location: ");
      response.append(redirect);
    }
    response.append(CRLF);
    response.append(CRLF);
    send_response(fd, (char *)response.c_str());

    response.clear();
    close(fd);
  }

  close(pubsock);
  close(prisock);
  return 0;
}


std::string get_status()
{
  std::string response = "+OK ";
  for (size_t i = 0; i < frontend_servers.size(); i++) {
    response.append(frontend_servers[i].ipaddr);
    response.push_back(':');
    response.append(std::to_string(frontend_servers[i].port));
    if (frontend_servers[i].isalive())
      response.append("-ON");
    else
      response.append("-OFF");
    if (i < frontend_servers.size() - 1)
      response.push_back(' ');
  }
  response.append(CRLF);
  return response;
}
