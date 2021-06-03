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

  //// Operators ////

  // constructors
  // default and parametrized constructor
  statement(sqlite3 *db_ = nullptr, std::string text_ = ""):
    prepared(false), db(db_), stmt(nullptr), text(text_), has_bind(false) {};
  statement(statement&& rhs) = delete; // move constructor
  statement(const statement& rhs) = delete; // copy constructor (deleted)

  // destructors
  ~statement() {
    finalize();
    text = "";
  };

  // bool operator
  operator bool() const { return stmt; }

  //// Public methods ////

  // Recycle a statement in the same database
  void recycle(std::string text_ = "");

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

template<> struct statement::bind_dispatcher< int, double > {
  static int impl(sqlite3_stmt *stmt, const int col, const double arg, bool transient, int nbytes){
    return sqlite3_bind_double(stmt,col,arg);}};

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
