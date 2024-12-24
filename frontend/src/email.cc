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
#include <resolv.h>
#include <arpa/nameser.h>
#include <string>
#include <vector>
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <iostream>
#include <string>
#include <sstream>

void send_SMTP_command(int sockfd, const std::string& command) {
    send(sockfd, command.c_str(), command.size(), 0);
}

std::string receive_response(int sockfd) {
    char buffer[1024];
    int len = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (len > 0) {
        buffer[len] = '\0';
        return std::string(buffer);
    }
    return "";
}

std::vector<std::pair<int, std::string>> parse_MX_records(u_char* dns, int len) {
    std::vector<std::pair<int, std::string>> mxRecords; // Vector of preference:record
    ns_msg handle; // Message handle
    ns_rr rr; // Parsed record

    if (ns_initparse(dns, len, &handle) < 0) {
        std::cerr << "Error parsing DNS response" << std::endl;
        return mxRecords;
    }

    for (int i = 0; i < ns_msg_count(handle, ns_s_an); i++) {
        if (ns_parserr(&handle, ns_s_an, i, &rr) < 0) {
            std::cerr << "Error parsing resource record" << std::endl;
            continue;
        }

        // Check if parsed record is mail routing information
        if (ns_rr_type(rr) == ns_t_mx) {
            int preference = ntohs(*(uint16_t*)ns_rr_rdata(rr)); // Record preference
            char name[NS_MAXDNAME];

            // Expand the domain name for easier parsing
            if (dn_expand(ns_msg_base(handle), ns_msg_end(handle), ns_rr_rdata(rr) + 2, name, sizeof(name)) < 0) {
                std::cerr << "Error expanding domain name" << std::endl;
                continue;
            }
            mxRecords.emplace_back(preference, std::string(name));
        }
    }
    return mxRecords;
}

int connect_SMTP_server(const std::string& hostname) {
    struct addrinfo hints, *res, *p;
    int sockfd;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    // Get smtp server address info, default to port 25
    if (getaddrinfo(hostname.c_str(), "25", &hints, &res) != 0) {
        std::cerr << "Failed to resolve address for " << hostname << std::endl;
        return -1;
    }

    // Iteratively connect to address until one succeeds
    for (p = res; p != nullptr; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) continue;

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == 0) {
            freeaddrinfo(res);
            return sockfd;
        }
        close(sockfd);
    }

    // Non of the address can be connected, return error
    freeaddrinfo(res);
    std::cerr << "Could not connect to any address for " << hostname << std::endl;
    return -1;
}
