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
#include <arpa/inet.h>
#include <climits>
#include <iostream>
#include <string>
#include <string.h>
#include <unistd.h>
#include <vector>
#include "constants.h"
#include "ioutils.h"
#include "message.h"
#include "membership.h"
#include "stringutils.h"

#define ADMIN_FEND_PASSWD "passwd"


void *send_heartbeats(void *args)
{
  struct sockaddr_in *rcpt = (struct sockaddr_in *)(((void **)args)[0]);
  struct sockaddr_in *server = (struct sockaddr_in *)(((void **)args)[1]);
  std::string secret = *(std::string *)(((void **)args)[2]);
  int sock = *(int *)(((void **)args)[3]);
  bool verbose = *(bool *)(((void **)args)[4]);

  char lbip[INET_ADDRSTRLEN + 1] = { '\0' };
  inet_ntop(rcpt -> sin_family, &(rcpt -> sin_addr), lbip, sizeof(*rcpt));
  unsigned short lbport = ntohs(rcpt -> sin_port);

  char ip[INET_ADDRSTRLEN + 1] = { '\0' };
  inet_ntop(server -> sin_family, &(server -> sin_addr), ip, sizeof(*server));
  unsigned short selfport = ntohs(server -> sin_port);

  std::string hb;
  hb.append(HB);
  hb.push_back(' ');
  hb.append(secret);
  hb.push_back(' ');
  hb.append(ip);
  hb.push_back(':');
  hb.append(std::to_string(selfport));
  hb.append(CRLF);

  int retval;
  while (!is_shutting_down) {
    retval = sendto(
      sock,
      (char *)hb.c_str(),
      hb.length() + 1,
      0,
      (struct sockaddr *)rcpt,
      sizeof(*rcpt)
    );
    if (retval <= 0) {
      perror("sendto");
      exit(EXIT_FAILURE);
    }
    if (verbose) {
      std::cerr
        << "[INFO] Sent heartbeat (" << hb.substr(0, hb.length() - 2)
	<< ") to " << lbip << ":" << std::to_string(lbport) << "\n";
    }
    sleep(HBINT);
  }
  pthread_exit(NULL);
}


bool is_server(std::string msg, std::string secret, struct sockaddr_in *src)
{
  std::string ip;
  unsigned short port;
  bool valid = false;

  std::string prefix;
  prefix.append(HB);
  prefix.push_back(' ');
  prefix.append(secret);
  prefix.push_back(' ');
  valid = valid || (msg.rfind(prefix, 0) == 0);

  if (!valid) {
    prefix.clear();
    prefix.append(HBINC);
    prefix.push_back(' ');
    prefix.append(secret);
    valid = valid || (msg.rfind(prefix, 0) == 0);
  }

  if (!valid) {
    prefix.clear();
    prefix.append(HBDEC);
    prefix.push_back(' ');
    prefix.append(secret);
    valid = valid || (msg.rfind(prefix, 0) == 0);
  }

  if (!valid)
    return false;

  ip = msg.substr(prefix.length(), msg.length() - prefix.length()); 
  port = atoi(
    (char *)ip.substr(ip.find(':') + 1, ip.length() - ip.find(':') - 1).c_str()
  );
  if (port == 0) {
    perror("port");
    exit(EXIT_FAILURE);
  }
  ip = ip.substr(0, ip.find(':'));
  inet_pton(src -> sin_family, (char *)ip.c_str(), &(src -> sin_addr));
  src -> sin_port = htons(port);
  return true;
}


HTTPServer::HTTPServer(struct sockaddr_in *server)
{
  ipaddr[INET_ADDRSTRLEN] = { '\0' };
  if ((*server).sin_addr.s_addr != INADDR_ANY)
    inet_pton(server -> sin_family, ipaddr, &(server -> sin_addr));
  else
    strcpy(ipaddr, "127.0.0.1");
  port = htons(server -> sin_port);
  last_heartbeat = time(NULL);
  num_connections = 0;
}


void HTTPServer::heartbeat()
{
  last_heartbeat = time(NULL);
}


bool HTTPServer::isalive()
{
  return time(NULL) - last_heartbeat <= HBTIMEOUT;
}


bool HTTPServer::match(struct sockaddr_in *server)
{
  char refip[INET_ADDRSTRLEN + 1] = { '\0' };
  inet_pton(server -> sin_family, refip, &(server -> sin_addr));
  if ((server -> sin_addr).s_addr != htons(INADDR_ANY))
    if (strncmp(refip, ipaddr, INET_ADDRSTRLEN) != 0)
      return false;
  return port == htons(server -> sin_port);
}


void HTTPServer::incload()
{
  if (num_connections >= UINT_MAX) {
    perror("incload");
    exit(EXIT_FAILURE);
  }
  num_connections++;
}


void HTTPServer::decload()
{
  if (num_connections <= 0) {
    perror("incload");
    exit(EXIT_FAILURE);
  }
  num_connections--;
}


unsigned int HTTPServer::getload()
{
  return num_connections;
}


void handle_fserver_state(
  std::vector<HTTPServer> *servers,
  char *buf,
  struct sockaddr_in *src,
  std::string secret,
  bool verbose
)
{
  std::string msg(buf);
  if (msg.back() == '\n')
    msg.pop_back();
  if (msg.back() == '\r')
    msg.pop_back();
  if (!is_server(msg, secret, src)) {
    perror("Not a server");
    exit(EXIT_FAILURE);
  }

  if (verbose)
    std::cerr << "[INFO] Received heartbeat (" << msg << ")" << "\n";

  for (size_t i = 0; i < (*servers).size(); i++) {
    if (!(*servers)[i].match(src))
      continue;

    if (msg.rfind(HBINC, 0) == 0)
      (*servers)[i].incload();
    else if (msg.rfind(HBDEC, 0) == 0)
      (*servers)[i].decload();
    else if (msg.rfind(HB, 0) == 0)
      (*servers)[i].heartbeat();
    else
      break;
    return;
  }

  if (msg.rfind(HB, 0) == 0)
    (*servers).push_back(HTTPServer(src));
  else {
    perror("Unrecognized server command");
    exit(EXIT_FAILURE);
  }
}


std::string get_roundrobin_redirect(
  std::vector<HTTPServer> *servers, size_t counter
) {
  if ((*servers).size() == 0)
    return {};

  std::string redirect("http://");
  size_t i;
  for (i = 0; i < (*servers).size(); i++) {
    if ((*servers)[(counter + i) % (*servers).size()].isalive())
      break;
  }
  if (i >= (*servers).size())
    return {};
  counter = counter + i;

  std::string ipaddr = std::string(
    (*servers)[counter % (*servers).size()].ipaddr
  );
  if (ipaddr.find("127.0.0.1") != std::string::npos)
    redirect.append("localhost");
  else
    redirect.append((*servers)[counter % (*servers).size()].ipaddr);
  redirect.push_back(':');
  redirect.append(
    std::to_string((*servers)[counter % (*servers).size()].port)
  );
  redirect.push_back('/');

  return redirect;
}


std::string fend_status(struct sockaddr_in flb)
{
  int sock;
  if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket");
    exit(EXIT_FAILURE);
  }
  connect(sock, (struct sockaddr *)&flb, sizeof(flb));

  std::string status = "GET ";
  status.append(ADMIN_FEND_PASSWD);
  status.append(" null null");
  status.append(CRLF);
  if (!do_write(sock, status)) {
    perror("do_write");
    exit(EXIT_FAILURE);
  }

  return do_read(sock, CRLF);
}
