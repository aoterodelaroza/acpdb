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

// Check that the DB is sane (at least a little bit)
int sqldb::checkdb(){
  if (!db) return 1;

  int rc;
  const char *check_statement = "SELECT COUNT(type) FROM sqlite_master WHERE type='table' AND name='Literature_refs';";
  sqlite3_stmt *statement;

  rc = sqlite3_prepare(db, check_statement, -1, &statement, NULL);
  if (rc) return rc;

  rc = sqlite3_step(statement);
  if (rc != SQLITE_ROW) return 1;

  rc = sqlite3_column_int(statement, 0);
  if (rc != 1) return 1;

  rc = sqlite3_finalize(statement);
  if (rc) return rc;

  return 0;
}

// connect to DB file filename
void sqldb::connect(const std::string &filename, int flags){
  if (sqlite3_open_v2(filename.c_str(), &db, flags, NULL)) {
    std::string errmsg = "Can't connect to database file " + filename + " (" + std::string(sqlite3_errmsg(db)) + ")";
    close();
    throw std::runtime_error(errmsg);
  }
  if (checkdb()){
    throw std::runtime_error("Error reading database " + filename);
  }
}

// connect to the first token in the list; remove that token from the list
void sqldb::connect(std::list<std::string> &tokens, int flags){
  dbfilename = get_front_token(tokens);
  connect(dbfilename);
}

// create a DB file in filename
void sqldb::create(const std::string &filename){
  // Connect to this file
  connect(filename, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);

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

// create a table in the first token in the list; remove that token from the list
void sqldb::create(std::list<std::string> &tokens){
  dbfilename = get_front_token(tokens);
  create(dbfilename);
}

// close a db connection
void sqldb::close(){
  if (sqlite3_close(db)) 
    throw std::runtime_error("Can't close database file " + dbfilename + " (" + sqlite3_errmsg(db) + ")");
  db = nullptr;
  dbfilename = "";
}
