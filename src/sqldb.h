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
#include "strtemplate.h"

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

  // Insert items into the database manually
  void insert_litref(std::ostream &os, const std::string &key, const std::unordered_map<std::string,std::string> &kmap);
  void insert_set(std::ostream &os, const std::string &key, const std::unordered_map<std::string,std::string> &kmap);
  void insert_method(std::ostream &os, const std::string &key, const std::unordered_map<std::string,std::string> &kmap);
  void insert_structure(std::ostream &os, const std::string &key, const std::unordered_map<std::string,std::string> &kmap);
  void insert_property(std::ostream &os, const std::string &key, const std::unordered_map<std::string,std::string> &kmap);
  void insert_evaluation(std::ostream &os, const std::unordered_map<std::string,std::string> &kmap);
  void insert_term(std::ostream &os, const std::unordered_map<std::string,std::string> &kmap);

  // Bulk insert
  void insert_calc(std::ostream &os, const std::unordered_map<std::string,std::string> &kmap);

  // Insert literature references into the database from a bibtex file
  void insert_litref_bibtex(std::ostream &os, const std::list<std::string> &tokens);

  // Insert additional info from an INSERT SET command (xyz keyword)
  void insert_set_xyz(std::ostream &os, const std::string &key, const std::unordered_map<std::string,std::string> &kmap);

  // Insert additional info from an INSERT SET command (din keyword)
  void insert_set_din(std::ostream &os, const std::string &key, const std::unordered_map<std::string,std::string> &kmap);

  // Delete items from the database
  void erase(std::ostream &os, const std::string &category, const std::list<std::string> &tokens);

  // Print items from the database
  void print(std::ostream &os, const std::string &category, bool dobib);

  // Print a summary of the contents of the database
  void printsummary(std::ostream &os, bool full);

  // List sets of properties in the database (din format)
  void print_din(std::ostream &os, const std::unordered_map<std::string,std::string> &kmap);

  // Verify the consistency of the database
  void verify(std::ostream &os);

  // Read data for the database or one of its subsets from a file, then
  // compare to a reference method.
  void read_and_compare(std::ostream &os, const std::unordered_map<std::string,std::string> &kmap);

  // Write input files for a database set
  void write_structures(const std::unordered_map<std::string,std::string> &kmap);

  // Write the structures with IDs given by the keys in smap. dir:
  // output directory. npack = package and compress in packets of
  // npack files (0 = no packing). Use molecule (template_m) and crystal
  // (template_c) templates.
  void write_many_structures(const std::string &template_m, const std::string &template_c,
                             const std::string &ext_m, const std::string &ext_c,
                             const std::unordered_map<int,int> &smap,
                             const std::string &dir="./", int npack=0);

  // Write the structure id in the database. Options have the same
  // meaning as in write_many_structures. Returns filename of the
  // written file.
  std::string write_one_structure(int id, const strtemplate &tmpl,
                                  const std::string &ext, const std::string &dir="./");

  // Find the property type ID corresponding to the key in the database table.
  // If toupper, uppercase the key before fetching the ID from the table. If
  // no such key is found in the table, return 0.
  int find_id_from_key(const std::string &key,const std::string &table,bool toupper=false);

  // Find the key corresponding to the ID in the database table. If
  // toupper, the key is returned in uppercase. If no such ID is found
  // in the table, return an empty string.
  std::string find_key_from_id(const int id,const std::string &table,bool toupper=false);

  // Parse the input string input and find if it is a number or a
  // key. If it is a key, find the corresponding ID in the table and
  // return both the key and id. If it is a number, find the key in the
  // table and return the key and id. If toupperi, uppercase the input
  // before parsing it. If touppero, uppercase the output key. Returns the
  // ID if succeeded, 0 if failed.
  int get_key_and_id(const std::string &input, const std::string &table,
                     std::string &key, int &id, bool toupperi=false, bool touppero=false);

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
