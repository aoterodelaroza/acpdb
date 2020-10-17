// -*- c++-mode -*-

#ifndef SQLDB_H
#define SQLDB_H

#include <string>
#include <list>
#include "sqlite3.h"

class sqldb {

 public:

  sqldb() : db(nullptr) {}; // default constructor
  // sqldb (const sqldb& rhs) {}; // copy constructor
  // ~sqldb() {}; // destructor
  // sqldb& operator= (const sqldb& rhs); // assignment operator (do not copy to self)
  // sqldb* operator& (); // address of operator
  // const sqldb* operator& () const {}; // address of operator (const)

  // Create a database file and open it for use. The file name is the first
  // token in the list
  void create(std::list<std::string> &tokens);

  // Close a database connection and reset the pointer to NULL
  void close();

 private:

  std::string dbfilename;
  sqlite3 *db;

};

// istream& istream::operator>> (istream& in, db this)
// ostream& ostream::operator<< (ostream& out, db this)

#endif

