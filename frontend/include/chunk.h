// -*- lsst-c++ -*-
/*
* Chunk Transfer Encoding Implementation
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
 * @file chunk.cc 
 *
 * @brief Chunk Transfer Encoding utility functions.
 *
 * @ingroup frontend
 *
 * @author Michael Yao
 * Contact: myao2199 [at] seas [dot] upenn [dot] edu
 */
#ifndef CHUNK_H
#define CHUNK_H
#include <sstream>
#include <string>

#define CHUNK_MAX_DOWNLOAD_SIZE 65536
#define CHUNK_THRESH_SIZE 1048576

/**
 * @brief Converts a base 10 integer to its string hexadecimal representation.
 *
 * @param n the base 10 integer to convert.
 * @return The string hexadecimal representation of the input number.
 */
std::string decToHex(size_t n);

/**
 * @brief Converts a hexadecimal number to its base 10 representation.
 *
 * @param hex the hexadecimal number to convert.
 * @return The base 10 integer value of the input number.
 */
size_t hexToDec(std::string hex);

/**
 * @brief Reads a chunk transfer-encoded data stream into a string.
 *
 * @param sock the socket to read the chunk transfer-encoded data from.
 * @param payload the payload string to read to.
 * @return NULL.
 */
void read_from_chunk_transfer(int sock, std::string payload);

/**
 * @brief Writes data into chunk transfer-encoded format.
 *
 * @param data the data to write into chunk transfer-encoded format.
 * @param chunksize the size of the individual chunks.
 * @return The chunk transfer-encoded data as a stream.
 */
std::ostringstream write_to_chunk_transfer(std::string data, size_t chunksize);
#endif
