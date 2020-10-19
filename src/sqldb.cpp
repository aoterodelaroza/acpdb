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

#include "sqldb.h"
#include "parseutils.h"
#include <stdexcept>

// Check if the DB is sane, empty, or not sane.
sqldb::dbstatus sqldb::checksane(bool except_on_error, bool except_on_empty){
  int icol;
  const char *check_statement = "SELECT COUNT(type) FROM sqlite_master WHERE type='table' AND name='Literature_refs';";
  sqlite3_stmt *statement = nullptr;

  if (!db) goto error;

  if (sqlite3_prepare(db, check_statement, -1, &statement, NULL)) goto error;
  if (sqlite3_step(statement) != SQLITE_ROW) goto error;

  icol = sqlite3_column_int(statement, 0);
  if (icol == 0) goto empty;

  if (sqlite3_finalize(statement)) goto error;

  return dbstatus_sane;

 error:
  if (statement) sqlite3_finalize(statement);
  if (except_on_error) throw std::runtime_error("Error reading connected database");
  return dbstatus_error;

 empty:
  if (statement) sqlite3_finalize(statement);
  if (except_on_empty) throw std::runtime_error("Empty database");
  return dbstatus_empty;
}

// Open a database file for use. 
void sqldb::connect(const std::string filename, int flags){
  // close the previous db if open
  close();

  // check if the string is empty
  if (filename.empty())
    throw std::runtime_error("Need a database file name");

  // open the new one
  if (sqlite3_open_v2(filename.c_str(), &db, flags, NULL)) {
    std::string errmsg = "Can't connect to database file " + filename + " (" + std::string(sqlite3_errmsg(db)) + ")";
    close();
    throw std::runtime_error(errmsg);
  }

  // write down the file name
  dbfilename = filename;
}

// Create the database skeleton.
void sqldb::create(){
  // skip if not open
  if (!db) return;

  // SQL statment for creating the database table
  const char *create_statement = R"SQL(
CREATE TABLE Literature_refs (
  id          INTEGER PRIMARY KEY NOT NULL,
  ref_key     TEXT NOT NULL,
  authors     TEXT,
  title       TEXT,
  journal     TEXT,
  volume      TEXT,
  page        TEXT,
  year        TEXT,
  doi         TEXT UNIQUE,
  description TEXT
);
)SQL";

  // Create the table 
  char *errmsg;
  if (sqlite3_exec(db, create_statement, NULL, NULL, &errmsg)){
    std::string errmsg_s = "Could not create table (" + std::string(errmsg) + ")";
    sqlite3_free(errmsg);
    close();
    throw std::runtime_error(errmsg_s);
  }
}

// Close a database connection if open and reset the pointer to NULL
void sqldb::close(){
  if (!db) return;
  if (sqlite3_close_v2(db)) 
    throw std::runtime_error("Can't close database file " + dbfilename + " (" + sqlite3_errmsg(db) + ")");
  db = nullptr;
  dbfilename = "";
}

// Insert an item into the database
void sqldb::insert(const std::string category, const std::string key, const std::map<std::string,std::string> &kmap){
  printf("hello!\n");
}
