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

  // Check that the DB is sane (at least a little bit)
  int checkdb();

  // Create a database file and open it for use. The file name is the
  // first token in the list or the string.
  void create(std::list<std::string> &tokens);
  void create(const std::string &filename);

  // Open a database file for use. The file name is the first token in
  // the list or the string.
  void connect(const std::string &filename, int flags = SQLITE_OPEN_READWRITE);
  void connect(std::list<std::string> &tokens, int flags = SQLITE_OPEN_READWRITE);

  // Close a database connection and reset the pointer to NULL
  void close();

 private:

  std::string dbfilename;
  sqlite3 *db;

};

// istream& istream::operator>> (istream& in, db this)
// ostream& ostream::operator<< (ostream& out, db this)

#endif

