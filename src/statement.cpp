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
)SQL",
[statement::STMT_BEGIN_TRANSACTION] =
"BEGIN TRANSACTION",
[statement::STMT_COMMIT_TRANSACTION] =
"COMMIT TRANSACTION",
[statement::STMT_CHECK_DATABASE] = 
R"SQL(
SELECT COUNT(type)
FROM sqlite_master
WHERE type='table' AND name='Literature_refs';
)SQL",
[statement::STMT_LIST_LITREF] = 
R"SQL(
SELECT id,ref_key,authors,title,journal,volume,page,year,doi,description
FROM Literature_refs
)SQL",
};

static const bool has_bindings[statement::number_stmt_types] = {
  [statement::STMT_CREATE_DATABASE] = false,
  [statement::STMT_BEGIN_TRANSACTION] = false,
  [statement::STMT_COMMIT_TRANSACTION] = false,
  [statement::STMT_CHECK_DATABASE] = false,
  [statement::STMT_LIST_LITREF] = false,
};

static void throw_exception(sqlite3 *db_){
  std::string errmsg = "Error bleh: " + std::string(sqlite3_errmsg(db_));
  throw std::runtime_error(errmsg);
}

int statement::execute(bool except){
  if (!db)
    throw std::runtime_error("Invalid database executing statement");

  char *errmsg = nullptr;
  int rc = sqlite3_exec(db, statement_text[type], NULL, NULL, &errmsg);
  if (except && rc){
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

int statement::step(bool except, bool reset_){
  if (!db)
    throw std::runtime_error("Invalid database stepping statement");
  if (type == STMT_NONE)
    throw std::runtime_error("Cannot step a NONE statement");
  
  if (!prepared)
    prepare();
  else if (reset_)
    reset();

  int rc = sqlite3_step(stmt);

  if (rc == SQLITE_DONE) 
    reset();
  else if (except && rc != SQLITE_ROW)
    throw_exception(db);

  return rc;
}

void statement::reset(){
  if (!db)
    throw std::runtime_error("Invalid database reset statement");
  if (type == STMT_NONE)
    throw std::runtime_error("Cannot reset a NONE statement");

  if (sqlite3_reset(stmt))
    throw_exception(db);
  if (has_bindings[type] && sqlite3_clear_bindings(stmt))
    throw_exception(db);
}

void statement::prepare(){
  if (!db)
    throw std::runtime_error("Invalid database preparing statement");

  int rc = 0;
  if (type == STMT_NONE)
    stmt = nullptr;
  else
    rc = sqlite3_prepare_v2(db, statement_text[type], -1, &stmt, NULL);

  if (rc) throw_exception(db);

  if (type != STMT_NONE) prepared = true;
}

void statement::finalize(){
  if (db && type != STMT_NONE && stmt){
    if (sqlite3_finalize(stmt)) 
      throw_exception(db);
  }
  prepared = false;
  stmt = nullptr;
}
