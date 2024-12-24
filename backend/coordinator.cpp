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

using namespace std;

struct server
{
	int fd;
	std::string ip_port; // this is the full port:port:port

	bool operator==(const server &other) const
	{
		return ip_port == other.ip_port;
	}
};

namespace std
{
	template <>
	struct hash<server>
	{
		std::size_t operator()(const server &s) const noexcept
		{
			// Hash the individual members
			std::size_t h1 = std::hash<int>{}(s.fd);
			std::size_t h2 = std::hash<std::string>{}(s.ip_port);

			// Combine the hash values (using a simple method)
			return h1 ^ (h2 << 1);
		}
	};
}

// HEARTBEAT CONFIG
int HEARTBEAT_INTERVAL = 1;						// interval at which heartbeat should be sent
int HEARTBEAT_THRESHOLD = 3;					// threshhold for determining server failure (if didnt receive heartbeat in this time)
unordered_map<string, time_t> server_heartbeat; // map of server IP:port:port:port to time received heartbeat
// bool print_heartbeat = false;

volatile sig_atomic_t shutdown_server = 0;

// IP AND PORTS
string server_ip;
int frontend_max_connections = 100;
int storage_max_connections = 100;
int frontend_port;
int storage_heartbeat_port;
int storage_comm_port;

// LISTENING SOCKETS
int frontendfd;			  // listening socket for frontend
int storage_comm_fd;	  // listening socket for communication with storage
int storage_heartbeat_fd; // listening socket for heartbeat from storage

// STATIC STORAGE SERVER INFO
unordered_map<string, int> partition_info;				  // IP:port:port:port to partition number
std::unordered_map<std::string, std::string> server_info; // IP:port to IP:port:port:port

// DYNAMIC STORAGE SERVER INFO
unordered_map<string, int> comm_fd;						 // IP:port:port:port : comm FD // send message
unordered_map<int, string> primary_server;				 // partition number : primary IP:port:port:port
unordered_map<int, unordered_set<string>> alive_servers; // partition : group of alive IP:port:port:port
unordered_set<server> all_servers;						 // set of all alive servers
mutex alive_servers_mutex;								 // because accessed by both threads

// PAUSED STORAGE SERVERS
unordered_map<int, unordered_set<string>> paused_servers; // partition : set of paused servers IP:port:port:port
unordered_map<string, int> paused_servers_frontend_fd;	  // IP:port:port:port : frontend FD that requested pause

// FILE INFO
// for storage, partition:IP:port:port:port (heartbeat, coord communication, listening)
// for coordinator, IP:frontend port:listen for heartbeat port: listen for primary message port
// ip:frontend port:storage port
// for sending primary message, <PRIMARY ip:LISTENING PORT>
// end all messages with \r\n

// choose a random server that is alive within partition
// return empty string if none available
string choose_random_server(int partition)
{
	print_debug(debug_print, "Choosing random server for partition %d\n", partition);
	lock_guard<mutex> lock(alive_servers_mutex);
	auto it = alive_servers.find(partition);
	if (it == alive_servers.end())
	{
		cerr << "Partition doesnt exit in alive_servers" << endl;
		return "";
	}
	unordered_set<string> servers = it->second;
	if (servers.empty())
	{
		cerr << "No servers exist in partition" << endl;
		return "";
	}

	// check if all the servers are paused
	if (servers.size() == paused_servers[partition].size())
	{
		cerr << "All servers in partition are paused" << endl;
		return "";
	}

	// keep choosing until find a non-paused server
	while (true)
	{
		int random = rand() % servers.size();
		auto it_server = servers.begin();
		advance(it_server, random);
		string chosen_server = *it_server;
		// if the server is paused, choose again
		if (paused_servers[partition].find(chosen_server) != paused_servers[partition].end())
		{
			continue;
		}
		// if all servers are paused return empty string
		if (servers.size() == paused_servers[partition].size())
		{
			cerr << "All servers in partition are paused" << endl;
			return "";
		}

		print_debug(debug_print, "Chose server %s\n", it_server->c_str());
		return *it_server;
	}
}

// function to handle storage server failure
void handle_server_failure(string ip_port_port_port)
{
	int partition = partition_info[ip_port_port_port];
	{
		lock_guard<std::mutex> lock(alive_servers_mutex);
		// clean up server
		comm_fd.erase(ip_port_port_port);
		// remove from alive_servers
		alive_servers[partition].erase(ip_port_port_port);
		// remove from heartbeat
		server_heartbeat.erase(ip_port_port_port);
	}
	// if primary, then choose new primary and send to all servers in group
	if (primary_server[partition] == ip_port_port_port)
	{
		string new_primary = choose_random_server(partition);
		// if no available servers
		if (new_primary == "")
		{
			// remove partition from alive_servers
			{
				lock_guard<std::mutex> lock(alive_servers_mutex);
				alive_servers.erase(partition);
			}
			// remove from primary_servers
			primary_server.erase(partition);
			cerr << "No new primary server available - the only server in partition left" << endl;
			return;
		}
		lock_guard<std::mutex> lock(alive_servers_mutex);
		primary_server[partition] = new_primary;
		// send primary message to all servers in group
		vector<string> tokens = split(primary_server[partition], ':');
		// also send how many total servers are in the partition
		int num_replicas = alive_servers[partition].size();
		string primary_message = "+OK PRIMARY " + tokens.at(0) + ":" + tokens.at(3) + " " + to_string(num_replicas) + "\r\n";
		// send message to the communication port
		for (string srv : alive_servers[partition])
		{
			do_write_backend(comm_fd[srv], primary_message.data(), primary_message.size());
		}
	}
}

void clean_up_storage_thread()
{
	// close all connections
	string result = "-ERR Server shutting down\r\n";
	for (auto &srv : all_servers)
	{
		do_write_backend(srv.fd, result.data(), result.size());
		print_debug(debug_print, "Closing storage heartbeat connection %d\n", srv.fd);
		close(srv.fd);
	}
	for (auto &fd : comm_fd)
	{
		do_write_backend(fd.second, result.data(), result.size());
		print_debug(debug_print, "Closing storage comm connection %d\n", fd.second);
		close(fd.second);
	}
	close(storage_heartbeat_fd);
	print_debug(debug_print, "Closing storage heartbeat fd %d\n", storage_heartbeat_fd);
	close(storage_comm_fd);
	print_debug(debug_print, "Closing storage comm fd %d\n", storage_heartbeat_fd);

	paused_servers.clear();
}

void shutdown()
{
	shutdown_server = 1;
	clean_up_storage_thread();
	close(frontendfd);
	exit(0);
}

void signal_handler(int signo)
{
	// set each connection to non-blocking (to avoid blocking on new system calls)
	if (signo == SIGINT)
	{
		shutdown();
	}
}

// single-threaded server for handling frontend connections TCP
void accept_frontend_connections()
{
	// fronend connections
	vector<int> frontend_connections;

	// create a socket
	frontendfd = socket(PF_INET, SOCK_STREAM, 0);
	if (frontendfd < 0)
	{
		cerr << "Failure to open socket" << endl;
		shutdown();
		return;
	}

	// ensures that after shutting down the server, you can bind to the same client again right away
	int option = 1;
	setsockopt(frontendfd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &option, sizeof(option));
	struct sockaddr_in serverAddr;
	bzero(&serverAddr, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(frontend_port);
	serverAddr.sin_addr.s_addr = inet_addr(server_ip.c_str());
	if (::bind(frontendfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
	{
		cerr << "Failed to bind to frontend port" << endl;
		shutdown();
		return;
	}

	// listen for frontend connections
	if (listen(frontendfd, frontend_max_connections) < 0)
	{
		cerr << "Failed to listen for frontend connections" << endl;
		shutdown();
		return;
	}

	while (shutdown_server == 0)
	{
		fd_set r;
		FD_ZERO(&r);
		FD_SET(frontendfd, &r);
		int max_fd = frontendfd;

		vector<int> closed_connections;

		// add frontend connections
		for (int fd : frontend_connections)
		{
			// check if valid or closed
			int flag = fcntl(fd, F_GETFD);
			if (flag == -1)
			{
				close(fd);
				closed_connections.push_back(fd);
				continue;
			}
			else
			{
				FD_SET(fd, &r);
				max_fd = max(max_fd, fd);
			}
		}
		// remove closed connections
		for (int fd : closed_connections)
		{
			frontend_connections.erase(remove(frontend_connections.begin(), frontend_connections.end(), fd), frontend_connections.end());
		}

		int ret = select(max_fd + 1, &r, NULL, NULL, NULL);
		if (ret < 0)
		{
			shutdown();
			return;
		}

		// if new client
		if (FD_ISSET(frontendfd, &r))
		{
			// accept the next incoming connection
			struct sockaddr_in src;
			socklen_t srclen = sizeof(src);
			int connfd = accept(frontendfd, (struct sockaddr *)&src, &srclen);
			string client_ip_port = get_address(src);
			if (connfd < 0)
			{
				cerr << "Error with accept function in frontend thread" << endl;
				continue;
			}
			print_debug(debug_print, "New frontend connection FD: %d\n", connfd);
			print_debug(debug_print, "New frontend client IP: %s\n", client_ip_port.c_str());

			// send greeting message
			string greeting = "+OK Server ready\r\n";
			do_write_backend(connfd, greeting.data(), greeting.size());
			frontend_connections.push_back(connfd);
		}

		for (int fd : frontend_connections)
		{
			if (FD_ISSET(fd, &r))
			{
				// if receive message from frontend
				// HELO passwd user_email\r\n
				string message = do_read_backend(fd, "\r\n");
				print_debug(debug_print, "Received message from frontend: %s\n", message.c_str());

				// if the frontend closes the connection
				if (message == "CLOSED" || message == "ERROR")
				{
					cerr << "FD is closed" << endl;
					close(fd);
					continue;
				}

				// SCENARIO 1 : HELO passwd user_email
				if (message.find("HELO passwd") != string::npos)
				{
					// find available server for the client (choose randomly)
					vector<string> tokens = split(message, ' ');
					string user_email = tokens.at(2);
					int partition = get_partition(user_email);
					print_debug(debug_print, "User email: ", user_email.c_str(), partition);
					print_debug(debug_print, "User partition: %d\n", partition);
					if (partition == -1)
					{
						cerr << "Frontend server request does not contain email" << endl;
						string response = "-ERR Invalid user email\r\n";
						do_write_backend(fd, response.c_str(), response.size());
						continue;
					}

					// send server info
					// +OK storage server IP:listening port
					string chosen_ip_port = choose_random_server(partition);
					if (chosen_ip_port == "")
					{
						cerr << "No available servers for frontend server" << endl;
						string response = "-ERR No available servers\r\n";
						do_write_backend(fd, response.c_str(), response.size());
						continue;
					}

					// get the listening port from the chosen server
					vector<string> tokens_chosen = split(chosen_ip_port, ':');
					string response = "+OK " + tokens_chosen.at(0) + ":" + tokens_chosen.at(3) + "\r\n";
					do_write_backend(fd, response.c_str(), response.size());
				}
				// SCENARIO 2 : GET passwd null null
				// return list of all the current nodes and whether they’re alive or not?
				// +OK partition-IP:port-ON partition-IP:port-OFF...\r\n
				else if (message.find("GET passwd null null") != string::npos)
				{
					// go through all the servers and check if they are alive
					string response = "+OK ";

					for (const auto &pair : partition_info)
					{
						// IP:port:port:port
						string ip_port = pair.first;
						int partition = pair.second;
						string status = "OFF";
						// check if online or offline
						{
							lock_guard<mutex> lock(alive_servers_mutex);
							if (alive_servers[partition].find(ip_port) != alive_servers[partition].end() && paused_servers[partition].find(ip_port) == paused_servers[partition].end())
							{
								status = "ON";
							}
						}
						response += to_string(partition) + "-" + ip_port + "-" + status + " ";
					}
					// remove the white space at the end
					response = response.substr(0, response.size() - 1);
					response += "\r\n";
					print_debug(debug_print, "Returning list of servers %s.\n", response.c_str());
					do_write_backend(fd, response.c_str(), response.size());
				}
				// SCENARIO 3 : STOP passwd ip:port:port:port
				// +OK\r\n
				else if (message.find("STOP passwd") != string::npos)
				{
					vector<string> tokens = split(message, ' ');

					// check if valid format
					if (tokens.size() != 3)
					{
						print_debug(debug_print, "Received invalid STOP request from frontend server\n");
						string response = "-ERR Invalid request\r\n";
						do_write_backend(fd, response.c_str(), response.size());
						continue;
					}

					// remove \r\n
					string ip_port = tokens.at(2);
					while (!ip_port.empty() && (ip_port.back() == '\r' || ip_port.back() == '\n'))
					{
						ip_port.pop_back();
					}

					// check if server is valid
					if (partition_info.find(ip_port) == partition_info.end())
					{
						string response = "-ERR Cannot identify server\r\n";
						do_write_backend(fd, response.c_str(), response.size());
						continue;
					}
					int partition = partition_info[ip_port];

					// check if server is already paused
					if (paused_servers[partition].find(ip_port) != paused_servers[partition].end())
					{
						string response = "-ERR Server is already paused\r\n";
						do_write_backend(fd, response.c_str(), response.size());
						continue;
					}

					// add to paused set
					paused_servers[partition].insert(ip_port);
					print_debug(debug_print, "Inserting %s into paused_servers set\n", ip_port.c_str());
					print_debug(debug_print, "Paused servers size: %zu\n", paused_servers[partition].size());
					print_debug(debug_print, "Alive servers size: %zu\n", alive_servers[partition].size());
					// save the frontend fd that requested the pause
					paused_servers_frontend_fd[ip_port] = fd;

					// send message to server to disconnect with all frontend servers
					// respond OK to FE once receive ACK from backend
					// to the primary server, if receive STOP then need to turn is_primary off and close all primary related threads
					do_write_backend(comm_fd[ip_port], "STOP\r\n", 6);
					print_debug(debug_print, "Sent STOP message to storage server %s\n", ip_port.c_str());

					// if pausing the primary, then choose a new primary
					// send new primary to backend servers
					if (ip_port == primary_server[partition])
					{
						string new_primary = choose_random_server(partition);
						// if no available servers
						if (new_primary == "")
						{
							// remove from primary_servers
							primary_server.erase(partition);
							cerr << "No new primary server available - the only server in partition left" << endl;
						}
						else
						{
							lock_guard<std::mutex> lock(alive_servers_mutex);
							primary_server[partition] = new_primary;
							// send primary message to all servers in group
							vector<string> tokens = split(primary_server[partition], ':');
							// also send how many total servers are in the partition
							int num_replicas = alive_servers[partition].size();
							string primary_message = "+OK PRIMARY " + tokens.at(0) + ":" + tokens.at(3) + " " + to_string(num_replicas) + "\r\n";
							// send message to the communication port
							for (string srv : alive_servers[partition])
							{
								do_write_backend(comm_fd[srv], primary_message.data(), primary_message.size());
							}
							print_debug(debug_print, "Sent primary message to all servers in partition %d\n", partition);
						}
					}
				}
				// SCENARIO 4 : START passwd ip:port:port:port
				else if (message.find("START passwd") != string::npos)
				{
					// check if valid format
					vector<string> tokens = split(message, ' ');
					if (tokens.size() != 3)
					{
						print_debug(debug_print, "Received invalid START request from frontend server\n");
						string response = "-ERR Invalid request\r\n";
						do_write_backend(fd, response.c_str(), response.size());
						continue;
					}

					// remove \r\n
					string ip_port = tokens.at(2);
					while (!ip_port.empty() && (ip_port.back() == '\r' || ip_port.back() == '\n'))
					{
						ip_port.pop_back();
					}

					// check if server is valid
					if (partition_info.find(ip_port) == partition_info.end())
					{
						string response = "-ERR Cannot identify server\r\n";
						do_write_backend(fd, response.c_str(), response.size());
						continue;
					}
					// check if server is already running
					int partition = partition_info[ip_port];
					if (paused_servers[partition].find(ip_port) == paused_servers[partition].end())
					{
						string response = "-ERR Server is already running\r\n";
						do_write_backend(fd, response.c_str(), response.size());
						continue;
					}

					// remove from paused set
					paused_servers[partition].erase(ip_port);
					paused_servers_frontend_fd.erase(ip_port);

					// if there are currently no primary servers, then choose a new primary
					if (primary_server.find(partition) == primary_server.end())
					{
						string new_primary = choose_random_server(partition);
						// if no available servers
						if (new_primary == "")
						{
							// remove from primary_servers
							primary_server.erase(partition);
							cerr << "No new primary server available - the only server in partition left" << endl;
						}
						else
						{
							lock_guard<std::mutex> lock(alive_servers_mutex);
							primary_server[partition] = new_primary;
							// send primary message to all servers in group
							vector<string> tokens = split(primary_server[partition], ':');
							// also send how many total servers are in the partition
							int num_replicas = alive_servers[partition].size();
							string primary_message = "+OK PRIMARY " + tokens.at(0) + ":" + tokens.at(3) + " " + to_string(num_replicas) + "\r\n";
							// send message to the communication port
							for (string srv : alive_servers[partition])
							{
								do_write_backend(comm_fd[srv], primary_message.data(), primary_message.size());
							}
						}
					}

					// send OK to FE
					string response = "+OK Server resumed\r\n";
					do_write_backend(fd, response.c_str(), response.size());
				}
				else
				{
					cerr << "Received invalid request from frontend server" << endl;
					string response = "-ERR Invalid request\r\n";
					do_write_backend(fd, response.c_str(), response.size());
					continue;
				}
			}
		}
	}
	// close all connections
	for (int fd : frontend_connections)
	{
		close(fd);
	}
	shutdown();
}

// one event driven thread for handling storage server heartbeats TCP
void accept_storage_connections()
{
	// create socket, listen, and save FD
	storage_heartbeat_fd = create_listen_socket(storage_heartbeat_port, storage_max_connections).first;
	storage_comm_fd = create_listen_socket(storage_comm_port, storage_max_connections).first;

	print_debug(debug_print, "Server listening for heartbeat\n", storage_heartbeat_fd);
	print_debug(debug_print, "Server listening for communications\n", storage_comm_fd);

	while (shutdown_server == 0)
	{
		fd_set r;
		FD_ZERO(&r);
		FD_SET(storage_heartbeat_fd, &r); // add heartbeat socket to set
		FD_SET(storage_comm_fd, &r);	  // add communication socket to set
		int maxFD = max(storage_heartbeat_fd, storage_comm_fd);

		// add communication fd to the set
		unordered_set<string> remove_ips;
		for (const auto &pair : comm_fd)
		{
			string key = pair.first;
			int fd = pair.second;
			// if fd is closed, then remove from comm_fd
			if (fcntl(fd, F_GETFD) == -1)
			{
				close(fd);
				remove_ips.insert(key);
				continue;
			}
			FD_SET(fd, &r);
			if (fd > maxFD)
			{
				maxFD = fd;
			}
		}
		// remove closed connections
		for (string key : remove_ips)
		{
			comm_fd.erase(key);
		}
		remove_ips.clear();

		// add other fds to the set
		unordered_set<server> remove_servers;
		for (auto it = all_servers.begin(); it != all_servers.end(); it++)
		{
			int fd = it->fd;
			string key = it->ip_port;
			// check if valid or closed
			int flag = fcntl(fd, F_GETFD);
			if (flag == -1)
			{
				print_debug(debug_print, "Handling closed file descriptor while adding fd to select()\n");
				close(fd);
				handle_server_failure(it->ip_port);
				remove_servers.insert(*it);
			}
			else
			{
				FD_SET(it->fd, &r);
				if (it->fd > maxFD)
				{
					maxFD = it->fd;
				}
			}
		}

		// remove closed connections
		for (auto it = remove_servers.begin(); it != remove_servers.end(); it++)
		{
			all_servers.erase(*it);
		}
		remove_servers.clear();

		// set timeout for heartbeat
		struct timeval timeout;
		timeout.tv_sec = HEARTBEAT_INTERVAL;
		timeout.tv_usec = 0;

		int ret = select(maxFD + 1, &r, NULL, NULL, &timeout);
		if (ret < 0)
		{
			clean_up_storage_thread();
			shutdown();
			return;
		}

		// if new heartbeat connection
		if (FD_ISSET(storage_heartbeat_fd, &r))
		{
			struct sockaddr_in clientaddr;
			socklen_t clientaddrlen = sizeof(clientaddr);
			int clientfd = accept(storage_heartbeat_fd, (struct sockaddr *)&clientaddr, &clientaddrlen);
			string ip_port = get_address(clientaddr);
			// if from unidentified server
			if (server_info.find(ip_port) == server_info.end())
			{
				cerr << "Invalid connection from unidentified storage server" << endl;
				close(clientfd);
				continue;
			}
			print_debug(debug_print, "New heartbeat connection from %s\n", ip_port.c_str());
			// update heartbeat
			server_heartbeat[ip_port] = time(NULL);
			{
				lock_guard<mutex> lock(alive_servers_mutex);
				// add to set of all servers
				server srv;
				srv.fd = clientfd;
				srv.ip_port = server_info[ip_port];
				all_servers.insert(srv);
				// add to alive_servers
				int partition = partition_info[server_info[ip_port]];
				print_debug(debug_print, "New server for partition: %d\n", partition);
				auto result = alive_servers[partition].insert(server_info[ip_port]);
				if (!result.second)
				{
					print_debug(debug_print, "Insertion into alive servers failed\n");
					continue;
				}
			}
			// choose primary if not already chosen
			int partition = partition_info[server_info[ip_port]];
			if (primary_server.find(partition) == primary_server.end())
			{
				primary_server[partition] = choose_random_server(partition);
			}
		}

		// if new comm connection
		if (FD_ISSET(storage_comm_fd, &r))
		{
			struct sockaddr_in clientaddr;
			socklen_t clientaddrlen = sizeof(clientaddr);
			int clientfd = accept(storage_comm_fd, (struct sockaddr *)&clientaddr, &clientaddrlen);
			string ip_port = get_address(clientaddr);
			print_debug(debug_print, "New comm connection from %s\n", ip_port.c_str());
			// add to comm_fd
			comm_fd[server_info[ip_port]] = clientfd;
			// send current primary server info
			// if no primary server, then send ERR
			int partition = partition_info[server_info[ip_port]];
			if (primary_server.find(partition) == primary_server.end())
			{
				string response = "-ERR No primary server available\r\n";
				do_write_backend(clientfd, response.c_str(), response.size());
			}
			else
			{
				lock_guard<std::mutex> lock(alive_servers_mutex);
				vector<string> tokens = split(primary_server[partition], ':');
				string listening_port = tokens.at(3);
				string primary_ip = tokens.at(0);
				int num_replicas = alive_servers[partition].size();
				string response = "+OK PRIMARY " + primary_ip + ":" + listening_port + " " + to_string(num_replicas) + "\r\n";
				do_write_backend(clientfd, response.c_str(), response.size());
			}
		}

		// if heartbeat message from existing servers, then update timestamp
		for (auto it = all_servers.begin(); it != all_servers.end(); it++)
		{
			int fd = it->fd;
			string ip_port = it->ip_port;
			if (FD_ISSET(fd, &r))
			{
				string message = do_read_backend(fd, "\r\n");

				if (message == "CLOSED" || message == "ERROR")
				{
					cerr << "FD is closed" << endl;
					handle_server_failure(ip_port);
					close(fd);
					remove_servers.insert(*it);
				}
				else if (message != "HEARTBEAT")
				{
					cerr << "Invalid heartbeat message" << endl;
				}
				else
				{
					server_heartbeat[ip_port] = time(NULL);
				}
			}
		}

		// remove closed servers
		for (auto it = remove_servers.begin(); it != remove_servers.end(); it++)
		{
			all_servers.erase(*it);
		}
		remove_servers.clear();

		// check if any alive server has failed
		time_t now = time(NULL);
		for (auto it = all_servers.begin(); it != all_servers.end(); it++)
		{
			string ip_port = it->ip_port;
			if (server_heartbeat.find(ip_port) != server_heartbeat.end() && (now - server_heartbeat[ip_port]) > HEARTBEAT_THRESHOLD)
			{
				print_debug(debug_print, "Server %s has failed\n", ip_port.c_str());
				handle_server_failure(ip_port);
				remove_servers.insert(*it);
			}
		}

		// remove closed servers
		for (auto it = remove_servers.begin(); it != remove_servers.end(); it++)
		{
			all_servers.erase(*it);
		}
		remove_servers.clear();

		// check for ACK message from paused servers
		for (auto it = comm_fd.begin(); it != comm_fd.end(); it++)
		{
			int fd = it->second;
			string full_ip_port = it->first;
			if (FD_ISSET(fd, &r))
			{
				string message = do_read_backend(fd, "\r\n");
				print_debug(debug_print, "Received message from %s : %s\n", full_ip_port.c_str(), message.c_str());
				if (message.find("ACK") != std::string::npos)
				{
					// send message to frontend server
					string response = "+OK Server paused\r\n";
					do_write_backend(paused_servers_frontend_fd[full_ip_port], response.c_str(), response.size());
				}
				// REPLICA COUNT PARTITION
				else if (message.find("REPLICA COUNT") != std::string::npos)
				{
					lock_guard<mutex> lock(alive_servers_mutex);
					vector<string> tokens = split(message, ' ');
					int partition = stoi(tokens.at(2));
					int num_replicas = alive_servers[partition].size(); // subtract paused server size
					string response = "REPLICA COUNT " + to_string(num_replicas) + "\r\n";
					do_write_backend(fd, response.c_str(), response.size());
				}
			}
		}
	}
	shutdown();
}

// command args: ./bcoordinator -v <server_file> <storage_file>
int main(int argc, char *argv[])
{
	// SETTINGS ///////////////////////////////////////////
	signal(SIGINT, signal_handler);

	if (argc < 3)
	{
		cerr << "Usage: ./bcoordinator <server_file> <storage_file>" << endl;
		return EXIT_FAILURE;
	}

	int opt;
	while ((opt = getopt(argc, argv, "v")) != -1)
	{
		switch (opt)
		{
		case 'v':
			debug_print = true;
			break;
		default:
			cerr << "Incorrect usage of command line arguments" << endl;
			return EXIT_FAILURE;
		}
	}

	// GET SERVER INFO ////////////////////////////////////
	// get the server info from the config file
	string server_file = argv[optind];
	ifstream file(server_file);
	if (!file)
	{
		cerr << "Cannot open server config file" << endl;
		return EXIT_FAILURE;
	}
	string line;
	getline(file, line);
	vector<string> info = split(line, ':');
	// get bind address and port
	// IP:frontend port:listen for heartbeat port:send primary message port
	server_ip = info.at(0);
	frontend_port = stoi(info.at(1));
	storage_heartbeat_port = stoi(info.at(2));
	storage_comm_port = stoi(info.at(3));
	print_debug(debug_print, "Server IP: %s\n", server_ip.c_str());
	print_debug(debug_print, "Frontend port: %d\n", frontend_port);
	print_debug(debug_print, "Storage heartbeat port: %d\n", storage_heartbeat_port);
	print_debug(debug_print, "Storage comm port: %d\n", storage_comm_port);
	file.close();

	// get the storage nodes info
	string storage_file = argv[optind + 1];
	ifstream s_file(storage_file);
	if (!s_file)
	{
		cerr << "Cannot open storage config file" << endl;
		return EXIT_FAILURE;
	}
	std::string s_line;
	while (getline(s_file, s_line))
	{
		// partition : IP : (heartbeat, coord communication, listening) ports
		vector<string> parts = split(s_line, ':');
		int partition = stoi(parts.at(0));
		string ip = parts.at(1);
		int heartbeat_port = stoi(parts.at(2)); // port used to send heartbeat to coordinator
		int comm_port = stoi(parts.at(3));		// port used to communicate with coordinator
		int listening_port = stoi(parts.at(4)); // port used to listen for frontend connections
		string last_port = parts.at(5);
		// store information
		string full_id = ip + ":" + to_string(heartbeat_port) + ":" + to_string(comm_port) + ":" + to_string(listening_port) + ":" + last_port;
		server_info[ip + ":" + to_string(listening_port)] = full_id;
		server_info[ip + ":" + to_string(heartbeat_port)] = full_id;
		server_info[ip + ":" + to_string(comm_port)] = full_id;
		partition_info[full_id] = partition;
		print_debug(debug_print, "Storage Server %s has partition %d\n", full_id.c_str(), partition);
		PARTITION_COUNT = max(PARTITION_COUNT, partition);
	}
	s_file.close();

	// print debug info
	for (const auto &srv : server_info)
	{
		print_debug(debug_print, "Storage Server %s has info %s\n", srv.first.c_str(), srv.second.c_str());
	}

	print_debug(debug_print, "Partition count: %d\n", PARTITION_COUNT);

	// RUN SERVER ////////////////////////////////////////
	thread storage_thread(accept_storage_connections);
	thread frontend_thread(accept_frontend_connections);
	frontend_thread.join();
	storage_thread.join();

	return EXIT_SUCCESS;
}
