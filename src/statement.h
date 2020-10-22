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

#ifndef STATEMENT_H
#define STATEMENT_H

#include "sqlite3.h"

// A SQLite3 statement class.
class statement {

 public:

  // enum: type of statement()
  enum stmttype { 
	     STMT_NONE = -1, // no statement
	     STMT_CREATE_DATABASE = 0, // create the database
	     STMT_BEGIN_TRANSACTION = 1, // begin a transaction
	     STMT_COMMIT_TRANSACTION = 2, // commit a transaction
	     STMT_CHECK_DATABASE = 3, // create the database
	     STMT_LIST_LITREF = 4, // list literature references
  };
  static const int number_stmt_types = 5; // number of statement types

  // constructors
  statement(sqlite3 *db_ = nullptr, const stmttype type_ = STMT_NONE) : 
    prepared(false), db(db_), type(type_),
    stmt(nullptr) {}; // default and parametrized constructor
  statement(statement&& rhs) = delete; // move constructor (deleted)
  statement(const statement& rhs) = delete; // copy constructor (deleted)

  // destructors
  ~statement() {
    finalize();
    type = STMT_NONE;
  };

  // bool operator
  operator bool() const { return stmt; }

  // Execute a statment directly. If except, throw exception on
  // error. Otherwise, return sqlite3 exit code.
  int execute(bool except = true);

  // Step an statement. Prepare the statement if it was not already
  // prepared. Reset the statement at the end if it is done. If
  // except, throw exception on error. Otherwise, return sqlite3 exit
  // code. If reset_, reset the statement and clear the bindings (if it
  // has any) before stepping.
  int step(bool except = true, bool reset_ = false);

  // Get the pointer to the statement
  sqlite3_stmt *ptr() { return stmt; }

  // Finalize the statement
  void finalize();

  // Reset the statement and clear all bindings
  void reset();

 private:

  // Prepare the statement.
  void prepare();

  bool prepared; // whether the statement has been prepared
  sqlite3 *db; // the database pointer
  stmttype type; // statement type
  sqlite3_stmt *stmt; // statement pointer

};

#endif

