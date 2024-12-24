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
#include <algorithm>
#include <openssl/sha.h>
#include <openssl/md5.h>
#include <string>
#include <map>
#include <unordered_map>
#include <vector>
#include <sstream>
#include <stdexcept>
#include <cctype>
#include <iostream>
#include <bitset>
#include "message.h"

const std::string BASE64_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string hash(std::string input)
{
  unsigned char tmp[SHA256_DIGEST_LENGTH] = { '\0' };
  SHA256_CTX c;
  SHA256_Init(&c);
  SHA256_Update(&c, input.c_str(), input.length());
  SHA256_Final(tmp, &c);
  char buf[4];
  std::string s;
  for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
    sprintf(buf, "%02x", (unsigned int) tmp[i]);
    s.push_back(buf[0]);
    s.push_back(buf[1]);
  }
  return s;
}


bool has_suffix(std::string str, std::string suffix)
{
  if (str.size() < suffix.size())
    return false;
  return (str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0);
}


void strip(std::string *str)
{
  (*str).erase(
    std::remove_if((*str).begin(), (*str).end(), isspace), (*str).end()
  );
}


int count_substring(std::string buf, std::string substr)
{
  if (substr.length() == 0)
    return 0;
  int count = 0;
  for (
    size_t offset = buf.find(substr);
    offset != std::string::npos;
    offset = buf.find(substr, offset + substr.length())
  )
    count++;
  return count;
}

std::map<std::string, std::string> parse_json_string(const std::string& json_string) {
    std::map<std::string, std::string> json_map;
    size_t n = 0;

    // Function to parse a string in "quotes", n starting at the opening quote
    auto parse_quoted_string = [&]() -> std::string {
        if (json_string[n] != '\"') {
            throw std::runtime_error("Expected opening quote at position " + std::to_string(n));
        }
        ++n; 
        std::string value;
        while (n < json_string.size() && json_string[n] != '\"') {
            if (json_string[n] == '\\') { 
                ++n;
                if (n >= json_string.size()) {
                    throw std::runtime_error("Incomplete escape sequence at position " + std::to_string(n));
                }
                switch (json_string[n]) {
                    case 'n': value += '\n'; break; 
                    default:
                        throw std::runtime_error("Invalid escape character at position " + std::to_string(n));
                }
            } else {
                value += json_string[n];
            }
            ++n;
        }
        if (n >= json_string.size() || json_string[n] != '\"') {
            throw std::runtime_error("Expected closing quote at position " + std::to_string(n));
        }
        ++n; 
        return value;
        // n at closing quote
    };

    ++n; // Skip the closing quote

    while (n < json_string.size() && json_string[n] != '}') {
        if (json_string[n] == ',') ++n; // We do not care about the comma
        if (json_string[n] != '\"') { // We expect to find a opening quote after comma
            throw std::runtime_error("Expected key starting with '\"' at position " + std::to_string(n));
        }
        std::string key = parse_quoted_string(); // Parse the quoted string (key)

        if (json_string[n] != ':') { // We expect a colon after the key
            throw std::runtime_error("Expected ':' after key at position " + std::to_string(n));
        }
        ++n; // Skip the colon

        if (json_string[n] == '\"') { // We expect a opening quote after the colon
            std::string value = parse_quoted_string(); // Parse quoted string (value)
            json_map[key] = value;
        } else {
            throw std::runtime_error("Expected value starting with '\"' at position " + std::to_string(n));
        }
    }
    return json_map;
}


std::string map_to_json(const std::unordered_map<std::string, std::string>& mmap)
{
  std::string json_string = "{";
  bool first = true;

  for (
    std::unordered_map<std::string, std::string>::const_iterator it = mmap.begin();
    it != mmap.end();
    ++it
  ) {
    if (!first)
      json_string += ",";
    first = false;

    json_string += "\"" + it -> first + "\":";
    json_string += "\"" + it -> second + "\"";
  }

  json_string += "}";
  return json_string;
}


std::vector<std::string> split(std::string str, char delimiter)
{
  std::stringstream ss(str);
  std::vector<std::string> res;
  std::string token;
  while (getline(ss, token, delimiter)) {
    res.push_back(token);
  }
  return res;
}


std::string replace(
  std::string str,
  const std::string from,
  const std::string to
) {
  size_t start_pos = str.find(from);
  if (start_pos == std::string::npos)
    return str;
  str.replace(start_pos, from.length(), to);
  return str;
}


std::string compute_hash_16(std::string data, int length)
{
  unsigned char digestBuffer[MD5_DIGEST_LENGTH];
  char hash_buffer[2 * MD5_DIGEST_LENGTH];
  MD5_CTX c;
  MD5_Init(&c);
  MD5_Update(&c, (void *) data.c_str(), length);
  MD5_Final(digestBuffer, &c);

  for(int p = 0; p < 16; p++){
    sprintf(hash_buffer + p * 2, "%02x", digestBuffer[p]);
  }

  std::string hash(hash_buffer);

  return hash;
}


std::string file_name_from_path(const std::string& full_path)
{
  if(full_path == "/"){
    return "/";
  }

  // If the full path is a folder, find the second to last / and return everything after
  if (!full_path.empty() && (full_path.back() == '/')) {
    size_t last_slash = full_path.find_last_of("/", full_path.size() - 2); 
    if (last_slash == std::string::npos) {
      return full_path; 
    }
    return full_path.substr(last_slash + 1);
  }

  // Not a folder, return everything after the last /
  size_t last_slash = full_path.find_last_of("/"); 

  // If there is no slash then return the whole thing
  if (last_slash == std::string::npos) {
    return full_path; 
  }

  return full_path.substr(last_slash + 1);
}


std::string stobs(std::string file_data)
{
  std::string binary_string = "";
  for (char& _char : file_data) {
    // Iteratively convert every char to 8 bits and convert that into string
    binary_string += std::bitset<8>(_char).to_string();
  }
  return binary_string;
}


std::vector<uint8_t> bstoby(const std::string& binary_string)
{
  std::vector<uint8_t> byte_array; // An array of bytes represented using uint_8

  // Iteratively parse 8 bits of the string to a byte
  for (size_t i = 0; i < binary_string.length(); i += 8) {
    std::string byte_str = binary_string.substr(i, 8); 
    uint8_t byte = static_cast<uint8_t>(std::bitset<8>(byte_str).to_ulong());
    byte_array.push_back(byte);
  }
  return byte_array;
}

std::string bytob64(const std::vector<uint8_t>& byte_array) {
    std::string encoded_string;
    size_t len = byte_array.size();

    for (size_t i = 0; i < len; i += 3) {
        // Transform 3 bytes into a 24 bit chunk
        uint32_t triple = ((byte_array[i] << 16) & 0xFFFFFF) +
                          (((i + 1 < len) ? byte_array[i + 1] : 0) << 8) +
                          ((i + 2 < len) ? byte_array[i + 2] : 0);

        // Extract 4 groups of 6 bits, convert into the corresponding character
        encoded_string += BASE64_CHARS[(triple >> 18) & 0x3F];
        encoded_string += BASE64_CHARS[(triple >> 12) & 0x3F];
        encoded_string += (i + 1 < len) ? BASE64_CHARS[(triple >> 6) & 0x3F] : '=';
        encoded_string += (i + 2 < len) ? BASE64_CHARS[triple & 0x3F] : '=';
    }
    return encoded_string;
}

std::string renameFolder(int table_fd, std::string user, std::string file_data, std::string old_file_name, std::string new_file_name, std::string parent_path) {
  std::string new_file_data = "";
  std::vector <std::string> s = split(file_data, ',');
  for (std::string &i : s) {
    if (i != "../") {
      // file
      if (i.back() != '/') {
        // Rename file in backend
        std::string data = bend_GET(&table_fd, user, i);
        bend_DELETE(&table_fd, user, i);
        std::string file_name = file_name_from_path(i);
        std::string folder_name = file_name_from_path(new_file_name);
        std::string full_name = parent_path + folder_name + file_name;
        bend_PUT(&table_fd, user, full_name, data);
        new_file_data.append(full_name);
      }
    }
    else {
      new_file_data.append(i);
    }
    new_file_data.append(",");
  }

  new_file_data.pop_back();
  return new_file_data;
}

std::string renameMovedFolder(int table_fd, std::string user, std::string file_data, std::string file_name, std::string new_path){
  std::string new_file_data = "";
  std::vector <std::string> s = split(file_data, ',');
  for (std::string &i : s) {
    if (i != "../") {
      // file
      if (i.back() != '/') {
        // Rename file in backend
        std::string data = bend_GET(&table_fd, user, i);
        bend_DELETE(&table_fd, user, i);
        std::string full_name = new_path + file_name;
        bend_PUT(&table_fd, user, full_name, data);
        new_file_data.append(full_name);
      }
    }
    else {
      new_file_data.append(i);
    }
    new_file_data.append(",");
  }

  new_file_data.pop_back();
  return new_file_data;
}