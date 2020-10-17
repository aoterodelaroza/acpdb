#include "sqldb.h"
#include <iostream>
#include <string>
#include <stdexcept>

void sqldb::create(std::list<std::string> &tokens){

  // Check that we have a first token
  if (tokens.empty())
    throw std::runtime_error("Need a file name for the database");

  // Get the file name
  std::string file = tokens.front();
  tokens.pop_front();
  dbfilename = file;

  // Try to open the filename
  if (sqlite3_open(file.c_str(), &db)) {
    std::string errmsg(sqlite3_errmsg(db));
    close();
    throw std::runtime_error("Can't open database file " + file + " (" + errmsg + ")");
  }

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
    std::string errmsg_s(errmsg);
    sqlite3_free(errmsg);
    close();
    throw std::runtime_error("Could not create table (" + errmsg_s + ")");
  }
}

void sqldb::close(){
  if (sqlite3_close(db)) 
    throw std::runtime_error("Can't close database file " + dbfilename + " (" + sqlite3_errmsg(db) + ")");
  db = nullptr;
  dbfilename = "";
}
