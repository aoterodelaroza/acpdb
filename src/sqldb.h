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
  void insert_litref(const std::string &key, std::unordered_map<std::string,std::string> &kmap);

  // Insert literature references into the database from a bibtex file
  void insert_litref_bibtex(std::list<std::string> &tokens);

  // Insert additional info from an INSERT SET command (xyz keyword)
  void insert_set_xyz(const std::string &key, std::unordered_map<std::string,std::string> &kmap);

  // Insert additional info from an INSERT SET command (din keyword)
  void insert_set_din(const std::string &key, std::unordered_map<std::string,std::string> &kmap);

  // Delete items from the database
  void erase(const std::string &category, std::list<std::string> &tokens);

  // Print items from the database
  void print(std::ostream &os, const std::string &category, bool dobib);

  // Print a summary of the contents of the database
  void printsummary(std::ostream &os, bool full);

  // List sets of properties in the database (din format)
  void list_din(std::unordered_map<std::string,std::string> &kmap);

  // Verify the consistency of the database
  void verify(std::ostream &os);

  // Write input files for a database set
  void write_structures(std::unordered_map<std::string,std::string> &kmap, const acp &a);

  // Read data for the database or one of its subsets from a file, then
  // compare to reference method refm.
  void read_and_compare(std::ostream &os, const std::string &file, const std::string &refm, std::unordered_map<std::string,std::string> &kmap);

  // Read data from a file, then insert as evaluation of the argument method.
  void read_and_insert(const std::string &file, const std::string &method);

  // Write the structures with IDs given by the keys in smap. The
  // values of smap give the types (xyz for an xyz file, terms for a
  // terms input file or energy_difference, etc. for a property input
  // file). gmap: writer-dependent options for the structures. dir:
  // output directory. npack = package and compress in packets of
  // npack files (0 = no packing). a: ACP to use in the inputs.
  // zat, l, exp: details for term inputs.
  void write_many_structures(std::unordered_map<int,std::string> &smap, const std::unordered_map<std::string,std::string> &gmap = {}, 
                             const std::string &dir = "./", int npack = 0, 
                             const acp &a = {},
                             const std::vector<unsigned char> &zat = {}, const std::vector<unsigned char> &lmax = {}, const std::vector<double> &exp = {});

  // Write the structure id in the database. Options have the same
  // meaning as in write_many_structures. Returns filename of the
  // written file.
  std::string write_one_structure(int id, const std::string type, const std::unordered_map<std::string,std::string> gmap = {},
                                  const std::string &dir = "./",
                                  const acp &a = {},
                                  const std::vector<unsigned char> &zat = {}, const std::vector<unsigned char> &lmax = {}, const std::vector<double> &exp = {});

  // Find the property type ID corresponding to the key 
  int find_id_from_key(const std::string &key,statement::stmttype type);

  // Get the Gaussian map from the method key
  std::unordered_map<std::string,std::string> get_program_map(const std::string &methodkey, const std::string &program);

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

