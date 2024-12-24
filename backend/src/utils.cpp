#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <cstring>
#include <string>
#include <csignal>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <filesystem>
#include <stdarg.h>
#include <vector>
#include <sys/wait.h>
#include <poll.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <algorithm>
#include <unordered_map>
#include <sstream>
#include <fstream>
#include <iostream>
#include "utils.h"
#include <string>
#include <fcntl.h>
#include <regex>
#include "externs.h"
int max_addr_len = 22;
int max_path_len = 200;

// Returns the absolute path given a local filename
std::string get_absolute_path(std::string filepath)
{
	char path[max_path_len];
	if (getcwd(path, sizeof(path)) != nullptr)
	{
		print_debug(debug_print, "Acquired current working directory: %s\n", path);
		return std::string(path) + "/" + filepath;
	}
	else
	{
		print_debug(debug_print, "Unable to get current working directory.\n");
		return filepath;
	}
}

// Helper function that checks if any of the tablets are still parsing their logfiles
bool is_recovering()
{
	return recovering[0] || recovering[1] || recovering[2] || recovering[3] || recovering[4];
}

// Creates the log queue, log file, and checkpoint file locks for all five tablets
void init_locks()
{
	for (int i = 0; i < 5; i++)
	{
		pthread_rwlock_init(&loglocks[i], NULL);
		pthread_rwlock_init(&filelocks[i], NULL);
		pthread_rwlock_init(&tablelocks[i], NULL);
	}
}

// Destroys the log queue, log file, and checkpoint file locks for all five tablets
void destroy_locks()
{
	for (int i = 0; i < 5; i++)
	{
		pthread_rwlock_destroy(&loglocks[i]);
		pthread_rwlock_destroy(&filelocks[i]);
		pthread_rwlock_destroy(&tablelocks[i]);
	}
}

// Creates the unique hashes used in replication
std::string create_hash(const std::string &message)
{
	auto now = std::chrono::system_clock::now();
	const std::time_t time = std::chrono::system_clock::to_time_t(now);
	auto time_str = std::string(std::ctime(&time));
	std::string hash_str = time_str + message;
	std::hash<std::string> hash_function;
	std::size_t hash_val = hash_function(hash_str);

	return std::to_string(hash_val);
}

// Takes an address string and returns a std::pair of IP address string and port number
std::pair<std::string, int> IP_port(std::string &addr)
{
	int col_pos = addr.rfind(':');
	std::string IP = addr.substr(0, col_pos);
	std::string port_ = addr.substr(col_pos + 1);
	int port = atoi(port_.c_str());
	std::pair<std::string, int> pair = {IP, port};

	return pair;
}

// Given a sockaddr_in object, get its address formatted as IP:port
std::string get_address(struct sockaddr_in &addr)
{
	char sender_ip[16];
	inet_ntop(AF_INET, &addr.sin_addr, sender_ip, sizeof(sender_ip));
	auto port = ntohs(addr.sin_port);
	std::string address = std::string(sender_ip) + ":" + std::to_string(port);

	return address;
}

// Prints the debug message parameter only if debug_print is set to true. Otherwise, does nothing.
void print_debug(bool debug_print, const char *message, ...)
{
	if (debug_print == true)
	{
		va_list args;
		va_start(args, message);
		vfprintf(stderr, message, args);
		va_end(args);
		// Immediately flush stderr to ensure the message prints right away.
		fflush(stderr);
	}
}

// Parses the coordinator_server.txt file to get the coordinator's address
void coord_address(const char *filename)
{
	std::ifstream file(filename);
	if (!file.is_open())
	{
		fprintf(stderr, "Error opening the configuration file.\n");
		exit(1);
	}
	std::string line;
	while (std::getline(file, line))
	{
		std::istringstream stream(line);
		std::string token;
		std::vector<std::string> tokens;
		while (std::getline(stream, token, ':'))
		{
			tokens.push_back(token);
		}
		std::string IP_addr = tokens[0];
		heartbeat_addr = IP_addr + ":" + tokens[2];
		comm_addr = IP_addr + ":" + tokens[3];
	}
	file.close();
}

// Parses the storage_servers.txt file that is passed in as a command-line arg to backend servers
// Extracts the IP address, port numbers, and partition number for the line number that specifies this server
// Adds other server addresses to the list corresponding to their partitions
void populate_addresses(const char *filename, int server_idx)
{
	std::ifstream file(filename);
	if (!file.is_open())
	{
		fprintf(stderr, "Error opening the configuration file.\n");
		exit(1);
	}
	int idx = 1;
	std::string line;
	while (std::getline(file, line))
	{
		std::istringstream stream(line);
		std::string token;
		std::vector<std::string> tokens;
		while (std::getline(stream, token, ':'))
		{
			tokens.push_back(token);
		}
		int part = atoi(tokens[0].c_str());
		std::string IP_addr = tokens[1];
		std::string list_port = tokens[4];
		std::string prim_port = tokens[5];
		std::string addr = IP_addr + ":" + list_port;
		std::string prim_addr = IP_addr + ":" + prim_port;
		if (idx == server_idx)
		{
			partition = part;
			IP = IP_addr;
			listen_port = atoi(list_port.c_str());
			primary_port = atoi(prim_port.c_str());
			heartbeat_port = atoi(tokens[2].c_str());
			comm_port = atoi(tokens[3].c_str());
			listen_address = addr;
		}
		else
		{
			forward_addresses.push_back(addr);
			partition_map[part].push_back(prim_addr); // Don't push back your own address
			print_debug(debug_print, "Added address to the list of addresses in the same partition: %d, %s\n", part, prim_addr.c_str());
		}
		idx++;
	}

	file.close();
}

// Create a socket that listens for connections using a TCP port
std::pair<int, int> create_listen_socket(int port, int max_conns)
{
	// Create a socket for the server.
	int fd = socket(PF_INET, SOCK_STREAM, 0);
	if (fd < 0)
	{
		fprintf(stderr, "Failure to open socket.\n");
		exit(1);
	}
	// This ensures that after shutting down the server, you can bind to the same client again right away.
	int option = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &option, sizeof(option));
	// Create an IVP4 server socket address, erases whatever may have previously been in the server address,
	// Sets the port to be the provided port number, then bind the server socket to this port.
	struct sockaddr_in serverAddr;
	bzero(&serverAddr, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(port);
	serverAddr.sin_addr.s_addr = htons(INADDR_ANY);
	if (bind(fd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
	{
		fprintf(stderr, "Failed to bind socket to server.\n");
		close(fd);
		exit(1);
	}
	if (listen(fd, max_conns) < 0)
	{
		fprintf(stderr, "Failed to begin listening to new connections.\n");
		close(fd);
		exit(1);
	}

	return std::make_pair(fd, port);
}

// Creates a port that binds the local_ip::local_port address to a socket and connects to the ip_address
int create_connect_socket_port(std::string ip_address, std::string local_ip, int local_port)
{
	// Create a socket for the server.
	int fd = socket(PF_INET, SOCK_STREAM, 0);
	if (fd < 0)
	{
		fprintf(stderr, "Failure to open socket.\n");
		exit(1);
	}

	// This ensures that after shutting down the server, you can bind to the same client again right away.
	int option = 1;
	// setsockopt(fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &option, sizeof(option));
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
	// setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &option, sizeof(option));  // TODO

	// bind to a specific local port
	struct sockaddr_in localAddr;
	bzero(&localAddr, sizeof(localAddr));
	localAddr.sin_family = AF_INET;
	localAddr.sin_addr.s_addr = inet_addr(local_ip.c_str());
	localAddr.sin_port = htons(local_port);

	if (bind(fd, (struct sockaddr *)&localAddr, sizeof(localAddr)) < 0)
	{
		fprintf(stderr, "Failure to bind socket to local port %d.\n", local_port);
		close(fd);
		exit(1);
	}

	// Create sockaddr_in object for the address being connected to
	struct sockaddr_in serverAddr;
	bzero(&serverAddr, sizeof(serverAddr));
	std::pair<std::string, int> addr = IP_port(ip_address);
	std::string IP_ = addr.first;
	int port_ = addr.second;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(port_);
	if (inet_pton(AF_INET, IP_.c_str(), &serverAddr.sin_addr) <= 0)
	{
		fprintf(stderr, "inet_pton failed.\n");
		close(fd);
		exit(1);
	}
	if (connect(fd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
	{
		int err = errno;
		fprintf(stderr, "Connection with coordinator failed: %s\n", strerror(err));
		close(fd);
		exit(1);
	}

	// print port

	return fd;
}

// Similar to the above function, but does not bind an ip address and port
int create_connect_socket(std::string ip_address)
{
	// Create a socket for the server.
	int fd = socket(PF_INET, SOCK_STREAM, 0);
	if (fd < 0)
	{
		fprintf(stderr, "Failure to open socket.\n");
		exit(1);
	}
	struct sockaddr_in serverAddr;
	bzero(&serverAddr, sizeof(serverAddr));
	std::pair<std::string, int> addr = IP_port(ip_address);
	std::string IP_ = addr.first;
	int port_ = addr.second;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(port_);
	if (inet_pton(AF_INET, IP_.c_str(), &serverAddr.sin_addr) <= 0)
	{
		fprintf(stderr, "inet_pton failed.\n");
		close(fd);
		exit(1);
	}
	if (connect(fd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
	{
		perror("Connection with server failed.\n");
		close(fd);
		exit(1);
	}

	// print port

	return fd;
}

// Forward the message to all other servers in the partition
void forward_msg(const std::string &message)
{
	pthread_mutex_lock(&partition_fds_lock);
	for (int fd : partition_fds)
	{
		// check if fd is closed
		if (fcntl(fd, F_GETFD) == -1 && errno == EBADF)
		{
			fprintf(stderr, "Replica fd %d is closed.\n", fd);
			// SHOULD WE REMOVE THE FD FROM THE LIST OF REPLICAS?
			continue;
		}
		// dont send message to primary itself
		if (fd == primary_fd)
		{
			continue;
		}
		do_write_backend(fd, message.c_str(), message.size());
	}
	pthread_mutex_unlock(&partition_fds_lock);
}

// Sends all the messages that are ready to send back to the frontend but were queued during checkpointing
void send_queued_messages()
{
	while (!checkpoint_queue.empty())
	{
		auto &pair = checkpoint_queue.front();
		int fd = pair.first;
		std::string &msg = pair.second;
		write(fd, msg.c_str(), msg.size());
		checkpoint_queue.pop();
	}
}

// Sends the messages queued for the previous, now downed primary to the new primary
void send_queued_primary()
{
	while (!primary_queue.empty())
	{
		std::string &msg = primary_queue.front();
		write(primary_fd, msg.c_str(), msg.size());
		primary_queue.pop();
	}
}

// In the case that the server with messages queued for the primary becomes the primary
// Currently only handles messages it would have forwarded to the primary from the frontend
void handle_primary_queue()
{
	if (!is_primary)
	{ // Just a sanity check
		return;
	}
	while (!primary_queue.empty())
	{
		std::string &msg_ = primary_queue.front();
		size_t space = msg_.find(' ');
		std::string hash_ = msg_.substr(0, space);
		std::string message_ = msg_.substr(space + 1);
		pthread_mutex_lock(&tpc_seq_lock);
		tpc_seq++;
		int this_seq = tpc_seq;
		pthread_mutex_unlock(&tpc_seq_lock);
		start_tpc(message_, hash_, this_seq);
		primary_queue.pop();
	}
}

// Splits string by delimiter and returns it as a vector of strings
std::vector<std::string> split(std::string word, char delimiter)
{
	std::vector<std::string> tokens;
	std::stringstream ss(word);

	std::string token;

	while (getline(ss, token, delimiter))
	{
		tokens.push_back(token);
	}

	return tokens;
};

// Logs the write command to the log queue, which periodically gets flushed to a file
// LOG LINE FORMAT - log sequence + message + global transaction sequence
void log_message(std::string message, int last_seq, int tablet_num)
{
	print_debug(debug_print, "[Server] Logging for tablet %d.\n", tablet_num);
	pthread_rwlock_wrlock(&loglocks[tablet_num]);
	log_sequences[tablet_num]++;
	std::string final_msg = std::to_string(log_sequences[tablet_num]) + " " + message + " " + std::to_string(last_seq);
	log_queues[tablet_num].push(std::move(final_msg));
	pthread_rwlock_unlock(&loglocks[tablet_num]);
	print_debug(debug_print, "Logged message for tablet %d.\n", tablet_num);
}

// Flushes the contents of the log queue by writing them line by line to the logfile
void flush_log(int tablet_num)
{
	std::string log_filepath = log_filepaths[tablet_num];
	std::filesystem::path file_p(log_filepath);
	if (!std::filesystem::exists(file_p))
	{																			  // If the logfile doesn't already exist, make it
		std::ofstream(file_p) << std::to_string(checkpoints[tablet_num]) << '\n'; // Create an empty file
		print_debug(debug_print, "Created empty file %s.\n", log_filepath.c_str());
	}
	pthread_rwlock_wrlock(&filelocks[tablet_num]);
	std::ofstream file(log_filepath.c_str(), std::ios::out | std::ios::app);
	if (!file)
	{
		fprintf(stderr, "Failed to open the log file %s.\n", log_filepath.c_str());
		pthread_rwlock_unlock(&filelocks[tablet_num]);
		return;
	}
	while (!log_queues[tablet_num].empty())
	{
		std::string msg = log_queues[tablet_num].front() + "||END||";
		file << msg << '\n';
		log_queues[tablet_num].pop();
		// print_debug(debug_print, "Removed from log queue %d: %s.\n", tablet_num, msg.c_str());
	}
	pthread_rwlock_unlock(&filelocks[tablet_num]);
}

// Gets the user partition number for the user email
int get_partition(const std::string &email)
{
	if (email.empty())
	{
		return -1;
	}
	// make lower case
	char first_char = tolower(email[0]);
	return (first_char % PARTITION_COUNT) + 1;
}

// Given a string that is a command, parse it and perform the relevant actions
void parsing_logic(std::string &message)
{
	size_t space0 = message.find(' ');
	if (space0 == std::string::npos)
	{ // Check that the message formatting is valid
		return;
	}
	std::string rcv = message.substr(space0 + 1);
	size_t space = rcv.find(' ');
	if (space == std::string::npos)
	{ // Check that a row value even exists in the message
		return;
	}
	std::string row = rcv.substr(0, space);
	std::string colv = rcv.substr(space + 1);
	int tablet_num = get_tablet(row);
	if (!active_tablets[tablet_num])
	{ // This should not be possible.
		print_debug(debug_print, "Attempted to parse message for tablet that is not currently loaded in memory: %d.\n", tablet_num);
		return;
	}
	if (message.substr(0, 3) == "PUT")
	{ // PUT case, create key lock if necessary and insert val into table at the specified key
		size_t space2 = colv.find(' ');
		std::string col = colv.substr(0, space2);
		std::string val = colv.substr(space2 + 1);
		Key key = {row, col};
		if (row_locks.find(key) == row_locks.end())
		{
			auto lock = std::shared_ptr<pthread_rwlock_t>(new pthread_rwlock_t, [](pthread_rwlock_t *lock)
														  {
				pthread_rwlock_destroy(lock);
				delete lock; });
			pthread_rwlock_init(lock.get(), nullptr);
			row_locks[key] = std::move(lock);
		}
		table[tablet_num][key] = std::move(val);
	}
	else if (message.substr(0, 4) == "CPUT")
	{ // CPUT case, same logic as PUT without creating the key lock because only valid CPUT commands should have been logged
		size_t space2 = colv.find(' ');
		std::string col = colv.substr(0, space2);
		std::string values = colv.substr(space2 + 1);
		Key key = {row, col};
		if (table[tablet_num].find(key) == table[tablet_num].end())
		{
			// Shouldn't encounter this case, as only valid CPUTS should be logged, but just in case
			return;
		}
		std::string &v1 = table[tablet_num][key];
		size_t loc = values.find(v1);
		std::string v2 = values.substr(loc + v1.size() + 1);
		table[tablet_num][key] = std::move(v2);
	}
	else if (message.substr(0, 6) == "DELETE")
	{ // DELETE case, remove the relevant row lock and table entry
		std::string col = colv;
		Key key = {row, col};
		if (table[tablet_num].find(key) == table[tablet_num].end())
		{
			return;
		}
		else
		{
			table[tablet_num].erase(key);
			row_locks.erase(key);
		}
	}
	else
	{
		fprintf(stderr, "Table file contains an invalid command: %s\n", message.substr(0, 20).c_str());
	}
}

// Write a line to the table file: this is the function used to write data to the disk
void write_table(const std::string &line, std::string table_filepath)
{
	std::ofstream file(table_filepath.c_str(), std::ios::out | std::ios::app);
	if (!file)
	{
		fprintf(stderr, "Failed to open the table file %s.\n", table_filepath.c_str());
		return;
	}
	file << line << '\n';
	print_debug(debug_print, "Wrote line to table file.\n");
}

// Recover in-flight table operations by parsing the messages in the file opened by the filepath
// It is used for recovering operations from the tablet's log file
void recover_table(std::string filepath, int tablet_num)
{
	std::filesystem::path file_p(filepath);
	if ((!std::filesystem::exists(file_p)) || (std::filesystem::file_size(filepath) == 0))
	{
		checkpoints[tablet_num] = 1;
		std::ofstream(file_p) << std::to_string(1) << '\n'; // Create an empty file
		print_debug(debug_print, "Created empty file %s.\n", filepath.c_str());
	}
	std::ifstream file(filepath.c_str());
	if (!file.is_open())
	{
		print_debug(debug_print, "Failed to open the log file %s.\n", filepath.c_str());
		return;
	}
	std::string checkpoint_str;
	std::getline(file, checkpoint_str); // Gets only the first line of the file, which should contain the checkpoint number
	print_debug(debug_print, "Checkpoint string from logfile is: %s\n", checkpoint_str.c_str());
	checkpoints[tablet_num] = atoi(checkpoint_str.c_str());
	std::string msg, message, row, col, value;
	std::string temp;
	bool multi_line = false;
	while (std::getline(file, msg))
	{
		if (!multi_line)
		{
			size_t space_idx = msg.find(' ');
			// std::string log_str = msg.substr(space_idx);
			// log_idx = atoi(log_str.c_str());
			message = msg.substr(space_idx + 1);
			if (message.find("||END||") == std::string::npos)
			{ // Multi-line entry
				message += "\n";
				multi_line = true;
				continue;
			}
			else
			{ // Single line entry, remove the delim and 2pc number and then process as usual.
				size_t last_sp = message.rfind(" ");
				message = message.substr(0, last_sp);
				parsing_logic(message);
				log_sequences[tablet_num]++;
				print_debug(debug_print, "Increasing log sequence for tablet %d to %d.\n", tablet_num, log_sequences[tablet_num]);
				// print_debug(debug_print, "Successfully parsed entry from log file: %s\n", message.c_str());
				value.clear();
			}
		}
		else
		{
			message += msg;
			if (message.find("||END||") != std::string::npos)
			{ // End of multi-line entry
				size_t last_sp = message.rfind(" ");
				message = message.substr(0, last_sp);
				parsing_logic(message);
				log_sequences[tablet_num]++;
				print_debug(debug_print, "Increasing log sequence for tablet %d to %d.\n", tablet_num, log_sequences[tablet_num]);
				// print_debug(debug_print, "Successfully parsed entry from log file: %s\n", message.c_str());
				value.clear();
				multi_line = false;
			}
			else
			{
				message += "\n";
			}
		}
	}
	print_debug(debug_print, "Finished parsing entries from logfile.\n");
}

// Recreate the values already saved in the tablet's checkpoint file after a crash
void restore_table(std::string filepath, int tablet_num)
{
	// std::string abs_path = get_absolute_path(filepath);
	std::filesystem::path file_p(filepath);
	if (!std::filesystem::exists(file_p))
	{
		std::ofstream(file_p) << ""; // Create an empty file
		print_debug(debug_print, "Created empty file %s.\n", filepath.c_str());
	}
	std::ifstream file(filepath.c_str());
	if (!file.is_open())
	{
		print_debug(debug_print, "Failed to open the table file %s.\n", filepath.c_str());
		return;
	}
	std::string line, row, col, value;
	bool multi_line = false;
	while (std::getline(file, line))
	{
		if (!multi_line)
		{
			std::istringstream stream(line);
			stream >> row >> col;
			std::getline(stream >> std::ws, value);
			if (value.find("||END||") == std::string::npos)
			{ // Multi-line entry
				value += "\n";
				multi_line = true;
				continue;
			}
			else
			{ // Single-line entry
				auto it = value.find("||END||");
				Key key = {row, col};
				value = value.substr(0, it);
				if (row_locks.find(key) == row_locks.end())
				{
					auto lock = std::shared_ptr<pthread_rwlock_t>(new pthread_rwlock_t, [](pthread_rwlock_t *lock)
																  {
						pthread_rwlock_destroy(lock);
						delete lock; });
					pthread_rwlock_init(lock.get(), nullptr);
					row_locks[key] = std::move(lock);
				}
				table[tablet_num][key] = value;
				print_debug(debug_print, "Successfully performed operation on row %s, col %s from table file.\n", row.c_str(), col.c_str());
				// print_debug(debug_print, "Successfully performed operation on row %s, col %s, and value %s from table file.\n", row.c_str(), col.c_str(), value.c_str());
				value.clear();
			}
		}
		else
		{
			value += line;
			if (value.find("||END||") != std::string::npos)
			{ // Reached the endline
				auto it = value.find("||END||");
				Key key = {row, col};
				value = value.substr(0, it);
				if (row_locks.find(key) == row_locks.end())
				{
					auto lock = std::shared_ptr<pthread_rwlock_t>(new pthread_rwlock_t, [](pthread_rwlock_t *lock)
																  {
						pthread_rwlock_destroy(lock);
						delete lock; });
					pthread_rwlock_init(lock.get(), nullptr);
					row_locks[key] = std::move(lock);
				}
				table[tablet_num][key] = value;
				print_debug(debug_print, "Successfully performed operation on row %s, col %s from table file.\n", row.c_str(), col.c_str());
				// print_debug(debug_print, "Successfully performed operation on row %s, col %s, and value %s from table file.\n", row.c_str(), col.c_str(), value.c_str());
				value.clear();
				multi_line = false;
			}
			else
			{
				value += "\n";
			}
		}
	}
}

// write to the FD
void do_write_backend(int fd, const char *buf, int len)
{
	// print_debug(debug_print, "Writing to fd %d [%s]", fd, std::string(buf, len).c_str());
	int sent = 0;
	while (sent < len)
	{
		int n = write(fd, &buf[sent], len - sent);
		// if write fails, then close connection and close thread
		if (n < 0)
		{
			std::cerr << "Error with do_write" << std::endl;
			close(fd);
			return;
		}
		sent += n;
	}
}

// read from FD until find delim (removes delim in the returned string)
std::string do_read_backend(int fd, std::string delim)
{
	std::string message;
	char buf[524288];
	while (true)
	{
		int r = read(fd, buf, sizeof(buf));
		// print_debug(debug_print, "Read from fd %d [%s]", fd, std::string(buf, r).c_str());
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

// Simply trims any newline characters at the end of a message
void trim_trailing_newline(std::string &str)
{
	while (!str.empty() && std::isspace(str.back()))
	{
		str.pop_back();
	}
}

// When a replica server asks a primary for its log, it must return the log file in the form of
// a string/char array. This function converts the log file into a string that can be parsed by the replica.
std::string create_log_string(int tablet_num)
{
	print_debug(debug_print, "Creating log string for tablet %d.\n", tablet_num);
	flush_log(tablet_num); // Flush the log right before creating the log string to make sure you don't miss any logs
	print_debug(debug_print, "Successfully flushed the log for tablet %d.\n", tablet_num);
	std::string log_filepath = log_filepaths[tablet_num];
	int checkpoint = checkpoints[tablet_num];
	std::filesystem::path file_p(log_filepath);
	if (!std::filesystem::exists(file_p))
	{
		std::ofstream(file_p) << std::to_string(checkpoint) << '\n'; // Create an empty file
		print_debug(debug_print, "Created empty file %s.\n", log_filepath.c_str());
	}
	print_debug(debug_print, "TEST HERE %d.\n", tablet_num);
	pthread_rwlock_rdlock(&filelocks[tablet_num]);
	std::ifstream file(log_filepath.c_str());
	if (!file.is_open())
	{
		print_debug(debug_print, "Failed to open the log file %s.\n", log_filepath.c_str());
		pthread_rwlock_unlock(&filelocks[tablet_num]);
		return "";
	}
	std::string full_log = "LOGFILE " + std::to_string(tablet_num) + " ";
	std::string message;
	while (std::getline(file, message))
	{
		full_log += message + "\n";
	}
	full_log += "\r\n";
	pthread_rwlock_unlock(&filelocks[tablet_num]);

	print_debug(debug_print, "Successfully created log string for tablet %d.\n", tablet_num);

	return full_log;
}

// Clears the file corresponding to the provided filepath
void clear_file(std::string filepath)
{
	std::ofstream file(filepath, std::ofstream::trunc);
	if (!file)
	{
		fprintf(stderr, "Failed to clear file %s.\n", filepath.c_str());
	}
}

// The function for parsing the log that was sent to the replica from the current primary
void parse_log_string(std::string &str, int tablet_num)
{
	if (str.size() == 0)
	{
		return;
	}
	std::string delim = "||END||";
	size_t start = 0;
	size_t end;
	std::string message;
	std::string msg;
	int log_idx;
	int log_seq_temp = log_sequences[tablet_num];
	int checkpoint = checkpoints[tablet_num];
	pthread_rwlock_wrlock(&filelocks[tablet_num]);
	std::ofstream file(log_filepaths[tablet_num].c_str(), std::ios::out | std::ios::app);
	size_t end_line1 = str.find('\n', start);
	std::string checkpoint_str = str.substr(start, end_line1 - start);
	if (std::filesystem::file_size(log_filepaths[tablet_num]) == 0)
	{ // Checks if current logfile even has a checkpoint number
		file << checkpoint_str << '\n';
	}
	int primary_checkpoint = atoi(checkpoint_str.c_str());
	std::string log_filepath = log_filepaths[tablet_num];
	start = end_line1 + 1;
	print_debug(debug_print, "Log sequence for tablet %d: %d\n", tablet_num, log_sequences[tablet_num]);
	print_debug(debug_print, "Successfully parsed primary checkpoint from log file: %d\n", primary_checkpoint);
	if (primary_checkpoint >= checkpoint)
	{ // The primary checkpoint is valid
		if (primary_checkpoint > checkpoint)
		{					  // If it's a new checkpoint, first clear the file and then append the new checkpoint to it
			log_seq_temp = 0; // Reset the temporary sequence number to 0
			clear_file(log_filepath);
			file.close();
			file.open(log_filepath.c_str(), std::ios::out | std::ios::app); // Reopen the file to append the checkpoint number
			if (!file.is_open())
			{
				fprintf(stderr, "Failed to reopen log file %s.\n", log_filepath.c_str());
				return;
			}
			file << std::to_string(primary_checkpoint) << '\n';
			print_debug(debug_print, "Checkpoint updated to %d.\n", primary_checkpoint);
		}
		while ((end = str.find(delim, start)) != std::string::npos)
		{
			msg = str.substr(start, end - start);
			size_t space_idx = msg.find(' ');
			std::string log_str = msg.substr(0, space_idx);
			log_idx = atoi(log_str.c_str());
			message = msg.substr(space_idx + 1);
			if ((log_idx <= log_sequences[tablet_num]) && (primary_checkpoint == checkpoint))
			{ // Same checkpoint, old message
				start = end + delim.size() + 1;
				continue;
			}
			else
			{
				file << msg << delim << '\n';
				if (active_tablets[tablet_num])
				{ // If the tablet is loaded into memory, perform the relevant table operation
					size_t last_sp = message.rfind(" ");
					message = message.substr(0, last_sp);
					parsing_logic(message);
				}
				print_debug(debug_print, "Successfully added new line to log file.\n");
				start = end + delim.size() + 1;
				log_seq_temp = std::max(log_seq_temp, log_idx);
			}
		}
		checkpoints[tablet_num] = primary_checkpoint;
	}
	else
	{
		fprintf(stderr, "The primary checkpoint is lower than the replica checkpoint. This should not be possible. %d, %d\n", primary_checkpoint, checkpoint);
		fprintf(stderr, "Actual primary checkpoint string: %s\n", checkpoint_str.c_str());
	}
	log_sequences[tablet_num] = log_seq_temp;
	pthread_rwlock_unlock(&filelocks[tablet_num]);
}

// Checks if the logfile is large enough to begin a checkpoint
bool checkpoint_eligible(std::string filepath)
{
	std::ifstream file(filepath.c_str(), std::ios::binary | std::ios::ate);
	if (!file.is_open())
	{
		fprintf(stderr, "Failed to open log file for checkpointing %s\n", filepath.c_str());
		return false;
	}
	size_t file_size = std::filesystem::file_size(filepath);
	if (file_size >= 100000000)
	{
		return true;
	}
	return false;
}

// The function called by the primary to start a new checkpoint
void start_new_checkpoint(int tablet_num)
{
	// sleep(5);
	print_debug(debug_print, "Primary is starting checkpoint for tablet %d.\n", tablet_num);
	std::string start = "CHECKPOINT " + std::to_string(tablet_num) + "\r\n";
	forward_msg(start); // the primary forwards the CHECKPOINT message to all replicas in the partition
	create_new_checkpoint(tablet_num);
}

// Helper function that takes a message string as input and parses the string according to the command at the front
// It then writes the relevant data to the table file
// NO LONGER USED
void checkpoint_helper(std::string message)
{
	// print_debug(debug_print, "Message contents: %s.\n", message.c_str());
	size_t space0 = message.find(' ');
	std::string rcv = message.substr(space0 + 1);
	// print_debug(debug_print, "rcv contents are: %s\n", rcv.c_str());
	size_t space1 = rcv.find(' ');
	if (space1 == std::string::npos)
		return;
	print_debug(debug_print, "Continuing checkpoint helper call.\n");
	std::string row = rcv.substr(0, space1);
	std::string colv = rcv.substr(space1 + 1);
	int tablet_num = get_tablet(row);
	std::string tb_filepath = table_filepaths[tablet_num];
	if (message.substr(0, 3) == "PUT")
	{
		size_t space2 = colv.find(' ');
		std::string col = colv.substr(0, space2);
		std::string val = colv.substr(space2 + 1);
		Key key = {row, col};
		std::string contents = row + " " + col + " " + val + "||END||";
		// print_debug(debug_print, "Writing line %s to table.\n", contents.c_str());
		write_table(contents, tb_filepath);
	}
	else if (message.substr(0, 4) == "CPUT")
	{
		size_t space2 = colv.find(' ');
		std::string col = colv.substr(0, space2);
		std::string values = colv.substr(space2 + 1);
		Key key = {row, col};
		if (table[tablet_num].find(key) == table[tablet_num].end())
		{
			// Shouldn't encounter this case, as only valid CPUTS should be logged, but just in case
			return;
		}
		std::string &v1 = table[tablet_num][key];
		size_t loc = values.find(v1);
		std::string v2 = values.substr(loc + v1.size() + 1);
		std::string contents = row + " " + col + " " + v2 + "||END||";
		// print_debug(debug_print, "Writing line %s to table.\n", contents.c_str());
		write_table(contents, tb_filepath);
	}
	else if (message.substr(0, 6) == "DELETE")
	{
		std::string contents = row + " " + colv + " DELETE||END||";
		// print_debug(debug_print, "Writing line %s to table.\n", contents.c_str());
		write_table(contents, tb_filepath);
	}
}

// Function for creating a new checkpoint. Creates a new checkpoint file,
// goes through all the data entries in memory, and writes them to this checkpoint file.
void create_new_checkpoint(int tablet_num)
{
	std::string table_filepath = table_filepaths[tablet_num];
	std::string log_filepath = log_filepaths[tablet_num];
	checkpointing[tablet_num] = true;
	checkpoints[tablet_num]++; // Increment the checkpoint number for this specific tablet
	pthread_rwlock_wrlock(&tablelocks[tablet_num]);
	clear_file(table_filepath); // Clear the checkpoint file before writing to it again
	std::ofstream file(table_filepath.c_str(), std::ios::out | std::ios::app);
	if (!file)
	{
		fprintf(stderr, "Failed to open the table file %s.\n", table_filepath.c_str());
		return;
	}

	std::ostringstream batch;
	for (const auto &[key, val] : table[tablet_num])
	{
		const auto &row = key.first;
		const auto &col = key.second;
		batch << row << " " << col << " " << val << "||END||\n";
	}
	file << batch.str();
	print_debug(debug_print, "Wrote entire batch to file.\n");
	pthread_rwlock_unlock(&tablelocks[tablet_num]);
	// Reset the logfile
	clear_file(log_filepath); // Clear the logfile so it's empty for the next checkpoint.
	log_sequences[tablet_num] = 0;
	std::filesystem::path file_p(log_filepath);
	if (!std::filesystem::exists(file_p))
	{
		std::ofstream(file_p) << ""; // Create an empty file
		print_debug(debug_print, "Created empty log file %s.\n", log_filepath.c_str());
	}
	pthread_rwlock_wrlock(&filelocks[tablet_num]);
	std::ofstream(file_p) << std::to_string(checkpoints[tablet_num]) << '\n';
	pthread_rwlock_unlock(&filelocks[tablet_num]);
	if (!is_primary)
	{ // If not the primary, send back an acknowledgement to the primary that checkpointing is done
		std::string checkpoint_ack = "CHECK_ACK " + std::to_string(tablet_num) + "\r\n";
		do_write_backend(primary_fd, checkpoint_ack.c_str(), checkpoint_ack.size());
		print_debug(debug_print, "Sent the checkpoint ack back to primary: %s.\n", checkpoint_ack.c_str());
	}
	checkpointing[tablet_num] = false;
}

// OPT == 1 PUT
// OPT == 2 CPUT
// OPT == 3 DELETE
std::string generate_client_response(bool success, int opt)
{
	std::string reply = success ? "+OK " : "-ERR ";

	// Action-specific messages
	const char *success_msg = nullptr;
	const char *fail_msg = nullptr;

	switch (opt)
	{
	case 1: // PUT
		success_msg = "PUT SUCCESSFUL.";
		fail_msg = "PUT FAILED.";
		break;
	case 2: // CPUT
		success_msg = "Replaced value one with value two in table.";
		fail_msg = "Could not replace value one with value two in table.";
		break;
	case 3: // DELETE
		success_msg = "DELETE successful.";
		fail_msg = "DELETE FAILED.";
		break;
	default:
		// If opt is out of range, handle gracefully or just break
		success_msg = "Unknown operation.";
		fail_msg = "Unknown operation failed.";
		break;
	}

	reply += success ? success_msg : fail_msg;
	reply += "\r\n";
	return reply;
}

// execute only after getting lock
// this function assumes you already got the locks
// this function also unlocks after executing
void execute_command(std::string command)
{
	// print_debug(debug_print, "Executing command %s.\n", command.c_str());
	std::vector<std::string> tokens = split(command, ' ');
	std::string row = tokens.at(1);
	std::string col = tokens.at(2);
	std::string message = tokens.at(0);
	int tablet_num = get_tablet(row);
	print_debug(debug_print, "Row: %s, Col: %s, Tablet: %d.\n", row.c_str(), col.c_str(), tablet_num);
	Key key = {row, col};
	if (message == "PUT")
	{
		// get the full value
		std::string val;
		for (size_t i = 3; i < tokens.size(); i++)
		{
			val += tokens.at(i);
			if (i != tokens.size() - 1)
			{
				val += " ";
			}
		}
		table[tablet_num][key] = std::move(val);
		pthread_rwlock_unlock(row_locks[key].get());
		print_debug(debug_print, "Placed value in table at row %s and col %s.\n", row.c_str(), col.c_str());
	}
	// CPUT A B C D
	else if (message == "CPUT")
	{
		// get all the values after the original val
		std::string original_val = table[tablet_num][key];
		size_t loc = command.find(original_val);
		std::string new_val = command.substr(loc + original_val.size() + 1);
		table[tablet_num][key] = std::move(new_val);
		pthread_rwlock_unlock(row_locks[key].get());
		print_debug(debug_print, "Placed value in table at row %s and col %s.\n", row.c_str(), col.c_str());
	}
	else if (message == "DELETE")
	{
		table[tablet_num].erase(key);
		pthread_rwlock_unlock(row_locks[key].get());
		row_locks.erase(key);
		print_debug(debug_print, "Deleted value in table at row %s and col %s.\n", row.c_str(), col.c_str());
	}
}

// OPT == 1 PUT
// OPT == 2 CPUT
// OPT == 3 DELETE
// Function for getting the relevant locks for a write operation. The function attempts five times, and if
// it fails after five attempts, it returns false. Otherwise, it acquires the lock and returns true.
bool get_command_locks(std::string command)
{
	// print_debug(debug_print, "Attempting to acquire locks for command %s.\n", command.c_str());
	std::vector<std::string> tokens = split(command, ' ');
	if (tokens.size() < 3)
	{
		fprintf(stderr, "Invalid command format.\n");
		return false;
	}
	std::string row = tokens.at(1);
	std::string col = tokens.at(2);
	Key key = {row, col};
	// if the key is not in the map, create a new lock
	if (row_locks.find(key) == row_locks.end())
	{
		auto lock = std::shared_ptr<pthread_rwlock_t>(new pthread_rwlock_t, [](pthread_rwlock_t *lock)
													  {
			pthread_rwlock_destroy(lock);
			delete lock; });
		pthread_rwlock_init(lock.get(), nullptr);
		row_locks[key] = std::move(lock);
	}
	int n_tries = 5;
	while (n_tries > 0)
	{
		int lock_attempt = pthread_rwlock_trywrlock(row_locks[key].get());
		if (lock_attempt == 0)
		{
			return true;
		}
		else if (lock_attempt == EBUSY)
		{
			n_tries--;
			print_debug(debug_print, "Lock is busy. Trying again.\n");
		}
		else
		{
			fprintf(stderr, "Error when attempting to acquire lock.\n");
			return false;
		}
	}
	print_debug(debug_print, "Unable to acquire lock.\n");
	return false;
}

// each server has its own tpc queue (running in the thread connected to primary)
// each server has its own last processed transaction number
void process_tpc_queue()
{
	print_debug(debug_print, "[TPC] Processing TPC queue.\n");
	pthread_mutex_lock(&tpc_queue_lock);
	int next_seq = last_seq + 1;
	while (!tpc_queue.empty() && tpc_queue.find(next_seq) != tpc_queue.end())
	{
		print_debug(debug_print, "[TPC] Processing transaction %d in TPC queue.\n", next_seq);
		struct transaction cur_t = tpc_queue[next_seq];
		std::string command = cur_t.command;
		std::string status = cur_t.status;
		std::string hash = cur_t.hash;
		if (status == "PREPARE")
		{
			// the first in queue is not ready
			print_debug(debug_print, "[TPC] First transction in TPC queue is not ready.\n");
			return;
		}
		// FORMAT : ACK:hash:tseq
		// std::string reply = "ACK:" + hash + ":" + std::to_string(next_seq) + "\r\n";
		if (status == "COMMIT")
		{
			print_debug(debug_print, "[TPC] Executing COMMAND in tpc_queue.\n", next_seq);
			// update last seq and next seq
			last_seq = next_seq;
			next_seq++;

			//  remove from queue
			tpc_queue.erase(next_seq);

			// log to file, execute command, and release locks
			size_t sp = command.find(' ');
			std::string row_ = command.substr(sp + 1);
			size_t sp2 = row_.find(' ');
			std::string row = row_.substr(0, sp2);
			int tablet_num = get_tablet(row);
			if (!active_tablets[tablet_num])
			{
				load_tablet(tablet_num);
			}
			print_debug(debug_print, "[TPC] About to log command to file.\n");
			log_message(command, next_seq, tablet_num);
			print_debug(debug_print, "[TPC] Executing command.\n", next_seq);
			// execute command releases lock
			execute_command(command);

			// if this is the server that received request from FE, then respond
			if (msg_queue.find(hash) != msg_queue.end() && msg_queue[hash]->from_frontend)
			{
				std::shared_ptr<message_queue> msg = msg_queue[hash];
				std::string row = msg->row;
				std::string reply = generate_client_response(true, msg_queue[hash]->opt);
				int tablet_num = get_tablet(row);
				print_debug(debug_print, "[Server] Getting tablet for row %s.\n", row.c_str());
				if (checkpointing[tablet_num])
				{
					print_debug(debug_print, "[Server] Checkpointing - adding to checkpoint queue: %s.\n", reply.c_str());
					checkpoint_queue.push(std::make_pair(msg->fd, reply));
				}
				else
				{
					print_debug(debug_print, "[Server] Writing back to frontend server: %s.\n", reply.c_str());
					if (msg->fd < 0)
					{
						print_debug(debug_print, "[Server] Error: Invalid frontend file descriptor.\n");
					}
					else
					{
						do_write_backend(msg->fd, reply.c_str(), reply.size());
						print_debug(debug_print, "[Server] Successfully written back to frontend server");
					}
				}
				msg_queue.erase(hash);
				print_debug(debug_print, "[Server] Erasing message from message queue.\n");
			}
		}
		else if (status == "ABORT")
		{
			print_debug(debug_print, "[TPC] Executing ABORT in tpc_queue.\n");
			// update last seq and next seq
			last_seq = next_seq;
			next_seq++;

			// remove from queue
			tpc_queue.erase(next_seq);

			// release the locks
			size_t sp = command.find(' ');
			std::string row_ = command.substr(sp + 1);
			size_t sp2 = row_.find(' ');
			std::string row = row_.substr(0, sp2);
			size_t sp3 = row_.find(' ', sp2 + 1);
			std::string col = row_.substr(sp2 + 1, sp3 - (sp2 + 1));
			Key key = {row, col};

			// only unlock if the lock is acquired
			// check if the key exists in the row_locks map
			auto it = row_locks.find(key);
			if (it != row_locks.end())
			{
				pthread_rwlock_unlock(it->second.get());
			}

			// if this is the server that received request from FE, then respond
			if (msg_queue.find(hash) != msg_queue.end() && msg_queue[hash]->from_frontend)
			{
				std::shared_ptr<message_queue> msg = msg_queue[hash];
				std::string row = msg->row;
				std::string reply = generate_client_response(false, msg_queue[hash]->opt);
				int tablet_num = get_tablet(row);
				print_debug(debug_print, "[Server] Getting tablet for row %s.\n", row.c_str());
				if (checkpointing[tablet_num])
				{
					print_debug(debug_print, "[Server] Checkpointing - adding to checkpoint queue: %s.\n", reply.c_str());
					checkpoint_queue.push(std::make_pair(msg->fd, reply));
				}
				else
				{
					print_debug(debug_print, "[Server] Writing back to frontend server: %s.\n", reply.c_str());
					if (msg->fd < 0)
					{
						print_debug(debug_print, "[Server] Error: Invalid frontend file descriptor.\n");
					}
					else
					{
						do_write_backend(msg->fd, reply.c_str(), reply.size());
						print_debug(debug_print, "[Server] Successfully written back to frontend server.\n");
					}
				}
				msg_queue.erase(hash);
				print_debug(debug_print, "[Server] Erasing message from message queue.\n");
			}
		}
	}
	pthread_mutex_unlock(&tpc_queue_lock);
}

// used by primary server to abort transaction
// assumes you already have tpc queue lock
void primary_abort_transaction(int seq_num, std::string hash)
{
	// check if already processed
	bool done = already_processed(seq_num);
	if (done)
	{
		print_debug(debug_print, "[TPC] Transaction %d is already aborted.\n", seq_num);
		return;
	}

	// add to to abort/commit set
	pthread_mutex_lock(&queued_transactions_lock);
	queued_transactions.insert(seq_num);
	pthread_mutex_unlock(&queued_transactions_lock);

	print_debug(debug_print, "[TPC] Primary is aborting transaction %d.\n", seq_num);

	// remove from tpc_responses
	pthread_mutex_lock(&tpc_response_lock);
	tpc_responses.erase(seq_num);
	pthread_mutex_unlock(&tpc_response_lock);

	// remove form tpc_start_time
	pthread_mutex_lock(&tpc_start_time_lock);
	tpc_start_time.erase(seq_num);
	pthread_mutex_unlock(&tpc_start_time_lock);

	// send message to replicas
	std::string log_message = "TPC:ABORT:" + hash + ":" + std::to_string(seq_num) + "\r\n";
	write_to_tpc_log("TPC:ABORT:" + hash + ":" + std::to_string(seq_num));
	forward_msg(log_message);
	print_debug(debug_print, "[TPC] Primary sent ABORT message to all replica.\n");

	// update status in message queue
	if (msg_queue.find(hash) != msg_queue.end())
	{
		msg_queue[hash]->tpc_success = "ABORT";
	}

	// update status in tpc_queue
	pthread_mutex_lock(&tpc_queue_lock);
	if (tpc_queue.find(seq_num) != tpc_queue.end())
	{
		tpc_queue[seq_num].status = "ABORT";
	}
	pthread_mutex_unlock(&tpc_queue_lock);

	// process tpc queue
	process_tpc_queue();
}

// used by primary server to commit transaction
void primary_commit_transaction(int seq_num, std::string hash)
{
	// check if already processed
	bool done = already_processed(seq_num);
	if (done)
	{
		print_debug(debug_print, "[TPC] Transaction %d is already committed.\n", seq_num);
		return;
	}

	// add to to abort/commit set
	pthread_mutex_lock(&queued_transactions_lock);
	queued_transactions.insert(seq_num);
	pthread_mutex_unlock(&queued_transactions_lock);

	print_debug(debug_print, "[TPC] Primary is committing transaction %d.\n", seq_num);

	// remove from tpc_responses
	pthread_mutex_lock(&tpc_response_lock);
	tpc_responses.erase(seq_num);
	pthread_mutex_unlock(&tpc_response_lock);

	// remove from tpc_start_time
	pthread_mutex_lock(&tpc_start_time_lock);
	tpc_start_time.erase(seq_num);
	pthread_mutex_unlock(&tpc_start_time_lock);

	// log and send message to replicas
	std::string log_message = "TPC:COMMIT:" + hash + ":" + std::to_string(seq_num) + "\r\n";
	write_to_tpc_log("TPC:COMMIT:" + hash + ":" + std::to_string(seq_num));
	forward_msg(log_message);
	print_debug(debug_print, "[TPC] Primary sent COMMIT message to all replica.\n");

	// update statue in message queue
	if (msg_queue.find(hash) != msg_queue.end())
	{
		msg_queue[hash]->tpc_success = "COMMIT";
	}

	// update status in tpc_queue
	pthread_mutex_lock(&tpc_queue_lock);
	if (tpc_queue.find(seq_num) != tpc_queue.end())
	{
		tpc_queue[seq_num].status = "COMMIT";
	}
	pthread_mutex_unlock(&tpc_queue_lock);

	// process tpc queue
	process_tpc_queue();
}

// when primary thread receives command from replica or from frontend
// TPC:PREPARE:hash:seq_num:command
void start_tpc(std::string command, std::string hash, int cur_seq)
{
	// wait 2pc until all the replicas connect to the primary
	// thread to sleep until the condition variable is signaled
	pthread_mutex_lock(&primary_ready_mutex);
	while (primary_recovering)
	{
		pthread_cond_wait(&primary_ready_cond, &primary_ready_mutex);
	}
	pthread_mutex_unlock(&primary_ready_mutex);

	print_debug(debug_print, "[TPC] Primary is starting 2PC process.\n");

	// add to data struct
	auto start = std::chrono::steady_clock::now();
	pthread_mutex_lock(&tpc_start_time_lock);
	tpc_start_time[cur_seq] = start;
	pthread_mutex_unlock(&tpc_start_time_lock);

	// primary put into its own queue
	struct transaction t;
	t.command = command;
	t.hash = hash;
	t.status = "PREPARE";
	pthread_mutex_lock(&tpc_queue_lock);
	tpc_queue[cur_seq] = t;
	pthread_mutex_unlock(&tpc_queue_lock);

	// identify list of alive replicas for the current transaction
	pthread_mutex_lock(&tpc_alive_replicas_lock);
	std::unordered_set<std::string> cur_replicas(alive_replicas_ipport);
	pthread_mutex_unlock(&tpc_alive_replicas_lock);
	tpc_replicas[cur_seq] = cur_replicas;
	for (std::string replica : cur_replicas)
	{
		print_debug(debug_print, "[TPC] Current replicas for TPC : %s\n", replica.c_str());
	}

	// log and send prepare to all replicas primary port
	write_to_tpc_log("TPC:PREPARE:" + hash + ":" + std::to_string(cur_seq));
	forward_msg("TPC:PREPARE:" + hash + ":" + std::to_string(cur_seq) + ":" + command + "\r\n");
	print_debug(debug_print, "[TPC] Primary sent PREPARE message to replica for transaction %d.\n", cur_seq);
}

// Function for writing to the TPC log file
void write_to_tpc_log(std::string message)
{
	print_debug(debug_print, "[TPC] Writing to TPC log: %s.\n", message.c_str());
	pthread_mutex_lock(&tpc_log_lock);
	tpc_file.clear();
	tpc_file.seekp(0, std::ios::end);
	tpc_file << message << std::endl;
	pthread_mutex_unlock(&tpc_log_lock);
	print_debug(debug_print, "[TPC] Successfully written to TPC log.\n");
}

// populate tpc pending for primary, tpc queue, tpc seq and tpc last seq
// run when server starts or gets elected as primary
void process_tpc_log()
{
	pthread_mutex_lock(&tpc_log_lock);
	// int tries = 3;
	// while (!tpc_file.is_open() && tries > 0)
	// {
	// 	print_debug(debug_print, "[TPC] Parsing TPC file but file is closed, attempting to open.\n");
	// 	tpc_file.open(tpc_filepath, std::ios::in | std::ios::out);
	// 	tries--;
	// }

	// if (!tpc_file.is_open())
	// {
	// 	print_debug(debug_print, "[TPC] Failed to open TPC file for parsing TPC file.\n");
	// 	pthread_mutex_unlock(&tpc_log_lock);
	// 	exit(1);
	// 	return;
	// }

	// clear EOF and other flags
	tpc_file.clear();
	// // move to the beginning of the file
	// tpc_file.seekg(0, std::ios::beg);

	// clear tpc_pending
	tpc_pending.clear();

	// TPC:PREPARE/COMMIT/ABORT:7017265559672412396:4
	std::string line;
	while (std::getline(tpc_file, line))
	{
		std::vector<std::string> tokens = split(line, ':');
		if (tokens.size() != 4)
		{
			print_debug(debug_print, "Invalid TPC log line: %s\n", line.c_str());
			continue;
		}
		std::string type = tokens.at(1);
		std::string hash = tokens.at(2);
		int seq = std::stoi(tokens.at(3));
		print_debug(debug_print, "[TPC] Processing TPC log line: %s %s %d.\n", type.c_str(), hash.c_str(), seq);
		// update tpc_seq number for primary
		pthread_mutex_lock(&tpc_seq_lock);
		tpc_seq = std::max(tpc_seq, seq);
		pthread_mutex_unlock(&tpc_seq_lock);

		// if line seq is lower than current tpc_seq or last_seq, then dont add to pending
		if (seq <= last_seq)
		{
			print_debug(debug_print, "[TPC] Not adding to pending because already processed.\n", seq);
			continue;
		}

		if (type == "PREPARE")
		{
			transaction t;
			t.status = type;
			t.hash = hash;
			// put into tpc_pending
			tpc_pending[seq] = t;
		}
		else if (type == "COMMIT" || type == "ABORT")
		{
			// remove from pending
			tpc_pending.erase(seq);
			// update last committed/aborted number
			last_seq = std::max(last_seq, seq);
		}
	}

	// print out pending transactions
	for (auto it = tpc_pending.begin(); it != tpc_pending.end(); it++)
	{
		print_debug(debug_print, "[TPC] Pending transaction %d.\n", it->first);
	}

	// print out last seq
	print_debug(debug_print, "[TPC] Last seq is %d.\n", last_seq);

	// print out tpc seq
	print_debug(debug_print, "[TPC] TPC seq is %d.\n", tpc_seq);

	pthread_mutex_unlock(&tpc_log_lock);
}

// function to check if the transaction is already committed/aborted
bool already_processed(int seq_num)
{
	// if transaction is already committed/aborted, then return
	pthread_mutex_lock(&tpc_last_seq_lock);
	pthread_mutex_lock(&tpc_queue_lock);
	if (seq_num <= last_seq || (tpc_queue.find(seq_num) != tpc_queue.end() && (tpc_queue[seq_num].status == "ABORT" || tpc_queue[seq_num].status == "COMMIT")))
	{
		print_debug(debug_print, "[TPC] seq num: %d | not in TPC queue: %d | last seq: %d.\n", seq_num, tpc_queue.find(seq_num) == tpc_queue.end(), last_seq);
		print_debug(debug_print, "[TPC] Transaction %d is already aborted/committed.\n", seq_num);
		pthread_mutex_unlock(&tpc_last_seq_lock);
		pthread_mutex_unlock(&tpc_queue_lock);
		return true;
	}
	pthread_mutex_unlock(&tpc_last_seq_lock);
	pthread_mutex_unlock(&tpc_queue_lock);

	// check if already marked for aborted/committed
	pthread_mutex_lock(&queued_transactions_lock);
	if (queued_transactions.find(seq_num) != queued_transactions.end())
	{
		print_debug(debug_print, "[TPC] Transaction %d is already marked for abort or commit.\n", seq_num);
		pthread_mutex_unlock(&queued_transactions_lock);
		return true;
	}
	pthread_mutex_unlock(&queued_transactions_lock);

	return false;
}

// Parses the existing files in the current working directory and checks if any of them fit the format for
// the table txt file for the current server. If so, this function checks the version number and gets the
// maximum version number of all files that fit the valid format. It then sets the global version number to
// be this max value.
void set_tablet_version()
{
	std::regex tablet_regex("table_partition" + std::to_string(partition) + "_" + std::to_string(server_idx) + "_v(\\d+)\\.txt");
	int max_v = 0;
	int version_;
	for (const auto &file : std::filesystem::directory_iterator("."))
	{
		if (file.is_regular_file())
		{
			std::string filename = file.path().filename().string();
			std::smatch match;
			if (std::regex_match(filename, match, tablet_regex))
			{
				if (match.size() == 2)
				{ // Version number was successfully captured
					version_ = std::stoi(match[1].str());
					max_v = std::max(max_v, version_);
					print_debug(debug_print, "Found version number for the tablet file: %d.\n", version_);
				}
			}
		}
	}
	version = max_v;
}

// Gets the ASCII number of the row string and takes its modulo of 5
int get_tablet(std::string row)
{
	unsigned long sum = 0;
	for (const char *char_ = row.c_str(); *char_ != '\0'; ++char_)
	{
		sum += static_cast<unsigned char>(*char_);
	}
	return (sum % 5);
}

// Checkpoints the tablet specified by tablet_num and then clears it from memory
void clear_tablet(int tablet_num)
{
	std::string filepath = log_filepaths[tablet_num];
	start_new_checkpoint(tablet_num);
	active_tablets[tablet_num] = false;
	table[tablet_num].clear();
}

// Function for a thread specifically forked for clearing a tablet from memory, done so
// multiple tablets can be cleared in parallel
void *clear_func(void *args)
{
	arg_params *params = static_cast<arg_params *>(args);
	int tablet_num = params->tablet_num;
	int msgID = params->conn_id;
	pthread_t threadc = params->thread;

	clear_tablet(tablet_num);
	print_debug(debug_print, "Cleared tablet %d from memory.\n", tablet_num);

	// remove from active threads and fds and add to available connections
	pthread_rwlock_wrlock(&rwlock);
	avail_connections.push(msgID);
	active_threads.remove(threadc);
	pthread_rwlock_unlock(&rwlock);
	pthread_exit(NULL);
}

// Function for creating the threads for clearing the tablets from memory in parallel
void create_clear_thread(int tablet_num)
{
	int code;
	pthread_rwlock_wrlock(&rwlock);
	int msgID = avail_connections.front();
	avail_connections.pop();
	pthread_rwlock_unlock(&rwlock);
	arg_params clear;
	clear.tablet_num = tablet_num;
	clear.conn_id = msgID;
	clear.thread = threads[msgID];
	arg_structs[msgID] = clear;
	code = pthread_create(&threads[msgID], NULL, &clear_func, static_cast<void *>(&arg_structs[msgID]));
	// If error, add the msgID back to the queue of available connections.
	if (code != 0)
	{
		pthread_rwlock_wrlock(&rwlock);
		avail_connections.push(msgID);
		pthread_rwlock_unlock(&rwlock);
		fprintf(stderr, "Failed to create thread for for clearing tablet.\n");
	}
	code = pthread_detach(threads[msgID]);
	if (code != 0)
	{
		fprintf(stderr, "Failed to detach thread.\n");
	}
}

// Function for loadding a tablet into memory
// It first parses the tablet's checkpoint file, then parses its logfile, and performs table operations accordingly
void load_tablet(int tablet_num)
{
	std::string filepath = table_filepaths[tablet_num];
	size_t avail_mem = total_avail_memory();
	size_t file_size_tb = std::filesystem::file_size(table_filepaths[tablet_num]);
	size_t file_size_log = std::filesystem::file_size(log_filepaths[tablet_num]);
	int idx = 0;
	while ((avail_mem - (file_size_tb + file_size_log) <= avail_mem * 0.5) && (idx < 5))
	{ // Clear existing tablets until there is enough memory
		if (active_tablets[idx])
		{
			create_clear_thread(idx);
		}
		idx++;
		avail_mem = total_avail_memory();
	}
	active_tablets[tablet_num] = true;
	restore_table(table_filepaths[tablet_num], tablet_num);
	recover_table(log_filepaths[tablet_num], tablet_num);
}

// Only works for Linux systems
// Opens the meminfo file and extracts the amount of currently available memory in the system,
// returns it in bytes
size_t total_avail_memory()
{
	std::ifstream memfile("/proc/meminfo");
	std::string line;
	size_t avail_memory = 0;
	if (!memfile.is_open())
	{
		fprintf(stderr, "Failed to open memfile.\n");
		return 0;
	}
	while (std::getline(memfile, line))
	{
		if (line.find("MemAvailable:") == 0)
		{
			std::istringstream iss(line);
			std::string mem, unit;
			iss >> mem >> avail_memory >> unit;
			avail_memory *= 1024;
			break;
		}
	}
	memfile.close();

	return avail_memory;
}

// Helper function that creates the filenames for the log and checkpoint filepaths for each tablet
// upon startup
void create_files()
{
	for (int i = 0; i < 5; i++)
	{
		std::string log_filepath = log_filepaths[i];
		std::string table_filepath = table_filepaths[i];
		std::filesystem::path file_p_log(log_filepath);
		std::filesystem::path file_p_tb(table_filepath);
		if (!std::filesystem::exists(file_p_log))
		{
			std::ofstream(file_p_log) << ""; // Create an empty file
		}
		if (!std::filesystem::exists(file_p_tb))
		{
			std::ofstream(file_p_tb) << ""; // Create an empty file
		}
	}
}

// Upon startup, begin loading tablets into memory until up to half of the previously available memory is filled.
void load_tablets_startup()
{
	size_t avail_mem = total_avail_memory();
	size_t max_threshold = avail_mem * 0.5;
	for (int i = 0; i < 5; i++)
	{
		size_t file_size_tb = std::filesystem::file_size(table_filepaths[i]);
		size_t file_size_log = std::filesystem::file_size(log_filepaths[i]);
		if (avail_mem - (file_size_tb + file_size_log) <= max_threshold)
		{ // Not enough memory to load in tablet
			print_debug(debug_print, "Unable to load next tablet %d: avail memory: %zu; combined file_size: %zu.\n", i, avail_mem, file_size_tb + file_size_log);
			continue;
		} else
		{
			active_tablets[i] = true;
			restore_table(table_filepaths[i], i);
			recover_table(log_filepaths[i], i);
			print_debug(debug_print, "Loaded tablet %d into memory.\n", i);
			avail_mem = total_avail_memory();
		}
	}
}

// Function for parsing a checkpoing file when the admin console requests table
// contents for a tablet that is not currently in memory
std::string parse_checkpoint_file(std::string filepath)
{
	std::string return_str = "";
	std::filesystem::path file_p(filepath);
	if (!std::filesystem::exists(file_p))
	{
		std::ofstream(file_p) << ""; // Create an empty file
		print_debug(debug_print, "Created empty file %s.\n", filepath.c_str());
	}
	std::ifstream file(filepath.c_str());
	if (!file.is_open())
	{
		print_debug(debug_print, "Failed to open the table file %s.\n", filepath.c_str());
		return "";
	}
	std::string msg;
	while (std::getline(file, msg))
	{
		if (msg.find("||END||") == std::string::npos)
		{ // Line that doesn't contain end of entry
			return_str += (msg + "\n");
		}
		else
		{ // Line that contains end of entry
			return_str += msg;
		}
	}

	return return_str;
}

// Function for parsing the log file when admin console requests table contents
// for a tablet that is currently not in memory (currently not in use)
std::string parse_log_file(std::string filepath)
{
	std::string return_str = "";
	std::filesystem::path file_p(filepath);
	if (!std::filesystem::exists(file_p))
	{
		std::ofstream(file_p) << ""; // Create an empty file
		print_debug(debug_print, "Created empty file %s.\n", filepath.c_str());
	}
	std::ifstream file(filepath.c_str());
	if (!file.is_open())
	{
		print_debug(debug_print, "Failed to open the table file %s.\n", filepath.c_str());
		return "";
	}
	std::string checkpoint_str;
	std::getline(file, checkpoint_str); // Gets only the first line of the file, which should contain the checkpoint number
	// Get remaining lines of logfile now
	std::string msg, message, message_;
	bool multi_line = false;
	while (std::getline(file, msg))
	{
		if (!multi_line)
		{
			size_t space_idx = msg.find(' ');
			message = msg.substr(space_idx + 1);
			if (message.find("||END||") == std::string::npos)
			{ // First line of a multi-line entry
				return_str += (message + "\n");
				multi_line = true;
			}
			else
			{ // Single line entry, remove the delim and 2pc number and then process as usual.
				size_t last_sp = message.rfind(" ");
				message_ = message.substr(0, last_sp);
				return_str += (message_ + "||END||");
			}
		}
		else
		{
			if (msg.find("||END||") != std::string::npos)
			{ // End of multi-line entry
				size_t last_sp = msg.rfind(" ");
				message = msg.substr(0, last_sp); // Remove the delim and 2pc number
				return_str += (message + "||END||");
				multi_line = false;
			}
			else
			{
				return_str += (msg + "\n");
			}
		}
	}

	return return_str;
}

// Makes a string of all the current key value pairs loaded into memory
std::string make_kv_pairs()
{
	std::string kv_pairs = "";
	for (int i = 0; i < 5; i++)
	{
		if (active_tablets[i])
		{
			for (const auto &[key, val] : table[i])
			{
				const auto &row = key.first;
				const auto &col = key.second;
				kv_pairs += (row + " " + col + " " + val + "||END||");
				print_debug(debug_print, "Accessed row %s and col %s from tablet %d.\n", row.c_str(), col.c_str(), i);
			}
			print_debug(debug_print, "Accessed existing entries in loaded tablet %d.\n", i);
		}
		else
		{
			std::string entries_tb = parse_checkpoint_file(table_filepaths[i]);
			// std::string entries_log = parse_log_file(log_filepaths[i]);
			kv_pairs += entries_tb; // TODO: is attempting to include logfile data worth it?
			print_debug(debug_print, "Accessed entries from checkpoint and log files %d.\n", i);
		}
	}
	kv_pairs += "\r\n";

	return kv_pairs;
}

// int read_fd_last_line(int fd)
// {
// 	print_debug(debug_print, "Reading last line from file descriptor %d.\n", fd);

// 	if (fd < 0)
// 	{
// 		perror("Invalid file descriptor");
// 		return 0;
// 	}

// 	// check if file is empty
// 	struct stat file_stat;
// 	if (fstat(fd, &file_stat) == -1)
// 	{
// 		perror("Failed to get file status");
// 		return 0;
// 	}

// 	off_t file_total_size = file_stat.st_size;
// 	if (file_total_size == 0)
// 	{
// 		print_debug(debug_print, "2pc file is empty.\n");
// 		return 0;
// 	}

// 	// Move to the end of the file
// 	off_t file_size = lseek(fd, 0, SEEK_END);
// 	if (file_size == -1)
// 	{
// 		perror("Failed to seek to the end of the file");
// 		return -1;
// 	}

// 	// Read backwards to find the last newline character
// 	std::vector<char> buffer(1);
// 	std::string last_line;
// 	off_t pos = file_size - 1;
// 	while (pos >= 0)
// 	{
// 		if (lseek(fd, pos, SEEK_SET) == -1)
// 		{
// 			perror("Failed to seek in the file");
// 			return -1;
// 		}
// 		if (read(fd, buffer.data(), 1) != 1)
// 		{
// 			perror("Failed to read from the file");
// 			return -1;
// 		}
// 		if (buffer[0] == '\n' && !last_line.empty())
// 		{
// 			break;
// 		}
// 		last_line.insert(last_line.begin(), buffer[0]);
// 		pos--;
// 	}

// 	print_debug(debug_print, "Last line from file: %s\n", last_line.c_str());

// 	std::vector<std::string> tokens = split(last_line, ':');
// 	if (tokens.size() < 4)
// 	{
// 		print_debug(debug_print, "Invalid last line format: %s\n", last_line.c_str());
// 		return -1;
// 	}

// 	return stoi(tokens.at(3));
// }

// void parse_tpc_log(std::string message)
// {
// 	tpc_log_count++;
// 	print_debug(debug_print, "Parsing TPC log message: %s\n", message.c_str());

// 	// if message is empty, then return
// 	if (message == "" || message.find("ERROR") != std::string::npos)
// 	{
// 		return;
// 	}

// 	// parse the log string
// 	std::istringstream stream(message);
// 	std::string line;

// 	int count = 0;
// 	// HASH:SEQ:STATUS:COMMAND
// 	// delimiter is |
// 	while (std::getline(stream, line, '|'))
// 	{
// 		print_debug(debug_print, "2pc log line: %s\n", line.c_str());
// 		if (count == 0)
// 		{
// 			print_debug(debug_print, "First log line is %s\n", line.c_str());
// 			tpc_seq = std::max(tpc_seq, stoi(line));
// 			count++;
// 		}
// 		else
// 		{
// 			print_debug(debug_print, "Parsing 2pc log line: %s\n", line.c_str());
// 			std::vector<std::string> tokens = split(line, ':');
// 			std::string hash = tokens.at(0);
// 			int seq_num = std::stoi(tokens.at(1));
// 			std::string status = tokens.at(2);
// 			std::string command = tokens.at(3);
// 			// if not in the pending queue
// 			pthread_mutex_lock(&tpc_pending_lock);
// 			if (tpc_pending.find(seq_num) == tpc_pending.end())
// 			{
// 				struct transaction t;
// 				t.command = command;
// 				t.hash = hash;
// 				t.status = status;
// 				tpc_pending[seq_num] = t;
// 			}
// 			pthread_mutex_unlock(&tpc_pending_lock);
// 		}
// 	}
// }

// std::string create_tpc_log_string(int tpc_fd)
// {
// 	print_debug(debug_print, "Creating TPC log string.\n");
// 	// close file descriptor and reopen
// 	close(tpc_fd);
// 	tpc_fd = open(tpc_filepath.c_str(), O_RDWR | O_CREAT, 0644);
// 	// read line by line
// 	std::string file_contents;
// 	char tpc_buffer[65536];

// 	ssize_t bytes_read;
// 	while ((bytes_read = read(tpc_fd, tpc_buffer, 65536)) > 0)
// 	{
// 		print_debug(debug_print, "Read from TPC log file: %s\n", std::string(tpc_buffer, bytes_read).c_str());
// 		file_contents.append(tpc_buffer, bytes_read);
// 	}

// 	if (bytes_read == -1)
// 	{
// 		perror("Failed to read the file");
// 		return "ERROR";
// 	}

// 	// read line by line and only send the ones that did not END
// 	std::istringstream stream(file_contents);
// 	std::string line;

// 	// list of transactions to send
// 	std::map<int, std::string> t_hash;
// 	std::map<int, std::string> t_status;
// 	std::map<int, std::string> t_command;

// 	int max_transaction = 0;
// 	while (std::getline(stream, line, '\n'))
// 	{
// 		if (!line.empty() && line.back() == '\r')
// 		{
// 			line.pop_back();
// 		}
// 		std::vector<std::string> tokens = split(line, ':');
// 		if (tokens.size() < 4)
// 		{
// 			print_debug(debug_print, "Invalid TPC log line: %s\n", line.c_str());
// 			continue;
// 		}
// 		std::string type = tokens.at(1);
// 		int seq = stoi(tokens.at(3));
// 		max_transaction = std::max(max_transaction, seq);
// 		// TPC:PREPARE:hash:seq_num:command
// 		if (type == "PREPARE")
// 		{
// 			t_hash[seq] = tokens.at(2);
// 			t_command[seq] = tokens.at(4);
// 		}
// 		// TPC:COMMIT:hash:seq_num:command
// 		// TPC:ABORT:hash:seq_num:command
// 		else if (type == "COMMIT" || type == "ABORT")
// 		{
// 			t_status[seq] = type;
// 		}
// 		// TPC:END:hash:seq_num
// 		else if (type == "END")
// 		{
// 			// remove from all maps
// 			t_hash.erase(seq);
// 			t_command.erase(seq);
// 			t_status.erase(seq);
// 		}
// 	}

// 	// send string HASH:SEQ:STATUS:COMMAND
// 	std::string final_message = std::to_string(max_transaction) + "|";
// 	for (auto it = t_hash.begin(); it != t_hash.end(); it++)
// 	{
// 		int seq = it->first;
// 		std::string hash = it->second;
// 		std::string command = t_command[seq];
// 		std::string status = t_status[seq];

// 		final_message += hash + ":" + std::to_string(seq) + ":" + status + ":" + command + "|";
// 	}

// 	print_debug(debug_print, "Final message for TPC log: %s\n", final_message.c_str());

// 	return final_message;
// }
