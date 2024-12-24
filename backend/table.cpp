#include <stdlib.h>
#include <stdio.h>
#include <chrono>
#include <memory>
#include <iostream>
#include <strings.h>
#include <cstring>
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
#include <fcntl.h>
#include <unistd.h>
#include <filesystem>
#include "utils.h"
#include "threads.h"
typedef std::vector<char> Buf;
typedef std::pair<std::string, int> addr_pair;
typedef std::pair<std::string, std::string> Key;
typedef std::chrono::time_point<std::chrono::system_clock> time_point;
typedef std::unordered_map<std::string, std::shared_ptr<message_queue>> MSG_QUEUE;

/*
Functions to begin outlining
Automatic heartbeat message to send to masternode, but only if you're the primary (in progress)
Detection of node failure through not getting heartbeat (masternode only) (in progress)
Electing new primary node
Replication protocol (done, at least the first attempt at implementing it is)
Crash recovery (executing log messages)
Deciding when to push log queue to file
Writing the changes to the table to the table file
Loading the data from the table file to the table data structure
Figuring out a way to remove the relevant line in a logfile after the operation has actually been performed
*/

void listen_heartbeat(std::string message)
{
	if (message == "HEARTBEAT\r\n")
	{
		heartbeat_timestamp = std::time(nullptr);
	}
}

void monitor_heartbeat(int timeout, int grace_period)
{
	while (true)
	{
		sleep(timeout);
		std::time_t now = std::time(nullptr);
		double time_diff = std::difftime(now, heartbeat_timestamp);
		if (time_diff > timeout + grace_period)
		{
			continue; // Filler line
		}
	}
}

void shut_down_func()
{
	// Close every active connection, meaning every fd in the active_fds list.
	// Also close every active thread, which should only be the threads in the active_threads list.
	const char *message = "-ERR Server shutting down\r\n";
	for (int fd : active_fds)
	{
		write(fd, message, strlen(message));
		print_debug(debug_print, "Closing socket %d\n", fd);
		close(fd);
	}
	for (pthread_t thread : active_threads)
	{
		print_debug(debug_print, "Closing thread.\n");
		pthread_join(thread, NULL);
	}
	if (is_primary)
	{
		pthread_join(tpc_tid, NULL); // join the tpc checking thread
	}
	active_fds.clear(); // Clear the buffer of active fds.
	active_threads.clear();
	msg_queue.clear();
	row_locks.clear();
	exit(3);
}

// Signal handler function automatically triggered when the user passes Ctrl + C
void shut_down(int param)
{
	shutting_down = 1;
	shut_down_func();
}

int main(int argc, char *argv[])
{
	if (argc < 4)
	{
		std::cerr << "Usage: ./btable <server_file> <storage_file> <server_idx>" << std::endl;
		return EXIT_FAILURE;
	}

	int opt;
	int argv_skip = 0;
	std::string config_filepath;
	std::string coord_filepath;
	std::string IP;
	int msgID;
	int code;
	signal(SIGINT, shut_down);

	// Using getopt() to parse option arguments from the command line.
	while ((opt = getopt(argc, argv, "v")) != -1)
	{
		switch (opt)
		{
		case 'v':
			// Print debug output to stderr.
			debug_print = true;
			argv_skip += 1;
			continue;
		case '?':
			// Some other command line argument or an -n argument without a number was given.
			// Raise an error and exit.
			fprintf(stderr, "Invalid command option.\n");
			exit(1);
		}
	}

	int arg_i = 1 + argv_skip;
	coord_filepath = argv[arg_i];
	config_filepath = argv[arg_i + 1];
	server_idx = atoi(argv[arg_i + 2]);
	coord_address(coord_filepath.c_str());
	populate_addresses(config_filepath.c_str(), server_idx);
	print_debug(debug_print, "Heartbeat address and coord address: %s, %s \n", heartbeat_addr.c_str(), comm_addr.c_str());
	for (int i = 0; i < 5; i++)
	{
		log_filepaths[i] = "server_log_" + std::to_string(server_idx) + "_tablet_" + std::to_string(i) + ".txt";
		table_filepaths[i] = "checkpoint_file_" + std::to_string(server_idx) + "_tablet_" + std::to_string(i) + ".txt";
	}

	create_files();
	// Load at least one tablet into memory
	load_tablets_startup();
	tpc_filepath = "tpc_log" + std::to_string(server_idx) + ".txt";
	print_debug(debug_print, "[TPC] TPC log file path: %s\n", tpc_filepath.c_str());

	// Before accepting any clients, all client IDs are available to use.
	for (int i = 0; i < max_clients; ++i)
	{
		avail_connections.push(i);
	}

	// Create sockets for the server to connect to the coordinator (for heartbeat and communications)
	int heartbeatfd = create_connect_socket_port(heartbeat_addr, "127.0.0.1", heartbeat_port);
	int coordfd = create_connect_socket_port(comm_addr, "127.0.0.1", comm_port);

	// Then create a socket for the server to listen in on new connections
	std::pair<int, int> pairl = create_listen_socket(listen_port, max_clients);
	int listenfd = pairl.first;

	create_thread(true, false, heartbeatfd);
	create_thread(false, true, coordfd);
	created_heartbeat = true;
	print_debug(debug_print, "Successfully connected to coordinator.\n");

	for (int i = 0; i < 5; i++)
	{
		create_log_thread(i);
	}
	print_debug(debug_print, "Successfully created log threads.\n");

	// get the last committed/aborted transaction number
	// get the most recently ongoing transaction number (may have been comitted or ongoing)
	// get any pending transactions (if primary)
	// create file if doesnt exist
	tpc_file.open(tpc_filepath, std::ios::in | std::ios::out);
	if (!tpc_file.is_open())
	{
		print_debug(debug_print, "[TPC] Creating new TPC log file.\n");
		std::filesystem::path file_p(tpc_filepath);
		if ((!std::filesystem::exists(file_p)) || (std::filesystem::file_size(tpc_filepath) == 0))
		{
			// Create an empty file
			std::ofstream create_file(tpc_filepath, std::ios::out);
			if (!create_file.is_open())
			{
				print_debug(debug_print, "[TPC] Failed to create TPC log file %s.\n", tpc_filepath.c_str());
				exit(1);
			}
			print_debug(debug_print, "[TPC] Created empty TPC log file %s.\n", tpc_filepath.c_str());
		}

		// Reopen the file in read/write mode
		tpc_file.open(tpc_filepath, std::ios::in | std::ios::out);
		if (!tpc_file.is_open())
		{
			print_debug(debug_print, "[TPC] Failed to reopen TPC log file %s for reading.\n", tpc_filepath.c_str());
			exit(1);
		}
	}
	process_tpc_log();

	// for (int idx = 0; idx < 5; idx++) {
	// create_clear_thread(idx);
	// }

	// No interrupt from terminal
	while (shutting_down == 0)
	{
		// Create an IPV4 client socket address, have the server accept the next incoming connection,
		// and save the return value as the client fd.
		struct sockaddr_in src;
		socklen_t srclen = sizeof(src);
		int connfd = accept(listenfd, (struct sockaddr *)&src, &srclen);
		print_debug(debug_print, "[%d] New connection\n", connfd);
		std::string connection_addr = get_address(src);

		// Check if there is a connection error. If so, go back to the beginning of the while loop.
		if (connfd < 0)
		{
			fprintf(stderr, "Client accept failed for client %d.\n", connfd);
			continue;
		}

		// When there are no more sockets to connect to, sleep for five seconds and then check again if one becomes available.
		while (avail_connections.empty())
		{
			print_debug(debug_print, "Waiting for a socket to become free.\n");
			sleep(5);
		}
		if (connection_addr == primary_addr)
		{
			primary_fd = connfd;
		}
		print_debug(debug_print, "Received a connection from the following connection address: %s\n", connection_addr.c_str());

		// Choose the first available client ID and remove it from the list of available client IDs.
		// Then define the struct of thread args for this client ID.
		pthread_rwlock_wrlock(&rwlock);
		msgID = avail_connections.front();
		avail_connections.pop();
		pthread_rwlock_unlock(&rwlock);
		arg_params args;
		args.conn_fd = connfd; // connection FD
		args.conn_id = msgID;
		args.thread = threads[msgID];		 // thread ID for thread servicing this connection
		args.conn_address = connection_addr; // client address
		args.backend_conn = false;

		// This message came from another table server
		// TODO: there's a good chance I can safely remove this.
		if (std::find(forward_addresses.begin(), forward_addresses.end(), connection_addr) != forward_addresses.end())
		{
			args.backend_conn = true;
			storage_fds.push_back(connfd);
			print_debug(debug_print, "Added backend storage fd to the list of storage fds from addr %s.\n", connection_addr.c_str());
		}

		std::vector<std::string> &part_addresses = partition_map[partition];
		// If this server is primary and connection is from another replica
		if ((std::find(part_addresses.begin(), part_addresses.end(), connection_addr) != part_addresses.end()) && is_primary)
		{
			args.backend_conn = true;
			// add to partition FDs
			pthread_mutex_lock(&partition_fds_lock);
			partition_fds.push_back(connfd);
			pthread_mutex_unlock(&partition_fds_lock);

			// add to alive replicas IP port
			pthread_mutex_lock(&tpc_alive_replicas_lock);
			alive_replicas_ipport.insert(connection_addr); // add IP port info to set of alive replicas
			pthread_mutex_unlock(&tpc_alive_replicas_lock);

			// add to replicas threads
			replica_threads.insert(threads[msgID]);
		}
		arg_structs[msgID] = args;

		// if frontend connection, then save thread and fd (for admin console)
		if (!args.backend_conn)
		{
			frontend_fds.insert(connfd);
			frontend_threads.insert(threads[msgID]);
		}

		// create a new thread to service this connection.
		code = pthread_create(&threads[msgID], NULL, &start_func, static_cast<void *>(&arg_structs[msgID]));

		// If error, close the socket and add the msgID back to the queue of available connections.
		if (code != 0)
		{
			close(connfd);
			pthread_rwlock_wrlock(&rwlock);
			avail_connections.push(msgID);
			pthread_rwlock_unlock(&rwlock);
			// remove from data structs
			if (args.backend_conn)
			{
				storage_fds.pop_back();
				partition_fds.pop_back();
			}
			else
			{
				frontend_fds.erase(connfd);
				frontend_threads.erase(threads[msgID]);
			}
		}
	}

	// Close the server sockets.
	close(listenfd);
	close(heartbeatfd);
	close(coordfd);
	// Destroy pthread mutexes.
	pthread_rwlock_destroy(&rwlock);
	destroy_locks();
	// Destory all tpc response locks
	pthread_mutex_destroy(&tpc_response_lock);
	pthread_cond_destroy(&primary_ready_cond);
	pthread_mutex_destroy(&primary_ready_mutex);
	pthread_mutex_destroy(&tpc_queue_lock);
	pthread_mutex_destroy(&tpc_pending_lock);
	pthread_mutex_destroy(&tpc_last_seq_lock);
	pthread_mutex_destroy(&tpc_log_lock);
	print_debug(debug_print, "Shutting down server.\n");
	shut_down_func(); // Remove any remaining sockets or threads, if there are any.

	return 0;
}
