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

#ifndef PARSEUTILS_H
#define PARSEUTILS_H

#include <unordered_map>
#include <list>
#include <string>

// Utility routines for parsing the input file.

// Transform a string to uppercase
void uppercase(std::string &s);

// Transform a string to lowercase
void lowercase(std::string &s);

// Print error and usage messages
void print_error_usage(const char *prog = 0, const char *errmsg = 0);

// Build a list from all words in a line. Skip the rest of the line if
// a comment character (#) is found as the first character in a token.
std::list<std::string> list_all_words(const std::string &line);

// Read lines from input stream is. Split each line into a key (first
// word) and content (rest of the line) pair. If toupper, capitalize
// the key. If the key END or the eof() is found, return the map.
std::unordered_map<std::string,std::string> map_keyword_pairs(std::istream *is, bool toupper = false);

// Get and remove front from list of strings. If the list is empty,
// return a zero-length string.
std::string popstring(std::list<std::string> &list, bool toupper = false);

// Compare two strings for equality regardless of case.
bool equali_strings(const std::string& a, const std::string& b);

// Check if a string can be converted to an integer.
inline bool isinteger(const std::string &a){
  return a.find_first_not_of("0123456789 ") == std::string::npos;
}

// Write a double to a string very precisely
std::string to_string_precise(double a);

// Atomic number from atomic name
unsigned char zatguess(std::string atsym);

// Atomic name from atomic number
std::string nameguess(unsigned char z);

// Read a line from stream and get the first keyword (str) and maybe
// double (res) from it. Skip lines that start with #. If there was a
// fail or eof, return 1. If no double could be read, return res = 0.
int line_get_double(std::istream &is, std::string &line, std::string &str, double &res);

// Get the next line from stream is. Use skipchar as the comment character.
std::istream &get_next_line(std::istream &is, std::string &line, char skipchar);


#endif

