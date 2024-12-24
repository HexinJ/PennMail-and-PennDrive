#ifndef EMAIL_H
#define EMAIL_H

#include <string>
#include <vector>

// Function for connecting to an external smtp server
void send_SMTP_command(int sockfd, const std::string& command);
std::string receive_response(int sockfd);
std::vector<std::pair<int, std::string>> parse_MX_records(u_char* dns, int len);
int connect_SMTP_server(const std::string& hostname);

#endif