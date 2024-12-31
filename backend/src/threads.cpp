#include <stdlib.h>
#include <stdio.h>
#include <chrono>
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
#include <fstream>
#include <fcntl.h>
#include <errno.h>
#include <string>
#include <string.h>
#include <iostream>
#include "threads.h"
#include "utils.h"
typedef std::vector<char> Buf;
// typedef std::pair<std::string, std::string> Key;

// Handling failures
// when primary recovers, ask all replicas for their log file
// primary create a new comprehensive log file
// primary send any missing transactions to replicas
// if there are any incomplete transactions, then just send ABORT to all replicas
// primary gets the last transaction number as MAX

// GET LOGS AND IDENTIFY LAST TRANSACTION NUMBER
// PRIMARY - log when starting 2PC, when send COMMIT/ABORT, when receive ACK from replicas, then SENT
// REPLICA - log when receive PREPARE, when send YES/NO, when receive COMMIT/ABORT (then log that to the checkpoint log as well)

// NOTES //////////////////////////////////////////////////////////////////////////////////////////////////////////
// Write to checkpoing log only when COMMIT commands
// Write to tpc log when PREPARE, COMMIT, ABORT, END

// Periodically sends the heartbeat message to the coordinator
void send_heartbeat(int fd)
{
	std::string heartbeat = "HEARTBEAT\r\n";
	int bytes_written = write(fd, heartbeat.c_str(), heartbeat.size());
	if (bytes_written <= 0)
	{
		fprintf(stderr, "Failed to send heartbeat message.\n");
	}
}

// Every second, the thread with this function calls the send_heartbeat() function
void *heartbeat_func(void *args)
{
	arg_params *params = static_cast<arg_params *>(args);
	int fd = params->conn_fd;
	int msgID = params->conn_id;
	pthread_t threadc = params->thread;

	while (shutting_down == 0)
	{
		send_heartbeat(fd);
		sleep(1);
	}

	// remove from active threads and fds and add to available connections
	pthread_rwlock_wrlock(&rwlock);
	avail_connections.push(msgID);
	active_threads.remove(threadc);
	pthread_rwlock_unlock(&rwlock);
	pthread_exit(NULL);
}

// Every three seconds, check if the logfile is large enough to start a checkpoint
// Start a checkpoint if it is, otherwise, flush the log queue
void *log_func(void *args)
{
	arg_params *params = static_cast<arg_params *>(args);
	int tablet_num = params->tablet_num;
	int msgID = params->conn_id;
	pthread_t threadc = params->thread;
	std::string log_filepath = log_filepaths[tablet_num];

	while (shutting_down == 0)
	{
		if (!(checkpointing[tablet_num]))
		{
			if (is_primary && checkpoint_eligible(log_filepath))
			{
				start_new_checkpoint(tablet_num);
			}
			else
			{
				flush_log(tablet_num);
			}
		}
		sleep(3);
	}

	// remove from active threads and fds and add to available connections
	pthread_rwlock_wrlock(&rwlock);
	avail_connections.push(msgID);
	active_threads.remove(threadc);
	pthread_rwlock_unlock(&rwlock);
	pthread_exit(NULL);
}

// Function for creating the thread dedicated to logging
void create_log_thread(int tablet_num)
{
	int code;
	pthread_rwlock_wrlock(&rwlock);
	int msgID = avail_connections.front();
	avail_connections.pop();
	pthread_rwlock_unlock(&rwlock);
	arg_params log_args;
	log_args.tablet_num = tablet_num;
	log_args.conn_id = msgID;
	log_args.thread = threads[msgID];
	arg_structs[msgID] = log_args;
	code = pthread_create(&threads[msgID], NULL, &log_func, static_cast<void *>(&arg_structs[msgID]));
	// If error, add the msgID back to the queue of available connections.
	if (code != 0)
	{
		pthread_rwlock_wrlock(&rwlock);
		avail_connections.push(msgID);
		pthread_rwlock_unlock(&rwlock);
		fprintf(stderr, "Failed to create thread for logging.\n");
	}
	code = pthread_detach(threads[msgID]);
	if (code != 0)
	{
		fprintf(stderr, "Failed to detach thread.\n");
	}
}

// Given the log string, this function parses it and performs the relevant table operationsss
void *recovery_func(void *args)
{
	arg_params *params = static_cast<arg_params *>(args);
	std::string &log_string = params->log_str;
	int tablet_num = params->tablet_num;
	int msgID = params->conn_id;
	pthread_t threadc = params->thread;
	recovering[tablet_num] = true;

	parse_log_string(log_string, tablet_num);
	print_debug(debug_print, "Parsed log for tablet %d.\n", tablet_num);

	// remove from active threads and fds and add to available connections
	pthread_rwlock_wrlock(&rwlock);
	avail_connections.push(msgID);
	active_threads.remove(threadc);
	pthread_rwlock_unlock(&rwlock);
	recovering[tablet_num] = false;
	pthread_exit(NULL);
}

// Function for creating the thread dedicating to parsing the primary's logfile
void create_recovery_thread(std::string &log_str, int tablet_num)
{
	int code;
	pthread_rwlock_wrlock(&rwlock);
	int msgID = avail_connections.front();
	avail_connections.pop();
	pthread_rwlock_unlock(&rwlock);
	arg_params log_args;
	log_args.tablet_num = tablet_num;
	log_args.log_str = log_str;
	log_args.conn_id = msgID;
	log_args.thread = threads[msgID];
	arg_structs[msgID] = log_args;
	code = pthread_create(&threads[msgID], NULL, &recovery_func, static_cast<void *>(&arg_structs[msgID]));
	// If error, add the msgID back to the queue of available connections.
	if (code != 0)
	{
		pthread_rwlock_wrlock(&rwlock);
		avail_connections.push(msgID);
		pthread_rwlock_unlock(&rwlock);
		fprintf(stderr, "Failed to create thread for logging.\n");
	}
	code = pthread_detach(threads[msgID]);
	if (code != 0)
	{
		fprintf(stderr, "Failed to detach thread.\n");
	}
}

// Function for creating threads dedicated to sending heartbeat messages
// or for communication with the coordinator, depending on the arguments
void create_thread(bool heartbeat, bool coord_connection, int thread_fd)
{
	int code;
	pthread_rwlock_wrlock(&rwlock);
	int msgID = avail_connections.front();
	avail_connections.pop();
	pthread_rwlock_unlock(&rwlock);
	arg_params args;
	args.conn_fd = thread_fd;
	args.conn_id = msgID;
	args.coord_conn = coord_connection;
	args.backend_conn = true;
	args.thread = threads[msgID];
	arg_structs[msgID] = args;
	// create heartbeat sending thread
	if (heartbeat)
	{
		code = pthread_create(&threads[msgID], NULL, &heartbeat_func, static_cast<void *>(&arg_structs[msgID]));
	}
	// create communication with coordinator thread
	else
	{
		code = pthread_create(&threads[msgID], NULL, &start_func, static_cast<void *>(&arg_structs[msgID]));
	}
	// if error, close the socket and add the msgID back to the queue of available connections.
	if (code != 0)
	{
		close(thread_fd);
		pthread_rwlock_wrlock(&rwlock);
		avail_connections.push(msgID);
		pthread_rwlock_unlock(&rwlock);
	}
}

// Function to handle primary recovery
// Primary needs to be connected to all current replicas before sending pending transactions to replicas
void *tpc_primary_recovery_function(void *args)
{
	arg_params *params = static_cast<arg_params *>(args);
	int conn_fd = params->conn_fd;

	pthread_mutex_lock(&tpc_replica_count_lock);
	while (is_primary)
	{
		pthread_mutex_lock(&tpc_alive_replicas_lock);
		bool all_replicas_connected = (alive_replicas_ipport.size() >= (size_t)replica_count);
		pthread_mutex_unlock(&tpc_alive_replicas_lock);

		if (all_replicas_connected)
		{
			break;
		}

		// ask coordinator for replica count
		std::string request = "REPLICA COUNT " + std::to_string(partition) + "\r\n";
		do_write_backend(conn_fd, request.c_str(), request.size());

		pthread_cond_wait(&primary_count_cond, &tpc_replica_count_lock);
		sleep(1);
	}
	pthread_mutex_unlock(&tpc_replica_count_lock);
	print_debug(debug_print, "[TPC] Primary is now connected to all replicas.\n");

	// abort all pending tpc transactions
	for (auto it = tpc_pending.begin(); it != tpc_pending.end(); it++)
	{
		int seq_num = it->first;
		struct transaction t = it->second;
		std::string hash = t.hash;
		std::string status = t.status;

		// print_debug(debug_print, "[TPC] Aborting pending transaction %d.\n", it->first);
		primary_abort_transaction(seq_num, hash);
	}

	// set primary as ready and signal that it is ready
	pthread_mutex_lock(&primary_ready_mutex);
	primary_recovering = false;
	pthread_cond_signal(&primary_ready_cond);
	pthread_mutex_unlock(&primary_ready_mutex);

	print_debug(debug_print, "[TPC] Primary has finished recovery.\n");
	pthread_exit(NULL);
}

// Function for primary to check ongoing tpc transactions, and aborting if necessary
void *tpc_check_function(void *args)
{
	// 2PC if primary, then the listening port thread should check if any transactions are expired
	// each replica connect to primary listening port and forward transactions
	// each thread for communicating with backend replicas
	std::chrono::seconds timeout_max(6000);
	while (shutting_down == 0 && is_primary)
	{
		std::vector<int> to_remove;

		// iterate through all ongoing transactions
		pthread_mutex_lock(&tpc_start_time_lock);
		for (auto it = tpc_start_time.begin(); it != tpc_start_time.end(); it++)
		{
			int seq_num = it->first;
			auto start_time = it->second;

			// if timeout
			auto diff = std::chrono::steady_clock::now() - start_time;
			if (diff > timeout_max)
			{
				auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(diff).count();
				print_debug(debug_print, "[TPC] Time diff for aborted transaction %lld ms.\n", (long long)ms);
				to_remove.push_back(seq_num);
			}
		}
		pthread_mutex_unlock(&tpc_start_time_lock);

		// remove any transactions that have expired only if not already done
		for (int seq_num : to_remove)
		{
			print_debug(debug_print, "[TPC] Aborting transaction due to timeout %d.\n", seq_num);

			// get the hash number
			pthread_mutex_lock(&tpc_queue_lock);
			if (tpc_queue.find(seq_num) == tpc_queue.end())
			{
				pthread_mutex_unlock(&tpc_queue_lock);
				continue;
			}
			std::string hash = tpc_queue[seq_num].hash;
			pthread_mutex_unlock(&tpc_queue_lock);

			primary_abort_transaction(seq_num, hash);
		}
		to_remove.clear();
	}
	pthread_exit(NULL);
}

// use MESSAGE QUEUE to keep track of final ACKS from replicas and the original FD
// use TRANSACTION QUEUE to keep track of pending 2PC transactions
void handle_message(Key key, int opt, std::string hash, std::string message, int connfd,
					bool is_primary, bool ack_message, bool backend_conn, bool ok_respond, std::string val, bool success = false, bool is_tpc = false, int tseq = -1, const std::string &original_ipport = "NA")
{
	print_debug(debug_print, "[Server] Handling message.\n");
	// SCENARIO 1 : HANDLE TPC MESSAGES ////////////////////////////////////////////////////////////////////////////
	if (is_tpc)
	{
		std::string new_message = message + "\r\n";
		if (is_primary)
		{
			// TPC:YES:hash:seq_num:IP_port
			// TPC:NO:hash:seq_num:IP_port
			if (message.find("YES") != std::string::npos || message.find("NO") != std::string::npos)
			{
				// parse message
				std::vector<std::string> tokens = split(message, ':');
				if (tokens.size() != 6)
				{
					print_debug(debug_print, "[TPC] Received invalid YES/NO message: %s\n", message.c_str());
					return;
				}
				std::string hash = tokens.at(2);
				int seq_num = std::stoi(tokens.at(3));
				std::string ip = tokens.at(4);
				std::string port = tokens.at(5);
				std::string ip_port = ip + ":" + port;

				// if transaction is already decided, then ignore
				bool done = already_processed(seq_num);
				if (done)
				{
					print_debug(debug_print, "[TPC] Transaction %d is already committed/aborted.\n", seq_num);
					return;
				}

				// get transaction info
				pthread_mutex_lock(&tpc_queue_lock);
				std::string command;
				if (tpc_queue.find(seq_num) == tpc_queue.end())
				{
					pthread_mutex_unlock(&tpc_queue_lock);
					return;
				}
				command = tpc_queue[seq_num].command;
				pthread_mutex_unlock(&tpc_queue_lock);

				// if YES, then add to responses
				if (message.find("YES") != std::string::npos)
				{
					print_debug(debug_print, "[TPC] Received YES from replica %s for transaction %d.\n", ip_port.c_str(), seq_num);

					// add to reponses
					pthread_mutex_lock(&tpc_response_lock);
					tpc_responses[seq_num].insert(ip_port);

					// check if all replicas responded YES
					pthread_mutex_lock(&tpc_alive_replicas_lock);
					print_debug(debug_print, "[TPC] Seq %d : Initial replica count: %zu, current response count: %zu, current replica count: %zu\n", seq_num, tpc_replicas[seq_num].size(), tpc_responses[seq_num].size(), alive_replicas_ipport.size());
					if (tpc_responses[seq_num].size() == tpc_replicas[seq_num].size() || (size_t)tpc_responses[seq_num].size() >= alive_replicas_ipport.size())
					{
						pthread_mutex_unlock(&tpc_response_lock);
						pthread_mutex_unlock(&tpc_alive_replicas_lock);
						print_debug(debug_print, "[TPC] All replicas have responded YES.\n");

						// check if the primary itself can commit the transaction
						// if get lock, commit
						bool success = get_command_locks(command);
						print_debug(debug_print, "[TPC] Primary is getting lock, status is: %d.\n", success);
						if (success)
						{
							primary_commit_transaction(seq_num, hash);
						}
						else
						{
							primary_abort_transaction(seq_num, hash);
						}
					}
					else
					{
						pthread_mutex_unlock(&tpc_response_lock);
						pthread_mutex_unlock(&tpc_alive_replicas_lock);
						print_debug(debug_print, "[TPC] Waiting for other replicas to respond YES or NO.\n");
					}
				}
				// if NO, abort transaction
				else if (message.find("NO") != std::string::npos)
				{
					print_debug(debug_print, "[TPC] Received NO from replica %s for transaction %d.\n", ip_port.c_str(), seq_num);

					// abort transaction
					primary_abort_transaction(seq_num, hash);
				}
			}
		}
		// if replica
		else
		{
			// TPC:PREPARE:hash:seq_num:command
			if (message.find("PREPARE") != std::string::npos)
			{
				// parse message
				size_t first_colon = message.find(':');
				std::string first = message.substr(0, first_colon);
				size_t second_colon = message.find(':', first_colon + 1);
				std::string second = message.substr(first_colon + 1, second_colon - (first_colon + 1));
				size_t third_colon = message.find(':', second_colon + 1);
				std::string hash = message.substr(second_colon + 1, third_colon - (second_colon + 1));
				size_t fourth_colon = message.find(':', third_colon + 1);
				int seq_num = stoi(message.substr(third_colon + 1, fourth_colon - (third_colon + 1)));
				std::string command = message.substr(fourth_colon + 1);
				print_debug(debug_print, "[TPC] Received PREPARE message.\n");

				// if transaction is already processed or in tpc_queue, then ignore
				bool in_tpc_queue = false;
				pthread_mutex_lock(&tpc_queue_lock);
				if (tpc_queue.find(seq_num) != tpc_queue.end())
				{
					in_tpc_queue = true;
				}
				pthread_mutex_unlock(&tpc_queue_lock);

				pthread_mutex_lock(&tpc_last_seq_lock);
				if (last_seq >= seq_num || in_tpc_queue)
				{
					pthread_mutex_unlock(&tpc_last_seq_lock);
					print_debug(debug_print, "[TPC] PREPARE for transaction %d has already been received.\n", seq_num);
					return;
				}
				pthread_mutex_unlock(&tpc_last_seq_lock);

				// log message to tpc file
				// ALLY TODO log in separate thread?
				write_to_tpc_log("TPC:PREPARE:" + hash + ":" + std::to_string(seq_num));

				// update tpc_seq
				pthread_mutex_lock(&tpc_seq_lock);
				tpc_seq = std::max(tpc_seq, seq_num);
				pthread_mutex_unlock(&tpc_seq_lock);

				// add to queue
				struct transaction t;
				t.command = command;
				t.hash = hash;
				t.status = "PREPARE";
				pthread_mutex_lock(&tpc_queue_lock);
				tpc_queue[seq_num] = t;
				pthread_mutex_unlock(&tpc_queue_lock);
				print_debug(debug_print, "[TPC] Added transaction %d to queue.\n", seq_num);

				// check if can get lock
				// TPC:YES:hash:seq_num:IP_port
				// TPC:NO:hash:seq_num:IP_port
				bool success = get_command_locks(command);
				std::string success_string;
				bool recover = is_recovering();
				if (success && !recover)
				{
					print_debug(debug_print, "[TPC] Replica voted YES for transaction %d.\n", seq_num);
					success_string = "YES";
				}
				else
				{
					print_debug(debug_print, "[TPC] Replica voted NO for transaction %d.\n", seq_num);
					success_string = "NO";
				}
				std::string response = "TPC:" + success_string + ":" + hash + ":" + std::to_string(seq_num) + ":" + IP + ":" + std::to_string(primary_port) + "\r\n";
				do_write_backend(primary_fd, response.c_str(), response.size());
			}
			// TPC:COMMIT:hash:seq_num
			// TPC:ABORT:hash:seq_num
			else if (message.find("COMMIT") != std::string::npos || message.find("ABORT") != std::string::npos)
			{
				// parse message
				std::vector<std::string> tokens = split(message, ':');
				if (tokens.size() != 4)
				{
					print_debug(debug_print, "[TPC] Received invalid COMMIT/ABORT message: %s\n", message.c_str());
					return;
				}
				std::string status = tokens.at(1);
				std::string hash = tokens.at(2);
				int seq_num = stoi(tokens.at(3));

				// if transaction is already committed/aborted, then ignore
				bool done = already_processed(seq_num);
				if (done)
				{
					print_debug(debug_print, "[TPC] Transaction %d is already committed/aborted.\n", seq_num);
					return;
				}
				pthread_mutex_lock(&queued_transactions_lock);
				queued_transactions.insert(seq_num);
				pthread_mutex_unlock(&queued_transactions_lock);

				// write to log
				write_to_tpc_log(message);

				// update message queue if in queue
				if (msg_queue.find(hash) != msg_queue.end())
				{
					msg_queue[hash]->tpc_success = status;
				}

				// update tpc queue
				pthread_mutex_lock(&tpc_queue_lock);
				if (tpc_queue.find(seq_num) != tpc_queue.end())
				{
					tpc_queue[seq_num].status = status;
				}
				pthread_mutex_unlock(&tpc_queue_lock);

				// process queue
				process_tpc_queue();
			}
		}
	}
	// SCENARIO 3 : PRIMARY RECEIVES COMMAND FROM FRONTEND OR REPLICA, START 2PC //////////////////////////////
	else if (is_primary)
	{
		// store message into message queue
		if (!backend_conn)
		{
			hash = create_hash(message);
			print_debug(debug_print, "[Primary] Primary received command from frontend server with hash %s.\n", hash.c_str());
		}
		else
		{
			print_debug(debug_print, "[Primary] Primary received command from replica with hash %s.\n", hash.c_str());
		}

		// update tpc_seq
		pthread_mutex_lock(&tpc_seq_lock);
		tpc_seq++;
		int this_seq = tpc_seq;
		pthread_mutex_unlock(&tpc_seq_lock);

		// put into message queue
		std::shared_ptr<message_queue> msg = std::make_shared<message_queue>();
		msg->fd = connfd;
		msg->tpc_success = "PREPARE";
		if (backend_conn)
		{
			msg->from_frontend = false;
		}
		else
		{
			msg->from_frontend = true;
		}
		size_t space_pos = message.find(' ');
		std::string opt_str = message.substr(0, space_pos);
		size_t second_space = message.find(' ', space_pos + 1);
		std::string row = message.substr(space_pos + 1, second_space - (space_pos + 1));
		size_t third_space = message.find(' ', second_space + 1);
		std::string col = message.substr(second_space + 1, third_space - (second_space + 1));
		int opt = -1;
		if (opt_str == "PUT")
		{
			opt = 1;
		}
		else if (opt_str == "CPUT")
		{
			opt = 2;
		}
		else if (opt_str == "DELETE")
		{
			opt = 3;
		}
		msg->original_ipport = original_ipport;
		msg->opt = opt;
		msg->row = row;
		msg->col = col;
		msg_queue[hash] = msg;
		print_debug(debug_print, "[Primary] Adding message to message queue.\n");

		// Handles the case that there are no replicas
		// Directly execute command without 2PC
		pthread_mutex_lock(&partition_fds_lock);
		int size = static_cast<int>(partition_fds.size());
		pthread_mutex_unlock(&partition_fds_lock);
		int tablet_num = get_tablet(row);
		if (size == 0)
		{
			bool succ = get_command_locks(message);
			if (succ)
			{
				if (!active_tablets[tablet_num])
				{
					load_tablet(tablet_num);
				}
				pthread_mutex_lock(&tpc_last_seq_lock);
				last_seq++;
				pthread_mutex_unlock(&tpc_last_seq_lock);
				log_message(message, last_seq, tablet_num);
				execute_command(message);
				std::string reply = generate_client_response(true, opt);
				if (checkpointing[tablet_num])
				{
					checkpoint_queue.push(std::make_pair(msg->fd, reply));
					print_debug(debug_print, "Checkpointing - adding to checkpoint queue: %s.\n", reply.c_str());
				}
				else
				{
					do_write_backend(msg->fd, reply.c_str(), reply.size());
					print_debug(debug_print, "Successfully written back to message sender: %s.\n", reply.c_str());
				}
				msg_queue.erase(hash);
				print_debug(debug_print, "Erasing message from message queue.\n");
			}
			else
			{
				std::string reply = generate_client_response(false, opt);
				do_write_backend(msg->fd, reply.c_str(), reply.size());
				msg_queue.erase(hash);
				print_debug(debug_print, "Successfully written back to message sender: %s.\n", reply.c_str());
				print_debug(debug_print, "Erasing message from message queue.\n");
			}
		}
		// start 2PC
		else
		{
			start_tpc(message, hash, this_seq);
		}
	}
	// SCENARIO 5 : IF REPLICA RECEIVES COMMAND FROM FRONTEND, FORWARD TO PRIMARY ////////////////////////s
	else
	{
		int bytes_written;
		hash = create_hash(message);
		std::string fwd = hash + " " + message + "\r\n";
		bytes_written = write(primary_fd, fwd.c_str(), fwd.size());
		std::shared_ptr<message_queue> msg = std::make_shared<message_queue>();
		if (bytes_written <= 0)
		{
			primary_queue.push(std::move(fwd));
			print_debug(debug_print, "Primary has closed, so instead of forwarding to the primary I've added it to the primary forwarding queue instead.\n");
		}
		print_debug(debug_print, "Forwarded message to primary.\n");
		msg->fd = connfd;
		msg->from_frontend = true;
		msg->tpc_success = "PREPARE";
		// get the first word of the message to determine the operation
		size_t space_pos = message.find(' ');
		std::string opt_str = message.substr(0, space_pos);
		if (opt_str == "PUT")
		{
			msg->opt = 1;
		}
		else if (opt_str == "CPUT")
		{
			msg->opt = 2;
		}
		else if (opt_str == "DELETE")
		{
			msg->opt = 3;
		}
		msg_queue[hash] = msg;
	}
}

// The function that handles client-server communications after a connection has been established.
// The args is a struct that contains the client fd, client ID, buffer, which is a vector of chars,
// and the thread that was created to handle the connection.
void *start_func(void *args)
{
	arg_params *params = static_cast<arg_params *>(args);
	int connfd = params->conn_fd;
	Buf buffer = params->buffer;
	int msgID = params->conn_id;
	std::string conn_address = params->conn_address;
	bool coord_conn = params->coord_conn;
	bool backend_conn = params->backend_conn;
	pthread_t threadc = params->thread;
	pthread_rwlock_wrlock(&rwlock);
	active_threads.push_back(threadc);
	active_fds.push_back(connfd);
	pthread_rwlock_unlock(&rwlock);
	struct pollfd pfd;
	pfd.fd = connfd;
	pfd.events = POLLIN | POLLHUP | POLLNVAL;
	std::vector<char> delimiter = {'\r', '\n'};
	bool open_connection = true;
	// bool max_buf = false;
	// Set the buffer size to zero before starting to read messages
	buffer.resize(0);

	// Send the greeting message to the connection
	const char *greeting = "+OK Server ready.\r\n";
	do_write_backend(connfd, greeting, strlen(greeting));

	// If the connection is to the primary, then request log file from primary
	if (connfd == primary_fd)
	{
		// save tid for other threads to access
		primary_comm_tid = pthread_self();
		for (int i = 0; i < 5; i++)
		{
			std::string request = "+OK REQUEST logfile " + std::to_string(i) + "\r\n"; // TODO
			do_write_backend(connfd, request.c_str(), request.size());
			print_debug(debug_print, "Requested logfile %d from the current primary.\n", i);
		}
		send_queued_primary();
	}

	// if connection is to primary, then send the replica should ask for current last_seq and tpc_seq to make the replica up to date
	// it should already have the most up to date log because got log files
	if (connfd == primary_fd)
	{
		std::string request = "TSEQNUM\r\n";
		do_write_backend(connfd, request.c_str(), request.size());
		print_debug(debug_print, "[TPC] Replica requested tseq and last_seq from the current primary.\n");
	}

	while (open_connection == true)
	{
		// if connection is to primary, and need to close thread
		if (connfd == primary_fd && terminate_primary_thread.load())
		{
			print_debug(debug_print, "[Server] Signaled to close current primary thread.\n");
			open_connection = false;
			break;
		}

		// use a pollfd struct to keep track of whether the client fd has data to read from.
		int ready = poll(&pfd, 1, -1);

		// HANDLE CLOSE AND CLEAN UP ////////////////////////////////////////////////////////////////////////////
		if (ready < 0)
		{
			fprintf(stderr, "Poll error.\n");

			// clean up partition_fds
			pthread_mutex_lock(&partition_fds_lock);
			auto it = std::find(partition_fds.begin(), partition_fds.end(), connfd);
			if (it != partition_fds.end())
			{
				partition_fds.erase(it);
			}
			pthread_mutex_unlock(&partition_fds_lock);

			// remove from alive replicas
			if (is_primary && backend_conn)
			{
				pthread_mutex_lock(&tpc_alive_replicas_lock);
				alive_replicas_ipport.erase(conn_address);
				pthread_mutex_unlock(&tpc_alive_replicas_lock);
			}

			// remove from active threads and fds and add to available connections
			pthread_rwlock_wrlock(&rwlock);
			avail_connections.push(msgID);
			active_threads.remove(threadc);
			active_fds.remove(connfd);
			pthread_rwlock_unlock(&rwlock);
			// close connection
			print_debug(debug_print, "Closing socket %d\n", connfd);
			close(connfd);
			print_debug(debug_print, "Closing thread.\n");
			pthread_exit(NULL);
		}
		////////////////////////////////////////////////////////////////////////////////////////////////////////////

		// Check for sudden closure of client fd
		// Does not detect all forms of client closure, but if detected, exit the loop and handle accordingly
		if ((pfd.revents & POLLHUP) || (pfd.revents & POLLNVAL))
		{
			print_debug(debug_print, "Client disconnected.\n");
			open_connection = false;
		}
		// If the client sends a message, begin reading
		else if (pfd.revents & POLLIN)
		{
			// Use FIONREAD to get the number of bytes in the client fd so we can determine the number of bytes to read ahead of time.
			// Cap the number of bytes read at the amount of available buffer space to avoid overwriting data.
			int read_bytes;
			ioctl(connfd, FIONREAD, &read_bytes);
			print_debug(debug_print, "Read bytes: %d.\n", read_bytes);

			// HANDLE CLOSE AND CLEAN UP ////////////////////////////////////////////////////////////////////////////
			if (read_bytes < 0)
			{
				fprintf(stderr, "read_bytes is negative somehow. This should not be possible.\n");

				// clean up partition_fds
				pthread_mutex_lock(&partition_fds_lock);
				auto it = std::find(partition_fds.begin(), partition_fds.end(), connfd);
				if (it != partition_fds.end())
				{
					partition_fds.erase(it);
				}
				pthread_mutex_unlock(&partition_fds_lock);

				// remove from alive replicas
				if (is_primary && backend_conn)
				{
					pthread_mutex_lock(&tpc_alive_replicas_lock);
					alive_replicas_ipport.erase(conn_address);
					pthread_mutex_unlock(&tpc_alive_replicas_lock);
				}

				// remove from active threads and fds and add to available connections
				pthread_rwlock_wrlock(&rwlock);
				avail_connections.push(msgID);
				active_threads.remove(threadc);
				active_fds.remove(connfd);
				pthread_rwlock_unlock(&rwlock);
				// close connection
				print_debug(debug_print, "Closing socket %d\n", connfd);
				close(connfd);
				print_debug(debug_print, "Closing thread.\n");
				pthread_exit(NULL);
			}
			////////////////////////////////////////////////////////////////////////////////////////////////////////////

			else if (read_bytes == 0)
			{
				// If POLLIN bit was triggered but there is no data to read, the client disconnected
				// Exit the loop and handle accordingly
				print_debug(debug_print, "Client disconnected.\n");
				open_connection = false;
			}

			// Push read_bytes chars into the vector
			char temp[65532];
			while (read_bytes > 0)
			{
				int bytes_read = std::min(65532, read_bytes);
				int num_bytes_read = recv(connfd, temp, bytes_read, 0);
				buffer.insert(buffer.end(), temp, temp + num_bytes_read);
				read_bytes -= num_bytes_read;
			}

			// Debug print statement to make sure the buffer size correctly changes
			// print_debug(debug_print, "Current buffer size is: %d\n", static_cast<int>(buffer.size()));
			// While there is still data to be processed in the buffer, handle the following cases
			while (buffer.size() > 0)
			{
				auto it = std::search(buffer.begin(), buffer.end(), delimiter.begin(), delimiter.end());
				// Case one: there is no delimiter in the buffer
				if (it == buffer.end())
				{
					std::string buffer_str(buffer.begin(), buffer.end());
					// print_debug(debug_print, "[%d] Buffer not full and contains a partial message: %s\n", connfd, buffer_str.substr(0, 50).c_str());
					print_debug(debug_print, "Reading incoming message...\n");
					break; // Either way, exit until the server can read more data.
				}
				else
				// There is a delimiter in the buffer
				// Get subset of the buffer up to (but not including) the first instance of delimiter characters,
				// then convert it into a std::string
				{
					std::string message(buffer.begin(), it);
					trim_trailing_newline(message);
					std::string hash = "";
					std::string val = "";
					bool ack_message = false;
					bool ok_respond = false;
					bool is_tpc = false;
					// status for whether transaction succeeded or failed
					bool success = false;
					Key key = {"", ""};
					// meanwhile, erase the buffer including the delimiter strings
					buffer.erase(buffer.begin(), it + delimiter.size());
					print_debug(debug_print, "Original message contents are %s.\n", message.substr(0, 50).c_str());

					// DIFFERENT SCENARIOS /////////////////////////////////////////////////////////////////////
					// (1) Coordinator asked server to STOP /////////////////////////////////////////////////////
					if (message.find("STOP") != std::string::npos && coord_conn)
					{
						print_debug(debug_print, "[Admin Console] Received STOP message from coordinator.\n");

						// close all frontend FDs
						for (int fd : frontend_fds)
						{
							close(fd);
							print_debug(debug_print, "[Admin Console] Closed frontend connection %d.\n", fd);
						}
						frontend_fds.clear();

						// join all frontend threads
						/*for (pthread_t thread : frontend_threads)
						{
							// pthread_join(thread, NULL);
							print_debug(debug_print, "[Admin Console] Joined frontend thread.\n");
						}*/
						frontend_threads.clear();

						// if primary server receives STOP, then need to turn is_primary off and close all primary related threads
						if (is_primary)
						{
							is_primary = false;

							// close the primary transaction checking thread
							if (tpc_tid != NULL)
							{
								print_debug(debug_print, "[Admin Console] Terminating primary transaction checking thread.\n");
								// pthread_join(tpc_tid, NULL);
								print_debug(debug_print, "[Admin Console] Joined primary transaction checking thread.\n");
							}

							// close all replica FDs and threads
							pthread_mutex_lock(&partition_fds_lock);
							for (int fd : partition_fds)
							{
								close(fd);
								print_debug(debug_print, "[Admin Console] Closed replica connection %d.\n", fd);
							}
							partition_fds.clear();
							pthread_mutex_unlock(&partition_fds_lock);
							print_debug(debug_print, "[Admin Console] Closed ALL replica connections.\n");

							// clear alive_replicas_ipport
							pthread_mutex_lock(&tpc_alive_replicas_lock);
							alive_replicas_ipport.clear();
							pthread_mutex_unlock(&tpc_alive_replicas_lock);

							// join all replica communication threads
							/* for (pthread_t thread : replica_threads)
							{
								// pthread_join(thread, NULL);
								print_debug(debug_print, "[Admin Console] Joined replica thread.\n");
							} */
							replica_threads.clear();
						}

						// send ACK to coordinator
						std::string response = "+ACK\r\n";
						do_write_backend(connfd, response.c_str(), response.size());
						print_debug(debug_print, "[Admin Console] Sent ACK for STOP server to coordinator.\n");
					}
					// (2) New primary has been chosen ///////////////////////////////////////////////////////////
					// FORMAT : +OK PRIMARY IP_port server_count
					else if (message.substr(4, 7) == "PRIMARY" && coord_conn)
					{
						print_debug(debug_print, "[Coordinator Message] Received PRIMARY message : %s.\n", message.c_str());
						std::vector<std::string> tokens = split(message, ' ');
						if (tokens.size() < 4)
						{
							print_debug(debug_print, "[Coordinator Message] Invalid primary message format.\n");
							continue;
						}

						// parse message
						primary_addr = tokens.at(2);
						pthread_mutex_lock(&tpc_replica_count_lock);
						replica_count = stoi(tokens.at(3)) - 1; // substract itself
						pthread_mutex_unlock(&tpc_replica_count_lock);

						// if this server is the new primary (and previously not the old primary)
						if (primary_addr == listen_address && !is_primary)
						{
							print_debug(debug_print, "[SERVER] This server was chosen to be the primary: %s.\n", primary_addr.c_str());
							is_primary = true;

							// wait sending tpc messages until all the current replicas connect to the primary
							// will receive new replica connections from listening port
							pthread_mutex_lock(&primary_ready_mutex);
							primary_recovering = true;
							pthread_mutex_unlock(&primary_ready_mutex);

							// set primary_fd to -1
							if (primary_fd > 0)
							{
								close(primary_fd);
								primary_fd = -1;
								// print_debug(debug_print, "[SERVER] Terminating primary communication thread.\n");
								// terminate_primary_thread.store(true);
								// shutdown(primary_fd, SHUT_RDWR);
								// close(primary_fd);
								// primary_fd = -1;
								// print_debug(debug_print, "[SERVER] Joining primary communication thread.\n");
								// pthread_join(primary_comm_tid, NULL);
								// terminate_primary_thread.store(false);
								// print_debug(debug_print, "[SERVER] Joined primary communication thread.\n");
							}

							// create separate thread to check 2pc pending transactions if primary server
							int res = pthread_create(&tpc_tid, NULL, tpc_check_function, NULL);
							if (res != 0)
							{
								fprintf(stderr, "[TPC] Failed to create thread for 2PC transaction checking.\n");
								shutting_down = 1;
								exit(1);
							}
							print_debug(debug_print, "[TPC] Created thread to check 2PC transactions.\n");

							// identify any pending tpc transactions from log file
							process_tpc_log();

							// create separate thread for primary recovery - check if all the current replicas are connected before sending out pending transactions
							// once recovered, send any pending transactions to replicas
							pthread_t recovery_tid;
							arg_params new_args;
							new_args.conn_fd = connfd;
							int result = pthread_create(&recovery_tid, NULL, tpc_primary_recovery_function, static_cast<void *>(&new_args));
							if (result != 0)
							{
								fprintf(stderr, "[TPC] Failed to create thread for 2PC primary recovery.\n");
								shutting_down = 1;
								exit(1);
							}
							print_debug(debug_print, "[TPC] Created thread to for 2PC primary recovering.\n");

							handle_primary_queue();
						}
						else if (primary_addr == listen_address && is_primary)
						{
							print_debug(debug_print, "[SERVER] This server is already the primary - not possible.\n");
						}
						else
						{
							is_primary = false;
							// close the current primary connection if exists
							if (primary_fd > 0)
							{
								print_debug(debug_print, "[SERVER] Terminating primary communication thread.\n");
								terminate_primary_thread.store(true);
								shutdown(primary_fd, SHUT_RDWR);
								close(primary_fd);
								primary_fd = -1;
								print_debug(debug_print, "[SERVER] Joining primary communication thread.\n");
								// pthread_join(primary_comm_tid, NULL);
								terminate_primary_thread.store(false);
								print_debug(debug_print, "[SERVER] Joined primary communication thread.\n");
							}
							sleep(1);
							// wait until can use the same port again
							int fd_ = create_connect_socket_port(primary_addr, IP, primary_port);
							primary_fd = fd_;
							create_thread(false, false, fd_);
							print_debug(debug_print, "[SERVER] Connected to primary address from port %d\n", primary_port);
						}
					}
					// (3) Replica is requesting current TSEQ and LAST SEQ from primary ///////////////////////////
					else if (is_primary && message.find("TSEQNUM") != std::string::npos)
					{
						// TSEQNUMRETURN tseq last_seq
						print_debug(debug_print, "[TPC] Received request for tseq and last_seq from replica.\n");
						pthread_mutex_lock(&tpc_seq_lock);
						int cur_seq = tpc_seq;
						pthread_mutex_unlock(&tpc_seq_lock);
						pthread_mutex_lock(&tpc_last_seq_lock);
						std::string response = "TSEQNUMRETURN " + std::to_string(cur_seq) + " " + std::to_string(last_seq) + "\r\n";
						pthread_mutex_unlock(&tpc_last_seq_lock);
						do_write_backend(connfd, response.c_str(), response.size());
						print_debug(debug_print, "[TPC] Primary sent tseq and last_seq to replica.\n");
					}
					// (4) Replica received tseq and last_seq from primary ///////////////////////////////////////
					// TSEQNUMRETURN tseq last_seq
					else if (message.find("TSEQNUMRETURN") != std::string::npos)
					{
						std::vector<std::string> tokens = split(message, ' ');
						if (tokens.size() != 3)
						{
							print_debug(debug_print, "[TPC] Invalid TSEQNUMRETURN message format.\n");
							continue;
						}
						int cur_tseq = std::stoi(tokens.at(1));
						int cur_last_seq = std::stoi(tokens.at(2));

						// set tpc_seq and last_seq
						pthread_mutex_lock(&tpc_seq_lock);
						tpc_seq = std::max(tpc_seq, cur_tseq);
						pthread_mutex_unlock(&tpc_seq_lock);
						pthread_mutex_lock(&tpc_last_seq_lock);
						last_seq = std::max(last_seq, cur_last_seq);
						pthread_mutex_unlock(&tpc_last_seq_lock);
						print_debug(debug_print, "[TPC] Current tpc_seq is %d.\n", tpc_seq);
						print_debug(debug_print, "[TPC] Current last_seq is %d.\n", last_seq);
					}
					// (5) Replica is requesting for log file from primary ///////////////////////////////////////
					// FORMAT : +OK REQUEST logfile
					else if (is_primary && message.substr(4, 7) == "REQUEST")
					{
						print_debug(debug_print, "Preparing log file.\n");
						size_t sp = message.rfind(" ");
						std::string num_ = message.substr(sp + 1);
						int num = atoi(num_.c_str());
						print_debug(debug_print, "Request logfile number: %d.\n", num);
						std::string logstring = create_log_string(num);
						print_debug(debug_print, "Log file contents are %s.\n", logstring.substr(0, 50).c_str());
						do_write_backend(connfd, logstring.c_str(), logstring.size());
						print_debug(debug_print, "Sent logfile %d to replica.\n", num);
					}
					// (6) Checking if checkpointing has finished /////////////////////////////////////
					else if (is_primary && message.substr(0, 9) == "CHECK_ACK")
					{
						size_t sp = message.find(' ');
						std::string num_ = message.substr(sp + 1);
						int num = atoi(num_.c_str());
						checkpoint_acks[num]++;
						pthread_mutex_lock(&partition_fds_lock);
						int partition_size = static_cast<int>(partition_fds.size());
						print_debug(debug_print, "Partition size: %d, number of checkpoint acks: %d", partition_size, checkpoint_acks[num]);
						pthread_mutex_unlock(&partition_fds_lock);
						if (checkpoint_acks[num] >= partition_size)
						{ // It's >= and not == in case some replicas suddenly go down
							std::string done_msg = "CHECK_DONE\r\n";
							forward_msg(done_msg);
							send_queued_messages();
							checkpoint_acks[num] = 0;
							print_debug(debug_print, "Finished the checkpointing process.\n");
						}
					}
					// (7) Replica received log file from primary /////////////////////////////////////////////////
					// FORMAT : LOGFILE log
					else if (connfd == primary_fd && message.substr(0, 7) == "LOGFILE")
					{
						size_t sp = message.find(' ');
						std::string log_num = message.substr(sp + 1) + "\n";
						size_t sp0 = log_num.find(' ');
						std::string num_ = log_num.substr(0, sp0);
						std::string log = log_num.substr(sp0 + 1);
						int num = atoi(num_.c_str());
						create_recovery_thread(log, num);
						print_debug(debug_print, "Recreated the table commands up until the most recent logfile.\n");
					}
					// (8) Replica received checkpointing message from primary ///////////////////////////////////////
					else if (connfd == primary_fd && message.substr(0, 10) == "CHECKPOINT")
					{
						size_t sp = message.find(' ');
						std::string t = message.substr(sp + 1);
						int num = atoi(t.c_str());
						create_new_checkpoint(num);
					}
					// (9) Received CHECKDONE message
					else if (message.substr(0, 10) == "CHECK_DONE")
					{
						send_queued_messages();
					}
					// (10) TPC messages ////////////////////////////////////////////////////////////////////////////
					// TPC:PREPARE:hash:seq_num:command
					// TPC:YES:hash:seq_num:IP_port
					// TPC:NO:hash:seq_num:IP_port
					// TPC:COMMIT:hash:seq_num:command (send truncated command)
					// TPC:ABORT:hash:seq_num:command (send truncated command)
					// TPC:END:hash:seq_num
					else if (message.substr(0, 3) == "TPC")
					{
						is_tpc = true;
						// if not PREPARE, print message
						if (message.substr(4, 7) != "PREPARE")
						{
							print_debug(debug_print, "[TPC] Received TPC message: %s.\n", message.c_str());
						}
						// pass the entire message to handle_message
						handle_message(key, -1, hash, message, connfd, is_primary, ack_message, backend_conn, ok_respond, val, success, is_tpc);
					}
					// (11) Primary server received replica count from coordinator
					// REPLICA COUNT X
					else if (message.find("REPLICA COUNT") != std::string::npos)
					{
						print_debug(debug_print, "[Coordinator Message] Received replica count from coordinator.\n");
						// parse message
						std::vector<std::string> tokens = split(message, ' ');
						pthread_mutex_lock(&tpc_replica_count_lock);
						replica_count = stoi(tokens.at(2)) - 1; // subtract itself
						print_debug(debug_print, "[Primary] New replica count is %d.\n", replica_count);
						pthread_cond_signal(&primary_count_cond);
						pthread_mutex_unlock(&tpc_replica_count_lock);
					}
					// (12) Otherwise, primary or replica should have received COMMANDS /////////////////////////////
					// FORMAT : IF FROM BACKEND, <HASH MESSAGE> | IF FRONTEND, <MESSAGE>
					else
					{
						if (backend_conn)
						{

							size_t sp = message.find(' ');
							hash = message.substr(0, sp);
							message = message.substr(sp + 1);
							print_debug(debug_print, "Trimmed message contents are %s.\n", message.substr(0, 50).c_str());
						}
						if (message.substr(0, 3) == "PUT" || message.substr(0, 3) == "GET" ||
							message.substr(0, 4) == "CPUT" || message.substr(0, 6) == "DELETE")
						{
							size_t space0 = message.find(' ');
							std::string rcv = message.substr(space0 + 1);
							size_t space = rcv.find(' ');
							std::string row = rcv.substr(0, space);
							std::string colv = rcv.substr(space + 1);
							if (message.substr(0, 3) == "PUT")
							{
								print_debug(debug_print, "Received PUT command.\n");
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
								print_debug(debug_print, "Calling handle_message");
								// print_debug(debug_print, "Message for handle_message is %s.\n", message.c_str());
								handle_message(key, 1, hash, message, connfd, is_primary, ack_message, backend_conn, ok_respond, val, success);
							}
							else if (message.substr(0, 3) == "GET")
							{
								print_debug(debug_print, "Received GET command.\n");
								if (message == "GET passwd null")
								{
									print_debug(debug_print, "Getting kv pairs for admin console.\n");
									std::string pairs = make_kv_pairs();
									do_write_backend(connfd, pairs.c_str(), pairs.size());
								}
								else
								{
									std::string col = colv;
									Key key = {row, col};
									int tablet = get_tablet(row);
									if (!active_tablets[tablet])
									{
										load_tablet(tablet);
									}
									print_debug(debug_print, "Tablet number: %d.\n", tablet);
									print_debug(debug_print, "Requested value from row and column %s %s.\n", row.c_str(), col.c_str());
									if (table[tablet].find(key) == table[tablet].end())
									{
										const char *reply = "-ERR 204 specific row and column does not exist.\r\n";
										print_debug(debug_print, "Could not find row %s in tablet %d.\n", row.c_str(), tablet);
										do_write_backend(connfd, reply, strlen(reply));
									}
									else
									{
										// const char *reply = "+OK GET successful.\r\n";
										// write(connfd, reply, strlen(reply));
										std::string table_val;
										int lock_attempt = pthread_rwlock_tryrdlock(row_locks[key].get());
										int n_tries = 5;
										bool lock_successful = false;
										while (n_tries > 0)
										{
											if (lock_attempt == 0)
											{ // Lock attempt succeeded
												table_val = table[tablet][key];
												pthread_rwlock_unlock(row_locks[key].get());
												lock_successful = true;
												break;
											}
											else if (lock_attempt == EBUSY)
											{
												n_tries--;
											}
											else
											{
												fprintf(stderr, "Error when attempting to acquire lock.\n");
											}
										}
										if (lock_successful == false)
										{
											table_val = table[tablet][key];
											print_debug(debug_print, "Unable to acquire the read lock after five tries, so I just read from the table without one to avoid a potential deadlock. Not sure if I'll improve this method in the future.\n");
										}
										std::string value = table_val + "\r\n";
										if (checkpointing[tablet])
										{
											print_debug(debug_print, "Checkpointing - adding GET reply to checkpoint queue.\n");
											checkpoint_queue.push(std::make_pair(connfd, value));
										}
										else
										{
											print_debug(debug_print, "Returning table value %s.\n", table_val.substr(0, 50).c_str());
											do_write_backend(connfd, value.c_str(), value.size());
										}
									}
								}
							}
							// CPUT A B C D
							else if (message.substr(0, 4) == "CPUT")
							{
								print_debug(debug_print, "Received CPUT command.\n");
								size_t space2 = colv.find(' ');
								std::string col = colv.substr(0, space2);
								std::string values = colv.substr(space2 + 1);
								int tablet = get_tablet(row);
								if (!active_tablets[tablet])
								{
									load_tablet(tablet);
								}
								print_debug(debug_print, "Tablet number: %d.\n", tablet);
								Key key = {row, col};
								// if the key doesnt exist already
								if (table[tablet].find(key) == table[tablet].end())
								{
									const char *reply = "-ERR 204 specific row and column does not exist.\r\n";
									do_write_backend(connfd, reply, strlen(reply));
									continue;
								}
								std::string &existing_val = table[tablet][key];
								std::string original_val = values.substr(0, std::min(existing_val.size(), values.size()));
								if (original_val != existing_val)
								{
									const char *reply = "-ERR 204 original value does not match.\r\n";
									do_write_backend(connfd, reply, strlen(reply));
									continue;
								}
								// if the new value is the same as the existing value
								std::string new_val = values.substr(existing_val.size() + 1, values.size());
								if (new_val == existing_val)
								{
									const char *reply = "+OK table entry did not change.\r\n";
									do_write_backend(connfd, reply, strlen(reply));
								}
								else
								{
									print_debug(debug_print, "Calling handle_message");
									// print_debug(debug_print, "Message for handle_message is %s.\n", message.c_str());
									handle_message(key, 2, hash, message, connfd, is_primary, ack_message, backend_conn, ok_respond, new_val, success);
								}
							}
							else if (message.substr(0, 6) == "DELETE")
							{
								print_debug(debug_print, "Received DELETE command.\n");
								std::string col = colv;
								int tablet = get_tablet(row);
								print_debug(debug_print, "Tablet number: %d.\n", tablet);
								Key key = {row, col};
								if (table[tablet].find(key) == table[tablet].end() && !(ack_message || ok_respond))
								{
									const char *reply = "-ERR 204 specific row and column does not exist.\r\n";
									do_write_backend(connfd, reply, strlen(reply));
									print_debug(debug_print, "Sending response to frontend %s.\n", reply);
								}
								else
								{
									print_debug(debug_print, "Calling handle_message");
									// print_debug(debug_print, "Message for handle_message is %s.\n", message.c_str());
									handle_message(key, 3, hash, message, connfd, is_primary, ack_message, backend_conn, ok_respond, "", success);
								}
							}
						}
						// (12) Invalid command //////////////////////////////////////////////////////////////////////
						else
						{
							if (!backend_conn)
							{
								// Invalid command. Tell the frontend the command is unknown.
								std::string error = "-ERR Unknown command: " + message.substr(0, 200) + "\r\n"; // If the string is too long, then only send back the first 200 characters
								do_write_backend(connfd, error.c_str(), error.size());
								print_debug(debug_print, "Invalid command from what is not a backend connection.\n");
								continue;
							}
							else
							{
								print_debug(debug_print, "Message could not be processed: %s.\n", message.c_str());
							}
						}
					}
				}
			}
		}
	}

	// HANDLE CLIENT DISCONNECTION //////////////////////////////////////////////////////////////////////////
	// Proceed to update the global lists and queues accordingly, close the client fd and tpc file, then exit the thread
	print_debug(debug_print, "[%d] Connection closed\n", connfd);

	// clean up partition_fds
	pthread_mutex_lock(&partition_fds_lock);
	auto it = std::find(partition_fds.begin(), partition_fds.end(), connfd);
	if (it != partition_fds.end())
	{
		partition_fds.erase(it);
	}
	pthread_mutex_unlock(&partition_fds_lock);

	// remove from alive replicas
	if (is_primary && backend_conn)
	{
		pthread_mutex_lock(&tpc_alive_replicas_lock);
		alive_replicas_ipport.erase(conn_address);
		pthread_mutex_unlock(&tpc_alive_replicas_lock);
	}

	// remove from active threads and fds and add to available connections
	pthread_rwlock_wrlock(&rwlock);
	avail_connections.push(msgID);
	active_threads.remove(threadc);
	active_fds.remove(connfd);
	pthread_rwlock_unlock(&rwlock);
	// close connection
	print_debug(debug_print, "Closing socket %d\n", connfd);
	close(connfd);
	print_debug(debug_print, "Closing thread.\n");
	pthread_exit(NULL);
	/////////////////////////////////////////////////////////////////////////////////////////////////////////
}
