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
 * @file url.cc 
 *
 * @brief URL parsing and manipulation functions.
 *
 * @ingroup frontend
 *
 * @author Michael Yao
 * Contact: myao2199 [at] seas [dot] upenn [dot] edu
 */
#ifndef URL_H
#define URL_H
#include <map>
#include <string>

/**
 * @brief Decodes an input URL.
 *
 * @param url The URL to decode.
 * @return The decoded URL.
 */
std::string url_decode(std::string url);

/**
 * @brief Extracts the URL parameters from an input URL.
 *
 * @param url The URL to extract parameters from.
 * @return The extracted parameter values from the URL.
 */
std::map<std::string, std::string> params_from_url(std::string url);


/**
 * @brief Encodes a set of parameters into URL parameters.
 *
 * @return params A mapping of the parameter values.
 * @return The URL encoded parameters.
 */
std::string params_to_url(std::map<std::string, std::string> params);
#endif
