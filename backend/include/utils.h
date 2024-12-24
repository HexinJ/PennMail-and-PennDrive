#ifndef UTILS_H
#define UTILS_H

#include "externs.h"

bool is_recovering();

std::string get_absolute_path(std::string filepath);

void init_locks();
void destroy_locks();

std::string create_hash(const std::string &message);

std::pair<std::string, int> IP_port(std::string &addr);

std::string get_address(struct sockaddr_in &addr);

void print_debug(bool debug_print, const char *message, ...);

void coord_address(const char *filename);
void populate_addresses(const char *filename, int server_idx = 0);

std::pair<int, int> create_listen_socket(int port, int max_conns);
int create_connect_socket(std::string ip_address);
int create_connect_socket_port(std::string ip_address, std::string ip, int port);

void forward_msg(const std::string &message);
void send_queued_messages();
void send_queued_primary();
void handle_primary_queue();

std::vector<std::string> split(std::string word, char delimiter);

void log_message(std::string message, int last_seq, int tablet_num);
void flush_log(int tablet_num);

int get_partition(const std::string &email);
void parsing_logic(std::string &message);
void write_table(const std::string &line, std::string table_filepath);

void recover_table(std::string filepath, int tablet_num);
void restore_table(std::string filepath, int tablet_num);

void do_write_backend(int fd, const char *buf, int len);
std::string do_read_backend(int fd, std::string delim);

void trim_trailing_newline(std::string &str);

std::string create_log_string(int tablet_num);
void clear_file(std::string filepath);
void parse_log_string(std::string &str, int tablet_num);

bool checkpoint_eligible(std::string filepath);
void start_new_checkpoint(int tablet_num);
void checkpoint_helper(std::string message);
void create_new_checkpoint(int tablet_num);
void create_new_checkpoint_from_logfile(std::string filepath);

void process_tpc_queue();
void primary_abort_transaction(int seq_num, std::string hash);
void primary_commit_transaction(int seq_num, std::string hash);
void execute_command(std::string command);
bool get_command_locks(std::string command);
void start_tpc(std::string command, std::string hash, int cur_seq);
bool already_processed(int seq_num);

std::string generate_client_response(bool success, int opt);

void create_new_tablet_log(std::string filepath);
void create_new_checkpoint_file(std::string filepath);
void set_tablet_version();
int get_tablet(std::string row);
void clear_tablet(int tablet_num);
void *clear_func(void *args);
void create_clear_thread(int tablet_num);
void load_tablet(int tablet_num);
size_t total_avail_memory();
void create_files();
void load_tablets_startup();

std::string parse_checkpoint_file(std::string filepath);
std::string parse_log_file(std::string filepath);
std::string make_kv_pairs();

void write_to_tpc_log(std::string message);

void process_tpc_log();

#endif
