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
#include <string>
#include <stdexcept>

static const char *statement_text[statement::number_stmt_types] = {
[statement::STMT_CREATE_DATABASE] = 
R"SQL(
CREATE TABLE Literature_refs (
  id          INTEGER PRIMARY KEY NOT NULL,
  ref_key     TEXT UNIQUE NOT NULL,
  authors     TEXT,
  title       TEXT,
  journal     TEXT,
  volume      TEXT,
  page        TEXT,
  year        TEXT,
  doi         TEXT UNIQUE,
  description TEXT
);
)SQL"
};

static void throw_exception(sqlite3 *db_){
  std::string errmsg = "Error bleh: " + std::string(sqlite3_errmsg(db_));
  throw std::runtime_error(errmsg);
}

int statement::execute(bool except){
  if (!db)
    throw std::runtime_error("Invalid database executing statement");

  char *errmsg;
  int rc = sqlite3_exec(db, statement_text[type], NULL, NULL, &errmsg);
  if (except && rc){
    std::string errmsg_s = "Error (" + std::string(errmsg) + ")";
    sqlite3_free(errmsg);
    throw std::runtime_error(errmsg_s);
  }
  return rc;
}

void statement::prepare(sqlite3 *db_, const stmttype type_){
  if (!db_)
    throw std::runtime_error("Invalid database preparing statement");

  int rc = 0;
  if (type_ == STMT_NONE)
    stmt = nullptr;
  else
    rc = sqlite3_prepare_v2(db_, statement_text[type_], -1, &stmt, NULL);

  if (rc) throw_exception(db_);

  type = type_;
  db = db_;
  if (type_ != STMT_NONE) prepared = true;
}

void statement::finalize(){
  if (db && type != STMT_NONE && stmt){
    if (sqlite3_finalize(stmt)) 
      throw_exception(db);
  }
  prepared = false;
  type = STMT_NONE;
  stmt = nullptr;
}
