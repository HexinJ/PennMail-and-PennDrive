#include <stdlib.h>
#include <stdio.h>
#include <chrono>
#include <iterator>
#include <memory>
#include <iostream>
#include <strings.h>
#include <cstring>
#include <csignal>
#include <unistd.h>
#include <stdarg.h>
#include <vector>
#include <list>
#include <queue>
#include <sys/wait.h>
#include <poll.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <fstream>
#include <string>
#include <thread>
#include <fcntl.h>
#include <mutex>
#include <functional>
#include "utils.h"
#include "threads.h"

// USAGE: ./test_client <coordinator IP>:<coordinator port>

using namespace std;

void signal_handler(int signo);

int sock;
string storage_IP;
int storage_port = -1;

void signal_handler(int signo)
{
	// set each connection to non-blocking (to avoid blocking on new system calls)
	if (signo == SIGINT)
	{
		// close the listening socket and terminate
		close(sock);
		exit(0);
	}
}

void do_write(int fd, const char *buf, int len)
{
	print_debug(debug_print, "Writing to fd %d [%s]\r\n", fd, string(buf, len).c_str());
	int sent = 0;
	while (sent < len)
	{
		int n = write(fd, &buf[sent], len - sent);
		// if write fails, then close connection and close thread
		if (n < 0)
		{
			cerr << "Error with do_write" << endl;
			close(fd);
			return;
		}
		sent += n;
	}
}

// read from FD until find delim (removes delim in the returned string)
string do_read(int fd, string delim)
{
	string message;
	char buf[1024];
	while (true)
	{
		int r = read(fd, buf, sizeof(buf));
		cout << "Message: " << string(buf, r) << endl;
		if (r < 0)
		{
			cerr << "Error with do_read" << endl;
			return "";
		}
		else if (r == 0)
		{
			cerr << "Connection closed during do_read" << endl;
			return "";
		}
		else
		{
			message.append(buf, r);
			size_t pos = message.find(delim);
			if (pos != string::npos)
			{
				string complete_message = message.substr(0, pos);
				print_debug(debug_print, "Read from fd %d [%s]\r\n", fd, message.c_str());
				return complete_message;
			}
		}
	}
}

int main(int argc, char *argv[])
{
	// CTRL C handler
	signal(SIGINT, signal_handler);
	debug_print = true;

	if (argc != 2)
	{
		cerr << "*** Author:  Ally Kim (allykim)" << endl;
		exit(1);
	}

	string IP_port = argv[1];
	stringstream ss(IP_port);
	vector<string> IP_port_vector;
	string token;
	while (getline(ss, token, ':'))
	{
		IP_port_vector.push_back(token);
	}
	if (IP_port_vector.size() != 2)
	{
		cerr << "Invalid IP and port format" << endl;
		exit(1);
	}

	sock = create_connect_socket_port(IP_port, "127.0.0.1", 7005);

	while (true)
	{
		fd_set r;
		FD_ZERO(&r);
		FD_SET(STDIN_FILENO, &r);
		FD_SET(sock, &r);

		int ret = select(sock + 1, &r, NULL, NULL, NULL);
		if (ret < 0)
		{
			close(sock);
			cerr << "Error with select function" << endl;
			exit(1);
		}

		// read from stdin and send message to server
		if (FD_ISSET(STDIN_FILENO, &r))
		{
			// read from stdin
			char buf[1000];
			int n = read(STDIN_FILENO, buf, 1000);
			if (n < 0)
			{
				cerr << "Cannot read from stdin" << endl;
				close(sock);
				exit(1);
			}
			buf[n] = '\r';
			buf[n + 1] = '\n';
			n += 2;

			// send message to server
			do_write(sock, buf, n);
		}

		// receive message from server and print to terminal
		if (FD_ISSET(sock, &r))
		{
			string response = do_read(sock, "\r\n");
			if (response.empty())
			{
				close(sock);
				exit(1);
			}

			print_debug(debug_print, "Received from server: %s\n", response.c_str());
			// if message is IP and port, then store the IP and port
			if (response.find(":") != string::npos && response.find("ON") == string::npos && response.find("OFF") == string::npos)
			{
				vector<string> tokens = split(response, ' ');
				string ip_port = tokens.at(1);
				vector<string> ip_port_tokens = split(ip_port, ':');
				storage_IP = ip_port_tokens.at(0);
				storage_port = stoi(ip_port_tokens.at(1));
				// connect to the storage port
				sock = create_connect_socket(storage_IP + ":" + to_string(storage_port));
				print_debug(debug_print, "Connected to storage server %s:%d\n", storage_IP.c_str(), storage_port);
			}
		}
	}
	return 0;
}