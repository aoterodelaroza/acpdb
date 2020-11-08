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
#include <vector>
#include <list>
#include "sqlite3.h"
#include "statement.h"
#include "acp.h"

// A SQLite3 database class.
class sqldb {

 public:

  // constructors
  sqldb() : db(nullptr) {}; // default constructor
  sqldb(const std::string &file) : db(nullptr), stmt() { // constructor using file name
    connect(file, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    if (!checksane())
      create();
  }; 
  sqldb(sqldb&& rhs) = delete; // move constructor (deleted)
  sqldb(const sqldb& rhs) = delete; // copy constructor (deleted)

  // destructors
  ~sqldb() { 
    close(); 
  };

  // bool operator
  operator bool() const { return db; }

  // Check if the DB is sane, empty, or not sane. If except_on_empty,
  // raise exception on empty. Always raise excepton on error. Return
  // 1 if sane, 0 if empty.
  int checksane(bool except_on_empty = false);

  // Open a database file for use.
  void connect(const std::string &filename, int flags = SQLITE_OPEN_READWRITE);

  // Create the database skeleton.
  void create();

  // Close a database connection if open and reset the pointer to NULL
  void close();

  // Insert an item into the database
  void insert(const std::string &category, const std::string &key, std::unordered_map<std::string,std::string> &kmap);

  // Insert literature references into the database from a bibtex file
  void insert_litref_bibtex(std::list<std::string> &tokens);

  // Insert additional info from an INSERT SET command (xyz keyword)
  void insert_set_xyz(const std::string &key, std::unordered_map<std::string,std::string> &kmap);

  // Insert additional info from an INSERT SET command (din keyword)
  void insert_set_din(const std::string &key, std::unordered_map<std::string,std::string> &kmap);

  // Delete items from the database
  void erase(const std::string &category, std::list<std::string> &tokens);

  // List items from the database
  void list(std::ostream &os, const std::string &category, std::list<std::string> &tokens);

  // List structures in the database (xyz format)
  void list_xyz(std::unordered_map<std::string,std::string> &kmap);

  // List sets of properties in the database (din format)
  void list_din(std::unordered_map<std::string,std::string> &kmap);

  // Verify the consistency of the database
  void verify(std::ostream &os);

  // Write input files for a database set
  void write_set_inputs(std::unordered_map<std::string,std::string> &kmap, const acp &a);

  // Write an input file for structure id in the database. Put the file in
  // directory dir and use ACP a in it.
  void write_one_input(int id, int methodid, const std::string &dir = "./", const acp &a = {});

  // Find the property type ID corresponding to the key 
  int find_id_from_key(const std::string &key,statement::stmttype type);

  // Begin a transaction
  void begin_transaction(){ stmt[statement::STMT_BEGIN_TRANSACTION]->execute(); }

  // Commit a transaction
  void commit_transaction(){ stmt[statement::STMT_COMMIT_TRANSACTION]->execute(); }

  // Rollback a transaction
  void rollback_transaction(){ stmt[statement::STMT_ROLLBACK_TRANSACTION]->execute(); }

  // Return a pointer to the database
  sqlite3 *ptr() { return db; }

 private:

  // prepared SQLite statments
  statement *stmt[statement::number_stmt_types];

  // database info
  std::string dbfilename;
  sqlite3 *db;
};

#endif

