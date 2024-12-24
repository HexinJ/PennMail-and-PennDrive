#ifndef THREADS_H
#define THREADS_H

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
#include <pthread.h>
#include <algorithm>
#include <unordered_map>
#include "externs.h"
// typedef std::pair<std::string, std::string> Key;

void send_heartbeat();
void *heartbeat_func(void* args);
void *log_func(void* args);

void create_log_thread(int tablet_num);
void create_thread(bool heartbeat, bool coord_connection, int thread_fd);
void handle_message(Key key, int opt, std::string hash, std::string message, int connfd, 
	bool is_primary, bool ack_message, bool backend_conn, bool ok_respond, std::string val = " ");
void *start_func(void* args);

#endif
