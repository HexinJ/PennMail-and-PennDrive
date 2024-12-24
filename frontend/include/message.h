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
 * @file message.cc 
 *
 * @brief Frontend-backend communication utility functions.
 *
 * @ingroup frontend
 *
 * @author Michael Yao
 * Contact: myao2199 [at] seas [dot] upenn [dot] edu
 */
#ifndef BENDMESSAGE_H
#define BENDMESSAGE_H
#include <arpa/inet.h>
#include <string>

/**
 * @brief Connects a frontend server to a backend (bend) server.
 *
 * @param uuid the UUID (i.e., email address) of the client.
 * @param bcoord the address of the backend coordinator to connect to.
 * @return The socket to use for frontend-backend communication.
 */
int bend_connect(std::string uuid, struct sockaddr_in bcoord);

/**
 * @brief Gets the status of the current backend (bend) servers.
 *
 * @param bcoord the address of the backend coordinator to connect to.
 * @return The status of the current backend (bend) servers.
 */
std::string bend_status(struct sockaddr_in bcoord);

/**
 * @brief Virtually shuts down a backend (bend) server.
 *
 * @param baddr the address of the backend server to shutdown.
 * @param bcoord the address of the backend coordinator to connect to.
 * @return The response of the coordinator.
 */
std::string bend_shutdown(std::string baddr, struct sockaddr_in bcoord);

/**
 * @brief Virtually restarts a backend (bend) server.
 *
 * @param baddr The address of the backend server to restart.
 * @param bcoord The address of the backend coordinator to connect to.
 * @return The response of the coordinator.
 */
std::string bend_restart(std::string baddr, struct sockaddr_in bcoord);

/**
 * @brief Retrieves all of the stored contents of a backend (bend) server.
 *
 * @param baddr The address of the backend server to get the contents of.
 * @return The contents of the specified backend server.
 */
std::string bend_listcontent(std::string baddr);

/**
 * @brief Reads from a socket until encountering an optional delimiter.
 *
 * @param fd: the socket to read from.
 * @param delim: the optional delilmiter to stop at.
 * @return the string read.
 */
std::string do_read(int fd, std::string delim);

/**
 * @brief Writes a message to a socket.
 *
 * @param fd: the socket to write to.
 * @param msg: the message to write to the socket.
 * @return whether the write was successful.
 */
bool do_write(int sock, std::string msg);

/**
 * @brief Sends a GET request to the backend server.
 *
 * @param sock a connected socket file descriptor.
 * @param uuid the UUID (row index) of the element to retrieve.
 * @param key the key (column name) of the element to retrieve.
 * @return GET request reponse.
 */
std::string bend_GET(int *sock, std::string uuid, std::string key);

/**
 * @brief Sends a PUT request to the backend server.
 *
 * @param sock a connected socket file descriptor.
 * @param uuid the UUID (row index) of the element to put.
 * @param key the key (column name) of the element to put.
 * @param val the value to put.
 * @return 0 if successful, else returns the error code.
 */
std::string bend_PUT(int *sock, std::string uuid, std::string key, std::string val);

/**
 * @brief Sends a CPUT request to the backend server.
 *
 * @param sock a connected socket file descriptor.
 * @param uuid the UUID (row index) of the element to put.
 * @param key the key (column name) of the element to put.
 * @param val the value to put.
 * @return 0 if successful, else returns the error code.
 */
std::string bend_CPUT(int *sock, std::string uuid, std::string key, std::string val);

/**
 * @brief Sends a DELETE request to the backend server.
 *
 * @param sock a connected socket file descriptor.
 * @param uuid the UUID (row index) of the element to delete.
 * @param key the key (column name) of the element to delete.
 * @return 0 if successful, else returns the error code.
 */
std::string bend_DELETE(int *sock, std::string uuid, std::string key);

#endif
