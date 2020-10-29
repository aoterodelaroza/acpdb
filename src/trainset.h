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

#ifndef TRAINSET_H
#define TRAINSET_H

#include <string>
#include <list>
#include <vector>
#include "sqldb.h"
#include "sqlite3.h"

// A SQLite3 statement class.
class trainset {

 public:

  // Add atoms and max. angular momentum
  void addatoms(const std::list<std::string> &tokens);

  // Add exponents
  void addexp(const std::list<std::string> &tokens);

  // Add sets
  void addset(sqldb &db, const std::list<std::string> &tokens);

 private:
  std::vector<unsigned char> zat;  // atomic numbers for the atoms
  std::vector<unsigned char> lmax; // maximum angular momentum channel for the atoms
  std::vector<double> exp; // exponents
  std::vector<std::string> setname; // name of the sets
  std::vector<int> setid; // IDs of the sets
};

#endif