#include "sqldb.h"
#include <iostream>
#include <string>
#include <stdexcept>

// Get the front string from the list of tokens. Remove that string from the list.
static std::string get_front_token(std::list<std::string> &tokens){
  // Check that we have a first token
  if (tokens.empty())
    throw std::runtime_error("Need a file name for the database");

  // Get the file name
  std::string result = tokens.front();
  tokens.pop_front();
  return result;
}

// Check if the DB is sane, empty, or not sane.
sqldb::dbstatus sqldb::checksane(){
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
  return dbstatus_error;

 empty:
  if (statement) sqlite3_finalize(statement);
  return dbstatus_empty;
}

// Open a database file for use. 
void sqldb::connect(const std::string filename, int flags){
  
  // close the previous db if open
  close();

  // open the new one
  if (sqlite3_open_v2(filename.c_str(), &db, flags, NULL)) {
    std::string errmsg = "Can't connect to database file " + filename + " (" + std::string(sqlite3_errmsg(db)) + ")";
    close();
    throw std::runtime_error(errmsg);
  }

  // write down the file name
  dbfilename = filename;
}

// Open a database file for use using the first token from the list.
void sqldb::connect(std::list<std::string> &tokens, int flags){
  connect(get_front_token(tokens),flags);
}

// Create the database skeleton.
void sqldb::create(){
  // skip if not open
  if (!db) return;

  // SQL statment for creating the database table
  const char *create_statement = R"SQL(
CREATE TABLE Literature_refs (
  ref_key     TEXT    NOT NULL UNIQUE ,
  authors     TEXT    ,
  title       TEXT    ,
  journal     TEXT    ,
  volume      TEXT    ,
  page        TEXT    ,
  year        TEXT    ,
  doi         TEXT    UNIQUE ,
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
