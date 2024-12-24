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
#include <fstream>
#include "constants.h"
#include "fileutils.h"


std::string slurp(std::string path)
{
  std::ifstream stream = std::ifstream(path.data());
  stream.exceptions(std::ios_base::badbit);

  if (!stream)
    return "";
    
  std::string out;
  std::string buf = std::string(CHUNK_SIZE, '\0');
  while (stream.read(&buf[0], CHUNK_SIZE))
    out.append(buf, 0, stream.gcount());
  out.append(buf, 0, stream.gcount());
  return out;
}
