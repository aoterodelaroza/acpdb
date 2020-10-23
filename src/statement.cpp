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

//// List of SQL statements ////
static const char *statement_text[statement::number_stmt_types] = {
[statement::STMT_CREATE_DATABASE] = 
R"SQL(
CREATE TABLE Literature_refs (
  id          INTEGER PRIMARY KEY NOT NULL,
  key         TEXT UNIQUE NOT NULL,
  authors     TEXT,
  title       TEXT,
  journal     TEXT,
  volume      TEXT,
  page        TEXT,
  year        TEXT,
  doi         TEXT UNIQUE,
  description TEXT
);
CREATE TABLE Property_types (
  id          INTEGER PRIMARY KEY NOT NULL,
  key         TEXT UNIQUE NOT NULL,
  description TEXT
);
CREATE TABLE Sets (
  id            INTEGER PRIMARY KEY NOT NULL,
  key           TEXT UNIQUE NOT NULL,
  property_type INTEGER NOT NULL,
  nstructures   INTEGER NOT NULL,
  nproperties   INTEGER NOT NULL,
  litrefs       TEXT,
  description   TEXT,
  FOREIGN KEY(property_type) REFERENCES Property_types(id)
);
CREATE TABLE Methods (
  id            INTEGER PRIMARY KEY NOT NULL,
  key           TEXT UNIQUE NOT NULL,
  comp_details  TEXT,
  litrefs       TEXT,
  description   TEXT
);
INSERT INTO Property_types (key,description)
       VALUES ('energy_difference','A difference of molecular or crystal energies (reaction energy, binding energy, lattice energy, etc.)'),
              ('energy','The total energy of a molecule or crystal');
)SQL",

[statement::STMT_INIT_DATABASE] = 
"PRAGMA foreign_keys = ON;", 

[statement::STMT_BEGIN_TRANSACTION] =
"BEGIN TRANSACTION;",

[statement::STMT_COMMIT_TRANSACTION] =
"COMMIT TRANSACTION;",

[statement::STMT_CHECK_DATABASE] = 
R"SQL(
SELECT COUNT(type)
FROM sqlite_master
WHERE type='table' AND name='Literature_refs';
)SQL",

[statement::STMT_QUERY_PROPTYPE] = 
R"SQL(
SELECT id
FROM Property_types
WHERE key = ?1;
)SQL",

[statement::STMT_LIST_LITREF] = 
R"SQL(
SELECT id,key,authors,title,journal,volume,page,year,doi,description
FROM Literature_refs;
)SQL",

[statement::STMT_DELETE_LITREF_ALL] = 
"DELETE FROM Literature_refs;",

[statement::STMT_DELETE_LITREF_WITH_KEY] = 
R"SQL(
DELETE FROM Literature_refs
WHERE key = ?1;
)SQL",

[statement::STMT_DELETE_LITREF_WITH_ID] =
R"SQL(
DELETE FROM Literature_refs
WHERE id = ?1;
)SQL",

[statement::STMT_INSERT_LITREF] =
R"SQL(
INSERT INTO Literature_refs (key,authors,title,journal,volume,page,year,doi,description)
       VALUES(:KEY,:AUTHORS,:TITLE,:JOURNAL,:VOLUME,:PAGE,:YEAR,:DOI,:DESCRIPTION);
)SQL",

[statement::STMT_QUERY_LITREF] = 
R"SQL(
SELECT id
FROM Literature_refs
WHERE key = ?1;
)SQL",

[statement::STMT_LIST_SET] = 
R"SQL(
SELECT id,key,property_type,nstructures,nproperties,litrefs,description
FROM Sets;
)SQL",

[statement::STMT_DELETE_SET_ALL] = 
"DELETE FROM Sets;",

[statement::STMT_DELETE_SET_WITH_KEY] = 
R"SQL(
DELETE FROM Sets
WHERE key = ?1;
)SQL",

[statement::STMT_DELETE_SET_WITH_ID] =
R"SQL(
DELETE FROM Sets
WHERE id = ?1;
)SQL",

[statement::STMT_INSERT_SET] =
R"SQL(
INSERT INTO Sets (key,property_type,nstructures,nproperties,litrefs,description)
       VALUES(:KEY,:PROPERTY_TYPE,:NSTRUCTURES,:NPROPERTIES,:LITREFS,:DESCRIPTION);
)SQL",

};
//// END of list of SQL statements ////

// whether the statement has bindings
static const bool has_bindings[statement::number_stmt_types] = {
  [statement::STMT_CREATE_DATABASE] = false,
  [statement::STMT_INIT_DATABASE] = false,
  [statement::STMT_BEGIN_TRANSACTION] = false,
  [statement::STMT_COMMIT_TRANSACTION] = false,
  [statement::STMT_CHECK_DATABASE] = false,
  [statement::STMT_QUERY_PROPTYPE] = true,
  [statement::STMT_LIST_LITREF] = false,
  [statement::STMT_DELETE_LITREF_ALL] = false,
  [statement::STMT_DELETE_LITREF_WITH_KEY] = true,
  [statement::STMT_DELETE_LITREF_WITH_ID] = true,
  [statement::STMT_INSERT_LITREF] = true,
  [statement::STMT_QUERY_LITREF] = true,
  [statement::STMT_LIST_SET] = false,
  [statement::STMT_DELETE_SET_ALL] = false,
  [statement::STMT_DELETE_SET_WITH_KEY] = true,
  [statement::STMT_DELETE_SET_WITH_ID] = true,
  [statement::STMT_INSERT_SET] = true,
};

static void throw_exception(sqlite3 *db_){
  std::string errmsg = "database error - " + std::string(sqlite3_errmsg(db_));
  throw std::runtime_error(errmsg);
}

int statement::execute(){
  if (!db)
    throw std::runtime_error("A database file must be connected before executing a statement");

  char *errmsg = nullptr;
  int rc = sqlite3_exec(db, statement_text[type], NULL, NULL, &errmsg);
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

int statement::step(){
  if (!db)
    throw std::runtime_error("A database file must be connected before stepping a statement");
  if (type == STMT_NONE)
    throw std::runtime_error("Cannot step a NONE statement");
  
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
  if (db && type != STMT_NONE && stmt){
    if (sqlite3_finalize(stmt)) 
      throw_exception(db);
  }
  prepared = false;
  stmt = nullptr;
}

// Reset the statement and clear all bindings
void statement::reset(){
  if (!db)
    throw std::runtime_error("A database file must be connected before resetting a statement");
  if (type == STMT_NONE)
    throw std::runtime_error("Cannot reset a NONE statement");

  if (sqlite3_reset(stmt))
    throw_exception(db);
  if (has_bindings[type] && sqlite3_clear_bindings(stmt))
    throw_exception(db);
}

// Prepare the statement.
void statement::prepare(){
  if (!db)
    throw std::runtime_error("A database file must be connected before preparing a statement");

  int rc = 0;
  if (type == STMT_NONE)
    stmt = nullptr;
  else
    rc = sqlite3_prepare_v2(db, statement_text[type], -1, &stmt, NULL);

  if (rc) throw_exception(db);

  if (type != STMT_NONE) prepared = true;
}

