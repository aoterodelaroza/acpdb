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
#include "structure.h"

#include "config.h"
#ifdef BTPARSE_FOUND  
#include "btparse.h"
#endif

int sqldb::find_id_from_key(const std::string &key,statement::stmttype type){
  stmt[type]->bind(1,key);
  stmt[type]->step();
  int rc = sqlite3_column_int(stmt[type]->ptr(),0);
  stmt[type]->reset();
  return rc;
}

// Check if the DB is sane, empty, or not sane. If except_on_empty,
// raise exception on empty. Always raise excepton on error. Return
// 1 if sane, 0 if empty.
int sqldb::checksane(bool except_on_empty /*=false*/){
  if (!db) 
    throw std::runtime_error("Error reading connected database");

  // query the database
  stmt[statement::STMT_CHECK_DATABASE]->reset();
  int rc = stmt[statement::STMT_CHECK_DATABASE]->step();
  int icol = sqlite3_column_int(stmt[statement::STMT_CHECK_DATABASE]->ptr(), 0);

  // if we did not get a row, error
  if (rc != SQLITE_ROW)
    throw std::runtime_error("Error accessing connected database");

  if (icol == 0){
    if (except_on_empty)
      throw std::runtime_error("Empty database");
    else
      return 0;
  }
  return 1;
}

// Open a database file for use. 
void sqldb::connect(const std::string &filename, int flags/*=SQLITE_OPEN_READWRITE*/){
  // close the previous db if open
  close();

  // check if the string is empty
  if (filename.empty())
    throw std::runtime_error("Need a database file name in connect");

  // open the new one
  if (sqlite3_open_v2(filename.c_str(), &db, flags, NULL)) {
    std::string errmsg = "Can't connect to database file " + filename + " (" + std::string(sqlite3_errmsg(db)) + ")";
    close();
    throw std::runtime_error(errmsg);
  }

  // write down the file name
  dbfilename = filename;

  // construct all statements
  for (int i = 0; i < statement::number_stmt_types; i++)
    stmt[i] = new statement(db,(statement::stmttype) i);

  // initialize the database
  stmt[statement::STMT_INIT_DATABASE]->execute();
}

// Create the database skeleton.
void sqldb::create(){
  // skip if not open
  if (!db) throw std::runtime_error("A database file must be connected before using CREATE");

  // Create the table 
  stmt[statement::STMT_CREATE_DATABASE]->execute();
}

// Close a database connection if open and reset the pointer to NULL
void sqldb::close(){
  if (!db) return;

  // finalize and deallocate all statements
  for (int i = 0; i < statement::number_stmt_types; i++){
    delete stmt[i];
    stmt[i] = nullptr;
  }

  // close the database
  if (sqlite3_close_v2(db)) 
    throw std::runtime_error("Can't close database file " + dbfilename + " (" + sqlite3_errmsg(db) + ")");
  db = nullptr;
  dbfilename = "";
}

// Insert an item into the database
void sqldb::insert(const std::string &category, const std::string &key, const std::unordered_map<std::string,std::string> &kmap) {
  if (!db) throw std::runtime_error("A database file must be connected before using INSERT");

  // check that the key is not empty
  if (key.empty())
    throw std::runtime_error("Empty key in INSERT " + category);

  // declare the map const_iterator for key searches
  std::unordered_map<std::string,std::string>::const_iterator im;

  if (category == "LITREF") {
    //// Literature references (LITREF) ////

    // bind the key
    stmt[statement::STMT_INSERT_LITREF]->bind((char *) ":KEY",key,false);

    // bind the rest of the values
    std::forward_list<std::string> vlist = {"AUTHORS","TITLE","JOURNAL","VOLUME","PAGE","YEAR","DOI","DESCRIPTION"};
    for (auto it = vlist.begin(); it != vlist.end(); ++it){
      im = kmap.find(*it);
      if (im != kmap.end())
        stmt[statement::STMT_INSERT_LITREF]->bind(":" + *it,im->second);
    }

    // submit
    stmt[statement::STMT_INSERT_LITREF]->step();
  } else if (category == "SET") {
    //// Sets (SET) ////  

    // bind the key
    stmt[statement::STMT_INSERT_SET]->bind((char *) ":KEY",key);

    // bind the rest of the values
    if ((im = kmap.find("DESCRIPTION")) != kmap.end())
      stmt[statement::STMT_INSERT_SET]->bind((char *) ":DESCRIPTION",im->second);

    if ((im = kmap.find("PROPERTY_TYPE")) != kmap.end()){
      if (isinteger(im->second)){
        stmt[statement::STMT_INSERT_SET]->bind((char *) ":PROPERTY_TYPE",std::stoi(im->second));
      } else {
        stmt[statement::STMT_INSERT_SET]->bind((char *) ":PROPERTY_TYPE",
                                               find_id_from_key(im->second,statement::STMT_QUERY_PROPTYPE));
      }
    }

    if ((im = kmap.find("NSTRUCTURES")) != kmap.end())
      stmt[statement::STMT_INSERT_SET]->bind((char *) ":NSTRUCTURES",std::stoi(im->second));

    if ((im = kmap.find("NPROPERTIES")) != kmap.end())
      stmt[statement::STMT_INSERT_SET]->bind((char *) ":NPROPERTIES",std::stoi(im->second));

    if ((im = kmap.find("LITREFS")) != kmap.end()){
      std::list<std::string> tokens = list_all_words(im->second);
      std::string str = "";
      for (auto it = tokens.begin(); it != tokens.end(); it++){
        int idx = find_id_from_key(*it,statement::STMT_QUERY_LITREF);
        if (!find_id_from_key(*it,statement::STMT_QUERY_LITREF))
          throw std::runtime_error("Litref not found: " + *it);
        str = str + *it + " ";
      }
      stmt[statement::STMT_INSERT_SET]->bind((char *) ":LITREFS",str);
    }

    // submit
    stmt[statement::STMT_INSERT_SET]->step();
  } else if (category == "METHOD") {
    //// Methods (METHOD) ////

    // bind the key
    stmt[statement::STMT_INSERT_METHOD]->bind((char *) ":KEY",key,false);

    // bind the rest of the values
    std::forward_list<std::string> vlist = {"COMP_DETAILS","LITREFS","DESCRIPTION"};
    for (auto it = vlist.begin(); it != vlist.end(); ++it){
      im = kmap.find(*it);
      if (im != kmap.end())
        stmt[statement::STMT_INSERT_METHOD]->bind(":" + *it,im->second);
    }

    // submit
    stmt[statement::STMT_INSERT_METHOD]->step();
  } else if (category == "STRUCTURE") {
    //// Structures (STRUCTURE) ////

    // read the molecular structure
    structure s;
    std::unordered_map<std::string,std::string>::const_iterator im;
    if ((im = kmap.find("XYZ")) != kmap.end()){
      s.readxyz(im->second);
    } else {
      throw std::runtime_error("A structure must be given in INSERT STRUCTURE");
    }

    // bind the key
    stmt[statement::STMT_INSERT_STRUCTURE]->bind((char *) ":KEY",key,false);

    // bind the integer values
    int nat = s.get_nat();
    stmt[statement::STMT_INSERT_STRUCTURE]->bind((char *) ":ISMOLECULE",s.ismolecule()?1:0,false);
    stmt[statement::STMT_INSERT_STRUCTURE]->bind((char *) ":NAT",nat,false);
    stmt[statement::STMT_INSERT_STRUCTURE]->bind((char *) ":CHARGE",s.get_charge(),false);
    stmt[statement::STMT_INSERT_STRUCTURE]->bind((char *) ":MULTIPLICITY",s.get_mult(),false);

    // the set ID
    if ((im = kmap.find("SET")) != kmap.end()){
      if (isinteger(im->second)){
        stmt[statement::STMT_INSERT_STRUCTURE]->bind((char *) ":SETID",std::stoi(im->second));
      } else {
        stmt[statement::STMT_INSERT_STRUCTURE]->bind((char *) ":SETID",
                                               find_id_from_key(im->second,statement::STMT_QUERY_SET));
      }
    }

    // bind the blobs
    if (!s.ismolecule())
      stmt[statement::STMT_INSERT_STRUCTURE]->bind((char *) ":CELL",(void *) s.get_r(),false,9 * sizeof(double));
    stmt[statement::STMT_INSERT_STRUCTURE]->bind((char *) ":ZATOMS",(void *) s.get_z(),false,nat * sizeof(unsigned char));
    stmt[statement::STMT_INSERT_STRUCTURE]->bind((char *) ":COORDINATES",(void *) s.get_x(),false,3 * nat * sizeof(double));

    // submit
    stmt[statement::STMT_INSERT_STRUCTURE]->step();
  }
}

// Insert literature refernces into the database from a bibtex file
void sqldb::insert_litref_bibtex(std::list<std::string> &tokens){
  if (!db) throw std::runtime_error("A database file must be connected before using INSERT");

#ifdef BTPARSE_FOUND  
  // check if the file name is empty
  if (tokens.empty())
    throw std::runtime_error("Need a bibtex file name in INSERT");

  // open the file name (need a char* and FILE* for btparse)
  char *filename = &(tokens.front()[0]);
  FILE *fp = fopen(filename,"r");
  if (!fp)
    throw std::runtime_error(std::string("Could not open bibtex file in INSERT: ") + filename);

  // begin the transaction
  stmt[statement::STMT_BEGIN_TRANSACTION]->execute();

  // loop over the contents of the bib file and add to the database
  boolean rc;
  while (AST *entry = bt_parse_entry(fp,filename,0,&rc)){
    if (rc && bt_entry_metatype(entry) == BTE_REGULAR) {
      std::string key = bt_entry_key(entry);
      std::string type = bt_entry_type(entry);

      if (equali_strings(type,"article")){
        // bind the key
        stmt[statement::STMT_INSERT_LITREF]->bind((char *) ":KEY",key,false);
        
        // bind the rest of the fields
        char *fname = NULL;
        AST *field = NULL;
        while (field = bt_next_field(entry,field,&fname)){
          // this prevents a memory leak if we throw an exception
          char *fvalue_ = bt_get_text(field);
          std::string fvalue(fvalue_);
          free(fvalue_);

          if (!strcmp(fname,"title")){
            stmt[statement::STMT_INSERT_LITREF]->bind((char *) ":TITLE",fvalue);
          } else if (!strcmp(fname,"author") || !strcmp(fname,"authors")){
            stmt[statement::STMT_INSERT_LITREF]->bind((char *) ":AUTHORS",fvalue);
          } else if (!strcmp(fname,"journal")){
            stmt[statement::STMT_INSERT_LITREF]->bind((char *) ":JOURNAL",fvalue);
          } else if (!strcmp(fname,"volume")){
            stmt[statement::STMT_INSERT_LITREF]->bind((char *) ":VOLUME",fvalue);
          } else if (!strcmp(fname,"page") || !strcmp(fname,"pages")){
            stmt[statement::STMT_INSERT_LITREF]->bind((char *) ":PAGE",fvalue);
          } else if (!strcmp(fname,"year")){
            stmt[statement::STMT_INSERT_LITREF]->bind((char *) ":YEAR",fvalue);
          } else if (!strcmp(fname,"doi")){
            stmt[statement::STMT_INSERT_LITREF]->bind((char *) ":DOI",fvalue);
          } else if (!strcmp(fname,"description")){
            stmt[statement::STMT_INSERT_LITREF]->bind((char *) ":DESCRIPTION",fvalue);
          }
        }
        stmt[statement::STMT_INSERT_LITREF]->step();
        if (field) bt_free_ast(field);
      }
    }
    
    // free the entry
    if (entry) bt_free_ast(entry);
  }

  // commit the transaction
  stmt[statement::STMT_COMMIT_TRANSACTION]->execute();

  // close the file
  fclose(fp);

#else
  throw std::runtime_error("Cannot use INSERT LITREF BIBTEX: not compiled with bibtex support");
#endif

}

// Delete items from the database
void sqldb::erase(const std::string &category, std::list<std::string> &tokens) {
  if (!db) throw std::runtime_error("A database file must be connected before using DELETE");

  if (category == "LITREF") {
    //// Literature references (LITREF) ////
    for (auto it = tokens.begin(); it != tokens.end(); it++){
      std::string key, param;

      if (*it == "*"){
        // all
        stmt[statement::STMT_DELETE_LITREF_ALL]->execute();
      } else if (isinteger(*it)){
        // an integer
        stmt[statement::STMT_DELETE_LITREF_WITH_ID]->bind(1,*it);
        stmt[statement::STMT_DELETE_LITREF_WITH_ID]->step();
      } else {
        // a key
        stmt[statement::STMT_DELETE_LITREF_WITH_KEY]->bind(1,*it);
        stmt[statement::STMT_DELETE_LITREF_WITH_KEY]->step();
      }
    }
  } else if (category == "SET") {
    //// Sets (SET) ////
    for (auto it = tokens.begin(); it != tokens.end(); it++){
      std::string key, param;

      if (*it == "*"){
        // all
        stmt[statement::STMT_DELETE_SET_ALL]->execute();
      } else if (isinteger(*it)){
        // an integer
        stmt[statement::STMT_DELETE_SET_WITH_ID]->bind(1,*it);
        stmt[statement::STMT_DELETE_SET_WITH_ID]->step();
      } else {
        // a key
        stmt[statement::STMT_DELETE_SET_WITH_KEY]->bind(1,*it);
        stmt[statement::STMT_DELETE_SET_WITH_KEY]->step();
      }
    }
  } else if (category == "METHOD") {
    //// Methods (METHOD) ////
    for (auto it = tokens.begin(); it != tokens.end(); it++){
      std::string key, param;

      if (*it == "*"){
        // all
        stmt[statement::STMT_DELETE_METHOD_ALL]->execute();
      } else if (isinteger(*it)){
        // an integer
        stmt[statement::STMT_DELETE_METHOD_WITH_ID]->bind(1,*it);
        stmt[statement::STMT_DELETE_METHOD_WITH_ID]->step();
      } else {
        // a key
        stmt[statement::STMT_DELETE_METHOD_WITH_KEY]->bind(1,*it);
        stmt[statement::STMT_DELETE_METHOD_WITH_KEY]->step();
      }
    }
  } else if (category == "STRUCTURE") {
    //// Methods (STRUCTURE) ////
    for (auto it = tokens.begin(); it != tokens.end(); it++){
      std::string key, param;

      if (*it == "*"){
        // all
        stmt[statement::STMT_DELETE_STRUCTURE_ALL]->execute();
      } else if (isinteger(*it)){
        // an integer
        stmt[statement::STMT_DELETE_STRUCTURE_WITH_ID]->bind(1,*it);
        stmt[statement::STMT_DELETE_STRUCTURE_WITH_ID]->step();
      } else {
        // a key
        stmt[statement::STMT_DELETE_STRUCTURE_WITH_KEY]->bind(1,*it);
        stmt[statement::STMT_DELETE_STRUCTURE_WITH_KEY]->step();
      }
    }
  }
}  

// List items from the database
void sqldb::list(const std::string &category, std::list<std::string> &tokens){
  if (!db) throw std::runtime_error("A database file must be connected before using LIST");

  //// Literature references (LITREF) ////
  if (category == "LITREF") {
    bool dobib = (!tokens.empty() && equali_strings(tokens.front(),"BIBTEX"));

    // print table header
    if (!dobib)
      printf("| id | key | authors | title | journal | volume | page | year | doi | description |\n");

    // run the statement
    while (stmt[statement::STMT_LIST_LITREF]->step() != SQLITE_DONE){
      sqlite3_stmt *statement = stmt[statement::STMT_LIST_LITREF]->ptr();

      int id = sqlite3_column_int(statement, 0);
      const unsigned char *key = sqlite3_column_text(statement, 1);
      const unsigned char *authors = sqlite3_column_text(statement, 2);
      const unsigned char *title = sqlite3_column_text(statement, 3);
      const unsigned char *journal = sqlite3_column_text(statement, 4);
      const unsigned char *volume = sqlite3_column_text(statement, 5);
      const unsigned char *page = sqlite3_column_text(statement, 6);
      const unsigned char *year = sqlite3_column_text(statement, 7);
      const unsigned char *doi = sqlite3_column_text(statement, 8);
      const unsigned char *description = sqlite3_column_text(statement, 9);

      if (dobib){
        printf("@article{%s\n",key);
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
               key,authors,title,journal,volume,page,year,doi,description);
      }
    }
  } else if (category == "SET") {
    // print table header
    printf("| id | key | property_type | nstructures | nproperties | litrefs | description |\n");

    // run the statement
    while (stmt[statement::STMT_LIST_SET]->step() != SQLITE_DONE){
      sqlite3_stmt *statement = stmt[statement::STMT_LIST_SET]->ptr();

      int id = sqlite3_column_int(statement, 0);
      const unsigned char *key = sqlite3_column_text(statement, 1);
      const unsigned char *property_type = sqlite3_column_text(statement, 2);
      int nstructures = sqlite3_column_int(statement, 3);
      int nproperties = sqlite3_column_int(statement, 4);
      const unsigned char *litrefs = sqlite3_column_text(statement, 5);
      const unsigned char *description = sqlite3_column_text(statement, 6);

      printf("| %d | %s | %s | %d | %d | %s | %s |\n",id,
             key,property_type,nstructures,nproperties,litrefs,description);
    }
  } else if (category == "METHOD") {
    // print table header
    printf("| id | key | comp_details | litrefs | description |\n");

    // run the statement
    while (stmt[statement::STMT_LIST_METHOD]->step() != SQLITE_DONE){
      sqlite3_stmt *statement = stmt[statement::STMT_LIST_METHOD]->ptr();

      int id = sqlite3_column_int(statement, 0);
      const unsigned char *key = sqlite3_column_text(statement, 1);
      const unsigned char *comp_details = sqlite3_column_text(statement, 2);
      const unsigned char *litrefs = sqlite3_column_text(statement, 3);
      const unsigned char *description = sqlite3_column_text(statement, 4);

      printf("| %d | %s | %s | %s | %s |\n",id,
             key,comp_details,litrefs,description);
    }
  } else if (category == "STRUCTURE") {
    // print table header
    printf("| id | key | set | ismolecule | nat |\n");

    structure s;

    // run the statement
    while (stmt[statement::STMT_LIST_STRUCTURE]->step() != SQLITE_DONE){
      sqlite3_stmt *statement = stmt[statement::STMT_LIST_STRUCTURE]->ptr();

      int id = sqlite3_column_int(statement, 0);
      const unsigned char *key = sqlite3_column_text(statement, 1);
      int setid = sqlite3_column_int(statement, 2);
      int ismol = sqlite3_column_int(statement, 3);
      int nat = sqlite3_column_int(statement, 6);

      printf("| %d | %s | %d | %d | %d |\n",id,key,setid,ismol,nat);
    }
  } else { 
    throw std::runtime_error("Unknown LIST category: " + category);
  }
}
