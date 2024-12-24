// -*- lsst-c++ -*-
/*
* Frontend Server Implementation
* See COPYRIGHT file at the top of the source tree.
*
* This product includes software developed by the
* CIS 505 Project Team 21 (https://github.com/CIS5550/fa24-cis5050-T21).
*
* This program is free software: you can redistribute it and/or modify it under
* the terms of the MIT License as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*/
#include <arpa/inet.h>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <string.h>
#include <unistd.h>
#include <resolv.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <sys/types.h>
#include <time.h>
#include <chrono>
#include "chunk.h"
#include "constants.h"
#include "cookie.h"
#include "fileutils.h"
#include "http.h"
#include "ioutils.h"
#include "message.h"
#include "membership.h"
#include "stringutils.h"
#include "url.h"
#include "email.h"

#define ADMIN_PASSWD "cis5050"


std::map<std::string, std::string> get_client_params(std::string request);


std::string post_redirect(
  std::string path, std::map<std::string, std::string> params
)
{
  std::string redirect;
  if (
    (params.find("login_failure") != params.end()) &&
    (params["login_failure"] == "true")
  ) {
    redirect.append("/?success=2");
    return redirect;
  }

  if (path == "/signup")
    redirect.append("/");
  else if (path == "/")
    redirect.append("/home");
  else if (path == "/login") {
    if (params["success"] == "0")
      redirect.append("/home");
    else if (params["success"] == "1")
      redirect.append("/");
  }
  else if (path == "/changepassword") {
    if (params["success"] == "3")
      redirect.append("/");
    else
      redirect.append("/changepassword");
  }
  redirect.append(params_to_url(params));
  return redirect;
}


std::string login_failure(std::string vno)
{
  std::string response;
  response.append(vno);
  response.append(" 303 See Other");
  response.append(CRLF);
  response.append("Location: /?success=1&login_failure=true");
  response.append(CRLF);
  response.append("Content-Type: text/html; charset=UTF-8");
  response.append(CRLF);
  response.append("Content-Length: 0");
  return response;
}


std::string redirect_admin(std::string vno)
{
  std::string response;
  response.append(vno);
  response.append(" 303 See Other");
  response.append(CRLF);
  response.append("Location: /admin?password=");
  response.append(ADMIN_PASSWD);
  response.append(CRLF);
  response.append("Content-Type: text/html; charset=UTF-8");
  response.append(CRLF);
  response.append("Content-Length: 0");
  return response;
}


std::string logout(std::string vno)
{
  std::string response;
  response.append(vno);
  response.append(" 303 See Other");
  response.append(CRLF);
  response.append("Location: /");
  response.append(CRLF);
  response.append("Set-Cookie: uuid=uuid; Max-Age=-1");
  response.append(CRLF);
  response.append("Set-Cookie: cookie=cookie; Max-Age=-1");
  response.append(CRLF);
  response.append("Set-Cookie: name=User; Max-Age=-1");
  response.append(CRLF);
  response.append("Content-Type: text/html; charset=UTF-8");
  response.append(CRLF);
  response.append("Content-Length: 0");
  return response;
}


std::string handle_OPTIONS(std::string host);


std::string handle_GET(
  std::map<std::string, std::string> data,
  bool *do_close_connection,
  bool verbose,
  struct sockaddr_in bend_coord,
  struct sockaddr_in flb,
  bool head
);


std::string handle_POST(
  std::string buf,
  std::map<std::string, std::string> data,
  bool *do_close_connection,
  bool verbose,
  struct sockaddr_in bend_coord
);

std::string handle_DELETE(
  std::string buf,
  std::map<std::string, std::string> data,
  bool *do_close_connection,
  bool verbose,
  struct sockaddr_in bend_coord
);


std::string handle_ERR(
  std::map<std::string, std::string> data,
  bool *do_close_connection,
  bool verbose,
  struct sockaddr_in bend_coord
);


bool is_full_body(std::string buf, std::map<std::string, std::string> data);


std::string auth_user(
  std::map<std::string, std::string> data, struct sockaddr_in bend_coord
);


void *handle_http_client(void *args)
{
  int fd_idx = ((int *)args)[0];
  bool verbose = ((int *)args)[1];
  struct sockaddr_in bend_coord = *(struct sockaddr_in *)(&((int *)args)[2]);
  struct sockaddr_in flb = *(struct sockaddr_in *)(
    (char *)args + (2 * sizeof(int) + sizeof(struct sockaddr_in))
  );

  free(args);

  if (verbose)
    std::cerr << "[" << fds[fd_idx] << "] New connection" << "\n";

  std::string CRLFCRLF;
  CRLFCRLF.append(CRLF);
  CRLFCRLF.append(CRLF);

  std::string buf;
  bool do_close_connection = false;
  char ctmp[65536];
  std::string response;
  std::map<std::string, std::string> reqdata;


  while (!is_shutting_down) {
    int n = read(fds[fd_idx], ctmp, sizeof(ctmp));
    if (n == 0) continue;
    else if (n <= 0) {
      std::cerr << "READ ERROR: " << errno << std::endl;
      continue;
    }

    buf.append(ctmp, n);

    if (reqdata.empty()) reqdata = get_client_params(buf);
    if (reqdata.count("Transfer-Encoding")) {
      std::transform(
        reqdata["Transfer-Encoding"].begin(),
        reqdata["Transfer-Encoding"].end(),
        reqdata["Transfer-Encoding"].begin(),
	      tolower
      );
      if (reqdata["Transfer-Encoding"] == "chunked")
        read_from_chunk_transfer(fds[fd_idx], buf);
    }

    if (buf.rfind(GET, 0) == 0) {
      if (verbose)
          std::cerr << "[" <<  fds[fd_idx] << "] C: " << buf;
      response.append(
        handle_GET(reqdata, &do_close_connection, verbose, bend_coord, flb, false)
      );
    }
    else if (buf.rfind(HEAD, 0) == 0) {
      if (verbose)
          std::cerr << "[" <<  fds[fd_idx] << "] C: " << buf;
      response.append(
        handle_GET(reqdata, &do_close_connection, verbose, bend_coord, flb, true)
      );
    }
    else if (buf.rfind(POST, 0) == 0) {
      if (is_full_body(buf, reqdata)) {
        auto start = std::chrono::high_resolution_clock::now();
        response.append(
          handle_POST(buf, reqdata, &do_close_connection, verbose, bend_coord)
        );

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        std::cout << "Execution time for POST: " << duration.count() << " milliseconds" << std::endl;
      }
      else{
        std::cout << "Bytes read: " << buf.length() << std::endl;
        continue;
      }
    }
    else if (buf.rfind(DELE, 0) == 0) {
      if (is_full_body(buf, reqdata)) {
        if (verbose)
          std::cerr << "[" <<  fds[fd_idx] << "] C: " << buf;
        response.append(
          handle_DELETE(buf, reqdata, &do_close_connection, verbose, bend_coord)
        );
      }
      else
        continue;
    }
    else if (buf.rfind(OPTIONS, 0) == 0) {
      if (verbose)
          std::cerr << "[" <<  fds[fd_idx] << "] C: " << buf;
      response.append(handle_OPTIONS(reqdata["Host"]));
    }
    else
      response.append(handle_ERR(reqdata, &do_close_connection, verbose, bend_coord));

    response.append(CRLF);
    response.append(CRLF);
    if (verbose)
      std::cerr << "[" << fds[fd_idx] << "] S: " << response;
    send_response(fds[fd_idx], (char *)response.c_str());

    buf.clear();
    response.clear();
    reqdata.clear();

    if (do_close_connection)
      break;
  }

  cleanup(fd_idx, false);
  pthread_exit(NULL);
}

int get_table_fd(bool verbose, std::string user) {
  int coor_fd = create_connect_socket("127.0.0.1:5001");
  std::string coor_message = do_read(coor_fd, CRLF);
  if (verbose) std::cout << "[COOR]: " << coor_message << std::endl;

  // Say hello to coordinator, receive table address
  std::string response = "HELO passwd " + user + "\r\n";
  send_response(coor_fd, (char *) response.c_str());
  coor_message = do_read(coor_fd, CRLF);
  if(coor_message == "-ERR No available servers"){
      return -1;
  }
  if (verbose) std::cout << "[COOR]: " << coor_message << std::endl;

  // Connect with table
  int table_fd = create_connect_socket(
    coor_message.substr(4, coor_message.size() - 4)
  );
  std::string table_message = do_read_backend(table_fd, "\r\n");
  if (verbose) std::cout << "[TABLE]: " << table_message << std::endl;

  return table_fd;
}

std::map<std::string, std::string> get_client_params(std::string request)
{
  std::map<std::string, std::string> data;

  std::stringstream ss(request);
  std::string method, path, http_vno, stmp;

  std::getline(ss, method, ' ');
  strip(&method);
  data["method"] = method;
  std::getline(ss, path, ' ');
  strip(&path);
  data["path"] = path;
  std::getline(ss, http_vno, CRLF[1]);
  strip(&http_vno);
  data["vno"] = http_vno;
  while (getline(ss, stmp, CRLF[1])) {
    std::stringstream line(stmp);
    std::string key;
    std::string val;
    std::getline(line, key, ':');
    std::getline(line, val, CRLF[1]);
    strip(&key);
    strip(&val);
    if (key.length())
      data[key] = val;
  }
  return data;
}

std::string handle_OPTIONS(std::string host)
{
  std::string response = create_http_response(200, "", host);
  return response;
}

std::string handle_DELETE(
  std::string buf,
  std::map<std::string, std::string> data,
  bool *do_close_connection,
  bool verbose,
  struct sockaddr_in bend_coord
)
{
  std::string response, user = auth_user(data, bend_coord);

  // Get body
  std::string CRLFCRLF;
  CRLFCRLF.append(CRLF);
  CRLFCRLF.append(CRLF);

  size_t body_sidx = buf.find(CRLFCRLF, 0) + CRLFCRLF.length();
  std::string body = buf.substr(body_sidx, std::stoi(data["Content-Length"]));

  // Code for deleting file by file_path
  if (split(data["path"], '?')[0].find("/delete-file") != std::string::npos) {
    if (user.length() == 0)
      return login_failure(data["vno"]);

    int table_fd = get_table_fd(verbose, user);
    std::map<std::string, std::string> params = parse_json_string(body);
    std::string folder = params["folder"];

    // Delete fid from fid_vector
    std::string fid_vector = bend_GET(&table_fd, user, folder);
    if (verbose)
      std::cout << "[FID VECTOR BEFORE ERASE]: " << fid_vector << std::endl;
    size_t idx = fid_vector.find(params["file"]);

    if (idx != 0) {
      fid_vector.erase(idx-1, params["file"].size() + 1);
    } else if (idx == std::string::npos) {
      return create_http_response(404, "", data["Host"]);
    } else {
      fid_vector.erase(0, params["file"].size() + 1);
    }

    if (verbose) std::cout << "[FID VECTOR AFTER ERASE]: " << fid_vector << std::endl;

    // DELETE fid_vector if empty
    if (fid_vector.empty()) {
      std::string delete_status = bend_DELETE(&table_fd, user, folder);
      if (verbose)
        std::cout << "[DELETE FID VECTOR STATUS]: " << delete_status << std::endl;
    }
    else {
      // PUT updated fid_vector if non-empty
      std::string put_status = bend_PUT(&table_fd, user, folder, fid_vector);
      if (verbose)
        std::cout << "[PUT FID VECTOR STATUS]: " << put_status << std::endl;
    }

    // Delete the fid:file
    std::string status = bend_DELETE(&table_fd, user, params["file"]);
    if (verbose) std::cout << "[DELETE_STATUS]: " << status << std::endl;

    if (status == "-ERR 204 specific row and column does not exist.") {
      response = create_http_response(500, "", data["Host"]);
      return response;
    }
    // Find the emails wanted by limit and offset
    else if (status.rfind(OK) == 0) {
      response = create_http_response(200, "", data["Host"]);
      return response;
    } else {
      response = create_http_response(404, "", data["Host"]);
      return response;
    }
  }
  // Code for deleting email by id
  else if (data["path"].find("/delete-email") != std::string::npos) {
    if (user.length() == 0)
      return login_failure(data["vno"]);

    // Connect to coordinator and table
    int table_fd = get_table_fd(verbose, user);

    // Construct email hash
    std::map<std::string, std::string> params = parse_json_string(body);

    char email_data[params["sender"].size() + params["timestamp"].size() + params["subject"].size() + 2];
    bzero(email_data, sizeof(email_data));
    strcat(email_data, params["sender"].c_str());
    strcat(email_data, "|");
    strcat(email_data, params["subject"].c_str());
    strcat(email_data, "|");
    strcat(email_data, params["timestamp"].c_str());

    // Store the hash
    if (verbose)
      std::cout << "[PREHASH]: " << email_data << std::endl;
    std::string hash = compute_hash_16(email_data, strlen(email_data));
    if (verbose)
      std::cout << "[HASH]: " << hash << std::endl;

    // Delete eid from eid_vector
    std::string eid_vector = bend_GET(&table_fd, user, "eid_vector");
    if (verbose)
      std::cout << "[EID VECTOR BEFORE ERASE]: " << eid_vector << std::endl;
    size_t idx = eid_vector.find(hash);

    if (idx != 0) {
      eid_vector.erase(idx - 1, hash.size() + 1);
    } else if (idx == std::string::npos) {
      return create_http_response(404, "", data["Host"]);
    } else {
      eid_vector.erase(0, hash.size() + 1);
    }

    if (verbose)
      std::cout << "[EID VECTOR AFTER ERASE]: " << eid_vector << std::endl;

    // DELETE eid_vector if empty
    if (eid_vector.empty()) {
      std::string delete_status = bend_DELETE(&table_fd, user, "eid_vector");
      if (verbose)
        std::cout << "[DELETE EID VECTOR STATUS]: " << delete_status << std::endl;
    }
    else {
      // PUT updated eid_vector if non-empty
      std::string put_status = bend_PUT(&table_fd, user, "eid_vector", eid_vector);
      if (verbose)
        std::cout << "[PUT EID VECTOR STATUS]: " << put_status << std::endl;
    }

    // Delete the eid:email
    std::string status = bend_DELETE(&table_fd, user, hash);
    if (verbose) std::cout << "[DELETE_STATUS]: " << status << std::endl;

    if (status == "-ERR 204 specific row and column does not exist.") {
      response = create_http_response(500, "", data["Host"]);
      return response;
    }
    // Find the emails wanted by limit and offset
    else if (status.rfind(OK) == 0) {
      response = create_http_response(200, "", data["Host"]);
      return response;
    }
    else {
      response = create_http_response(404, "", data["Host"]);
      return response;
    }
  }
  perror("delete");
  exit(EXIT_FAILURE);
}

std::string handle_GET(
  std::map<std::string, std::string> data,
  bool *do_close_connection,
  bool verbose,
  struct sockaddr_in bend_coord,
  struct sockaddr_in flb,
  bool head
)
{
  std::string response, user = auth_user(data, bend_coord);
  // bservs, bstats, fservs, fstats are only relevant for the admin console.
  // bend_content and bend_node only relevant for displaying admin content for
  // for a particular backend node. These variables can be ignored otherwise.
  std::string bservs, bstats, fservs, fstats, bend_content, bend_node;

  // Code for retrieving username
  if (data["path"] == "/username") {
    if (user.empty()) {
      return create_http_response(404, "", data["Host"]);
    }

    int table_fd = get_table_fd(verbose, user);

    std::string user_name = bend_GET(&table_fd, user, "name");
    if (user_name.empty() || user_name == "-ERR 204 specific row and column does not exist.")
      return create_http_response(500, "", data["Host"]);

    std::string body = "{\"username\":\"" + user_name + "\"}";
    return create_http_response(200, body, data["Host"]);
  }
  // Code for downloading one file
  else if (split(data["path"], '?')[0].find("/download") != std::string::npos) {
    if (user.length() == 0)
      return login_failure(data["vno"]);

    // Connect to coordinator and table
    int table_fd = get_table_fd(verbose, user);
    std::map<std::string, std::string> params = params_from_url(
      data["path"].substr(data["path"].find('?') + 1)
    );

    std::string file_binary = bend_GET(&table_fd, user, params["file"]);
    // File not found
    if (file_binary == "-ERR 204 specific row and column does not exist.") {
      response = create_http_response(404, "", data["Host"]);
      return response;
    }
    else {
      // Convert binary string to bytes
      std::vector<uint8_t> byteArray = bstoby(file_binary);

      // Encode the bytes to Base64
      std::string base64FileData = bytob64(byteArray);

      // Send response back with file data as body
      std::ostringstream response_stream;
      response_stream << "HTTP/1.1 " << 200 << " " << "OK" << CRLF;
      response_stream << "Access-Control-Allow-Origin: *" << CRLF;
      response_stream << "Access-Control-Allow-Methods: DELETE, POST, GET, OPTIONS" << CRLF;
      response_stream << "Access-Control-Allow-Headers: content-type, x-file-name, x-file-path" << CRLF;
      response_stream << "Content-Type: application/octet-stream\r\n";
      response_stream << "Content-Disposition: attachment; filename=\"" << file_name_from_path(params["file"]) << "\"" << CRLF;
      if (base64FileData.length() < CHUNK_THRESH_SIZE) {
        response_stream << "Content-Length: " << base64FileData.length() << CRLF << CRLF;
        response_stream << base64FileData;
      } else {
        response_stream << "Transfer-Encoding: chunked" << CRLF << CRLF;
        response_stream << write_to_chunk_transfer(base64FileData, CHUNK_MAX_DOWNLOAD_SIZE).str();
      }
      return response_stream.str();
    }
  }
  // Code for retrieving files
  else if (split(data["path"], '?')[0].find("/files-list") != std::string::npos) {
    if (user.length() == 0)
      return login_failure(data["vno"]);

    // Connect to coordinator and table
    int table_fd = get_table_fd(verbose, user);
    std::map <std::string, std::string> params = params_from_url(data["path"].substr(data["path"].find('?') + 1));
    std::string folder = params["folder"];

    // GET the fid_vector
    std::string fids = bend_GET(&table_fd, user, folder);
    if (verbose) std::cout << "[FID VECTOR]: " << fids << std::endl;

    // No email in inbox yet
    if (fids == "-ERR 204 specific row and column does not exist.") {
      response = create_http_response(404, "", data["Host"]);
      return response;
    } else {
      std::vector <std::string> fid_vector = split(fids, ',');
      std::string body_string = "[";
      for (size_t i = 0; i < fid_vector.size(); i++) {
        body_string += "{\"name\": \"";
        body_string += file_name_from_path(fid_vector[i]);
        body_string += "\"},";
      }
      body_string.pop_back();
      body_string += "]";
      response = create_http_response(200, body_string, data["Host"]);
      return response;
    }
  }
  // Code for retrieving inbox and outbox
  else if (split(data["path"], '?')[0].find("/email-list") != std::string::npos) {
    // Connect to coordinator and table
    int table_fd = get_table_fd(verbose, user);

    // GET the eid_vector
    std::string eids = bend_GET(&table_fd, user, "eid_vector");
    if (verbose) std::cout << "[EID VECTOR]: " << eids << std::endl;

    // No email in inbox yet
    if (eids == "-ERR 204 specific row and column does not exist." || eids == "") {
      response = create_http_response(404, "", data["Host"]);
      return response;
    }
    // Find the emails wanted by limit and offset
    else {
      // Get params
      std::map <std::string, std::string> params = params_from_url(data["path"].substr(data["path"].find('?') + 1));
      size_t offset = stol(params["offset"]);
      int limit = stoi(params["limit"]);

      // Get email data
      std::vector <std::string> eid_vector = split(eids, ',');
      std::reverse(eid_vector.begin(), eid_vector.end());

      // If offset is bigger than length, return error
      if (offset > eid_vector.size()) {
        response = create_http_response(400, "", data["Host"]);
        return response;
      } else {
        std::string body_string = "[";
        for (
          size_t i = offset;
          i < std::min(eid_vector.size(), (size_t) offset + limit);
          i++
        ) {
          std::string cur_email = bend_GET(&table_fd, user, eid_vector[i]);
          if (cur_email == "-ERR 204 specific row and column does not exist.") {
            response = create_http_response(500, "", data["Host"]);
            return response;
          }
          else{
            // Convert binary string to bytes
            std::vector<uint8_t> byteArray = bstoby(cur_email);
            for (auto &byte : byteArray) {
              body_string.push_back(char(byte));
            }
            body_string += ",";
          }
        }
        body_string.pop_back();
        body_string += "]";
        response = create_http_response(200, body_string, data["Host"]);
        return response;
      }
    }
  }
  else if (split(data["path"], '?')[0].find("/logout") != std::string::npos) {
    return logout(data["vno"]);
  }
  else if (split(data["path"], '?')[0].find("/home") != std::string::npos) {
    if (user.length() == 0)
      return login_failure(data["vno"]);
  }
  else if (split(data["path"], '?')[0].find("/changepassword") != std::string::npos) {
    if (user.length() == 0)
      return login_failure(data["vno"]);
  }
  else if (split(data["path"], '?')[0].find("/admincontent") != std::string::npos) {
    std::string tmp = data["path"];
    while (tmp.front() != '?')
      tmp.erase(0, 1);
    std::map<std::string, std::string> aparams = params_from_url(url_decode(tmp));
    if (!(aparams.count("password") && aparams["password"] == ADMIN_PASSWD))
      return login_failure(data["vno"]);
    if (aparams.count("node")) {
      bend_content = bend_listcontent(aparams["node"]);
      bend_node = aparams["node"];
    }
    else {
      return redirect_admin(data["vno"]);
    }
  }
  else if (split(data["path"], '?')[0].find("/admin") != std::string::npos) {
    std::string tmp = data["path"];
    if (tmp.find('?') != std::string::npos) {
      while (tmp.front() != '?' && tmp.length() > 0)
        tmp.erase(0, 1);
    }
    std::map<std::string, std::string> aparams = params_from_url(url_decode(tmp));
    if ((aparams.count("password") == 0) || (aparams["password"] != ADMIN_PASSWD))
      return login_failure(data["vno"]);
    std::string bstatus = bend_status(bend_coord), fstatus = fend_status(flb);
    if (bstatus.find("OK") == std::string::npos) {
      perror("bend_status");
      exit(EXIT_FAILURE);
    }
    if (fstatus.find("OK") == std::string::npos) {
      perror("fend_status");
      exit(EXIT_FAILURE);
    }
    std::vector<std::string> bservers = split(bstatus, ' ');
    std::vector<std::string> fservers = split(fstatus, ' ');
    size_t delimpos;
    for (size_t i = 1; i < bservers.size(); i++) {
      delimpos = bservers[i].find_last_of('-');
      if (delimpos == std::string::npos)
        continue;
      bstats.push_back('"');
      bstats.append(bservers[i].substr(delimpos + 1));
      bstats.push_back('"');
      if (i < bservers.size() - 1)
        bstats.append(", ");

      bservs.push_back('"');
      bservs.append(bservers[i].substr(0, delimpos));
      bservs.push_back('"');
      if (i < bservers.size() - 1)
        bservs.append(", ");
    }
    for (size_t i = 1; i < fservers.size(); i++) {
      delimpos = fservers[i].find_last_of('-');
      if (delimpos == std::string::npos)
        continue;
      fstats.push_back('"');
      fstats.append(fservers[i].substr(delimpos + 1));
      fstats.push_back('"');
      if (i < fservers.size() - 1)
        fstats.append(", ");

      fservs.push_back('"');
      fservs.append(fservers[i].substr(0, delimpos));
      fservs.push_back('"');
      if (i < fservers.size() - 1)
        fservs.append(", ");
    }
  }
  else if (split(data["path"], '?')[0].find("/shutdown") != std::string::npos) {
    std::string tmp = data["path"];
    while (tmp.front() != '?')
      tmp.erase(0, 1);
    std::map<std::string, std::string> aparams = params_from_url(url_decode(tmp));
    if (!(aparams.count("password") && aparams["password"] == ADMIN_PASSWD))
      return login_failure(data["vno"]);
    if (aparams.count("node"))
      bend_shutdown(aparams["node"], bend_coord);
    return redirect_admin(data["vno"]);
  }
  else if (split(data["path"], '?')[0].find("/restart") != std::string::npos) {
    std::string tmp = data["path"];
    while (tmp.front() != '?')
      tmp.erase(0, 1);
    std::map<std::string, std::string> aparams = params_from_url(url_decode(tmp));
    if (!(aparams.count("password") && aparams["password"] == ADMIN_PASSWD))
      return login_failure(data["vno"]);
    if (aparams.count("node"))
      bend_restart(aparams["node"], bend_coord);
    return redirect_admin(data["vno"]);
  }

  std::string stemp(__FILE__);
  stemp = stemp.substr(0, stemp.find_last_of('/'));
  stemp = stemp.substr(0, stemp.find_last_of('/') + 1);
  stemp.append("templates");
  std::string serr(stemp);
  serr.append("404.html");

  stemp.append(split(data["path"], '?')[0]);
  if (split(data["path"], '?')[0].compare("/") == 0)
    stemp.append("index");
  if (!has_suffix(stemp, ".html"))
    stemp.append(".html");

  std::string content = slurp(stemp);

  // Admin console logic.
  if (
    content.find("'<|BNODES|>'") != std::string::npos &&
    content.find("'<|BSTATUS|>'") != std::string::npos &&
    content.find("'<|FNODES|>'") != std::string::npos &&
    content.find("'<|FSTATUS|>'") != std::string::npos &&
    content.find("<|PASSWORD|>") != std::string::npos &&
    bservs.length() > 0 &&
    bstats.length() > 0
  ) {
    content = replace(content, "'<|BNODES|>'", bservs);
    content = replace(content, "'<|BSTATUS|>'", bstats);
    content = replace(content, "'<|FNODES|>'", fservs);
    content = replace(content, "'<|FSTATUS|>'", fstats);
    content = replace(content, "<|PASSWORD|>", ADMIN_PASSWD);
  } else if (
    content.find("<|BNODE|>") != std::string::npos &&
    content.find("<|PASSWORD|>") != std::string::npos &&
    bend_node.length() > 0
  ) {
    content = replace(content, "<|BCONTENT|>", bend_content);
    content = replace(content, "<|BNODE|>", bend_node);
    content = replace(content, "<|PASSWORD|>", ADMIN_PASSWD);
  }

  response.append(data["vno"]);
  if (content.length())
    response.append(" 200 OK");
  else {
    response.append(" 404 Not Found");
    std::string content = slurp(serr);
  }
  response.append(CRLF);

  response.append("Content-type: text/html");
  response.append(CRLF);
  if (data.count("Host") != 0) {
    response.append("Access-Control-Allow-Origin: ");
    response.append(data["Host"]);
    response.append(CRLF);
  }
  response.append("Access-Control-Allow-Credentials: true");
  response.append(CRLF);

  if (data.count("Connection") && !data["Connection"].compare("keep-alive")) {
    *do_close_connection = true;
    response.append("Connection: keep-alive");
  } else {
    *do_close_connection = false;
    response.append("Connection: close");
  }
  response.append(CRLF);

  response.append("Content-length: ");
  response.append(std::to_string(content.length()));
  response.append(CRLF);
  response.append(CRLF);

  if (!head)
    response.append(content);

  return response;
}


std::string handle_POST(
  std::string buf,
  std::map<std::string, std::string> data,
  bool *do_close_connection,
  bool verbose,
  struct sockaddr_in bend_coord
)
{
  std::string CRLFCRLF;
  CRLFCRLF.append(CRLF);
  CRLFCRLF.append(CRLF);

  std::string user = auth_user(data, bend_coord);

  size_t body_sidx = buf.find(CRLFCRLF, 0) + CRLFCRLF.length();
  std::string body = buf.substr(body_sidx, std::stoi(data["Content-Length"]));
  std::map<std::string, std::string> response_params;

  data["path"] = std::regex_replace(data["path"], std::regex(".html"), "");
  data["path"] = std::regex_replace(data["path"], std::regex("index"), "");

  if (data["path"] == "/changepassword") {
    if (user.length() == 0)
      return login_failure(data["vno"]);
    std::map<std::string, std::string> params = params_from_url(
      url_decode(body)
    );
    if (!params.count("password"))
      return login_failure(data["vno"]);
    params["old-password"] = hash(params["old-password"]);
    params["password"] = hash(params["password"]);

    int bend;
    response_params["success"] = "3";
    if ((bend = bend_connect(user, bend_coord)) < 0)
      response_params["success"] = "1"; 
    else {
      do_read(bend, CRLF);
      if (bend_GET(&bend, user, "password").find(params["old-password"]) == std::string::npos)
        response_params["success"] = "2"; 
      else if (std::string::npos == (
        bend_PUT(&bend, user, "password", params["password"]).find("OK")
      ))
        response_params["success"] = "1"; 
    }
  }
  else if (data["path"] == "/move-file") {
    if (user.length() == 0)
      return login_failure(data["vno"]);

    // Connect to coordinator and table, get body params
    int table_fd = get_table_fd(verbose, user);
    std::map<std::string, std::string> body_map = parse_json_string(body);
    std::string full_path = body_map["fullPath"];
    std::string file_name = body_map["fileName"];
    std::string old_path = body_map["oldPath"]; // Current path
    std::string new_path = body_map["newPath"];

    // Modify old_path fid_vector
    std::string old_fids = bend_GET(&table_fd, user, old_path);
    if (verbose) std::cout << "[GET_OLD_FIDS]: " << old_fids << std::endl;

    size_t idx = old_fids.find(full_path);

    if (idx != 0) {
      old_fids.erase(idx-1, full_path.size() + 1);
    } else if (idx == std::string::npos) {
      return create_http_response(404, "", data["Host"]);
    } else {
      old_fids.erase(0, full_path.size() + 1);
    }

    if (verbose)
      std::cout << "[OLD_FIDS_AFTER_ERASE]: " << old_fids << std::endl;
    std::string put_status = bend_PUT(&table_fd, user, old_path, old_fids);
    if (verbose)
      std::cout << "[PUT_OLD_FIDS_STATUS]: " << put_status << std::endl;

    // Append to new_path fid_vector
    std::string new_fids = bend_GET(&table_fd, user, new_path);
    if (verbose)
      std::cout << "[GET_NEW_FIDS]: " << new_fids << std::endl;

    // Check if name already exists
    if ((idx = new_fids.find(full_path)) != std::string::npos) {
      return create_http_response(409, "", data["Host"]);
    }

    new_fids += "," + new_path + file_name;
    if (verbose)
      std::cout << "[NEW_FIDS_AFTER_APPEND]: " << new_fids << std::endl;

    put_status = bend_PUT(&table_fd, user, new_path, new_fids);
    if (verbose)
      std::cout << "[PUT_NEW_FIDS_STATUS]: " << put_status << std::endl;

    // Rename the file to have new absolute path
    std::string file_data = bend_GET(&table_fd, user, full_path);

    std::string new_file_data = "";

    std::cout << file_name << std::endl;
    std::cout << old_path << std::endl;
    std::cout << new_path << std::endl;
    std::cout << full_path << std::endl;

    if (file_name.back() == '/') {
      new_file_data = renameMovedFolder(table_fd, user, file_data, file_name, new_path);
    }

    // Delete original file_id:file_data
    std::string delete_status = bend_DELETE(&table_fd, user, full_path);
    if (verbose)
      std::cout << "[DELETE_OLD_STATUS]: " << delete_status << std::endl;

    if (delete_status.rfind(ERR) == 0)
      return create_http_response(500, "", data["Host"]);

    // PUT new file_id:file_data
    put_status = bend_PUT(&table_fd, user, new_path + file_name, file_data);
    if (verbose)
      std::cout << "[PUT_NEW_STATUS]: " << put_status << std::endl;

    // Return response
    if (put_status.rfind(OK) == 0)
      return create_http_response(200, "", data["Host"]);
    else
      return create_http_response(500, "", data["Host"]);
  }
  else if (data["path"] == "/rename-file") {
    if (user.length() == 0)
      return login_failure(data["vno"]);

    // Connect to coordinator and table, get body params
    int table_fd = get_table_fd(verbose, user);
    std::map<std::string, std::string> body_map = parse_json_string(body);
    std::string parent_path = body_map["parentPath"];
    std::string old_file_name = parent_path + body_map["oldFileName"];
    std::string new_file_name = parent_path + body_map["newFileName"];

    // Get parent fids, and rename file
    std::string fids = bend_GET(&table_fd, user, parent_path);
    if (verbose) std::cout << "[GET_FIDS]: " << fids << std::endl;

    // Check if name already exists
    size_t idx = 0;
    if ((idx = fids.find(new_file_name)) != std::string::npos) {
      return create_http_response(409, "", data["Host"]);
    }

    idx = fids.find(old_file_name);
    if (idx != std::string::npos) {
      fids.replace(idx, old_file_name.size(), new_file_name);
    } else {
      // No such file name exists
      return create_http_response(404, "", data["Host"]);
    }

    // Put new fids
    std::string put_new_fid_status = bend_PUT(&table_fd, user, parent_path, fids);
    if (verbose) {
      std::cout << "[NEW_FIDS]: " << fids << std::endl;
      std::cout << "[PUT_NEW_FID_STATUS]: " << put_new_fid_status << std::endl;
    }

    // Get file data
    std::string file_data = bend_GET(&table_fd, user, old_file_name);

    // If renaming a folder, need to rename the full path of all files inside
    std::cout << file_data << std::endl;
    std::cout << old_file_name << std::endl;
    std::cout << new_file_name << std::endl;

    std::string new_file_data = "";

    if (old_file_name.back() == '/') {
      new_file_data = renameFolder(table_fd, user, file_data, old_file_name, new_file_name, parent_path);
    }

    std::cout << new_file_data << std::endl;


    // Delete original file_id:file_data
    std::string delete_status = bend_DELETE(&table_fd, user, old_file_name);
    if (verbose)
      std::cout << "[DELETE_OLD_STATUS]: " << delete_status << std::endl;

    if (delete_status.rfind(ERR) == 0) {
      return create_http_response(500, "", data["Host"]);
    }

    // PUT new file_id:file_data
    std::string put_status = "";

    if (old_file_name.back() == '/') {
      put_status = bend_PUT(&table_fd, user, new_file_name, new_file_data);
    }
    else {
      put_status = bend_PUT(&table_fd, user, new_file_name, file_data);
    }
    if (verbose)
      std::cout << "[PUT_NEW_STATUS]: " << put_status << std::endl;

    if (put_status.rfind(OK) == 0)
      return create_http_response(200, "", data["Host"]);
    else
      return create_http_response(500, "", data["Host"]);
  }
  else if (data["path"] == "/create-folder") {
    if (user.length() == 0)
      return login_failure(data["vno"]);

    // Connect to coordinator and table
    int table_fd = get_table_fd(verbose, user);
    std::map<std::string, std::string> body_map = parse_json_string(body);
    std::string name = body_map["name"]; // full path
    std::string parent_path = body_map["parentPath"];

    // Get parent fids list, and update
    std::string fids = bend_GET(&table_fd, user, parent_path);
    if (verbose)
      std::cout << "[GET_FIDS]: " << fids << std::endl;

    size_t idx = 0;

    if (fids == "-ERR 204 specific row and column does not exist.") {
      std::string put_fid_status = bend_PUT(&table_fd, user, parent_path, name);
      if (verbose)
        std::cout << "[PUT_FID_STATUS]: " << put_fid_status << std::endl;
    }
    // Check if folder name already exists
    else if ((idx = fids.find(name)) != std::string::npos) {
      return create_http_response(409, "", data["Host"]);
    } else {
      fids += ",";
      fids += name;
      std::string put_fid_status = bend_PUT(&table_fd, user, parent_path, fids);
      if (verbose)
        std::cout << "[PUT_FID_STATUS]: " << put_fid_status << std::endl;
    }

    // Create a fid vector for the folder, with "../" as first value
    std::string put_new_fid_status = bend_PUT(&table_fd, user, name, "../");
    if (verbose)
      std::cout << "[PUT_NEW_FID_STATUS]: " << put_new_fid_status << std::endl;

    if (put_new_fid_status.rfind(OK) == 0)
      return create_http_response(200, "", data["Host"]);
    else
      return create_http_response(500, "", data["Host"]);
  }
  else if (data["path"] == "/upload-file") {
    if (user.length() == 0)
      return login_failure(data["vno"]);

    // Connect to coordinator and table
    int table_fd = get_table_fd(verbose, user);

    // Retrieve file path, file name and file data
    std::string parent_path = data["X-File-Path"];
    std::string file_path = data["X-File-Path"] + data["X-File-Name"];
    if (verbose)
      std::cout << "[FILE PATH AND NAME]: " << file_path << std::endl;

    // Get fid_vector from backend
    std::string fids = bend_GET(&table_fd, user, parent_path);
    if (verbose)
      std::cout << "[GET_FIDS]: " << fids << std::endl;

    size_t idx = 0;

    // Append the hash of current email to the vector and update the value
    if (fids == "-ERR 204 specific row and column does not exist.") {
      std::string put_fid_status = bend_PUT(&table_fd, user, parent_path, file_path);
      if (verbose)
        std::cout << "[PUT_FID_STATUS]: " << put_fid_status << std::endl;
    }
    // Check if folder name already exists
    else if ((idx = fids.find(file_path)) != std::string::npos) {
      return create_http_response(409, "", data["Host"]);
    } else {
      fids += ",";
      fids += file_path;
      std::string put_fid_status = bend_PUT(
        &table_fd, user, parent_path, fids
      );
      if (verbose)
        std::cout << "[PUT_FID_STATUS]: " << put_fid_status << std::endl;
    }

    // Store file_path:file_data
    std::string body_binary = stobs(body);
    std::string status = bend_PUT(&table_fd, user, file_path, body_binary);

    if (verbose) std::cout << "[PUT_FILE_STATUS]: " << status << std::endl;

    if (status.rfind(OK) == 0)
      return create_http_response(200, "", data["Host"]);
    else
      return create_http_response(500, "", data["Host"]);
  }
  else if (data["path"] == "/send-email") {
    if (user.length() == 0)
      return login_failure(data["vno"]);

    std::map<std::string, std::string> body_map = parse_json_string(body);

    std::string recipient = body_map["recipient"];
    std::string domain = recipient.substr(recipient.find("@") + 1);

    // Sending to external mailbox
    if (domain != "penncloud.com") {
      // Perform DNS lookup
      u_char dns[4096];
      int len = res_query(domain.c_str(), ns_c_in, ns_t_mx, dns, sizeof(dns));

      if (len < 0) {
        std::cerr << "DNS query failed" << std::endl;
        return create_http_response(404, "", data["Host"]);
      }

      auto mxRecords = parse_MX_records(dns, len);
      if (mxRecords.empty()) {
          std::cerr << "No MX records found" << std::endl;
          return create_http_response(404, "", data["Host"]);
      }

      // Sort MX records by preference
      std::sort(mxRecords.begin(), mxRecords.end());

      int smtp_fd = connect_SMTP_server(mxRecords[0].second);
      if (smtp_fd < 0) {
          std::cerr << "Failed to connect to SMTP server" << std::endl;
          return create_http_response(500, "", data["Host"]);
      }

      std::string smtp_response = receive_response(smtp_fd);
      if (verbose) {
        // Read initial server greeting
        std::cout << "[SMTP RESPONSE]: " << smtp_response;
      }

      send_SMTP_command(smtp_fd, "EHLO " + domain + "\r\n");
      smtp_response = receive_response(smtp_fd);
      if (verbose) {
        // Read EHLO response
        std::cout << "[SMTP RESPONSE]: " << smtp_response;
      }

      send_SMTP_command(smtp_fd, "MAIL FROM:<" + user + ">\r\n");
      smtp_response = receive_response(smtp_fd);
      if (verbose) {
        // Read MAIL FROM response
        std::cout << "[SMTP RESPONSE]: " << smtp_response;
      }

      // Parse and send to recipient
      send_SMTP_command(smtp_fd, "RCPT TO:<" + recipient + ">\r\n");
      smtp_response = receive_response(smtp_fd);
      if (verbose) {
        // Read RCPT TO response
        std::cout << "[SMTP RESPONSE]: " << smtp_response;
      }

      // Send data.
      send_SMTP_command(smtp_fd, "DATA\r\n");
      smtp_response = receive_response(smtp_fd);
      if (verbose) {
        // Read DATA response
        std::cout << "[SMTP RESPONSE]: " << smtp_response;
      }
      sleep(1);

      std::stringstream message;
      message << "From: " << "<" << user << ">\r\n";
      message << "To: " << "<" << recipient << ">\r\n";
      message << "Subject: " << body_map["subject"] << "\r\n";

      // Get current time
      time_t curtime;
      time(&curtime);
      char *current_time = ctime(&curtime);
      current_time[strlen(current_time)] = '\0';
      message << "Date: " << current_time;
      message << "Message-ID: " << "<" << time(NULL) << "." << time(NULL) << "@penncloud.com" << ">\r\n";
      message << "\r\n\r\n";
      message << body_map["message"].data() << "\r\n";

      if (verbose)
        std::cout << "[EMAIL BODY]: " << message.str() << std::endl;

      send_SMTP_command(smtp_fd, message.str());
      send_SMTP_command(smtp_fd, ".\r\n");

      // Read response status
      std::string status = receive_response(smtp_fd);
      if (verbose)
        std::cout << "[SMTP SERVER STATUS]: " << status << std::endl;
      if (status.rfind(EXTERNAL_OK) == 0)
        return create_http_response(200, "", data["Host"]);
      else
        return create_http_response(500, "", data["Host"]);
    }
    // Internal mailbox
    else {
      if (user.length() == 0)
        return login_failure(data["vno"]);

      int smtp_socket = create_connect_socket("127.0.0.1:2500");

      if(smtp_socket == -1){
          return create_http_response(500, "", data["Host"]);
      }

      std::string smtp_response = do_read(smtp_socket, CRLF);
      if (verbose)
        std::cout << "[SMTP SERVER]: " << smtp_response << std::endl;

      char request_buf[128];
      std::string helo_message = "HELO " + recipient + "\r\n";
      send_response(smtp_socket, (char *)helo_message.c_str());
      smtp_response = do_read(smtp_socket, CRLF);
      if (verbose)
        std::cout << "[SMTP SERVER]: " << smtp_response << std::endl;

      snprintf(request_buf, 128, "MAIL FROM:<%s>\r\n", user.c_str());
      send_response(smtp_socket, request_buf);
      smtp_response = do_read(smtp_socket, CRLF);
      if (verbose)
        std::cout << "[SMTP SERVER]: " << smtp_response << std::endl;

      // Send subject of email.
      snprintf(request_buf, 128, "TITLE %s\r\n", body_map["subject"].c_str());
      send_response(smtp_socket, request_buf);
      smtp_response = do_read(smtp_socket, CRLF);
      if (verbose)
        std::cout << "[SMTP SERVER]: " << smtp_response << std::endl;

      // Parse and send to recipient
      snprintf(request_buf, 128, "RCPT TO:<%s>\r\n", recipient.c_str());
      send_response(smtp_socket, request_buf);
      smtp_response = do_read(smtp_socket, CRLF);
      if (verbose)
        std::cout << "[SMTP SERVER]: " << smtp_response << std::endl;

      // Send data.
      send_response(smtp_socket, (char *)"DATA\r\n");
      smtp_response = do_read(smtp_socket, CRLF);
      if (verbose)
        std::cout << "[SMTP SERVER]: " << smtp_response << std::endl;
      sleep(1);

      char messsage_buf[body_map["message"].size() + 6];
      bzero(messsage_buf, sizeof(messsage_buf));
      snprintf(
        messsage_buf, sizeof(messsage_buf), "%s", body_map["message"].data()
      );
      strcat(messsage_buf, "\r\n");

      send_response(smtp_socket, messsage_buf);
      send_response(smtp_socket, (char *)".\r\n");

      // Read response status
      std::string status = do_read(smtp_socket, CRLF);
      if (verbose)
        std::cout << "[SMTP SERVER STATUS]: " << status << std::endl;
      if (status.rfind(OK) == 0)
        return create_http_response(200, "", data["Host"]);
      else if (status.rfind(ERR) == 0)
        return create_http_response(500, "", data["Host"]);
    }
  }
  else if (data["path"] == "/signup") {
    std::map<std::string, std::string> params = params_from_url(
      url_decode(body)
    );
    if (params.count("password"))
      params["password"] = hash(params["password"]);

    int bend = bend_connect(params["email"], bend_coord);
    if (bend < 0) {
      response_params["success"] = "2";
      response_params["login_failure"] = "true";
    } else {
      do_read(bend, CRLF);

      std::string existing = bend_GET(&bend, params["email"], "password");
      response_params["success"] = "0";
      if (existing.rfind("-ERR", 0) != 0) {
        response_params["success"] = "2";
        response_params["login_failure"] = "true";
      } else {
        for (
          std::map<std::string, std::string>::const_iterator it = params.begin();
          it != params.end();
          ++it
        ) {
          if (it -> first == "email")
           continue;
          if (std::string::npos == (
            bend_PUT(&bend, params["email"], it -> first, it -> second).find("OK")
          )) {
            response_params["success"] = "2";
            break;
          }
        }
      }
    }
  } else if (data["path"] == "/login") {
    std::map<std::string, std::string> params = params_from_url(
      url_decode(body)
    );
    if (params.count("password"))
      params["password"] = hash(params["password"]);
    int bend = bend_connect(params["email"], bend_coord);
    do_read(bend, CRLF);
    std::string password = bend_GET(&bend, params["email"], "password");
    response_params["success"] = "0";
    if (params["password"] != password)
      response_params["success"] = "1";
    else {
      std::string cke = generate_cookie(), val;
      val.append("cookie=");
      val.append(cke);
      val.append("; SameSite=None; Secure;");
      val.append(CRLF);
      val.append("Set-Cookie: uuid=");
      val.append(params["email"]);
      val.append("; SameSite=None; Secure;");
      val.append(CRLF);
      val.append("Set-Cookie: name=");
      val.append(bend_GET(&bend, params["email"], "name"));
      val.append("; SameSite=None; Secure;");
      if (
        std::string::npos != (
          bend_PUT(&bend, params["email"], "cookie", cke).find("OK")
        )
      )
      response_params["Set-Cookie"] = val;
    }
  }

  std::string response;
  response.append(data["vno"]);
  response.append(" 303 See Other");
  response.append(CRLF);
  if (response_params.find("Set-Cookie") != response_params.end()) {
    response.append("Set-Cookie: ");
    response.append(response_params["Set-Cookie"]);
    response.append(CRLF);
    response_params.erase("Set-Cookie");
  }
  response.append("Location: ");
  data["path"] = std::regex_replace(data["path"], std::regex(".html"), "");
  data["path"] = std::regex_replace(data["path"], std::regex("index"), "");
  response.append(post_redirect(data["path"], response_params));
  response.append(CRLF);
  response.append("Content-Type: text/html; charset=UTF-8");
  response.append(CRLF);
  response.append("Content-Length: 0");
  response.append(CRLF);
  if (data.count("Host") != 0) {
    response.append("Access-Control-Allow-Origin: ");
    response.append(data["Host"]);
    response.append(CRLF);
  }
  response.append("Access-Control-Allow-Credentials: true");
  return response;
}


std::string handle_ERR(
  std::map<std::string, std::string> data,
  bool *do_close_connection,
  bool verbose,
  struct sockaddr_in bend_coord
)
{
  return NULL;
}


bool is_full_body(std::string buf, std::map<std::string, std::string> data)
{
  std::string CRLFCRLF;
  CRLFCRLF.append(CRLF);
  CRLFCRLF.append(CRLF);

  size_t payload_size = std::stoi(data["Content-Length"]);
  size_t body_sidx = buf.find(CRLFCRLF, 0) + CRLFCRLF.length();
  return buf.length() >= payload_size + body_sidx;
}


std::string auth_user(
  std::map<std::string, std::string> data, struct sockaddr_in bend_coord
)
{
  if (data.find("Cookie") == data.end())
    return "";
  std::vector<std::string> cookie = split(data["Cookie"], ';'), kv;
  std::map<std::string, std::string> params;
  for (size_t itr { 0 }; itr < cookie.size(); ++itr) {
    kv = split(cookie[itr], '=');
    if (kv.size() != 2)
      continue;
    strip(&kv[0]);
    strip(&kv[1]);
    params[kv[0]] = kv[1];
  }
  if (params.find("cookie") == params.end())
    return "";
  if (params.find("uuid") == params.end())
    return "";

  int bend = bend_connect(params["uuid"], bend_coord);
  if (bend < 0)
    return "";
  do_read(bend, CRLF);
  if (params["cookie"] == bend_GET(&bend, params["uuid"], "cookie"))
    return params["uuid"];
  return "";
}
