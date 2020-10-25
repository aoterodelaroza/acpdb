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
#include <algorithm>
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

  // declare the map const_iterator for key searches
  std::unordered_map<std::string,std::string>::const_iterator im;

  if (category == "LITREF") {
    //// Literature references (LITREF) ////

    // check
    if (key.empty())
      throw std::runtime_error("Empty key in INSERT " + category);

    // bind 
    stmt[statement::STMT_INSERT_LITREF]->bind((char *) ":KEY",key,false);
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

    // check
    if (key.empty())
      throw std::runtime_error("Empty key in INSERT " + category);

    // bind
    stmt[statement::STMT_INSERT_SET]->bind((char *) ":KEY",key);
    if ((im = kmap.find("DESCRIPTION")) != kmap.end())
      stmt[statement::STMT_INSERT_SET]->bind((char *) ":DESCRIPTION",im->second);
    if ((im = kmap.find("PROPERTY_TYPE")) != kmap.end()){
      if (isinteger(im->second))
        stmt[statement::STMT_INSERT_SET]->bind((char *) ":PROPERTY_TYPE",std::stoi(im->second));
      else
        stmt[statement::STMT_INSERT_SET]->bind((char *) ":PROPERTY_TYPE",find_id_from_key(im->second,statement::STMT_QUERY_PROPTYPE));
    }
    if ((im = kmap.find("LITREFS")) != kmap.end()){
      std::list<std::string> tokens = list_all_words(im->second);
      std::string str = "";
      for (auto it = tokens.begin(); it != tokens.end(); it++){
        int idx = find_id_from_key(*it,statement::STMT_QUERY_LITREF);
        if (!idx)
          throw std::runtime_error("Litref not found: " + *it);
        str = str + *it + " ";
      }
      stmt[statement::STMT_INSERT_SET]->bind((char *) ":LITREFS",str);
    }

    // submit
    stmt[statement::STMT_INSERT_SET]->step();
  } else if (category == "METHOD") {
    //// Methods (METHOD) ////

    // check
    if (key.empty())
      throw std::runtime_error("Empty key in INSERT " + category);

    // bind
    stmt[statement::STMT_INSERT_METHOD]->bind((char *) ":KEY",key,false);
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

    // check
    if (key.empty())
      throw std::runtime_error("Empty key in INSERT " + category);

    // read the molecular structure
    structure s;
    std::unordered_map<std::string,std::string>::const_iterator im;
    if ((im = kmap.find("XYZ")) != kmap.end()){
      s.readxyz(im->second);
    } else {
      throw std::runtime_error("A structure must be given in INSERT STRUCTURE");
    }

    // bind
    int nat = s.get_nat();
    stmt[statement::STMT_INSERT_STRUCTURE]->bind((char *) ":KEY",key,false);
    stmt[statement::STMT_INSERT_STRUCTURE]->bind((char *) ":ISMOLECULE",s.ismolecule()?1:0,false);
    stmt[statement::STMT_INSERT_STRUCTURE]->bind((char *) ":NAT",nat,false);
    stmt[statement::STMT_INSERT_STRUCTURE]->bind((char *) ":CHARGE",s.get_charge(),false);
    stmt[statement::STMT_INSERT_STRUCTURE]->bind((char *) ":MULTIPLICITY",s.get_mult(),false);
    if ((im = kmap.find("SET")) != kmap.end()){
      if (isinteger(im->second))
        stmt[statement::STMT_INSERT_STRUCTURE]->bind((char *) ":SETID",std::stoi(im->second));
      else
        stmt[statement::STMT_INSERT_STRUCTURE]->bind((char *) ":SETID",find_id_from_key(im->second,statement::STMT_QUERY_SET));
    }
    if (!s.ismolecule())
      stmt[statement::STMT_INSERT_STRUCTURE]->bind((char *) ":CELL",(void *) s.get_r(),false,9 * sizeof(double));
    stmt[statement::STMT_INSERT_STRUCTURE]->bind((char *) ":ZATOMS",(void *) s.get_z(),false,nat * sizeof(unsigned char));
    stmt[statement::STMT_INSERT_STRUCTURE]->bind((char *) ":COORDINATES",(void *) s.get_x(),false,3 * nat * sizeof(double));

    // submit
    stmt[statement::STMT_INSERT_STRUCTURE]->step();
  } else if (category == "PROPERTY") {
    //// Properties (PROPERTY) ////

    // check
    if (key.empty())
      throw std::runtime_error("Empty key in INSERT " + category);

    // bind
    stmt[statement::STMT_INSERT_PROPERTY]->bind((char *) ":KEY",key,false);
    if ((im = kmap.find("PROPERTY_TYPE")) != kmap.end()){
      if (isinteger(im->second))
        stmt[statement::STMT_INSERT_PROPERTY]->bind((char *) ":PROPERTY_TYPE",std::stoi(im->second));
      else
        stmt[statement::STMT_INSERT_PROPERTY]->bind((char *) ":PROPERTY_TYPE",find_id_from_key(im->second,statement::STMT_QUERY_PROPTYPE));
    }
    if ((im = kmap.find("SET")) != kmap.end()){
      if (isinteger(im->second))
        stmt[statement::STMT_INSERT_PROPERTY]->bind((char *) ":SETID",std::stoi(im->second));
      else
        stmt[statement::STMT_INSERT_PROPERTY]->bind((char *) ":SETID",find_id_from_key(im->second,statement::STMT_QUERY_SET));
    }

    // bind structures
    int nstructures = 0;
    if ((im = kmap.find("NSTRUCTURES")) != kmap.end()){
      nstructures = std::stoi(im->second);
      stmt[statement::STMT_INSERT_PROPERTY]->bind((char *) ":NSTRUCTURES",nstructures);
    }
    if (nstructures > 0 && (im = kmap.find("STRUCTURES")) != kmap.end()){
      std::list<std::string> tokens = list_all_words(im->second);

      int n = 0;
      int *str = new int[nstructures];
      for (auto it = tokens.begin(); it != tokens.end(); it++){
        int idx = 0;
        if (isinteger(*it))
          idx = std::stoi(*it);
        else
          idx = find_id_from_key(*it,statement::STMT_QUERY_STRUCTURE);

        if (!idx)
          throw std::runtime_error("Structure not found: " + *it);
        str[n++] = idx;
      }
      stmt[statement::STMT_INSERT_PROPERTY]->bind((char *) ":STRUCTURES",(void *) str,true,nstructures * sizeof(int));
      delete str;
    }

    // bind coefficients
    if (nstructures > 0 && (im = kmap.find("COEFFICIENTS")) != kmap.end()){
      std::list<std::string> tokens = list_all_words(im->second);

      int n = 0;
      double *str = new double[nstructures];
      for (auto it = tokens.begin(); it != tokens.end(); it++)
        str[n++] = std::stod(*it);
      stmt[statement::STMT_INSERT_PROPERTY]->bind((char *) ":COEFFICIENTS",(void *) str,true,nstructures * sizeof(double));
      delete str;
    }

    // submit
    stmt[statement::STMT_INSERT_PROPERTY]->step();
  } else if (category == "EVALUATION") {
    //// Evaluations (EVALUATION) ////

    // bind
    if ((im = kmap.find("METHOD")) != kmap.end()){
      if (isinteger(im->second))
        stmt[statement::STMT_INSERT_EVALUATION]->bind((char *) ":METHODID",std::stoi(im->second));
      else 
        stmt[statement::STMT_INSERT_EVALUATION]->bind((char *) ":METHODID",find_id_from_key(im->second,statement::STMT_QUERY_METHOD));
    }
    if ((im = kmap.find("PROPERTY")) != kmap.end()){
      if (isinteger(im->second))
        stmt[statement::STMT_INSERT_EVALUATION]->bind((char *) ":PROPID",std::stoi(im->second));
      else 
        stmt[statement::STMT_INSERT_EVALUATION]->bind((char *) ":PROPID",find_id_from_key(im->second,statement::STMT_QUERY_PROPERTY));
    }
    if ((im = kmap.find("VALUE")) != kmap.end())
      stmt[statement::STMT_INSERT_EVALUATION]->bind((char *) ":VALUE",std::stod(im->second));
    if ((im = kmap.find("UNIT")) != kmap.end()){
      std::string unit = im->second;
      uppercase(unit);
      stmt[statement::STMT_INSERT_EVALUATION]->bind((char *) ":UNIT",unit);
    };

    // submit
    stmt[statement::STMT_INSERT_EVALUATION]->step();
  } else if (category == "TERM") {
    //// Terms (TERM) ////

    // bind
    if ((im = kmap.find("METHOD")) != kmap.end()){
      if (isinteger(im->second))
        stmt[statement::STMT_INSERT_TERM]->bind((char *) ":METHODID",std::stoi(im->second));
      else 
        stmt[statement::STMT_INSERT_TERM]->bind((char *) ":METHODID",find_id_from_key(im->second,statement::STMT_QUERY_METHOD));
    }
    if ((im = kmap.find("PROPERTY")) != kmap.end()){
      if (isinteger(im->second))
        stmt[statement::STMT_INSERT_TERM]->bind((char *) ":PROPID",std::stoi(im->second));
      else 
        stmt[statement::STMT_INSERT_TERM]->bind((char *) ":PROPID",find_id_from_key(im->second,statement::STMT_QUERY_PROPERTY));
    }
    if ((im = kmap.find("ATOM")) != kmap.end())
      stmt[statement::STMT_INSERT_TERM]->bind((char *) ":ATOM",std::stoi(im->second));
    if ((im = kmap.find("L")) != kmap.end())
      stmt[statement::STMT_INSERT_TERM]->bind((char *) ":L",std::stoi(im->second));
    if ((im = kmap.find("EXPONENT")) != kmap.end())
      stmt[statement::STMT_INSERT_TERM]->bind((char *) ":EXPONENT",std::stod(im->second));
    if ((im = kmap.find("VALUE")) != kmap.end())
      stmt[statement::STMT_INSERT_TERM]->bind((char *) ":VALUE",std::stod(im->second));
    if ((im = kmap.find("UNIT")) != kmap.end()){
      std::string unit = im->second;
      uppercase(unit);
      stmt[statement::STMT_INSERT_TERM]->bind((char *) ":UNIT",unit);
    };
    if ((im = kmap.find("MAXCOEF")) != kmap.end())
      stmt[statement::STMT_INSERT_TERM]->bind((char *) ":MAXCOEF",std::stod(im->second));

    // submit
    stmt[statement::STMT_INSERT_TERM]->step();
  }
}

// Insert literature references into the database from a bibtex file
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

  // whether star has been passed
  bool istar = false;
  auto it = std::find(tokens.begin(), tokens.end(), "*");
  if (it != tokens.end())
    istar = true;

  // pick the statment
  statement::stmttype all, id, key;
  if (category == "LITREF") {
    all = statement::STMT_DELETE_LITREF_ALL;
    id  = statement::STMT_DELETE_LITREF_WITH_ID;
    key = statement::STMT_DELETE_LITREF_WITH_KEY;
  } else if (category == "SET") {
    all = statement::STMT_DELETE_SET_ALL;
    id  = statement::STMT_DELETE_SET_WITH_ID;
    key = statement::STMT_DELETE_SET_WITH_KEY;
  } else if (category == "METHOD") {
    all = statement::STMT_DELETE_METHOD_ALL;
    id  = statement::STMT_DELETE_METHOD_WITH_ID;
    key = statement::STMT_DELETE_METHOD_WITH_KEY;
  } else if (category == "STRUCTURE") {
    all = statement::STMT_DELETE_STRUCTURE_ALL;
    id  = statement::STMT_DELETE_STRUCTURE_WITH_ID;
    key = statement::STMT_DELETE_STRUCTURE_WITH_KEY;
  } else if (category == "PROPERTY") {
    all = statement::STMT_DELETE_PROPERTY_ALL;
    id  = statement::STMT_DELETE_PROPERTY_WITH_ID;
    key = statement::STMT_DELETE_PROPERTY_WITH_KEY;
  } else if (category == "EVALUATION") {
    all = statement::STMT_DELETE_EVALUATION_ALL;
    id  = statement::STMT_DELETE_EVALUATION_WITH_ID;
    key = statement::STMT_CUSTOM;
  } else if (category == "TERM") {
    all = statement::STMT_DELETE_TERM_ALL;
    id  = statement::STMT_DELETE_TERM_WITH_ID;
    key = statement::STMT_CUSTOM;
  }

  // execute
  if (istar)
    stmt[all]->execute();
  else{
    for (auto it = tokens.begin(); it != tokens.end(); it++){
      if (isinteger(*it)){
        stmt[id]->bind(1,*it);
        stmt[id]->step();
      } else if (key != statement::STMT_CUSTOM){
        stmt[key]->bind(1,*it);
        stmt[key]->step();
      }
    }
  }
}  

// List items from the database
void sqldb::list(std::ostream &os, const std::string &category, std::list<std::string> &tokens){
  if (!db) throw std::runtime_error("A database file must be connected before using LIST");

  //// Literature references (LITREF) ////
  if (category == "LITREF") {
    bool dobib = (!tokens.empty() && equali_strings(tokens.front(),"BIBTEX"));

    // print table header
    if (!dobib)
      std::cout << "| id | key | authors | title | journal | volume | page | year | doi | description |" << std::endl;

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
        os << "@article{" << key << std::endl;
        if (authors) os << " authors={" << authors << "}," << std::endl;
        if (title) os << " title={" << title << "}," << std::endl;
        if (journal) os << " journal={" << journal << "}," << std::endl;
        if (volume) os << " volume={" << volume << "}," << std::endl;
        if (page) os << " page={" << page << "}," << std::endl;
        if (year) os << " year={" << year << "}," << std::endl;
        if (doi) os << " doi={" << doi << "}," << std::endl;
        if (description) os << " description={" << description << "}," << std::endl;
        os << "}" << std::endl;
      } else {
        os << "| " << id << " | " << key << " | "
           << (authors?authors:(const unsigned char*) "")  << " | "
           << (title?title:(const unsigned char*) "")  << " | "
           << (journal?journal:(const unsigned char*) "")  << " | "
           << (volume?volume:(const unsigned char*) "")  << " | "
           << (page?page:(const unsigned char*) "")  << " | "
           << (year?year:(const unsigned char*) "")  << " | "
           << (doi?doi:(const unsigned char*) "")  << " | "
           << (description?description:(const unsigned char*) "")  << " | "
           << std::endl;
      }
    }
  } else if (category == "SET") {
    // print table header
    os << "| id | key | property_type | litrefs | description |" << std::endl;

    // run the statement
    while (stmt[statement::STMT_LIST_SET]->step() != SQLITE_DONE){
      sqlite3_stmt *statement = stmt[statement::STMT_LIST_SET]->ptr();

      int id = sqlite3_column_int(statement, 0);
      const unsigned char *key = sqlite3_column_text(statement, 1);
      const unsigned char *property_type = sqlite3_column_text(statement, 2);
      const unsigned char *litrefs = sqlite3_column_text(statement, 3);
      const unsigned char *description = sqlite3_column_text(statement, 4);

      os << "| " << id << " | " << key << " | "
         << (property_type?property_type:(const unsigned char*) "")  << " | "
         << (litrefs?litrefs:(const unsigned char*) "")  << " | "
         << (description?description:(const unsigned char*) "")  << " | "
         << std::endl;
    }
  } else if (category == "METHOD") {
    // print table header
    os << "| id | key | comp_details | litrefs | description |" << std::endl;

    // run the statement
    while (stmt[statement::STMT_LIST_METHOD]->step() != SQLITE_DONE){
      sqlite3_stmt *statement = stmt[statement::STMT_LIST_METHOD]->ptr();

      int id = sqlite3_column_int(statement, 0);
      const unsigned char *key = sqlite3_column_text(statement, 1);
      const unsigned char *comp_details = sqlite3_column_text(statement, 2);
      const unsigned char *litrefs = sqlite3_column_text(statement, 3);
      const unsigned char *description = sqlite3_column_text(statement, 4);

      os << "| " << id << " | " << key << " | "
         << (comp_details?comp_details:(const unsigned char*) "")  << " | "
         << (litrefs?litrefs:(const unsigned char*) "")  << " | "
         << (description?description:(const unsigned char*) "")  << " | "
         << std::endl;
    }
  } else if (category == "STRUCTURE") {
    // print table header
    os << "| id | key | set | ismolecule | nat |" << std::endl;

    structure s;

    // run the statement
    while (stmt[statement::STMT_LIST_STRUCTURE]->step() != SQLITE_DONE){
      sqlite3_stmt *statement = stmt[statement::STMT_LIST_STRUCTURE]->ptr();

      int id = sqlite3_column_int(statement, 0);
      const unsigned char *key = sqlite3_column_text(statement, 1);
      int setid = sqlite3_column_int(statement, 2);
      int ismol = sqlite3_column_int(statement, 3);
      int nat = sqlite3_column_int(statement, 6);

      os << "| " << id << " | " << key << " | "
         << setid << " | " << ismol << " | " << nat << " | "
         << std::endl;
    }
  } else if (category == "PROPERTY") {
    // print table header
    os << "| id | key | property_type | setid | nstructures |" << std::endl;

    // run the statement
    while (stmt[statement::STMT_LIST_PROPERTY]->step() != SQLITE_DONE){
      sqlite3_stmt *statement = stmt[statement::STMT_LIST_PROPERTY]->ptr();

      int id = sqlite3_column_int(statement, 0);
      const unsigned char *key = sqlite3_column_text(statement, 1);
      int proptype = sqlite3_column_int(statement, 2);
      int setid = sqlite3_column_int(statement, 3);
      int nstructures = sqlite3_column_int(statement, 4);

      os << "| " << id << " | " << key << " | "
         << proptype << " | " << setid << " | " << nstructures << " | "
         << std::endl;
    }
  } else if (category == "EVALUATION") {
    // print table header
    os << "| id | methodid | propid | value | unit |" << std::endl;

    // run the statement
    while (stmt[statement::STMT_LIST_EVALUATION]->step() != SQLITE_DONE){
      sqlite3_stmt *statement = stmt[statement::STMT_LIST_EVALUATION]->ptr();

      int id = sqlite3_column_int(statement, 0);
      int methodid = sqlite3_column_int(statement, 1);
      int propid = sqlite3_column_int(statement, 2);
      double value = sqlite3_column_double(statement, 3);
      const unsigned char *unit = sqlite3_column_text(statement, 4);

      std::streamsize prec = os.precision(10);
      os << "| " << id << " | " << methodid << " | " << propid << " | "
         << value << " | " << unit << " | " << std::endl;
      os.precision(prec);
    }
  } else if (category == "TERM") {
    // print table header
    os << "| id | methodid | propid | atom | l | exponent | value | unit | maxcoef |" << std::endl;

    // run the statement
    while (stmt[statement::STMT_LIST_TERM]->step() != SQLITE_DONE){
      sqlite3_stmt *statement = stmt[statement::STMT_LIST_TERM]->ptr();

      int id = sqlite3_column_int(statement, 0);
      int methodid = sqlite3_column_int(statement, 1);
      int propid = sqlite3_column_int(statement, 2);
      int atom = sqlite3_column_int(statement, 3);
      int l = sqlite3_column_int(statement, 4);
      double exponent = sqlite3_column_double(statement, 5);
      double value = sqlite3_column_double(statement, 6);
      const unsigned char *unit = sqlite3_column_text(statement, 7);
      double maxcoef = sqlite3_column_double(statement, 8);

      std::streamsize prec = os.precision(10);
      os << "| " << id << " | " << methodid << " | " << propid << " | "
         << atom << " | " << l << " | " << exponent << " | " 
         << value << " | " << unit << " | " << maxcoef << " | "
         << std::endl;
      os.precision(prec);
    }
  } else { 
    throw std::runtime_error("Unknown LIST category: " + category);
  }
}
