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
 * @file cookie.cc 
 *
 * @brief Functions to generate and parse HTTP Cookie objects.
 *
 * @ingroup frontend
 *
 * @author Michael Yao
 * Contact: myao2199 [at] seas [dot] upenn [dot] edu
 */
#ifndef COOKIE_H
#define COOKIE_H
#include <string>

/**
 * @brief Generates a random alphanumeric cookie.
 *
 * @param None.
 * @return A random alphanumeric cookie.
 */
std::string generate_cookie();
#endif
