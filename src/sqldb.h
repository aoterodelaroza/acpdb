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
  sqldb(const std::string &file) : db(nullptr) { // constructor using file name
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

  // Bulk insert: read data from a file, then insert as evaluation or terms.
  void insert_maxcoef(std::ostream &os, const std::unordered_map<std::string,std::string> &kmap);
  void insert_calc(std::ostream &os, const std::unordered_map<std::string,std::string> &kmap,
		   const std::vector<unsigned char> &zat={}, const std::vector<std::string> &symbol={},
		   const std::vector<unsigned char> &lmax={},
		   const std::vector<double> &exp={},
		   const std::vector<int> &exprn={});

  // Insert literature references into the database from a bibtex file
  void insert_litref_bibtex(std::ostream &os, const std::list<std::string> &tokens);

  // Insert additional info from an INSERT SET command (xyz and POSCAR keywords)
  void insert_set_xyz(std::ostream &os, const std::string &key, const std::unordered_map<std::string,std::string> &kmap);

  // Insert additional info from an INSERT SET command (din keyword)
  void insert_set_din(std::ostream &os, const std::string &key, const std::unordered_map<std::string,std::string> &kmap);

  // Calculate energy differences from total energies
  void calc_ediff(std::ostream &os);

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

  // Read data from a file, and compare to the whole database data or
  // one of its subsets. If usetrain >= 0, assume the training set is
  // defined and compare to the whole training set.
  void read_and_compare(std::ostream &os, const std::unordered_map<std::string,std::string> &kmap,
			int usetrain=-1);

  // Write input files for a database set or the whole database. The
  // options go in map kmap. If the ACP is present, it is passed down to
  // the structure writer for keyword expansion. If smapin is present,
  // write only the structures that are keys in the map (the value of
  // the map is 0 if crystal or 1 if molecule). zat, lmax, exp, and coef
  // are used for loop expansion in the template. prefix = prefix the
  // file names with this string. os = output stream for verbose
  // notifications.
  void write_structures(std::ostream &os, const std::unordered_map<std::string,std::string> &kmap, const acp &a,
			const std::unordered_map<int,int> &smapin={},
			const std::vector<unsigned char> &zat={}, const std::vector<std::string> &symbol={},
			const std::vector<std::string> &termstring={}, const std::vector<unsigned char> &lmax={},
			const std::vector<double> &exp={}, const std::vector<int> &exprn={}, const std::vector<double> &coef={},
			const std::string &prefix="");

  // Write the structures with IDs given by the keys in smap. The
  // values of smap should be 1 if the structures are molecules or
  // zero if they are crystals. Use template_m and template_c as
  // templates for molecules and crystals. Use ext_m and ext_c as file
  // extensions for molecules and crystals. dir: output
  // directory. npack = package and compress in packets of npack files
  // (0 = no packing). prefix = prefix the file names with this
  // string. os is the output stream for verbose notifications. For
  // the template expansion, use the information in ACP a. For the
  // loop expansion, use the list of atomic IDs, atomic numbers (zat),
  // symbols (symbol), angular
  // momenta (l), exponents (exp), and coefficients (coef). Rename = 0
  // (do not rename), = 1 (write an extended name with atom, l, and
  // exponent info in it), = 2 (with atom, l, exponent, coef).
  void write_many_structures(std::ostream &os,
			     const std::string &template_m, const std::string &template_c,
			     const std::string &ext_m, const std::string &ext_c,
			     const acp &a,
			     const std::unordered_map<int,int> &smap,
			     const std::vector<int> &atid,
			     const std::vector<unsigned char> &zat,
			     const std::vector<std::string> &symbol, const std::vector<std::string> &termstring,
			     const std::vector<unsigned char> &l,
			     const std::vector<double> &exp, const std::vector<int> &exprn, const std::vector<double> &coef,
			     const int rename,
			     const std::string &dir="./", int npack=0,
			     const std::string &prefix="");

  // Write one structure to output directory dir with name prefix
  // prefix and extension ext. Rename = 0 (do not rename), = 1 (write
  // an extended name with atom, l, and exponent info in it), = 2
  // (with atom, l, exponent, coef). If the run is verbose, write a
  // note to stream os. The structure written has database ID equal to
  // id. Use template in tmpl. For the keyword expansion, use the
  // information in the ACP (a), atomic ID (atid), atomic number (zat), angular
  // momentum (l), exponent (exp), exponent ID (iexp), and coefficient
  // (coef).
  std::string write_one_structure(std::ostream &os, int id, const strtemplate &tmpl,
				  const std::string &ext, const acp& a, int atid,
				  const unsigned char zat, const std::string &symbol, const std::string &termstring, 
				  const unsigned char l, const double exp, const int exprn, const int iexp,
				  const double coef, const int icoef, const int rename,
				  const std::string &dir="./",
				  const std::string &prefix="");

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
  void begin_transaction(){
    statement st(db,"BEGIN TRANSACTION;");
    st.execute();
  }
  // Commit a transaction
  void commit_transaction(){
    statement st(db,"COMMIT TRANSACTION;");
    st.execute();
  }
  // Rollback a transaction
  void rollback_transaction(){
    statement st(db,"ROLLBACK TRANSACTION;");
    st.execute();
  }

  // Return a pointer to the database
  sqlite3 *ptr() { return db; }

 private:

  // database info
  std::string dbfilename;
  sqlite3 *db;
};

#endif
