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
 * @file ioutils.cc
 *
 * @brief I/O utility functions.
 *
 * @ingroup frontend
 *
 * @author Michael Yao
 * Contact: myao2199 [at] seas [dot] upenn [dot] edu
 */
#ifndef IOUTILS_H
#define IOUTILS_H
#include <atomic>
#include <map>
#include <pthread.h>
#include <signal.h>
#include "constants.h"

extern volatile sig_atomic_t is_shutting_down;

extern volatile int fds[MAX_CONCURRENT_CONNECTIONS];

extern pthread_t threads[MAX_CONCURRENT_CONNECTIONS];

/**
 * @brief Parses the command line arguments.
 *
 * @param argc the number of command line arguments.
 * @param argv a list of pointers to the command line argument values.
 * @param port a pointer to the server binding port variable.
 * @param verbose a pointer to the verbose flag output variable.
 * @param config_fn a filepath to the frontend configuration file.
 * @param bend_coord_config_fn a filepath to the backend coordinator
 *   configuration file.
 * @return None.
 */
void parse_cli(
  int argc,
  char *argv[],
  unsigned short *port,
  bool *verbose,
  std::string *config_fn,
  std::string *bend_coord_config_fn
);

/**
 * @brief Binds the socket to the specified port.
 *
 * @param port the port to bind the socket to.
 * @param ipaddr the optional IP address to bind the socket to. If NULL, then
 *   set to INADDR_ANY.
 * @param tcpsock a required pointer to the TCP socket file descriptor.
 * @param udpsock an optional pointer to the UDP socket file descriptor.
 * @return None.
 */
void bind_socket_and_listen(
  unsigned short port, char *ipaddr, int *tcpsock, int *udpsock
);

/**
 * @brief Writes a server response message to the client.
 *
 * @param fd the file descriptor of the connected socket.
 * @param verbose whether to print verbose outputs to stderr.
 * @param msg the server response message to send to the client.
 * @return None.
 */
void write_status(int fd, bool verbose, const char *msg);

/**
 * @brief Cleans up the terminated client connection.
 *
 * @param fd_idx the index of the file descriptor to clean up.
 * @param dirty whether the cleanup process was triggered from a user signal.
 * @return None.
 */
void cleanup(int fd_idx, bool dirty);

/**
 * @brief Establishes a SIGINT signal handler method.
 *
 * @param None.
 * @return None.
 */
void setup_sigint_handler();

/**
 * @brief Finds and returns an available index in the active connection log.
 *
 * @param None.
 * @return An available index in the active connection log. Returns -1 if no
 *   indices are available.
 */
int find_open_fd_idx();

/**
 * @brief Writes a response message to a client. CRLF is not added to message.
 *
 * @param fd the file descriptor of the client connection.
 * @param response the response to send to the client.
 * @return None.
 */
void send_response(int fd, char *response);

/**
 * @brief Prints the key-value pairs from a map for debugging purposes.
 *
 * @param mmap: the map to print out.
 * @return None.
 */
void pprint_map(std::map<std::string, std::string> mmap);


/**
 * @brief Loads the configuration file to configure the load balancer.
 *
 * @param config_fn: the path to the configuration file.
 * @param lb: the object to store the public loadbalancer address and port in.
 * @param secret: the string to store the frontend secret to.
 * @return None.
 */
void load_config(
  std::string config_fn, struct sockaddr_in *lb, std::string *secret
);

/**
 * @brief Returns a socket from local_ip and local_port connected to ip_address
 *
 * @param ip_address: the ip and port to connect to.
 * @return the file descriptor of the socket
 */
int create_connect_socket(std::string ip_address);

/**
 * @brief Creates HTTP response string
 *
 * @param statusCode: status code of response
 * @param body: body of response 
 * @param host: the frontend HTTP server host domain.
 * @return HTTP response string
 */
std::string create_http_response(
  int status_code, const std::string& body, std::string host
);

/**
 * @brief Reads from socket until a delimiter
 *
 * @param fd: socket to read from
 * @param delim: delim to stop at
 * @return string read
 */
std::string do_read_backend(int fd, std::string delim);
#endif
