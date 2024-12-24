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

/**
 * @file stringutils.cc
 *
 * @brief String parsing and manipulation functions.
 *
 * @ingroup frontend
 *
 * @author Michael Yao
 * Contact: myao2199 [at] seas [dot] upenn [dot] edu
 */
#ifndef STRINGUTILS_H
#define STRINGUTILS_H
#include <string>
#include <vector>
#include <unordered_map>
#include <map>

/**
 * @brief Computes the SHA256 hash of an input string.
 *
 * @param input The input string (e.g., an email) to compute the hash for.
 * @return The hash string of the input.
 */
std::string hash(std::string input);

/**
 * @brief Returns whether a string ends with a suffix.
 *
 * @param str The input string.
 * @param suffix The suffix to test for.
 * @return Whether a string ends with a suffix.
 */
bool has_suffix(std::string str, std::string suffix);

/**
 * @brief Strips whitespace from the beginning and end of a string.
 *
 * @param str A pointer to the input string.
 * @return None.
 */
void strip(std::string *str);


/**
 * @brief Counts the number of non-overlapping substring occurences.
 *
 * @param buf The buffer to search in.
 * @param substr The substring to search for.
 * @return The number of non-overlapping substring occurences in buf.
 */
int count_substring(std::string buf, std::string substr);

/**
 * @brief Returns a map given a json-like string
 *
 * @param jsonString the json-like string
 * @return A map with key:value pair matching that of the string
 */
std::map<std::string, std::string> parse_json_string(const std::string& json_string);

/**
 * @brief Returns a json-like string given a map
 * 
 * @param data the map
 * @return A json-like string in the format of
 * {
 *   "recipient":"test@test.com",
 *   "subject":"Hello",
 *   "message":"This is a test message"
 * }
 */
std::string map_to_json(const std::unordered_map<std::string, std::string>& map);

/**
 * @brief Splits a string by delimiter
 *
 * @param str The string to split
 * @param delimiter The delimiter
 * @return A vector with the split strings
 */
std::vector<std::string> split(std::string str, char delimiter);

/**
 * @brief Replaces a substring by another string.
 *
 * @param str The parent string to modify.
 * @param from The substring to replace.
 * @param to The new substring to replace the original substring.
 * @return The potentially modified parent string.
 */
std::string replace(
  std::string str,
  const std::string from,
  const std::string to
);

/**
 * @brief Computes a 16 character hash of a string
 * 
 * @param data The data string
 * @param dataLengthBytes The length of the data in bytes
 * @return A string of the hashed data (length 16)
 */
std::string compute_hash_16(std::string data, int length);

/**
 * @brief Returns the name of a file given full path
 * 
 * @param fullPath: the full path of to the file
 * @return name of the file
 */
std::string file_name_from_path(const std::string& full_path);

/**
 * @brief Returns a binary string given a string
 * @param file_data: the string to convert
 * @return the binary string
 */
std::string stobs(std::string file_data);

/**
 * @brief Returns a byte array given a binary string
 * @param binary_string: the string to convert
 * @return the byte array
 */
std::vector<uint8_t> bstoby(const std::string& binary_string);

/**
 * @brief Returns a base64 encoded string given a byte array
 * @param byte_array: the byte array to convert
 * @return the base64 encoded string
 */
std::string bytob64(const std::vector<uint8_t>& byte_array);

/**
 * @brief Renames all files in the fid vector after renaming a folder
 * @return the renamed fid vector
 */
std::string renameFolder(int table_fd, std::string user, std::string file_data, std::string old_file_name, std::string new_file_name, std::string parent_path);

/**
 * @brief Renames all files in the fid vector after moving a folder
 * @return the renamed fid vector
 */
std::string renameMovedFolder(int table_fd, std::string user, std::string file_data, std::string file_name, std::string new_path);

#endif
