// -*- c++-mode -*-
/*
Copyright (c) 2020 Alberto Otero de la Roza <aoterodelaroza@gmail.com>

acpdb is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or (at
your option) any later version.

acpdb is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "parseutils.h"
#include <iostream>
#include <sstream>
#include <algorithm>

// Print error and usage messages
void print_error_usage(const char *prog, const char *errmsg) {
  if (errmsg) {
    printf("Error: %s\n",errmsg);
  }
  if (prog) {
    printf("Usage: %s [inputfile [outputfile]]\n",prog);
    printf("Options:\n");
    printf("  -h : show this message and exit\n");
  }
}

// List the words in an input line. Skip the rest of the line if a
// comment character (#) is found as the first character in a token.
std::list<std::string> list_all_words(const std::string &line) {

  std::istringstream iss(line);
  std::list<std::string> result;
  std::string token;

  while (iss >> token){
    if (token[0] == '#') break;
    result.push_back(token);
  }

  return result;
}  

// Read lines from input stream is. Split each line into a key (first
// word) and content (rest of the line) pair. If toupper, capitalize
// the key. If the key END or the eof() is found, return the map.
std::map<std::string,std::string> map_keyword_pairs(std::istream *is, bool toupper){

  std::map<std::string,std::string> result;
  std::string ukeyw, keyw, line;
  while(*is >> keyw){
    line.clear();
    getline(*is,line);

    if (keyw.empty() || keyw[0] == '#') continue;
    ukeyw = keyw;
    transform(ukeyw.begin(), ukeyw.end(), ukeyw.begin(), ::toupper);
    if (ukeyw == "END") break;
    if (toupper)
      result[ukeyw] = line;
    else
      result[keyw] = line;
  }
  return result;
}

// Get and remove front from list of strings. If toupper is true,
// convert the string to uppercase.
std::string popstring(std::list<std::string> &list, bool toupper){
  if (list.empty()) return "";

  std::string result = list.front();
  list.pop_front();
  if (toupper)
    transform(result.begin(), result.end(), result.begin(), ::toupper);
  return result;
}

