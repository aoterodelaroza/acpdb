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

#include "statement.h"
#include <cstddef>

static void throw_exception(sqlite3 *db_){
  std::string errmsg = "database error - " + std::string(sqlite3_errmsg(db_));
  throw std::runtime_error(errmsg);
}

// Recycle a statement in the same database
void statement::recycle(std::string text_/* = ""*/){
  finalize();
  text = text_;
  stmt = nullptr;
}

// Execute a statment directly
int statement::execute(){
  if (!db)
    throw std::runtime_error("A database file must be connected before executing a statement");

  char *errmsg = nullptr;
  int rc = sqlite3_exec(db, text.c_str(), NULL, NULL, &errmsg);
  if (rc){
    std::string errmsg_s;
    if (errmsg)
      errmsg_s = "Error (" + std::string(errmsg) + ")";
    else
      errmsg_s = "Error";
    sqlite3_free(errmsg);
    throw std::runtime_error(errmsg_s);
  }
  return rc;
}

// Step an statement. Prepare the statement if it was not already
// prepared. Reset the statement at the end if it is done.
int statement::step(){
  if (!db)
    throw std::runtime_error("A database file must be connected before stepping a statement");

  if (!prepared)
    prepare();

  int rc = sqlite3_step(stmt);
  if (rc == SQLITE_DONE)
    reset();
  else if (rc != SQLITE_ROW)
    throw_exception(db);

  return rc;
}

// Finalize the statement
void statement::finalize(){
  if (db && stmt){
    if (prepared && sqlite3_finalize(stmt))
      throw_exception(db);
  }
  prepared = false;
  stmt = nullptr;
  has_bind = false;
  text.clear();
}

// Reset the statement and clear all bindings
void statement::reset(){
  if (!db)
    throw std::runtime_error("A database file must be connected before resetting a statement");

  if (prepared){
    if (sqlite3_reset(stmt))
      throw_exception(db);
    if (has_bind && sqlite3_clear_bindings(stmt))
      throw_exception(db);
  }
}

// Prepare the statement and record whether the statement has bindings.
void statement::prepare(){
  if (!db)
    throw std::runtime_error("A database file must be connected before preparing a statement");

  int rc = 0;
  rc = sqlite3_prepare_v2(db, text.c_str(), -1, &stmt, NULL);
  if (rc) throw_exception(db);

  rc = sqlite3_bind_parameter_count(stmt);
  has_bind = (rc>0);

  prepared = true;
}
