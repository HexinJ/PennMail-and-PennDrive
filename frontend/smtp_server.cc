#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <time.h>
#include <sys/file.h>
#include <iostream>
#include <sstream>
#include <openssl/md5.h>
#include "message.h"
#include "ioutils.h"
#include "stringutils.h"

// Define sizes
#define BUFFER_SIZE 2000
#define MAX_USER_NAME_SIZE 64
#define MAX_DOMAIN_SIZE 64
#define MAIL_FROM_BUFFER_SIZE (MAX_USER_NAME_SIZE + MAX_DOMAIN_SIZE + 1)
#define RCPT_TO_BUFFER_SIZE 7400
#define DATA_SIZE 2000
#define MAX_RECIPIENTS 100
#define MAX_CONNECTIONS 10000
#define MAX_MAILBOXES 500

// Define states
#define NEW 0 // In this state, the connection just started and the only valid command is HELO
#define READY 1 // In this state, the server can accept HELO and MAIL_FROM commands
#define TRANSACTION 2 // In this state, the server received a MAIL_FROM command, and is ready to receive recipients
#define DATA 3 // In this state, the DATA command is called and everything that is entered is mail data

// Define commands
#define HELO_INT 0
#define MAIL_FROM 1
#define RCPT_TO 2
#define DATA 3
#define QUIT 4
#define RSET 5
#define NOOP 6
#define UNKNOWN_COMMAND 7
#define TITLE 8

int open_connections = 0;
int open_fds[MAX_CONNECTIONS];
int listen_fd;
bool debug_mode = false;
bool is_shutdown = false;

bool verify_domain(char *domain){
    // Verify if domain is of the correct format *@*, * cannot be NULL
    char *ptr;
    ptr = strchr(domain, '@');
    if(ptr == NULL){
        return false;
    }

    if(ptr == domain || strlen(ptr) == 1){
        return false;
    }
    
    return true;
}

int identify_instruction(char *instruction, int length){
    length --;
    // printf("Length: %d\n", length);
    if(length == 4){
        if(strncasecmp(instruction, "QUIT", 4) == 0){
            return QUIT;
        }
        else if(strncasecmp(instruction, "DATA", 4) == 0){
            return DATA;
        }
        else if(strncasecmp(instruction, "RSET", 4) == 0){
            return RSET;
        }
        else if(strncasecmp(instruction, "NOOP", 4) == 0){
            return NOOP;
        }
    }
    if(length > 5){
        if(strncasecmp(instruction, "HELO ", 5) == 0){
            return HELO_INT;
        }
        else if(strncasecmp(instruction, "TITLE ", 6) == 0){
            return TITLE;
        }
    }
    if(length > 8){
        if(strncasecmp(instruction, "RCPT TO:", 8) == 0){
            return RCPT_TO;
        }
    }
    if(length > 10){
        if(strncasecmp(instruction, "MAIL FROM:", 10) == 0){
            return MAIL_FROM;
        }
    }
    return UNKNOWN_COMMAND;
}

bool do_write(int fd, char *buf, int len){
    int bytes_sent = 0;
    while(bytes_sent < len){
        int n = write(fd, &buf[bytes_sent], len-bytes_sent);
        if(n < 0){
            return false;
        }
        bytes_sent += n;
    }
    return true;
}

void SIGINT_handler(int sig){
    // Send all open sockets error message and closing
    is_shutdown = true;

    // Closing all connections
    for(int i = 0; i < MAX_CONNECTIONS; i++){
        if(open_fds[i] != -1){
            do_write(open_fds[i], (char *)"421 <localhost> Service not available, closing transmission channel\r\n", 69);
            close(open_fds[i]);
            open_connections --;
        }
    }

    // Close socket
    close(listen_fd);
    exit(1);
}

void* worker(void * arg){
    // Worker function to service incoming connections
    int comm_fd = *(int*)arg;

    // Greeting message
    if (debug_mode) printf("[%d] 220 localhost Simple Mail Transfer Service Ready\r\n", comm_fd);
    do_write(comm_fd, (char *)"220 localhost Simple Mail Transfer Service Ready\r\n", 50); // not writing the \0

    // My name - to be filled by HELO
    char my_name[MAIL_FROM_BUFFER_SIZE];

    // Creating buffers and states
    char buffer[BUFFER_SIZE]; // read buffer
    char mail_from_buffer[MAIL_FROM_BUFFER_SIZE]; // buffer for sender address
    char* rcpt_to_buffer[MAX_RECIPIENTS]; // buffer for recipient addresses
    char data_buffer[DATA_SIZE]; // buffer for mail data
    std::string title; // title of email
    int state = NEW; // server is ready to accept HELO

    // Creating variables for counting
    int mail_from_buffer_idx = 0; // length of the name in mail_from buffer
    int rcpt_to_buffer_idx = 0; // number of recipients
    int data_buffer_idx = 0;

    bzero(my_name, sizeof(my_name));
    bzero(buffer, sizeof(buffer));
    bzero(mail_from_buffer, sizeof(mail_from_buffer));
    bzero(rcpt_to_buffer, sizeof(rcpt_to_buffer));
    bzero(data_buffer, sizeof(data_buffer));


    int read_offset = 0;
    while(true){
        // If state = DATA, read into data buffer and check for <CRLF>.<CRLF> 
        // Send emails if received <CRLF>.<CRLF>
        if(state == DATA){
            // Read 
            int n = read(comm_fd, data_buffer + data_buffer_idx, DATA_SIZE);
            if (n <= 0){
                break; // User terminated client
            }
            if (debug_mode) printf("[%d] C: %s\r\n", comm_fd, data_buffer + data_buffer_idx);

            bool received_dot = false;
            
            // Compare each newly read character with <CRLF>.<CRLF>
            // printf("buffer_idx: %d\n", data_buffer_idx);
            for(int i = data_buffer_idx; i <= data_buffer_idx + n - 3; i++){
                if(i >= 2 && strncmp(data_buffer + i - 2, "\r\n.\r\n", 5) == 0){
                    // Terminate this stage and send email
                    if (debug_mode) printf("[%d] S: Received the final .\n", comm_fd);

                    // Get name of sender
                    char sender[mail_from_buffer_idx + 1];
                    strncpy(sender, mail_from_buffer, mail_from_buffer_idx + 1);
                    if (debug_mode) printf("[%d] S: Sender: %s\n", comm_fd, sender);

                    // Get current time
                    time_t curtime;
                    time(&curtime);
                    char *current_time = ctime(&curtime);

                    // Thunderbird?
                    bool external = false;

                    if(title.empty()){
                        external = true;
                        char *temp = strstr(data_buffer, "Subject: ") + 9;
                        int idx = 0;
                        while(temp[idx] != '\r'){
                          title.push_back(temp[idx]);
                          idx++;
                        }
                    }

                    char email_data[strlen(sender) + strlen(current_time) + strlen(title.c_str()) + i + 2];
                    bzero(email_data, sizeof(email_data));
                    strcat(email_data, sender);
                    strcat(email_data, "|");

                    if (debug_mode) printf("[%d] S: Title: %s\n", comm_fd, title.c_str());
                    strcat(email_data, title.c_str());
                    strcat(email_data, "|");
                    strncat(email_data, current_time, strlen(current_time) - 1);

                    // Store the hash
                    std::string hash = compute_hash_16(email_data, strlen(email_data));

                    strcat(email_data, "|");
                    strncat(email_data, data_buffer, i - 2);

                    // Establish a connection with the coordinator
                    // Create a socket for the server.
                    int coor_fd = socket(PF_INET, SOCK_STREAM, 0);
                    struct sockaddr_in serverAddr;
                    bzero(&serverAddr, sizeof(serverAddr));
                    serverAddr.sin_family = AF_INET;
                    serverAddr.sin_port = htons(5001);
                    if (inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr) <= 0)
                    {
                        fprintf(stderr, "inet_pton failed.\n");
                        close(coor_fd);
                        exit(1);
                    }
                    if (connect(coor_fd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
                    { // TODO: this doesn't currently account for if the server has crashed and can't immediately connect
                        fprintf(stderr, "Connection with coordinator failed.\n");
                        close(coor_fd);
                        exit(1);
                    }

                    // Receiving greeting from coordinator
                    std::string coor_message = do_read(coor_fd, CRLF);
                    if(debug_mode) std::cout << "[COOR]: " << coor_message << std::endl;

                    // Sending helo to coordinator
                    std::string helo_message = "HELO passwd "; 
                    helo_message.append(my_name);
                    helo_message += "\r\n";
                    do_write(coor_fd, (char *)helo_message.c_str(), helo_message.length());

                    // Expect message from coordinator
                    coor_message = do_read(coor_fd, CRLF);
                    if(debug_mode) std::cout << "[COOR]: " << coor_message << std::endl;

                    // Create socket and connect to table server
                    int table_fd = create_connect_socket(coor_message.substr(4, coor_message.size() - 4));
                    std::string table_message = do_read_backend(table_fd, "\r\n");
                    if(debug_mode) std::cout << "[TABLE]: " << table_message << std::endl;

                    for(int o = 0; o < rcpt_to_buffer_idx; o++){
                        // Get recipient domain
                        char *recipient = rcpt_to_buffer[o];

                        // Get eid_vector from backend
                        std::string eids = bend_GET(&table_fd, recipient, "eid_vector");
                        if(debug_mode) std::cout << "[GET_EIDS]: " << eids << std::endl;

                        // Append the hash of current email to the vector and update the value
                        if(eids == "-ERR 204 specific row and column does not exist."){
                            std::string put_eid_status = bend_PUT(&table_fd, recipient, "eid_vector", hash);
                            if(debug_mode) std::cout << "[PUT_EID_STATUS]: " << put_eid_status << std::endl;
                        }
                        else{
                            eids += ",";
                            eids += hash;
                            std::string put_eid_status = bend_PUT(&table_fd, recipient, "eid_vector", eids);
                            if(debug_mode) std::cout << "[PUT_EID_STATUS]: " << put_eid_status << std::endl;
                        }

                        // Construct the json-like email string
                        std::unordered_map <std::string, std::string> email_map;
                        email_map["sender"] = sender;
                        email_map["subject"] = title;
                        email_map["timestamp"] = current_time;
                        email_map["timestamp"] = email_map["timestamp"].substr(0, strlen(current_time) - 1);

                        if(!external){
                          email_map["message"] = data_buffer;
                          email_map["message"] = email_map["message"].substr(0, strlen(data_buffer) - 5);
                        }
                        else{
                          std::string temp_buffer = data_buffer;
                          temp_buffer = temp_buffer.substr(temp_buffer.find("\r\n\r\n")+4);
                          for(int u = 0; u < 5; u++){
                            temp_buffer.pop_back();
                          }
                          email_map["message"] = temp_buffer;
                        }

                        std::string email_string = map_to_json(email_map);
                        email_string = stobs(email_string);
                        std::cout << "SENDER: " << sender << std::endl;
                        if (debug_mode) std::cout << "[EMAIL STRING]: " << email_string << std::endl;

                        // Put email in the file system
                        std::string status = bend_PUT(&table_fd, recipient, hash, email_string);
                        if (debug_mode) std::cout << "[PUT_EMAIL_STATUS]: " << status << std::endl;
                        
                        // Successful writing of email
                        if(status.rfind(OK) == 0){
                            if (debug_mode) printf("[%d] S: 250 Email successfully sent to: %s\n", comm_fd, recipient);
                            std::string response = "+OK ";
                            response += recipient;
                            response += "\r\n";
                            if(!external){
                                do_write(comm_fd, (char *)(response.data()), response.size());
                            }
                            else{
                                do_write(comm_fd, (char *)"250 OK\r\n", 8);
                            }
                        }
                        else{
                            if (debug_mode) printf("[%d] S: 550 Error when sending email to: %s\n", comm_fd, recipient);
                            std::string response = "-ERR ";
                            response += recipient;
                            response += "\r\n";
                            if(!external){
                                do_write(comm_fd, (char *)(response.data()), response.size());
                            }
                            else{
                                do_write(comm_fd, (char *)"550 Requested action not taken: mailbox unavailable\r\n", 53);
                            }
                        }
                    }

                    // Set state to READY
                    state = READY;

                    // Reset all counters
                    rcpt_to_buffer_idx = 0;
                    data_buffer_idx = 0;
                    mail_from_buffer_idx = 0;

                    // Clear all buffers
                    bzero(my_name, sizeof(my_name));
                    bzero(mail_from_buffer, sizeof(mail_from_buffer));
                    bzero(data_buffer, sizeof(data_buffer));

                    // Free rcpt_to buffer
                    for(int o = 0; o < MAX_RECIPIENTS; o++){
                        if(rcpt_to_buffer[o] != NULL){
                            free(rcpt_to_buffer[o]);
                        }
                    }

                    do_write(comm_fd, (char *)"250 OK\r\n", 8);
                    received_dot = true;
                    break;
                }
            }

            if (received_dot == false){
                // If no <CRLF>.<CRLF> is detected, increment idx
                data_buffer_idx += n;
                if (debug_mode) printf("[%d] S: Data buffer: %s\n", comm_fd, data_buffer);
            }
            
            continue;
        }

        // Not DATA state: read anything that is available into normal buffer
        int n = read(comm_fd, buffer + read_offset, BUFFER_SIZE);
        if (n <= 0){
            break; // User terminated client
        }
        read_offset += n;

        // Execute any full lines in the buffer
        int end_idx = 0;
        char *instruction;
        int instruction_size;
        bool found_instruction = false;
        bool close_connection = false;

        for(int i = 0; i < read_offset; i++){
            if(buffer[i] == '\r' && buffer[i + 1] == '\n'){
                instruction_size = i + 1 - end_idx;
                instruction = (char *) malloc(instruction_size);
                for(int o = end_idx; o < i; o++){
                    instruction[o - end_idx] = buffer[o];
                }
                instruction[i - end_idx] = '\0';
                if (debug_mode) printf("[%d] C: %s\r\n", comm_fd, instruction);

                end_idx = i + 2;
                found_instruction = true;
                
                // Identify instruction
                int int_code = identify_instruction(instruction, instruction_size);
                // if (debug_mode) printf("[%d] S: Instruction code: %d\n", comm_fd, int_code);

                // STATE NEW: Only accept a HELO command, fill in name 
                if(state == NEW){
                    if(int_code == HELO_INT){
                        // Find name of user
                        int idx = 5;
                        while(idx < instruction_size){
                            my_name[idx - 5] = instruction[idx];
                            idx++;
                        }
                        my_name[idx + 1] = '\0';
                        if (debug_mode) printf("[%d] S: Received HELO, name: %s\n", comm_fd, my_name);
                        do_write(comm_fd, (char *)"250 localhost\r\n", 15);
                        // Change state to READY
                        state = READY;
                    }
                    // Not a HELO command, send error
                    else{
                        if(int_code != 7){
                            // Bad sequence of commands
                            if (debug_mode) printf("[%d] S: 503 Bad sequence of commands\n", comm_fd);
                            do_write(comm_fd, (char *)"503 Bad sequence of commands\r\n", 30);
                        }
                        else{
                            // Command not recognized
                            if (debug_mode) printf("[%d] S: 500 Syntax error, command unrecognized\n", comm_fd);
                            do_write(comm_fd, (char *)"500 Syntax error, command unrecognized\r\n", 40);
                        }
                        
                    }
                }
                // STATE READY: In this state, the server is ready to accept a MAIL_FROM (and other commands)
                else if(state == READY){
                    // MAIL_FROM
                    if(int_code == MAIL_FROM){
                        // Detect "<"
                        if(instruction[10] != '<'){
                            if (debug_mode) printf("[%d] S: 500 Syntax error, spaces between : and < or domain not bound in <>\n", comm_fd);
                            do_write(comm_fd, (char *)"500 Syntax error, spaces between : and < or domain not bound in <>\r\n", 68);
                        }
                        else{
                            // Set mail_from_buffer
                            int idx = 11;
                            char temp_buffer[MAIL_FROM_BUFFER_SIZE];

                            // Copy data to temp buffer
                            while(instruction[idx] != '>' && idx < instruction_size - 1){
                                temp_buffer[idx - 11] = instruction[idx];
                                idx++;
                            }
                            temp_buffer[idx - 11] = '\0';
                            // Validate recipient
                            // printf("Temp buffer: %s\n", temp_buffer);
                            bool result = verify_domain(temp_buffer);
                            // printf("Verified domain: %d\n", result);

                            // If incorrect domain format, return error
                            if(result == false){
                                if (debug_mode) printf("[%d] S: 500 Syntax error, domain must follow the format *@*\n", comm_fd);
                                do_write(comm_fd, (char *)"500 Syntax error, domain must follow the format *@*\r\n", 53);
                            }
                            else{
                                strncpy(mail_from_buffer, temp_buffer, idx - 10);
                                mail_from_buffer_idx = idx - 10;
                                if (debug_mode) printf("[%d] S: mail_from_buffer: %s\n", comm_fd, mail_from_buffer);
                                do_write(comm_fd, (char *)"250 OK\r\n", 8);
                                state = TRANSACTION;
                            }
                        }
                    }
                    // RSET
                    else if(int_code == RSET){
                        // Set state to READY
                        state = READY;

                        // Reset all counters
                        rcpt_to_buffer_idx = 0;
                        data_buffer_idx = 0;
                        mail_from_buffer_idx = 0;

                        // Clear all buffers
                        bzero(my_name, sizeof(my_name));
                        bzero(mail_from_buffer, sizeof(mail_from_buffer));
                        bzero(data_buffer, sizeof(data_buffer));

                        // Free rcpt_to buffer
                        for(int o = 0; o < MAX_RECIPIENTS; o++){
                            if(rcpt_to_buffer[o] != NULL){
                                free(rcpt_to_buffer[o]);
                            }
                        }

                        if (debug_mode) printf("[%d] S: 250 OK in response to RSET\n", comm_fd);
                        do_write(comm_fd, (char *)"250 OK\r\n", 8);
                    }
                    // QUIT
                    else if(int_code == QUIT){
                        close_connection = true;
                        do_write(comm_fd, (char *)"221 <localhost> Service closing transmission channel\r\n", 54);
                    }
                    // NOOP
                    else if(int_code == NOOP){
                        if (debug_mode) printf("[%d] S: 250 OK in response to NOOP\n", comm_fd);
                        do_write(comm_fd, (char *)"250 OK\r\n", 8);
                    }
                    // Unknown command
                    else{
                        if(int_code != 7){
                            // Bad sequence of commands
                            if (debug_mode) printf("[%d] S: 503 Bad sequence of commands\n", comm_fd);
                            do_write(comm_fd, (char *)"503 Bad sequence of commands\r\n", 30);
                        }
                        else{
                            // Command not recognized
                            if (debug_mode) printf("[%d] S: 500 Syntax error, command unrecognized\n", comm_fd);
                            do_write(comm_fd, (char *)"500 Syntax error, command unrecognized\r\n", 40);
                        }
                    }
                }
                // STATE TRANSACTION: In this state, the server received a MAIL_FROM command, and is ready to receive recipients
                // Able to receive RCPT_TO and MAIL_FROM command, which resets everything, and DATA after at least one RCPT_TO
                else if(state == TRANSACTION){
                    // RCPT_TO
                    if(int_code == RCPT_TO){
                        // Detect "<"
                        if(instruction[8] != '<'){
                            if (debug_mode) printf("[%d] S: 500 Syntax error, spaces between : and < or domain not bound in <>\n", comm_fd);
                            do_write(comm_fd, (char *)"500 Syntax error, spaces between : and < or domain not bound in <>\r\n", 68);
                        }
                        else{
                            // Set rcpt_to_buffer
                            int idx = 9;
                            char temp_buffer[MAIL_FROM_BUFFER_SIZE];

                            // Copy data to temp buffer
                            while(instruction[idx] != '>' && idx < instruction_size - 1){
                                temp_buffer[idx - 9] = instruction[idx];
                                idx++;
                            }
                            temp_buffer[idx - 9] = '\0';
                            // Validate domain
                            // printf("Temp buffer: %s\n", temp_buffer);
                            bool is_valid = verify_domain(temp_buffer);
                            // printf("Verified recipient: %s\n", recipient);

                            // If recipient is not a valid domain, return error
                            if(is_valid == false){
                                if (debug_mode) printf("[%d] S: 550 Requested action not taken: domain syntax incorrect\n", comm_fd);
                                do_write(comm_fd, (char *)"550 Requested action not taken: domain syntax incorrect\r\n", 57);
                            }
                            else{
                                rcpt_to_buffer[rcpt_to_buffer_idx] = (char *) malloc(strlen(temp_buffer));
                                strcpy(rcpt_to_buffer[rcpt_to_buffer_idx], temp_buffer);
                                rcpt_to_buffer_idx++;
                                if (debug_mode) {
                                    for(int i = 0; i < rcpt_to_buffer_idx; i++){
                                        printf("[%d] S: rcpt_to_buffer: %s\n", comm_fd, rcpt_to_buffer[i]);
                                    }
                                    
                                }
                                do_write(comm_fd, (char *)"250 OK\r\n", 8);
                            }                         
                        }
                    }
                    // TITLE
                    else if(int_code == TITLE){
                        // Set title
                        title = instruction;
                        title = title.substr(6, strlen(instruction) - 6);
                        // cout << "title: " << title << endl;
                        if (debug_mode) printf("[%d] S: 250 OK, title: %s\n", comm_fd, title.c_str());
                        do_write(comm_fd, (char *)"250 OK\r\n", 8);
                    }
                    // DATA
                    else if(int_code == DATA){
                        // Check if we have at least 1 recipient in the buffer
                        if(rcpt_to_buffer_idx < 1){
                            // Bad sequence of commands
                            if (debug_mode) printf("[%d] S: 503 Bad sequence of commands, please specify at least 1 recipient\n", comm_fd);
                            do_write(comm_fd, (char *)"503 Bad sequence of commands, please specify at least 1 recipient\r\n", 67);
                        }
                        else{
                            if (debug_mode) printf("[%d] S: 354 Start mail input, end with <CRLF>.<CRLF>\n", comm_fd);
                            do_write(comm_fd, (char *)"354 Start mail input, end with <CRLF>.<CRLF>\r\n", 46);
                            state = DATA;
                        }
                    }
                    // RSET
                    else if(int_code == RSET){
                        // Set state to READY
                        state = READY;

                        // Reset all counters
                        rcpt_to_buffer_idx = 0;
                        data_buffer_idx = 0;
                        mail_from_buffer_idx = 0;

                        // Clear all buffers
                        bzero(my_name, sizeof(my_name));
                        bzero(mail_from_buffer, sizeof(mail_from_buffer));
                        bzero(data_buffer, sizeof(data_buffer));

                        // Free rcpt_to buffer
                        for(int o = 0; o < MAX_RECIPIENTS; o++){
                            if(rcpt_to_buffer[o] != NULL){
                                free(rcpt_to_buffer[o]);
                            }
                        }

                        if (debug_mode) printf("[%d] S: 250 OK in response to RSET\n", comm_fd);
                        do_write(comm_fd, (char *)"250 OK\r\n", 8);
                    }
                    // QUIT
                    else if(int_code == QUIT){
                        close_connection = true;
                        do_write(comm_fd, (char *)"221 <localhost> Service closing transmission channel\r\n", 54);
                    }
                    // NOOP
                    else if(int_code == NOOP){
                        if (debug_mode) printf("[%d] S: 250 OK in response to NOOP\n", comm_fd);
                        do_write(comm_fd, (char *)"250 OK\r\n", 8);
                    }
                    // Unknown command
                    else{
                        if(int_code != 7){
                            // Bad sequence of commands
                            if (debug_mode) printf("[%d] S: 503 Bad sequence of commands\n", comm_fd);
                            do_write(comm_fd, (char *)"503 Bad sequence of commands\r\n", 30);
                        }
                        else{
                            // Command not recognized
                            if (debug_mode) printf("[%d] S: 500 Syntax error, command unrecognized\n", comm_fd);
                            do_write(comm_fd, (char *)"500 Syntax error, command unrecognized\r\n", 40);
                        }
                    }
                }
                
                free(instruction);     
            }
        }

        // Did not find instruction, read again
        if(!found_instruction){
            continue;
        }
        else if(close_connection){
            break;
        }

        int overflow = read_offset - end_idx;
        // Deleting the instruction from the buffer
        char temp[overflow];
        bzero(&temp, sizeof(temp));

        for(int i = end_idx; i < end_idx + overflow; i++){
            temp[i - end_idx] = buffer[i];
        }

        // Rewrite the buffer and delete the full command
        bzero(&buffer, BUFFER_SIZE);
        for(int i = 0; i < overflow; i++){
            buffer[i] = temp[i];
        }
        read_offset = overflow;
    }

    // Clean up the connection fd in the list of open fds
    for(int i = 0; i < MAX_CONNECTIONS; i++){
        if(open_fds[i] == comm_fd){
            open_fds[i] = -1;
            break;
        }
    }

    // Clean up and exit
    if (debug_mode) printf("[%d] S: Connection closed\r\n", comm_fd);
    open_connections --;
    close(comm_fd);
    free(arg);
    pthread_exit(NULL);
}

int main(int argc, char *argv[])
{
    // Setting up signal handler for Ctrl+C
    signal(SIGINT, SIGINT_handler);

    // Initialize the array of open_fds[MAX_CONNECTIONS] to -1
    for(int i = 0; i < MAX_CONNECTIONS; i++){
        open_fds[i] = -1;
    }

    // Parsing command line arguments
    int opt;
    int port = 2500;

    while((opt = getopt(argc, argv, "v")) != -1)  
    {  
        switch(opt)  
        {
            case 'v':
                debug_mode = true;

            default:
                break;
        }  
    }

    if(debug_mode) printf("Debug mode enabled\n");

    // Creating a listen socket
    listen_fd = socket(PF_INET, SOCK_STREAM, 0);
    if(listen_fd < 0){
        fprintf(stderr, "socket() failed, errno: %d\n", errno);
        exit(1);
    }

    struct sockaddr_in servaddr;
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htons(INADDR_ANY);
    servaddr.sin_port = htons(port);

    // Reuse sockets
    int opt_1 = 1;
    int ret = setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt_1, sizeof(opt_1));
    if (ret < 0) { 
        fprintf(stderr, "setsockopt() failed, errno: %d\n", errno);
        exit(1);
    }

    // associate socket to port
    bind(listen_fd, (struct sockaddr *)&servaddr, sizeof(servaddr));

    // Listen with the socket
    listen(listen_fd, 100);

    // Connect to the server
    while(true){
        struct sockaddr_in clientaddr;
        socklen_t clientaddrlen = sizeof(clientaddr);

        while(open_connections > MAX_CONNECTIONS){
            // Busy wait when over the max connection limit
        }

        // Accepting a new connection in the listen queue
        int *comm_fd = (int*) malloc(sizeof(int));
        *comm_fd = accept(listen_fd, (struct sockaddr *)&clientaddr, &clientaddrlen);
        open_connections ++;

        // Check if we are shutting down, if so, close the cononection immediately
        if(is_shutdown){
            close(*comm_fd);
            free(comm_fd);
            exit(1);
        }

        // Recording the new connection fd
        for(int i = 0; i < MAX_CONNECTIONS; i++){
            if(open_fds[i] == -1){
                open_fds[i] = *comm_fd;
                break;
            }
        }
        
        if (debug_mode) printf("[%d] New connection\n", *comm_fd);

        // Creating a worker thread
        pthread_t thread;
        pthread_create(&thread, NULL, worker, comm_fd);
    }

    return 0;
}
