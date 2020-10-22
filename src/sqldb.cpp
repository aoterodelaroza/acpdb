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

#include <stdexcept>
#include <iostream>
#include <forward_list>
#include <string.h>
#include "sqldb.h"
#include "parseutils.h"
#include "statement.h"

#include "config.h"
#ifdef BTPARSE_FOUND  
#include "btparse.h"
#endif

void sqldb::allocate_statements(){
  if (!db)
    throw std::runtime_error("Tried to allocate statments but db not connected");

  for (int i = 0; i < statement::number_stmt_types; i++)
    stmt[i] = new statement(db,(statement::stmttype) i);
}

void sqldb::deallocate_statements(){
  if (!db) return;

  for (int i = 0; i < statement::number_stmt_types; i++){
    delete stmt[i];
    stmt[i] = nullptr;
  }
}

// Check if the DB is sane, empty, or not sane.
sqldb::dbstatus sqldb::checksane(bool except_on_error, bool except_on_empty){
  int icol;
  const char *check_statement = "SELECT COUNT(type) FROM sqlite_master WHERE type='table' AND name='Literature_refs';";
  sqlite3_stmt *statement = nullptr;

  if (!db) goto error;

  if (sqlite3_prepare_v2(db, check_statement, -1, &statement, NULL)) goto error;
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
void sqldb::connect(const std::string &filename, int flags){
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

  // prepare all statements
  allocate_statements();
}

// Create the database skeleton.
void sqldb::create(){
  // skip if not open
  if (!db) throw std::runtime_error("A db must be connected before using CREATE");

  // SQL statment for creating the database table
  const char *create_statement = R"SQL(
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

  // finalize and deallocate all statements
  deallocate_statements();

  // close the database
  if (sqlite3_close_v2(db)) 
    throw std::runtime_error("Can't close database file " + dbfilename + " (" + sqlite3_errmsg(db) + ")");
  db = nullptr;
  dbfilename = "";
}

// Insert an item into the database
void sqldb::insert(const std::string &category, const std::string &key, const std::unordered_map<std::string,std::string> &kmap) {
  if (!db) throw std::runtime_error("A db must be connected before using INSERT");

  // a generic sqlite3 statment, for preparing
  sqlite3_stmt *statement = nullptr;

  // check that the key is not empty
  if (key.empty())
    throw std::runtime_error("Empty key in INSERT " + category);

  //// Literature references (LITREF) ////
  if (category == "LITREF") {

    // declare the map const_iterator for key searches
    std::unordered_map<std::string,std::string>::const_iterator im;

    // prepare the insert statement
    const char *insert_statement = R"SQL(
INSERT INTO Literature_refs (ref_key,authors,title,journal,volume,page,year,doi,description)
       VALUES(:REF_KEY,:AUTHORS,:TITLE,:JOURNAL,:VOLUME,:PAGE,:YEAR,:DOI,:DESCRIPTION)
)SQL";
    if (sqlite3_prepare_v2(db, insert_statement, -1, &statement, NULL)) goto error;

    // reset the statement and the bindings
    if (sqlite3_reset(statement)) goto error;
    if (sqlite3_clear_bindings(statement)) goto error;

    // bind the key
    if (sqlite3_bind_text(statement,sqlite3_bind_parameter_index(statement, ":REF_KEY"),key.c_str(),-1,SQLITE_STATIC)) goto error;

    // bind author or authors
    if ((im = kmap.find("AUTHOR")) != kmap.end() || (im = kmap.find("AUTHORS")) != kmap.end()){
      if (sqlite3_bind_text(statement,sqlite3_bind_parameter_index(statement,":AUTHORS"),
                            im->second.c_str(),-1,SQLITE_TRANSIENT)) goto error;
    }

    // bind page or pages
    if ((im = kmap.find("PAGE")) != kmap.end() || (im = kmap.find("PAGES")) != kmap.end()){
      if (sqlite3_bind_text(statement,sqlite3_bind_parameter_index(statement,":PAGE"),
                            im->second.c_str(),-1,SQLITE_TRANSIENT)) goto error;
    }

    // bind the rest of the values
    std::forward_list<std::string> vlist = {"TITLE","JOURNAL","VOLUME","YEAR","DOI","DESCRIPTION"};
    for (auto it = vlist.begin(); it != vlist.end(); ++it){
      im = kmap.find(*it);
      if (im != kmap.end()){
        std::string str = ":" + *it;
        if (sqlite3_bind_text(statement,
                              sqlite3_bind_parameter_index(statement,str.c_str()),
                              im->second.c_str(),
                              -1,SQLITE_TRANSIENT)) goto error;
      }
    }

    // submit the statement
    if (sqlite3_step(statement) != SQLITE_DONE) goto error;

    // finalize the statement
    if (sqlite3_finalize(statement)) goto error;

  } else { 
    throw std::runtime_error("Unknown INSERT category: " + category);
  }
  return;

  error:
  if (statement) sqlite3_finalize(statement);
  std::string errmsg = "Error inserting data: " + std::string(sqlite3_errmsg(db));
  throw std::runtime_error(errmsg);
}

void sqldb::insert_litref_bibtex(std::list<std::string> &tokens){
  if (!db) throw std::runtime_error("A db must be connected before using INSERT");

#ifdef BTPARSE_FOUND  
  // check if the file name is empty
  if (tokens.empty())
    throw std::runtime_error("Need a bibtex file name");

  // error message and statement; ASTs and boolean flags for the parsing
  std::string errmsg = "";
  sqlite3_stmt *statement = nullptr;
  AST *entry = NULL, *field = NULL;
  char *fvalue = NULL;
  boolean rc;

  // define the insert statement
  const char *insert_statement = R"SQL(
INSERT INTO Literature_refs (ref_key,authors,title,journal,volume,page,year,doi,description)
       VALUES(:REF_KEY,:AUTHORS,:TITLE,:JOURNAL,:VOLUME,:PAGE,:YEAR,:DOI,:DESCRIPTION)
)SQL";

  // open the file name
  char *filename = &(tokens.front()[0]);
  FILE *fp = fopen(filename,"r");
  if (!fp){
    errmsg = "Could not open file " + tokens.front();
    goto error;
  }

  // commence the transaction
  if (sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL)){
    goto error;
  }

  // prepare the insert statement
  if (sqlite3_prepare_v2(db, insert_statement, -1, &statement, NULL)) goto error;

  // loop over the contents of the bib file and add to the database
  while (entry = bt_parse_entry(fp,filename,0,&rc)){
    if (!rc) continue;
    if (bt_entry_metatype(entry) != BTE_REGULAR) continue;
    char *key = bt_entry_key(entry);
    char *type = bt_entry_type(entry);

    if (!strncmp(type,"article",7)){
      // reset the statement and the bindings
      if (sqlite3_reset(statement)) goto error;
      if (sqlite3_clear_bindings(statement)) goto error;

      // bind the key
      if (sqlite3_bind_text(statement,sqlite3_bind_parameter_index(statement,":REF_KEY"),key,-1,SQLITE_STATIC)) goto error;

      // bind the rest of the fields
      char *fname = NULL;
      while (field = bt_next_field(entry,field,&fname)){
        fvalue = bt_get_text(field);
        if (!strcmp(fname,"title")){
          if (sqlite3_bind_text(statement,sqlite3_bind_parameter_index(statement,":TITLE"),fvalue,-1,SQLITE_TRANSIENT)) goto error;
        } else if (!strcmp(fname,"author") || !strcmp(fname,"authors")){
          if (sqlite3_bind_text(statement,sqlite3_bind_parameter_index(statement,":AUTHORS"),fvalue,-1,SQLITE_TRANSIENT)) goto error;
        } else if (!strcmp(fname,"journal")){
          if (sqlite3_bind_text(statement,sqlite3_bind_parameter_index(statement,":JOURNAL"),fvalue,-1,SQLITE_TRANSIENT)) goto error;
        } else if (!strcmp(fname,"volume")){
          if (sqlite3_bind_text(statement,sqlite3_bind_parameter_index(statement,":VOLUME"),fvalue,-1,SQLITE_TRANSIENT)) goto error;
        } else if (!strcmp(fname,"page") || !strcmp(fname,"pages")){
          if (sqlite3_bind_text(statement,sqlite3_bind_parameter_index(statement,":PAGE"),fvalue,-1,SQLITE_TRANSIENT)) goto error;
        } else if (!strcmp(fname,"year")){
          if (sqlite3_bind_text(statement,sqlite3_bind_parameter_index(statement,":YEAR"),fvalue,-1,SQLITE_TRANSIENT)) goto error;
        } else if (!strcmp(fname,"doi")){
          if (sqlite3_bind_text(statement,sqlite3_bind_parameter_index(statement,":DOI"),fvalue,-1,SQLITE_TRANSIENT)) goto error;
        } else if (!strcmp(fname,"description")){
          if (sqlite3_bind_text(statement,sqlite3_bind_parameter_index(statement,":DESCRIPTION"),fvalue,-1,SQLITE_TRANSIENT)) goto error;
        }
        if (fvalue) free(fvalue);
        fvalue = NULL;
      }
      // free the field
      if (field) bt_free_ast(field);
      field = NULL;

      // submit the statement
      if (sqlite3_step(statement) != SQLITE_DONE) goto error;
    }

    // free the entry
    if (entry) bt_free_ast(entry);
    entry = NULL;
  }

  // finalize the statement
  if (sqlite3_finalize(statement)) goto error;
  statement = NULL;

  // commit the transaction
  if (sqlite3_exec(db, "COMMIT TRANSACTION", NULL, NULL, NULL)) goto error;

  return;

error:
  if (statement) sqlite3_finalize(statement);
  if (fvalue) free(fvalue);
  if (field) bt_free_ast(field);
  if (entry) bt_free_ast(entry);
  if (errmsg.empty())
    errmsg = std::string(sqlite3_errmsg(db));
  throw std::runtime_error("Error inserting data: " + errmsg);
#else
  throw std::runtime_error("Cannot use INSERT LITREF BIBTEX: not compiled with bibtex support");
#endif

}

void sqldb::erase(const std::string &category, std::list<std::string> &tokens) {
  if (!db) throw std::runtime_error("A db must be connected before using DELETE");

  sqlite3_stmt *statement_all = nullptr;
  sqlite3_stmt *statement_with_key = nullptr;
  sqlite3_stmt *statement_with_id = nullptr;

  //// Literature references (LITREF) ////
  if (category == "LITREF") {
    const char *delete_statement_all = R"SQL(
DELETE FROM Literature_refs;
)SQL";
    const char *delete_statement_with_key = R"SQL(
DELETE FROM Literature_refs WHERE ref_key = ?1;
)SQL";
    const char *delete_statement_with_id = R"SQL(
DELETE FROM Literature_refs WHERE id = ?1;
)SQL";
    if (sqlite3_prepare_v2(db, delete_statement_all, -1, &statement_all, NULL)) goto error;
    if (sqlite3_prepare_v2(db, delete_statement_with_key, -1, &statement_with_key, NULL)) goto error;
    if (sqlite3_prepare_v2(db, delete_statement_with_id, -1, &statement_with_id, NULL)) goto error;

    for (auto it = tokens.begin(); it != tokens.end(); it++){
      std::string key, param;

      if (*it == "*"){
        // delete all
        if (sqlite3_reset(statement_all)) goto error;
        if (sqlite3_step(statement_all) != SQLITE_DONE) goto error;
      } else if (it->find_first_not_of("0123456789") == std::string::npos){
        // an integer
        if (sqlite3_reset(statement_with_id)) goto error;
        if (sqlite3_bind_text(statement_with_id,1,it->c_str(),-1,SQLITE_TRANSIENT)) goto error;
        if (sqlite3_step(statement_with_id) != SQLITE_DONE) goto error;
      } else {
        // a key
        if (sqlite3_reset(statement_with_key)) goto error;
        if (sqlite3_bind_text(statement_with_key,1,it->c_str(),-1,SQLITE_TRANSIENT)) goto error;
        if (sqlite3_step(statement_with_key) != SQLITE_DONE) goto error;
      }
    }
  }
  if (sqlite3_finalize(statement_all)) goto error;
  if (sqlite3_finalize(statement_with_key)) goto error;
  if (sqlite3_finalize(statement_with_id)) goto error;
  return;

  error:
  if (statement_all) sqlite3_finalize(statement_all);
  if (statement_with_key) sqlite3_finalize(statement_with_key);
  if (statement_with_id) sqlite3_finalize(statement_with_id);
  std::string errmsg = "Error deleting data: " + std::string(sqlite3_errmsg(db));
  throw std::runtime_error(errmsg);
}  

// List items from the database
void sqldb::list(const std::string &category, std::list<std::string> &tokens){
  if (!db) throw std::runtime_error("A db must be connected before using LIST");

  sqlite3_stmt *statement = nullptr;
  
  //// Literature references (LITREF) ////
  if (category == "LITREF") {
    bool dobib = (!tokens.empty() && equali_strings(tokens.front(),"BIBTEX"));

    // print table header
    if (!dobib)
      printf("| id | ref_key | authors | title | journal | volume | page | %year | doi | description |\n");

    // prepare the statement
    const char *list_statement = R"SQL(
SELECT id,ref_key,authors,title,journal,volume,page,year,doi,description FROM Literature_refs
)SQL";
    if (sqlite3_prepare_v2(db, list_statement, -1, &statement, NULL)) goto error;

    // run the statement and print the results
    int rc; 
    while ((rc = sqlite3_step(statement)) != SQLITE_DONE){
      if (rc != SQLITE_ROW) goto error;

      int id = sqlite3_column_int(statement, 0);
      const unsigned char *ref_key = sqlite3_column_text(statement, 1);
      const unsigned char *authors = sqlite3_column_text(statement, 2);
      const unsigned char *title = sqlite3_column_text(statement, 3);
      const unsigned char *journal = sqlite3_column_text(statement, 4);
      const unsigned char *volume = sqlite3_column_text(statement, 5);
      const unsigned char *page = sqlite3_column_text(statement, 6);
      const unsigned char *year = sqlite3_column_text(statement, 7);
      const unsigned char *doi = sqlite3_column_text(statement, 8);
      const unsigned char *description = sqlite3_column_text(statement, 9);

      if (dobib){
        printf("@article{%s\n",ref_key);
        if (sqlite3_column_type(statement,2) != SQLITE_NULL) printf(" authors={%s},\n",authors);
        if (sqlite3_column_type(statement,3) != SQLITE_NULL) printf(" title={%s},\n",title);
        if (sqlite3_column_type(statement,4) != SQLITE_NULL) printf(" journal={%s},\n",journal);
        if (sqlite3_column_type(statement,5) != SQLITE_NULL) printf(" volume={%s},\n",volume);
        if (sqlite3_column_type(statement,6) != SQLITE_NULL) printf(" page={%s},\n",page);
        if (sqlite3_column_type(statement,7) != SQLITE_NULL) printf(" year={%s},\n",year);
        if (sqlite3_column_type(statement,8) != SQLITE_NULL) printf(" doi={%s},\n",doi);
        if (sqlite3_column_type(statement,9) != SQLITE_NULL) printf(" description={%s},\n",description);
        printf("}\n");
      } else {
        printf("| %d | %s | %s | %s | %s | %s | %s | %s | %s | %s |\n",id,
               ref_key,authors,title,journal,volume,page,year,doi,description);
      }
    }

    // finalize the statement
    if (sqlite3_finalize(statement)) goto error;
  } else { 
    throw std::runtime_error("Unknown LIST category: " + category);
  }

  return;

  error:
  if (statement) sqlite3_finalize(statement);
  std::string errmsg = "Error listing data: " + std::string(sqlite3_errmsg(db));
  throw std::runtime_error(errmsg);
}
