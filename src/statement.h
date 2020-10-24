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
#include <string>
#include <stdexcept>

// A SQLite3 statement class.
class statement {

 public:

  //// Named constants ////

  // enum: type of statement()
  enum stmttype { 
	     STMT_CUSTOM = -1, // no statement
	     STMT_CREATE_DATABASE = 0, // create the database
	     STMT_INIT_DATABASE = 1, // initialize the database
	     STMT_BEGIN_TRANSACTION = 2, // begin a transaction
	     STMT_COMMIT_TRANSACTION = 3, // commit a transaction
	     STMT_CHECK_DATABASE = 4, // create the database
	     STMT_QUERY_PROPTYPE = 5, // query propety types
	     STMT_LIST_LITREF = 6, // list literature references
	     STMT_DELETE_LITREF_ALL = 7, // delete literature references, all
	     STMT_DELETE_LITREF_WITH_KEY = 8, // delete literature references, with key
	     STMT_DELETE_LITREF_WITH_ID = 9, // delete literature references, with id
	     STMT_INSERT_LITREF = 10, // insert literature references
	     STMT_QUERY_LITREF = 11, // query literature references
	     STMT_LIST_SET = 12, // list sets
	     STMT_DELETE_SET_ALL = 13, // delete sets, all
	     STMT_DELETE_SET_WITH_KEY = 14, // delete sets, with key
	     STMT_DELETE_SET_WITH_ID = 15, // delete sets, with id
	     STMT_INSERT_SET = 16, // insert sets
	     STMT_QUERY_SET = 17, // query sets
	     STMT_LIST_METHOD = 18, // list sets
	     STMT_DELETE_METHOD_ALL = 19, // delete sets, all
	     STMT_DELETE_METHOD_WITH_KEY = 20, // delete sets, with key
	     STMT_DELETE_METHOD_WITH_ID = 21, // delete sets, with id
	     STMT_INSERT_METHOD = 22, // insert sets
	     STMT_LIST_STRUCTURE = 23, // list structures
	     STMT_DELETE_STRUCTURE_ALL = 24, // delete structures, all
	     STMT_DELETE_STRUCTURE_WITH_KEY = 25, // delete structures, with key
	     STMT_DELETE_STRUCTURE_WITH_ID = 26, // delete structures, with id
	     STMT_INSERT_STRUCTURE = 27, // insert structures
	     STMT_QUERY_STRUCTURE = 28, // query propety types
	     STMT_LIST_PROPERTY = 29, // list structures
	     STMT_DELETE_PROPERTY_ALL = 30, // delete structures, all
	     STMT_DELETE_PROPERTY_WITH_KEY = 31, // delete structures, with key
	     STMT_DELETE_PROPERTY_WITH_ID = 32, // delete structures, with id
	     STMT_INSERT_PROPERTY = 33, // insert structures
	     STMT_LIST_EVALUATION = 34, // list structures
	     STMT_DELETE_EVALUATION_ALL = 35, // delete structures, all
	     STMT_DELETE_EVALUATION_WITH_KEY = 36, // delete structures, with key
	     STMT_DELETE_EVALUATION_WITH_ID = 37, // delete structures, with id
	     STMT_INSERT_EVALUATION = 38, // insert structures
  };
  static const int number_stmt_types = 39; // number of statement types

  //// Operators ////

  // constructors
  // default and parametrized constructor
  statement(sqlite3 *db_ = nullptr, const stmttype type_ = STMT_CUSTOM, std::string text_ = "");
  statement(statement&& rhs) = delete; // move constructor (deleted)
  statement(const statement& rhs) = delete; // copy constructor (deleted)

  // destructors
  ~statement() {
    finalize();
    type = STMT_CUSTOM;
    text = "";
  };

  // bool operator
  operator bool() const { return stmt; }

  //// Public methods ////

  // Execute a statment directly
  int execute();

  // Step an statement. Prepare the statement if it was not already
  // prepared. Reset the statement at the end if it is done.
  int step();

  // Get the pointer to the statement
  sqlite3_stmt *ptr() const { return stmt; }

  // Finalize the statement
  void finalize();

  // Reset the statement and clear all bindings
  void reset();

  // Prepare the statement and record whether the statement has bindings.
  void prepare();

  //// Public template functions ////

  // Bind arguments to the parameters of the statement
  template<typename Tcol, typename Targ>
  int bind(const Tcol &col, const Targ &arg, const bool transient = true, int nbytes = 0){
    if (!db)
      throw std::runtime_error("A database file must be connected before binding");

    if (!prepared) prepare();
    if (!has_bind)
      throw std::runtime_error("bind error - no bindings in this statement");

    int rc = 0;
    rc = bind_dispatcher<Tcol,Targ>::impl(stmt,col,arg,transient,nbytes);

    if (rc)
      throw std::runtime_error("bind error - " + std::string(sqlite3_errmsg(db)));

    return rc;
  }

 private:

  //// Private variables ////

  bool prepared; // whether the statement has been prepared
  bool has_bind; // whether the statement has bindings
  sqlite3 *db; // the database pointer
  stmttype type; // statement type
  sqlite3_stmt *stmt; // statement pointer
  std::string text; // text of a custom statement

  //// Template function code ////
  template<typename Tcol, typename Targ> struct bind_dispatcher; // bind dispatcher, for generic bind selection
};

// bind dispatcher template specializations
template<> struct statement::bind_dispatcher< int, std::string > {
  static int impl(sqlite3_stmt *stmt, const int col, const std::string &arg, bool transient, int nbytes){ 
    return sqlite3_bind_text(stmt,col,arg.c_str(),-1,transient?SQLITE_TRANSIENT:SQLITE_STATIC);}};

template<> struct statement::bind_dispatcher< std::string, std::string > {
  static int impl(sqlite3_stmt *stmt, const std::string &col, const std::string &arg, bool transient, int nbytes){ 
    return sqlite3_bind_text(stmt,sqlite3_bind_parameter_index(stmt,col.c_str()),arg.c_str(),-1,transient?SQLITE_TRANSIENT:SQLITE_STATIC);}};

template<> struct statement::bind_dispatcher< char *, std::string > {
  static int impl(sqlite3_stmt *stmt, const char *col, const std::string &arg, bool transient, int nbytes){ 
    return sqlite3_bind_text(stmt,sqlite3_bind_parameter_index(stmt,col),arg.c_str(),-1,transient?SQLITE_TRANSIENT:SQLITE_STATIC);}};

template<> struct statement::bind_dispatcher< char *, char * > {
  static int impl(sqlite3_stmt *stmt, const char *col, const char *arg, bool transient, int nbytes){ 
    return sqlite3_bind_text(stmt,sqlite3_bind_parameter_index(stmt,col),arg,-1,transient?SQLITE_TRANSIENT:SQLITE_STATIC);}};

template<> struct statement::bind_dispatcher< std::string, int > {
  static int impl(sqlite3_stmt *stmt, const std::string &col, const int arg, bool transient, int nbytes){ 
    return sqlite3_bind_int(stmt,sqlite3_bind_parameter_index(stmt,col.c_str()),arg);}};

template<> struct statement::bind_dispatcher< char *, int > {
  static int impl(sqlite3_stmt *stmt, const char *col, const int arg, bool transient, int nbytes){ 
    return sqlite3_bind_int(stmt,sqlite3_bind_parameter_index(stmt,col),arg);}};

template<> struct statement::bind_dispatcher< char *, void * > {
  static int impl(sqlite3_stmt *stmt, const char *col, const void *arg, bool transient, int nbytes){ 
    return sqlite3_bind_blob(stmt,sqlite3_bind_parameter_index(stmt,col),arg,nbytes,transient?SQLITE_TRANSIENT:SQLITE_STATIC);}};

#endif

