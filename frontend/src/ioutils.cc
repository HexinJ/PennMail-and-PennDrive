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
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sstream>
#include "ioutils.h"


static pthread_mutex_t lock;


volatile sig_atomic_t is_shutting_down = 0;


volatile int fds[MAX_CONCURRENT_CONNECTIONS] = { -1 };


pthread_t threads[MAX_CONCURRENT_CONNECTIONS] = { 0 };


void parse_cli(
  int argc,
  char *argv[],
  unsigned short *port,
  bool *verbose,
  std::string *config_fn,
  std::string *bend_coord_config_fn
)
{
  int opt;
  const char *sopt;
  if (port == NULL)
    sopt = ":av";
  else {
    sopt = "p:av";
    *port = DEFAULT_PORT;
  }
  *verbose = false;
  while ((opt = getopt(argc, argv, sopt)) != -1) {
    if (opt == 'p') {
      *port = atoi(optarg);
    } else if (opt == 'v') {
      *verbose = true;
    } else if (opt == 'a') {
      fprintf(
        stderr,
        "CIS 5050 Final Project Team T%d: %s %s\n",
        TEAMNO,
        AUTHORS,
        PENNKEYS
      );
      exit(EXIT_SUCCESS);
    } else {
      if (port)
        fprintf(stderr, "Usage: %s [-p port] [-a] [-v] config\n", argv[0]);
      else
        fprintf(stderr, "Usage: %s [-a] [-v] config\n", argv[0]);
      exit(EXIT_FAILURE);
    }
  }

  if (!(optind == argc - 1 && bend_coord_config_fn == NULL) && (
    !(optind == argc - 2 && bend_coord_config_fn)
  )) {
    if (port && bend_coord_config_fn)
      fprintf(
        stderr, "Usage: %s [-p port] [-a] [-v] fconfig bconfig\n", argv[0]
      );
    else if (port)
      fprintf(stderr, "Usage: %s [-p port] [-a] [-v] config\n", argv[0]);
    else if (bend_coord_config_fn)
      fprintf(stderr, "Usage: %s [-a] [-v] fconfig bconfig\n", argv[0]);
    else
      fprintf(stderr, "Usage: %s [-a] [-v] config\n", argv[0]);
    exit(EXIT_FAILURE);
  } else if (optind == argc - 2 && bend_coord_config_fn) {
    (*bend_coord_config_fn).clear();
    (*bend_coord_config_fn).append(argv[argc - 1]);
  }
  (*config_fn).clear();
  (*config_fn).append(
    argv[argc - 1 - (optind == argc - 2 && bend_coord_config_fn)]
  );
}


void bind_socket_and_listen(
  unsigned short port, char *ipaddr, int *tcpsock, int *udpsock
)
{
  *tcpsock = socket(PF_INET, SOCK_STREAM, 0);
  if (*tcpsock < 0) {
    std::cerr << "Cannot open socket!" << "\n";
    exit(EXIT_FAILURE);
  }
  int option = 1;
  setsockopt(*tcpsock, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

  struct sockaddr_in servaddr;
  bzero(&servaddr, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  if (ipaddr == NULL)
    ipaddr = (char* ) INADDR_ANY;
  servaddr.sin_addr.s_addr = htons(INADDR_ANY);
  servaddr.sin_port = htons(port);
  bind(*tcpsock, (struct sockaddr *)&servaddr, sizeof(servaddr));
  listen(*tcpsock, BACKLOG);

  if (udpsock == NULL)
    return;

  *udpsock = socket(PF_INET, SOCK_DGRAM, 0);
  if (*udpsock < 0) {
    std::cerr << "Cannot open socket!" << "\n";
    exit(EXIT_FAILURE);
  }
  bind(*udpsock, (struct sockaddr *)&servaddr, sizeof(servaddr));
}


void write_status(int fd, bool verbose, const char *msg)
{
  if (verbose)
    std::cerr << "[" << fd << "] S: " << msg << "\n";

  if (1 > write(fd, msg, strlen(msg)) || 1 > write(fd, &CRLF, strlen(CRLF))) {
    perror("write");
    exit(EXIT_FAILURE);
  }
}


void cleanup(int fd_idx, bool dirty)
{
  if (dirty)
    write_status(fds[fd_idx], true, "-ERR Server shutting down");
  close(fds[fd_idx]);
  fds[fd_idx] = -1;
}


void sighandler(int signum)
{
  if (signum != SIGINT)
    return;

  is_shutting_down = 1;

  for (int i = 0; i < MAX_CONCURRENT_CONNECTIONS; i++) {
    if (fds[i] <= 0)
      continue;
    fcntl(fds[i], F_SETFL, fcntl(fds[i], F_GETFL) | O_NONBLOCK);
    cleanup(i, true);
    pthread_cancel(threads[i]);
  }

  pthread_exit(NULL);
}


void setup_sigint_handler()
{
  struct sigaction sa;
  sa.sa_handler = sighandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0 | SA_RESTART;
  if (sigaction(SIGINT, &sa, NULL) == -1) {
    perror("sigaction");
    exit(EXIT_FAILURE);
  }
}


int find_open_fd_idx()
{
  pthread_mutex_lock(&lock);
  int idx = -1;
  for (int i = 0; i < MAX_CONCURRENT_CONNECTIONS; i++) {
    if (fds[i] <= 0) {
      idx = i;
      break;
    }
  }
  pthread_mutex_unlock(&lock);
  return idx;
}


void send_response(int fd, char *response)
{
  size_t sent = 0, n;
  while (sent < strlen(response)) {
    n = write(fd, &response[sent], strlen(response) - sent);
    if (n < 0) {
      perror("write");
      exit(EXIT_FAILURE);
    }
    sent += n;
  }
}


void pprint_map(std::map<std::string, std::string> mmap)
{
  for (
    std::map<std::string, std::string>::const_iterator it = mmap.begin();
    it != mmap.end();
    ++it
  )
    std::cerr << it -> first << ": " << it -> second << "\n";
}


void load_config(
  std::string config_fn, struct sockaddr_in *lb, std::string *secret
)
{
  std::ifstream f(config_fn);
  if (!f.is_open()) {
    perror("ifstream");
    exit(EXIT_FAILURE);
  }

  if (secret)
    (*secret).clear();
  std::string tmp;
  for (int i = 0; i < 2 - (secret == NULL); i++) {
    tmp.clear();
    if (!getline(f, tmp)) {
      f.close();
      perror("getline");
      exit(EXIT_FAILURE);
    }

    if (!tmp.empty() && tmp[tmp.length() - 1] == '\n')
      tmp.erase(tmp.length() - 1);

    if (i == 0) {
      bzero(lb, sizeof(*lb));
      std::string ipaddr = tmp.substr(0, tmp.find(':'));
      lb -> sin_family = AF_INET;
      inet_pton(lb -> sin_family, (char *)ipaddr.c_str(), &(lb -> sin_addr));
      lb -> sin_port = htons(
        atoi((char *)tmp.substr(tmp.find(':') + 1, tmp.length()).c_str())
      );
    } else
      *secret = tmp;
  }
  f.close();
}


std::pair<std::string, int> IP_port(std::string &addr)
{
  int col_pos = addr.rfind(':');
  std::string IP = addr.substr(0, col_pos);
  std::string port_ = addr.substr(col_pos + 1);
  int port = atoi(port_.c_str());
  std::pair<std::string, int> pair = {IP, port};
  return pair;
}


int create_connect_socket(std::string ip_address)
{
  int fd;
  if ((fd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket");
    return -1;
  }

  struct sockaddr_in addr;
  bzero(&addr, sizeof(addr));
  std::pair<std::string, int> ipport = IP_port(ip_address);
  std::string IP_ = ipport.first;
  unsigned short port_ = ipport.second;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port_);
  if (inet_pton(AF_INET, IP_.c_str(), &addr.sin_addr) <= 0) {
    perror("inet_pton");
    close(fd);
    return -1;
  }
  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("connect");
    close(fd);
    return -1;
  }
  return fd;
}


std::string create_http_response(
  int status_code, const std::string& body, std::string host
) {
  std::string status;
  if (host.size() <= 0) {
    perror("No host provided");
    exit(EXIT_FAILURE);
  }
  
  if (status_code == 200) {
    status = "OK";
  } 
  else if (status_code == 204){
    status = "No Content";
  } 
  else if (status_code == 400) {
    status = "Bad Request";
  } 
  else if (status_code == 404) {
    status = "Not Found";
  } 
  else if (status_code == 500) {
    status = "Internal Server Error";
  } 
  else if (status_code == 409) {
    status = "Conflict";
  }
  else {
    status = "Unknown Status";
  }

  std::ostringstream response;
  response << "HTTP/1.1 " << status_code << " " << status << "\r\n";
  response << "Content-Type: text/plain\r\n";
  response << "Access-Control-Allow-Origin: " << host << "\r\n";
  response << "Access-Control-Allow-Methods: DELETE, POST, HEAD, GET, OPTIONS\r\n";
  response << "Access-Control-Allow-Headers: content-type, x-file-name, x-file-path, cookie\r\n";
  response << "Access-Control-Allow-Credentials: true\r\n";
  response << "Content-Length: " << body.size() << "\r\n";
  response << "\r\n";
  response << body;

  return response.str();
}

std::string do_read_backend(int fd, std::string delim)
{
  std::string message;
  char buf[1024];
  while (true)
  {
    int r = read(fd, buf, sizeof(buf));
    if (r < 0)
    {
      std::cerr << "Error with do_read" << std::endl;
      return "ERROR";
    }
    else if (r == 0)
    {
      std::cerr << "Connection closed during do_read" << std::endl;
      return "CLOSED";
    }
    else
    {
      message.append(buf, r);
      size_t pos = message.find(delim);
      if (pos != std::string::npos)
      {
        std::string complete_message = message.substr(0, pos);
        return complete_message;
      }
    }
  }
}
