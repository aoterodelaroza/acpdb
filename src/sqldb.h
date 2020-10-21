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

#ifndef SQLDB_H
#define SQLDB_H

#include <string>
#include <unordered_map>
#include <list>
#include "sqlite3.h"

// A SQLite3 database class.
class sqldb {

 public:

  // enum: output of checksane()
  enum dbstatus { dbstatus_sane = 0, dbstatus_empty = 1, dbstatus_error = 2 };

  // constructors
  sqldb() : db(nullptr) {}; // default constructor
  sqldb(const std::string &file) : db(nullptr) { // constructor using file name
    connect(file, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    if (checksane() == dbstatus_empty)
      create();
  }; 
  sqldb(sqldb&& rhs) : db(rhs.db) { rhs.db = nullptr; } // move constructor
  sqldb(const sqldb& rhs) = delete; // copy constructor (deleted)

  // destructors
  ~sqldb() { close(); };

  // bool operator
  operator bool() const { return db; }

  // Check if the DB is sane, empty, or not sane. If
  // except_on_error/empty, raise exception on error/empty.
  dbstatus checksane(bool except_on_error = false, bool except_on_empty = false);

  // Open a database file for use.
  void connect(const std::string &filename, int flags = SQLITE_OPEN_READWRITE);

  // Create the database skeleton.
  void create();

  // Close a database connection if open and reset the pointer to NULL
  void close();

  // Insert an item into the database
  void insert(const std::string &category, const std::string &key, const std::unordered_map<std::string,std::string> &kmap);

  // Insert literature refernces into the database from a bibtex file
  void insert_litref_bibtex(const std::list<std::string> &tokens);

  // Delete items from the database
  void erase(const std::string &category, std::list<std::string> &tokens);

  // List items from the database
  void list(const std::string &category, std::list<std::string> &tokens);

 private:

  std::string dbfilename;
  sqlite3 *db;

};

#endif

