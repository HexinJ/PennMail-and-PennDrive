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
#include <cctype>
#include <map>
#include <sstream>
#include <string>


std::string url_decode(std::string url)
{
  std::string decoded_url, tmp;
  int len = url.length();
  for (int i = 0; i < len; i++) {
    if (url[i] == '%') {
      if (i + 2 < len && isxdigit(url[i + 1]) && isxdigit(url[i + 2])) {
        tmp.push_back(url[i + 1]);
        tmp.push_back(url[i + 2]);
        decoded_url.push_back(static_cast<char>(std::stoi(tmp.c_str(), nullptr, 16)));
        i += 2;
	tmp.clear();
      } else {
        decoded_url.push_back(url[i]);
      }
    } else if (url[i] == '+') {
      decoded_url.push_back(' ');
    } else {
      decoded_url.push_back(url[i]);
    }
  }
  return decoded_url;
}


std::map<std::string, std::string> params_from_url(std::string url)
{
  std::map<std::string, std::string> params;
  std::stringstream ss(url);
  std::string key, val;

  if (url.back() != '?')
     url.push_back('?');

  size_t pos;
  while (std::getline(ss, key, '&')) {
    if ((pos = key.find('=')) == std::string::npos)
      continue;
    val = key.substr(pos + 1);
    key = key.substr(0, pos);
    if (key.rfind('?', 0) == 0)
      key = key.substr(1);
    params[key] = val;
  }

  return params; 
}


std::string params_to_url(std::map<std::string, std::string> params)
{
  std::string url;

  for (
    std::map<std::string, std::string>::const_iterator it = params.begin();
    it != params.end();
    ++it
  ) {
    url.append(it -> first);
    url.push_back('=');
    url.append(it -> second);
    url.push_back('&');
  }

  if (url.back() == '&')
    url.pop_back();
  if (url.size() > 0)
    url.insert(0, 1, '?');
  return url;
}
