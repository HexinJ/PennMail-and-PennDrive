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
#include <string>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include "http.h"
#include "ioutils.h"
#include "membership.h"
#include "stringutils.h"


int main(int argc, char *argv[])
{
  unsigned short port;
  bool verbose;
  std::string config_fn;
  std::string backend_coord_config_fn;
  parse_cli(argc, argv, &port, &verbose, &config_fn, &backend_coord_config_fn);

  struct sockaddr_in lb;
  std::string fsecret;
  load_config(config_fn, &lb, &fsecret);

  struct sockaddr_in bend_coord;
  load_config(backend_coord_config_fn, &bend_coord, NULL);

  int sock, hbsock;
  bind_socket_and_listen(port, NULL, &sock, &hbsock);
  struct sockaddr_in selfaddr;
  memset(&selfaddr, '\0', sizeof(selfaddr));
  selfaddr.sin_family = AF_INET;
  selfaddr.sin_addr.s_addr = htons(INADDR_ANY);
  selfaddr.sin_port = htons(port);

  setup_sigint_handler();

  pthread_t hbthread;
  void *args[5];
  args[0] = (void *)&lb;
  args[1] = (void *)&selfaddr;
  args[2] = (void *)&fsecret;
  args[3] = (void *)&hbsock;
  args[4] = (void *)&verbose;
  pthread_create(&hbthread, NULL, send_heartbeats, (void *)args);

  int fd_idx;
  struct sockaddr_in inaddr;
  socklen_t inaddrlen = sizeof(inaddr);
  while (!is_shutting_down) {
    fd_idx = -1;
    while (fd_idx < 0)
      fd_idx = find_open_fd_idx();

    fds[fd_idx] = accept(sock, (struct sockaddr *)&inaddr, &inaddrlen);
    if (0 >= fds[fd_idx])
      continue;
    void *args = malloc((2 * sizeof(int)) + (2 * sizeof(struct sockaddr_in)));
    ((int *)args)[0] = fd_idx;
    ((int *)args)[1] = verbose;
    *(struct sockaddr_in *)(&((int *)args)[2]) = bend_coord;
    *(struct sockaddr_in *)(
      (char *)args + (2 * sizeof(int) + sizeof(struct sockaddr_in))
    ) = lb;

    pthread_create(&(threads[fd_idx]), NULL, handle_http_client, args);
  }

  return close(sock);
}
