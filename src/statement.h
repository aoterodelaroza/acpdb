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

  static const int number_stmt_types = 1; // number of statement types

  // enum: type of statement()
  enum stmttype { 
	     STMT_NONE = -1, // no statement
	     STMT_CREATE_DATABASE = 0, // create the database
  };

  // constructors
  statement(sqlite3 *db_ = nullptr, const stmttype type_ = STMT_NONE) : 
    prepared(false), db(db_), type(type_),
    stmt(nullptr) {}; // default and parametrized constructor
  statement(statement&& rhs) = delete; // move constructor (deleted)
  statement(const statement& rhs) = delete; // copy constructor (deleted)

  // destructors
  ~statement() {
    finalize();
  };

  // bool operator
  operator bool() const { return stmt; }

  // Execute a statment directly. If except, throw exception on
  // error. Otherwise, return sqlite3 exit code.
  int execute(bool except = true);

 private:

  // Prepare a statement of type type_ in database db_
  void prepare(sqlite3 *db_, const stmttype type_);

  // Finalize the statement
  void finalize();

  bool prepared; // whether the statement has been prepared
  sqlite3 *db; // the database pointer
  stmttype type; // statement type
  sqlite3_stmt *stmt; // statement pointer

};

#endif

