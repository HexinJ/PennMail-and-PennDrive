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
#include <arpa/inet.h>
#include <iostream>
#include <string>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "constants.h"
#include "message.h"
#include "ioutils.h"
#include "stringutils.h"

#define BGET "GET"
#define OPUT "PUT"
#define CPUT "CPUT"
#define DELE "DELETE"
#define ADMIN_BEND_PASSWD "passwd"


bool do_write(int sock, std::string msg);


std::string do_read(int fd, std::string delim);


std::string bend_uk(int *sock, std::string u, std::string k, std::string meth);


std::string bend_ukv(
  int *sock, std::string u, std::string k, std::string v, std::string meth
);


std::string greet_bend(std::string uuid, struct sockaddr_in bcoord)
{
  int sock;
  if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket");
    exit(EXIT_FAILURE);
  }
  connect(sock, (struct sockaddr *)&bcoord, sizeof(bcoord));
  if (do_read(sock, CRLF).find(OK) == std::string::npos) {
    perror("backend server connection error");
    exit(EXIT_FAILURE);
  }

  std::string greeting(HELO);
  greeting.push_back(' ');
  greeting.append(BEND_PASS);
  greeting.push_back(' ');
  greeting.append(uuid);
  greeting.append(CRLF);
  if (!do_write(sock, greeting)) {
    perror("do_write");
    exit(EXIT_FAILURE);
  }

  return do_read(sock, CRLF);
}


int bend_connect(std::string uuid, struct sockaddr_in bcoord)
{
  int sock;
  if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket");
    exit(EXIT_FAILURE);
  }

  std::string reply = greet_bend(uuid, bcoord);
  strip(&reply);
  if (reply.compare(0, strlen(OK), OK) != 0)
    return -1;
  reply = reply.substr(strlen(OK));
  strip(&reply);

  if (reply.find(':') == std::string::npos) {
    perror("malformed IP address reply");
    exit(EXIT_FAILURE);
  }
  unsigned short port = stoi(reply.substr(reply.find(':') + 1));
  std::string ip_addr = reply.substr(0, reply.find(':'));

  struct sockaddr_in bendaddr;
  bzero(&bendaddr, sizeof(bendaddr));
  bendaddr.sin_family = AF_INET;
  bendaddr.sin_port = htons(port);
  inet_pton(bendaddr.sin_family, ip_addr.c_str(), &(bendaddr.sin_addr));
  connect(sock, (struct sockaddr *)&bendaddr, sizeof(bendaddr));
  return sock;
}


std::string bend_status(struct sockaddr_in bcoord)
{
  int sock;
  if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket");
    exit(EXIT_FAILURE);
  }
  connect(sock, (struct sockaddr *)&bcoord, sizeof(bcoord));
  if (do_read(sock, CRLF).find(OK) == std::string::npos) {
    perror("backend server connection error");
    exit(EXIT_FAILURE);
  }

  std::string status = "GET ";
  status.append(ADMIN_BEND_PASSWD);
  status.append(" null null");
  status.append(CRLF);
  if (!do_write(sock, status)) {
    perror("do_write");
    exit(EXIT_FAILURE);
  }

  return do_read(sock, CRLF);
}


std::string bend_shutdown(std::string baddr, struct sockaddr_in bcoord)
{
  int sock;
  if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket");
    exit(EXIT_FAILURE);
  }
  connect(sock, (struct sockaddr *)&bcoord, sizeof(bcoord));
  if (do_read(sock, CRLF).find(OK) == std::string::npos) {
    perror("backend server connection error");
    exit(EXIT_FAILURE);
  }

  if (baddr.find('-') != std::string::npos)
    baddr = split(baddr, '-')[1];

  std::string shutdown = "STOP ";
  shutdown.append(ADMIN_BEND_PASSWD);
  shutdown.push_back(' ');
  shutdown.append(baddr);
  shutdown.append(CRLF);
  if (!do_write(sock, shutdown)) {
    perror("do_write");
    exit(EXIT_FAILURE);
  }

  return do_read(sock, CRLF);
}


std::string bend_restart(std::string baddr, struct sockaddr_in bcoord)
{
  int sock;
  if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket");
    exit(EXIT_FAILURE);
  }
  connect(sock, (struct sockaddr *)&bcoord, sizeof(bcoord));
  if (do_read(sock, CRLF).find(OK) == std::string::npos) {
    perror("backend server connection error");
    exit(EXIT_FAILURE);
  }

  if (baddr.find('-') != std::string::npos)
    baddr = split(baddr, '-')[1];

  std::string restart = "START ";
  restart.append(ADMIN_BEND_PASSWD);
  restart.push_back(' ');
  restart.append(baddr);
  restart.append(CRLF);
  if (!do_write(sock, restart)) {
    perror("do_write");
    exit(EXIT_FAILURE);
  }

  return do_read(sock, CRLF);
}


std::string bend_listcontent(std::string baddr)
{
  if (baddr.find('-') != std::string::npos)
    baddr = split(baddr, '-')[1];

  int sock;
  if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket");
    exit(EXIT_FAILURE);
  }

  unsigned short port = stoi(split(baddr, ':')[3]);
  std::string ip_addr = split(baddr, ':')[0];

  struct sockaddr_in bendaddr;
  bzero(&bendaddr, sizeof(bendaddr));
  bendaddr.sin_family = AF_INET;
  bendaddr.sin_port = htons(port);
  inet_pton(bendaddr.sin_family, ip_addr.c_str(), &(bendaddr.sin_addr));
  connect(sock, (struct sockaddr *)&bendaddr, sizeof(bendaddr));

  do_read(sock, CRLF);

  std::string cmd = "GET ";
  cmd.append(ADMIN_BEND_PASSWD);
  cmd.append(" null");
  cmd.append(CRLF);
  if (!do_write(sock, cmd)) {
    perror("do_write");
    exit(EXIT_FAILURE);
  }

  return do_read(sock, CRLF);
}


std::string bend_GET(int *sock, std::string uuid, std::string key)
{
  return bend_uk(sock, uuid, key, BGET);
}


std::string bend_PUT(int *sock, std::string uuid, std::string key, std::string val)
{
  return bend_ukv(sock, uuid, key, val, OPUT);
}


std::string bend_CPUT(int *sock, std::string uuid, std::string key, std::string val)
{
  return bend_ukv(sock, uuid, key, val, CPUT);
}


std::string bend_DELETE(int *sock, std::string uuid, std::string key)
{
  return bend_uk(sock, uuid, key, DELE);
}


std::string bend_uk(int *sock, std::string u, std::string k, std::string meth)
{
  std::string cmd;
  cmd.append(meth);
  cmd.append(" ");
  cmd.append(u);
  cmd.append(" ");
  cmd.append(k);
  cmd.append(CRLF);

  if (!do_write(*sock, cmd)) {
    std::cerr << "Failed to send message [" << cmd << "] to backend" << "\n";
    exit(EXIT_FAILURE);
  }
  return do_read(*sock, CRLF);
}


std::string bend_ukv(
  int *sock, std::string u, std::string k, std::string v, std::string meth
)
{
  std::string cmd;
  cmd.append(meth);
  cmd.append(" ");
  cmd.append(u);
  cmd.append(" ");
  cmd.append(k);
  cmd.append(" ");
  cmd.append(v);
  cmd.append(CRLF);

  if (!do_write(*sock, cmd)) {
    std::cerr << "Failed to send message [" << cmd << "] to backend" << "\n";
    exit(EXIT_FAILURE);
  }
  return do_read(*sock, CRLF);
}


bool do_write(int sock, std::string msg)
{
  unsigned long sent = 0;
  int n;
  const char *buf = msg.c_str();
  while (sent < msg.length()) {
    if ((n = write(sock, &buf[sent], msg.length() - sent)) < 0)
      return false;
    sent += n;
  }
  return true;
}


std::string do_read(int fd, std::string delim)
{
  std::string message;
  char buf[CHUNK_SIZE];
  size_t n, pos;
  while ((n = read(fd, buf, CHUNK_SIZE)) > 0) {
    message.append(buf, 0, n);
    if (delim.size() > 0 && ((pos = message.find(delim)) != std::string::npos))
      return message.substr(0, pos);
  }
  return message;
}
