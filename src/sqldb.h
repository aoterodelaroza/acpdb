// -*- c++-mode -*-

#ifndef SQLDB_H
#define SQLDB_H

#include <string>
#include <list>
#include "sqlite3.h"

class sqldb {

 public:

  // enum: output of checksane()
  enum dbstatus { dbstatus_sane = 0, dbstatus_empty = 1, dbstatus_error = 2 };

  // constructors
  sqldb() : db(nullptr) {}; // default constructor
  sqldb(std::string file) : db(nullptr) { 
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

  // Check if the DB is sane, empty, or not sane.
  dbstatus checksane();

  // Open a database file for use. The file name is the first token in
  // the list or the string.
  void connect(const std::string filename, int flags = SQLITE_OPEN_READWRITE);
  void connect(std::list<std::string> &tokens, int flags = SQLITE_OPEN_READWRITE);

  // Create the database skeleton.
  void create();

  // Close a database connection if open and reset the pointer to NULL
  void close();

 private:

  std::string dbfilename;
  sqlite3 *db;

};

// istream& istream::operator>> (istream& in, db this)
// ostream& ostream::operator<< (ostream& out, db this)

#endif

