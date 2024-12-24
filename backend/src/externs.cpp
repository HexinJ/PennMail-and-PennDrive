#include <externs.h>

typedef std::unordered_map<std::string, std::shared_ptr<message_queue>> MSG_QUEUE;

// SERVER INFORMATION //////////////////////////////////////////////
int server_idx;
int partition;
int total_partitions = 1;
int version;
int checkpoints[5];
// int primary_checkpoint;
int log_sequences[5];
int max_clients = 100;
std::string listen_address; // Global variables for all the server binding and forwarding addresses
std::string IP;
int listen_port;
int heartbeat_port;
int comm_port; // Address for regular communication with the coordinator
int primary_port;
std::string forward_address;
std::vector<std::string> forward_addresses; // Vectors for addresses, room members, and clients
std::string heartbeat_addr;
std::string comm_addr;
std::string primary_addr;
int PARTITION_COUNT = -1;
bool debug_print = false;
bool is_primary = false;
bool created_heartbeat = false;
volatile sig_atomic_t shutting_down = 0;

// CONNECTIONS //////////////////////////////////////////////
std::vector<int> storage_fds;		 // fds of all the backend servers (could be coo)
std::vector<int> partition_fds;		 // FOR PRIMARY: fds from replicas in the partition
int primary_fd;						 // fd to primary server
int load_balancer_fd;				 // fd for the load balancing server
int heartbeat_fd;					 // fd to communicate heartbeat with coordinator
std::list<pthread_t> active_threads; // list of all currently active threads
std::queue<int> avail_connections;	 // Queue to keep track of which client IDs are available to use.
std::list<int> active_fds;			 // all currently active fds

// use this variable to check number of active replicas for 2pc
// FOR PRIMARY: set of alive replicas IP port
std::unordered_set<std::string> alive_replicas_ipport;
int replica_count = -1;

// FOR ADMIN CONSOLE
// threads should automatically close when client fd closes
std::unordered_set<int> frontend_fds;
std::unordered_set<pthread_t> frontend_threads; // all the frontend threads
std::unordered_set<pthread_t> replica_threads;	// FOR PRIMARY: set of all replica thread

// LOGGING, CHECKPOINTING, TABLETS //////////////////////////////////
std::unordered_map<std::pair<std::string, std::string>, std::string, key_hash> table[5];
// std::map<std::pair<std::string, std::string>, std::string, Col_Sort> table;
std::unordered_map<int, std::vector<std::string>> partition_map;
std::queue<std::string> log_queues[5];
std::queue<std::string> primary_queue;
std::time_t heartbeat_timestamp;

std::string log_filepaths[5];
std::string table_filepaths[5];
bool active_tablets[5];
bool checkpointing[5] = {false, false, false, false, false};
int checkpoint_acks[5];
bool recovering[5] = {false, false, false, false, false};

pthread_rwlock_t rwlock = PTHREAD_RWLOCK_INITIALIZER; // Initialize mutexes.
pthread_rwlock_t loglocks[5];						  // locks for log queues
pthread_rwlock_t filelocks[5];						  // locks for logfiles
pthread_rwlock_t tablelocks[5];						  // locks for checkpoint files

MSG_QUEUE msg_queue;
std::queue<std::pair<int, std::string>> checkpoint_queue;
std::unordered_map<std::pair<std::string, std::string>, std::shared_ptr<pthread_rwlock_t>, key_hash> row_locks;

std::vector<pthread_t> threads(max_clients);	  // Vector to ensure thread addresses are stable
std::vector<arg_params> arg_structs(max_clients); // Vector to ensure arg struct addresses are stable

// 2PC DATA STRUCTURES //////////////////////////////////////////////
// FOR PRIMARY
// replica receives PUT command and forward it to primary
// 1) primary receives and assign seq number
// 1) Store the list of all replicas in partition into LOG + seq number (SEQ-list of replicas)
// 2) send PREPARE + seq number + command to all replicas
// 3) wait for NO/YES + tid + from all replicas - store responses in tpc_responses
// 4) if all YES, send COMMIT + seq to all replicas
// 4) if any NO, send ABORT + seq to all replicas
// 5) wait for ACK + seq from all replicas
// 6) log END + seq
std::unordered_map<int, std::unordered_set<std::string>> tpc_responses; // transaction ID : responses from replica IP port
std::unordered_map<int, std::unordered_set<std::string>> tpc_replicas;	// list of current replicas for each transaction
int tpc_seq = 0;														// global sequence number variable
std::map<int, std::chrono::steady_clock::time_point> tpc_start_time;	// list of all ongoing transactions start time
// primary has tpc_queue as well
std::unordered_set<int> queued_transactions; // list of transactions to commit or abort

// FOR REPLICAS (running in communication with primary thread)
// 1) if receive PREPARE, put transaction into queue, check if possible to commit and respond
// 2) if receive COMMIT, update and remove from queue and respond ACK
// 3) if receive ABORT, update and remove from queue and respond ACK
std::map<int, struct transaction> tpc_queue; // transaction seq, command
int last_seq = 0;							 // last processed transaction number

// VARIABLES FOR RECOVERY
// used by primary to gather all pending transactions across replicas when new primary is chosen
// while new primary is recovering from 2pc, it should pause other 2pc transactions until all replicas connect
std::map<int, struct transaction> tpc_pending; // list of pending transactions
bool primary_recovering = false;			   // flag to indicate if new primary is recovering
pthread_mutex_t primary_ready_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t primary_ready_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t primary_count_cond = PTHREAD_COND_INITIALIZER;

// OTHER VARIABLES
std::chrono::seconds timeout_max = std::chrono::seconds(5); // timeout duration for 2PC
pthread_t tpc_tid;											// thread checking for transaction timeouts
pthread_mutex_t tpc_response_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t tpc_start_time_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t tpc_queue_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t tpc_last_seq_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t queued_transactions_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t tpc_alive_replicas_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t tpc_replica_count_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t tpc_log_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t tpc_seq_lock = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t tpc_pending_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t partition_fds_lock = PTHREAD_MUTEX_INITIALIZER;

// LOCKS TO HAVE
// tpc responses, tpc start time,
// tpc queue, last seq, queued transactions
// alive ip port, replica count
// log file
// tpc seq

// TPC LOG FILE INFORMATION
std::fstream tpc_file;
std::string tpc_filepath;

// tid for communication with primary
pthread_t primary_comm_tid;
std::atomic<bool> terminate_primary_thread(false);
