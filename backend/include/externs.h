#ifndef EXTERN_H
#define EXTERN_H

#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <strings.h>
#include <cstring>
#include <queue>
#include <ctime>
#include <csignal>
#include <list>
#include <pthread.h>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <map>
#include <memory>
#include <fstream>
#include <chrono>
#include <atomic>

typedef std::pair<std::string, std::string> Key;

// struct for transactions for 2PC
struct transaction
{
	std::string status;
	std::string command;
	std::string hash;

	// // comparison for the set
	// bool operator<(const transaction &other) const
	// {
	// 	return seq_num < other.seq_num;
	// }
};

struct message_queue
{
	int fd;
	// std::string message;
	int n_acks = 0;
	bool from_frontend;
	std::string tpc_success;	 // PREPARE, COMMIT, or ABORT
	std::string original_ipport; // IPPORT of the original sender (server or client)
	int opt = -1;
	std::string row;
	std::string col;
};
typedef std::unordered_map<std::string, std::shared_ptr<message_queue>> MSG_QUEUE;

struct key_hash
{
	template <class Row, class Col>
	std::size_t operator()(const std::pair<Row, Col> &pair) const
	{
		auto hash1 = std::hash<Row>{}(pair.first);
		auto hash2 = std::hash<Col>{}(pair.second);

		return hash1 * 31 + hash2;
	}
};

struct Col_Sort
{
	bool operator()(const std::pair<std::string, std::string> &v1,
					const std::pair<std::string, std::string> &v2) const
	{
		if (v1.second == v2.second)
		{
			return v1.first < v2.first;
		}
		return v1.second < v2.second;
	}
};

// The struct of params that are passed into pthread_create().
struct arg_params
{
	int conn_fd;
	int conn_id;
	bool coord_conn = false;
	std::vector<char> buffer;
	pthread_t thread;
	bool backend_conn;
	std::string conn_address;
	int tablet_num = 0;
	std::string log_str;
};

struct Delete_RWLock
{
	void operator()(pthread_rwlock_t *lock) const
	{
		if (lock)
		{
			pthread_rwlock_destroy(lock);
			delete lock;
		}
	}
};

extern int server_idx;
extern int partition;
extern int total_partitions;
extern int version;
extern int checkpoints[5];
// extern int primary_checkpoint;
extern int log_sequences[5];
extern int max_clients;
extern std::string listen_address; // Global variables for all the server binding and forwarding addresses
extern std::string IP;
extern int listen_port;
extern int heartbeat_port;
extern int comm_port; // Address for regular communication with the coordinator
extern int primary_port;
extern std::string forward_address;
extern std::vector<std::string> forward_addresses; // Vectors for addresses, room members, and clients
extern std::string heartbeat_addr;
extern std::string comm_addr;
extern std::string primary_addr;
extern std::vector<int> storage_fds; // fds of all the backend servers
extern std::vector<int> partition_fds;
extern int primary_fd;
extern int load_balancer_fd; // The fd for the load balancing server
extern int heartbeat_fd;
extern bool debug_print;
extern bool is_primary;
extern bool created_heartbeat;
extern std::unordered_map<std::pair<std::string, std::string>, std::string, key_hash> table[5];
// extern std::map<std::pair<std::string, std::string>, std::string, Col_Sort> table;
extern std::unordered_map<int, std::vector<std::string>> partition_map;
extern std::queue<std::string> log_queues[5];
extern std::queue<std::string> primary_queue;
extern std::time_t heartbeat_timestamp;
extern std::list<pthread_t> active_threads; // List to keep track of currently active threads.
extern std::queue<int> avail_connections;	// Queue to keep track of which client IDs are available to use.
extern std::list<int> active_fds;			// List to keep track of currently active fds.
extern std::string log_filepaths[5];		// The array of filepaths of the server's logfile
extern std::string table_filepaths[5];		// The array of filepaths of the server's most recent checkpoint file
extern bool active_tablets[5];
extern bool checkpointing[5];
extern int checkpoint_acks[5];
extern bool recovering[5];

extern pthread_rwlock_t rwlock;
extern pthread_rwlock_t loglocks[5];   // Locks for the log queues
extern pthread_rwlock_t filelocks[5];  // Locks for the log files
extern pthread_rwlock_t tablelocks[5]; // Locks for the checkpoint files
extern MSG_QUEUE msg_queue;
extern std::queue<std::pair<int, std::string>> checkpoint_queue;
extern std::unordered_map<std::pair<std::string, std::string>, std::shared_ptr<pthread_rwlock_t>, key_hash> row_locks;
extern std::vector<pthread_t> threads;		// Vector to ensure thread addresses are stable
extern std::vector<arg_params> arg_structs; // Vector to ensure arg struct addresses are stable

// data structs for 2PC
extern std::unordered_map<int, std::unordered_set<std::string>> tpc_responses;
extern std::map<int, struct transaction> tpc_queue;
extern int tpc_seq;
extern std::map<int, std::chrono::steady_clock::time_point> tpc_start_time;
extern std::chrono::seconds timeout_max; // MAX RESPONSE WAITING TIME FOR 2PC
extern std::unordered_map<int, std::unordered_set<std::string>> tpc_replicas;
extern std::unordered_set<std::string> alive_replicas_ipport; // set of alive replicas
extern int last_seq;										  // last transaction processed in this server

extern std::map<int, struct transaction> tpc_pending;
extern bool primary_recovering;
extern int replica_count;
extern pthread_t tpc_tid;
// extern std::map<int, pthread_mutex_t> tpc_response_locks;
extern pthread_mutex_t tpc_response_lock;
extern pthread_mutex_t primary_ready_mutex;
extern pthread_cond_t primary_ready_cond;
extern pthread_cond_t primary_count_cond;
extern pthread_mutex_t tpc_queue_lock;
extern pthread_mutex_t tpc_pending_lock;
extern pthread_mutex_t tpc_last_seq_lock;
extern int tpc_last_executed_seq;
extern pthread_mutex_t tpc_log_lock;
extern pthread_mutex_t tpc_alive_replicas_lock;
extern pthread_mutex_t partition_fds_lock;
extern pthread_mutex_t tpc_replica_count_lock;
extern pthread_mutex_t tpc_start_time_lock;
extern pthread_mutex_t tpc_seq_lock;

extern std::unordered_set<int>
	frontend_fds;
extern std::unordered_set<pthread_t> frontend_threads;
extern std::unordered_set<pthread_t> replica_threads;

extern std::fstream tpc_file;
extern std::string tpc_filepath;

extern std::unordered_set<int> queued_transactions; // list of transactions to commit or abort
extern pthread_mutex_t queued_transactions_lock;

extern int PARTITION_COUNT;
extern volatile sig_atomic_t shutting_down;

extern pthread_t primary_comm_tid;
extern std::atomic<bool> terminate_primary_thread;

#endif
