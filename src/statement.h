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
	     STMT_ROLLBACK_TRANSACTION = 4, // rollback a transaction
	     STMT_CHECK_DATABASE = 5, // create the database
	     STMT_QUERY_PROPTYPE = 6, // query propety types
	     STMT_LIST_LITREF = 7, // list literature references
	     STMT_DELETE_LITREF_ALL = 8, // delete literature references, all
	     STMT_DELETE_LITREF_WITH_KEY = 9, // delete literature references, with key
	     STMT_DELETE_LITREF_WITH_ID = 10, // delete literature references, with id
	     STMT_INSERT_LITREF = 11, // insert literature references
	     STMT_QUERY_LITREF = 12, // query literature references
	     STMT_LIST_SET = 13, // list sets
	     STMT_DELETE_SET_ALL = 14, // delete sets, all
	     STMT_DELETE_SET_WITH_KEY = 15, // delete sets, with key
	     STMT_DELETE_SET_WITH_ID = 16, // delete sets, with id
	     STMT_INSERT_SET = 17, // insert sets
	     STMT_QUERY_SET = 18, // query sets
	     STMT_LIST_METHOD = 19, // list methods
	     STMT_DELETE_METHOD_ALL = 20, // delete methods, all
	     STMT_DELETE_METHOD_WITH_KEY = 21, // delete methods, with key
	     STMT_DELETE_METHOD_WITH_ID = 22, // delete methods, with id
	     STMT_INSERT_METHOD = 23, // insert methods
	     STMT_QUERY_METHOD = 24, // query methods
	     STMT_LIST_STRUCTURE = 25, // list structures
	     STMT_DELETE_STRUCTURE_ALL = 26, // delete structures, all
	     STMT_DELETE_STRUCTURE_WITH_KEY = 27, // delete structures, with key
	     STMT_DELETE_STRUCTURE_WITH_ID = 28, // delete structures, with id
	     STMT_INSERT_STRUCTURE = 29, // insert structures
	     STMT_QUERY_STRUCTURE = 30, // query structures
	     STMT_LIST_PROPERTY = 31, // list properties
	     STMT_DELETE_PROPERTY_ALL = 32, // delete properties, all
	     STMT_DELETE_PROPERTY_WITH_KEY = 33, // delete properties, with key
	     STMT_DELETE_PROPERTY_WITH_ID = 34, // delete properties, with id
	     STMT_INSERT_PROPERTY = 35, // insert properties
	     STMT_QUERY_PROPERTY = 36, // query properties
	     STMT_LIST_EVALUATION = 37, // list evaluations
	     STMT_DELETE_EVALUATION_ALL = 38, // delete evaluations, all
	     STMT_INSERT_EVALUATION = 39, // insert evaluations
	     STMT_LIST_TERM = 40, // list terms
	     STMT_DELETE_TERM_ALL = 41, // delete terms, all
	     STMT_INSERT_TERM = 42, // insert terms
  };
  static const int number_stmt_types = 43; // number of statement types

  //// Operators ////

  // constructors
  // default and parametrized constructor
  statement(sqlite3 *db_ = nullptr, const stmttype type_ = STMT_CUSTOM, std::string text_ = "");
  statement(statement&& rhs) = delete; // move constructor
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

  // Recycle a statement in the same database
  void recycle(const stmttype type_ = STMT_CUSTOM, std::string text_ = "");

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

    if (!prepared) {
      prepare();
      reset();
    }
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
template<> struct statement::bind_dispatcher< int, int > {
  static int impl(sqlite3_stmt *stmt, const int col, const int arg, bool transient, int nbytes){ 
    return sqlite3_bind_int(stmt,col,arg);}};

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

template<> struct statement::bind_dispatcher< char *, double > {
  static int impl(sqlite3_stmt *stmt, const char *col, const double arg, bool transient, int nbytes){ 
    return sqlite3_bind_double(stmt,sqlite3_bind_parameter_index(stmt,col),arg);}};

template<> struct statement::bind_dispatcher< char *, void * > {
  static int impl(sqlite3_stmt *stmt, const char *col, const void *arg, bool transient, int nbytes){ 
    return sqlite3_bind_blob(stmt,sqlite3_bind_parameter_index(stmt,col),arg,nbytes,transient?SQLITE_TRANSIENT:SQLITE_STATIC);}};

#endif

