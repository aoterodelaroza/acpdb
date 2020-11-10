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
#include <numeric>
#include <iostream>
#include <forward_list>
#include <algorithm>
#include <filesystem>
#include <regex>
#include <fstream>
#include <string>
#include "sqldb.h"
#include "parseutils.h"
#include "statement.h"
#include "structure.h"
#include "outputeval.h"
#include "globals.h"

#include "config.h"
#ifdef BTPARSE_FOUND  
#include "btparse.h"
#endif

// essential information for a property
struct propinfo {
  int fieldasrxn = 0;
  std::vector<std::string> names;
  std::vector<double> coefs;
  double ref = 0;
};

namespace fs = std::filesystem;

// Find the id from a key from an sql table
int sqldb::find_id_from_key(const std::string &key,statement::stmttype type){
  stmt[type]->bind(1,key);
  stmt[type]->step();
  int rc = sqlite3_column_int(stmt[type]->ptr(),0);
  stmt[type]->reset();
  return rc;
}

// Get the Gaussian map from the method key
std::unordered_map<std::string,std::string> sqldb::get_gaussian_map(const std::string &methodkey){
  statement st(db,statement::STMT_CUSTOM,"SELECT gaussian_keyword FROM Methods WHERE key=?1;");
  st.bind(1,methodkey);
  st.step();
  if (sqlite3_column_type(st.ptr(),0) == SQLITE_NULL)
    throw std::runtime_error("METHOD is unknown or has no associated Gaussian keyword: " + methodkey);
  std::string gkeyw = (char *) sqlite3_column_text(st.ptr(), 0);
  std::unordered_map<std::string,std::string> gmap = map_keyword_pairs(gkeyw,';',true);
  return gmap;
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
void sqldb::insert(const std::string &category, const std::string &key, std::unordered_map<std::string,std::string> &kmap) {
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

    // interpret the xyz keyword
    if (kmap.find("XYZ") != kmap.end())
      insert_set_xyz(key, kmap);

    // interpret the din/directory/method keyword combination
    if (kmap.find("DIN") != kmap.end())
      insert_set_din(key, kmap);

  } else if (category == "METHOD") {
    //// Methods (METHOD) ////

    // check
    if (key.empty())
      throw std::runtime_error("Empty key in INSERT " + category);

    // bind
    stmt[statement::STMT_INSERT_METHOD]->bind((char *) ":KEY",key,false);
    std::forward_list<std::string> vlist = {"GAUSSIAN_KEYWORD","LITREFS","DESCRIPTION"};
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
      if (s.readxyz(im->second))
        throw std::runtime_error("Error reading xyz file: " + im->second);
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
    if ((im = kmap.find("ORDERID")) != kmap.end())
      stmt[statement::STMT_INSERT_PROPERTY]->bind((char *) ":ORDERID",std::stoi(im->second));

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
  begin_transaction();

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
  commit_transaction();

  // close the file
  fclose(fp);

#else
  throw std::runtime_error("Cannot use INSERT LITREF BIBTEX: not compiled with bibtex support");
#endif
}

// Insert additional info from an INSERT SET command (xyz keyword)
void sqldb::insert_set_xyz(const std::string &key, std::unordered_map<std::string,std::string> &kmap){
  if (!db) throw std::runtime_error("A database file must be connected before using INSERT");

  // tokenize the line following the xyz keyword
  std::list<std::string> tokens(list_all_words(kmap["XYZ"]));
  if (tokens.empty())
    throw std::runtime_error("Need arguments after XYZ");
  
  // prepare
  std::string skey;
  std::unordered_map<std::string,std::string> smap;

  // begin the transaction
  begin_transaction();

  if (fs::is_directory(tokens.front())){
    // add a directory //

    // interpret the input and build the regex
    auto it = tokens.begin();
    std::string dir = *it, rgx_s;
    if (std::next(it) != tokens.end())
      rgx_s = *(std::next(it));
    else
      rgx_s = ".*\\.xyz$";
    std::regex rgx(rgx_s, std::regex::awk | std::regex::icase | std::regex::optimize);

    // run over directory files and add the structures
    for (const auto& file : fs::directory_iterator(dir)){
      std::string filename = file.path().filename();
      if (std::regex_match(filename.begin(),filename.end(),rgx)){
        skey = key + "." + std::string(file.path().stem());
        
        smap.clear();
        smap["XYZ"] = file.path().string();
        smap["SET"] = key;
        insert("STRUCTURE",skey,smap);
      }
    }

  } else if (fs::is_regular_file(tokens.front())) {
    // add a list of files //

    for (auto it = tokens.begin(); it != tokens.end(); it++){
      if (fs::is_regular_file(*it)){
        skey = key + "." + std::string(fs::path(*it).stem());
        
        smap.clear();
        smap["XYZ"] = *it;
        smap["SET"] = key;
        insert("STRUCTURE",skey,smap);
      } else {
        throw std::runtime_error("File or directory not found: " + *it);
      }
    }
  } else {
    throw std::runtime_error("File or directory not found: " + tokens.front());
  }

  // commit the transaction
  commit_transaction();
}

// Insert additional info from an INSERT SET command (xyz keyword)
void sqldb::insert_set_din(const std::string &key, std::unordered_map<std::string,std::string> &kmap){
  if (!db) throw std::runtime_error("A database file must be connected before using INSERT");

  // Check sanity of the keywords
  bool havemethod = (kmap.find("METHOD") != kmap.end());

  std::string dir = fetch_directory(kmap);

  std::string din = kmap["DIN"];
  if (!fs::is_regular_file(din))
    throw std::runtime_error("din file " + din + " not found");

  // open the din file
  std::ifstream ifile(din,std::ios::in);
  if (ifile.fail()) 
    throw std::runtime_error("Error reading din file " + din);

  // read the din file header
  int fieldasrxn = 0;
  bool havefield = false;
  std::string str, saux, line; 
  while (std::getline(ifile,line)){
    if (ifile.fail()) 
      throw std::runtime_error("Error reading din file " + din);
    std::istringstream iss(line);
    iss >> str;

    if (str.substr(0,2) == "#@"){
      iss >> str;
      if (str == "fieldasrxn"){
        havefield = true;
        iss >> fieldasrxn;
      }
    } else if (str[0] == '#' || str.empty()) 
      continue;
    else
      break;
  }
  if (!havefield)
    throw std::runtime_error("Error reading din file (fieldasrxn not present) " + din);

  // process the rest of the din file
  int n = 0;
  double c = std::stod(str), caux;
  std::vector<propinfo> info;
  propinfo aux;
  while (!ifile.eof() && !ifile.fail()){
    aux.names.clear();
    aux.coefs.clear();
    while (c != 0){
      if (line_get_double(ifile,line,str,caux))
        throw std::runtime_error("Error reading din file " + din);
      aux.coefs.push_back(c);
      aux.names.push_back(str);
      if (line_get_double(ifile,line,saux,c))
        throw std::runtime_error("Error reading din file " + din);
    }
    if (line_get_double(ifile,line,saux,aux.ref))
      throw std::runtime_error("Error reading din file " + din);

    // clean up and prepare next iteration
    info.push_back(aux);
    if (line_get_double(ifile,line,saux,c)){
      if (ifile.eof())
        break;
      else
        throw std::runtime_error("Error reading din file " + din);
    }
  }
  ifile.close();

  // begin the transaction
  begin_transaction();

  // process the entries
  std::unordered_map<std::string,boolean> used;
  for (int k = 0; k < info.size(); k++){
    std::unordered_map<std::string,std::string> smap;
    std::string skey;

    // insert structures
    int n = info[k].names.size();
    for (int i = 0; i < n; i++){
      smap.clear();
      if (used.find(info[k].names[i]) == used.end()){
        skey = key + "." + info[k].names[i];
        std::string filename = dir + "/" + info[k].names[i] + ".xyz";
        if (!fs::is_regular_file(filename))
          throw std::runtime_error("xyz file not found (" + filename + ") processing din file " + din);
        smap["XYZ"] = filename;
        smap["SET"] = key;
        insert("STRUCTURE",skey,smap);
        used[info[k].names[i]] = true;
      }
    }

    // insert property
    if (fieldasrxn != 0)
      skey = key + "." + info[k].names[fieldasrxn>0?fieldasrxn-1:n+fieldasrxn];
    else{
      skey = key + "." + info[k].names[0];
      for (int i = 1; i < info[k].names.size(); i++)
        skey += "_" + info[k].names[i];
    }
    smap.clear();
    smap["PROPERTY_TYPE"] = "energy_difference";
    smap["SET"] = key;
    smap["ORDERID"] = std::to_string(k+1);
    smap["NSTRUCTURES"] = std::to_string(n);
    smap["STRUCTURES"] = "";
    smap["COEFFICIENTS"] = "";
    for (int i = 0; i < n; i++){
      smap["STRUCTURES"] = smap["STRUCTURES"] + key + "." + info[k].names[i] + " ";
      smap["COEFFICIENTS"] = smap["COEFFICIENTS"] + to_string_precise(info[k].coefs[i]) + " ";
    }
    insert("PROPERTY",skey,smap);

    // insert the evaluation
    if (havemethod){
      smap.clear();
      smap["METHOD"] = kmap["METHOD"];
      smap["PROPERTY"] = skey;
      smap["VALUE"] = to_string_precise(info[k].ref);
      insert("EVALUATION",skey,smap);
    }
  }

  // commit the transaction
  commit_transaction();
}

// Delete items from the database
void sqldb::erase(const std::string &category, std::list<std::string> &tokens) {
  if (!db) throw std::runtime_error("A database file must be connected before using DELETE");

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
    id  = statement::STMT_DELETE_EVALUATION_ALL;
    key = statement::STMT_CUSTOM;
  } else if (category == "TERM") {
    all = statement::STMT_DELETE_TERM_ALL;
    id  = statement::STMT_DELETE_TERM_ALL;
    key = statement::STMT_CUSTOM;
  }

  // execute
  if (tokens.empty() || category == "TERM" || category == "EVALUATION")
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

  enum entrytype { t_str, t_int, t_double };

  std::vector<entrytype> types;
  std::vector<std::string> headers;
  std::vector<int> cols;
  bool dobib = false;
  statement::stmttype type;
  if (category == "LITREF"){
    headers = {"id", "key","authors","title","journal","volume","page","year","doi","description"};
    types   = {t_int,t_str,    t_str,  t_str,    t_str,   t_str, t_str, t_str,t_str,        t_str};
    cols    = {    0,    1,        2,      3,        4,       5,     6,     7,    8,            9};
    dobib = (!tokens.empty() && equali_strings(tokens.front(),"BIBTEX"));
    type = statement::STMT_LIST_LITREF;
  } else if (category == "SET"){
    headers = {"id", "key","property_type","litrefs","description"};
    types   = {t_int,t_str,          t_int,    t_str,        t_str};
    cols    = {    0,    1,              2,        3,            4};
    type = statement::STMT_LIST_SET;
  } else if (category == "METHOD"){
    headers = { "id","key","gaussian_keyword","litrefs","description"};
    types   = {t_int,t_str,             t_str,    t_str,        t_str};
    cols    = {    0,    1,                 2,        3,            4};
    type = statement::STMT_LIST_METHOD;
  } else if (category == "STRUCTURE"){
    headers = { "id","key","set","ismolecule","charge","multiplicity","nat"};
    types   = {t_int,t_str,t_int,       t_int,   t_int,         t_int,t_int};
    cols    = {    0,    1,     2,          3,       4,             5,    6};
    type = statement::STMT_LIST_STRUCTURE;
  } else if (category == "PROPERTY"){
    headers = { "id","key","property_type","setid","orderid","nstructures"};
    types   = {t_int,t_str,          t_int,  t_int,    t_int,        t_int};
    cols    = {    0,    1,              2,      3,        4,            5};
    type = statement::STMT_LIST_PROPERTY;
  } else if (category == "EVALUATION"){
    headers = {"methodid","propid", "value"};
    types   = {     t_int,   t_int,t_double};
    cols    = {         0,       1,       2};
    type = statement::STMT_LIST_EVALUATION;
  } else if (category == "TERM"){
    headers = {"methodid","propid", "atom",   "l","exponent", "value","maxcoef"};
    types   = {     t_int,   t_int,  t_int, t_int,  t_double,t_double, t_double};
    cols    = {         0,       1,      2,     3,         4,       5,        6};
    type = statement::STMT_LIST_TERM;
  } else { 
    throw std::runtime_error("Unknown LIST category: " + category);
  }

  // print table header
  int n = headers.size();
  if (!dobib){
    for (int i = 0; i < n; i++)
      os << "| " << headers[i];
    os << "|" << std::endl;
  }

  // print table body
  std::streamsize prec = os.precision(10);
  while (stmt[type]->step() != SQLITE_DONE){
    for (int i = 0; i < n; i++){
      if (types[i] == t_str){
        const unsigned char *field = sqlite3_column_text(stmt[type]->ptr(), cols[i]);
        if (!dobib)
          os << "| " << (field?field:(const unsigned char*) "");
        else{
          if (headers[i] == "key")
            os << "@article{" << field << std::endl;
          else if (field)
            os << " " << headers[i] << "={" << field << "}," << std::endl;
        }
      } else if (types[i] == t_int && !dobib){
        os << "| " << sqlite3_column_int(stmt[type]->ptr(), cols[i]);
      } else if (types[i] == t_double && !dobib){
        os << "| " << sqlite3_column_double(stmt[type]->ptr(), cols[i]);
      }
    }
    if (!dobib)
      os << "|" << std::endl;
    else
      os << "}" << std::endl;
  }
  os.precision(prec);
}

// List sets of properties in the database (din format)
void sqldb::list_din(std::unordered_map<std::string,std::string> &kmap){
  if (!db) throw std::runtime_error("A database file must be connected before using LIST DIN");

  // get the directory
  std::string dir = fetch_directory(kmap);

  // get the list of sets
  auto im = kmap.find("SET");
  std::vector<int> idset;
  if (im != kmap.end()){
    std::list<std::string> vlist = list_all_words(im->second);
    for (auto it = vlist.begin(); it != vlist.end(); ++it){
      if (isinteger(*it))
        idset.push_back(std::stoi(*it));
      else
        idset.push_back(find_id_from_key(*it,statement::STMT_QUERY_SET));
    }
  } else {
    statement *st = stmt[statement::STMT_LIST_SET];
    while (st->step() != SQLITE_DONE)
      idset.push_back(sqlite3_column_int(st->ptr(), 0));
  }
  if (idset.empty())
    throw std::runtime_error("No sets found in LIST DIN");

  // get the set names
  statement stname(db,statement::STMT_CUSTOM,R"SQL(
SELECT key FROM Sets WHERE id = ?1;
)SQL");
  std::vector<std::string> nameset;
  for (int i = 0; i < idset.size(); i++){
    stname.bind(1,idset[i]);
    stname.step();
    std::string name = (char *) sqlite3_column_text(stname.ptr(), 0);
    nameset.push_back(name);
    stname.reset();
  }

  // get the method and prepare the statement
  statement st(db);
  im = kmap.find("METHOD");
  int methodid = 0;
  if (im != kmap.end()){
    // get the method ID
    if (isinteger(im->second))
      methodid = std::stoi(im->second);
    else
      methodid = find_id_from_key(im->second,statement::STMT_QUERY_METHOD);

    // prepare the statement text
    std::string sttext = R"SQL(
SELECT Properties.nstructures, Properties.structures, Properties.coefficients, Evaluations.value
FROM Properties
INNER JOIN Evaluations ON (Properties.id = Evaluations.propid)
INNER JOIN Methods ON (Evaluations.methodid = Methods.id)
WHERE Properties.setid = :SET AND Methods.id = )SQL";
    sttext = sttext + std::to_string(methodid) + " " + R"SQL(
ORDER BY Properties.orderid;
)SQL";
    st.recycle(statement::STMT_CUSTOM,sttext);
  } else {
    // no method was given - zeros as reference values
    std::string sttext = R"SQL(
SELECT Properties.nstructures, Properties.structures, Properties.coefficients
FROM Properties
WHERE Properties.setid = :SET 
ORDER BY Properties.orderid;
)SQL";
    st.recycle(statement::STMT_CUSTOM,sttext);
  }

  // the statement to fetch a structure name
  stname.recycle(statement::STMT_CUSTOM,R"SQL(
SELECT key FROM Structures WHERE id = ?1;
)SQL");

  for (int i = 0; i < idset.size(); i++){
    // open and write the din header
    std::string fname = dir + "/" + nameset[i] + ".din";
    std::ofstream ofile(fname,std::ios::trunc);
    if (ofile.fail()) 
      throw std::runtime_error("Error writing din file " + fname);
    std::streamsize prec = ofile.precision(10);
    ofile << "# din file crated by acpdb" << std::endl;
    ofile << "# setid = " << idset[i] << std::endl;
    ofile << "# setname = " << nameset[i] << std::endl;
    ofile << "# reference id = " << methodid << std::endl;
  
    // step over the components of this set
    st.bind((char *) ":SET",idset[i]);
    while (st.step() != SQLITE_DONE){
      int nstr = sqlite3_column_int(st.ptr(),0);
      int *str = (int *) sqlite3_column_blob(st.ptr(),1);
      double *coef = (double *) sqlite3_column_blob(st.ptr(),2);

      for (int j = 0; j < nstr; j++){
        stname.bind(1,str[j]);
        stname.step();
        std::string name = (char *) sqlite3_column_text(stname.ptr(), 0);
        ofile << coef[j] << std::endl;
        ofile << name << std::endl;
        stname.reset();
      }
      ofile << "0" << std::endl;
      
      if (methodid > 0){
        double value = sqlite3_column_double(st.ptr(),3);
        ofile << value << std::endl;
      } else {
        ofile << "0.0" << std::endl;
      }
    }
  }
}

// Verify the consistency of the database
void sqldb::verify(std::ostream &os){
  if (!db) throw std::runtime_error("A database file must be connected before using VERIFY");

  // check litrefs in sets
  statement stmt(db,statement::STMT_CUSTOM,R"SQL(
SELECT litrefs,key FROM Sets;
)SQL");
  while (stmt.step() != SQLITE_DONE){
    const char *field_s = (char *) sqlite3_column_text(stmt.ptr(), 0);
    if (!field_s) continue;

    std::string field = std::string(field_s);
    std::list<std::string> tokens = list_all_words(field);
    for (auto it = tokens.begin(); it != tokens.end(); it++){
      if (!find_id_from_key(*it,statement::STMT_QUERY_LITREF))
        os << "LITREF (" + *it + ") in SET (" + std::string((char *) sqlite3_column_text(stmt.ptr(), 1)) + ") not found" << std::endl;
    }
  }

  // check litrefs in methods
  stmt.recycle(statement::STMT_CUSTOM,R"SQL(
SELECT litrefs,key FROM Methods;
)SQL");
  while (stmt.step() != SQLITE_DONE){
    const char *field_s = (char *) sqlite3_column_text(stmt.ptr(), 0);
    if (!field_s) continue;

    std::string field = std::string(field_s);
    std::list<std::string> tokens = list_all_words(field);
    for (auto it = tokens.begin(); it != tokens.end(); it++){
      if (!find_id_from_key(*it,statement::STMT_QUERY_LITREF))
        os << "LITREF (" + *it + ") in METHODS (" + std::string((char *) sqlite3_column_text(stmt.ptr(), 1)) + ") not found" << std::endl;
    }
  }

  // check structures in properties
  stmt.recycle(statement::STMT_CUSTOM,R"SQL(
SELECT key,nstructures,structures FROM Properties;
)SQL");
  statement stcheck(db,statement::STMT_CUSTOM,R"SQL(
SELECT id FROM Structures WHERE id = ?1;
)SQL");
  while (stmt.step() != SQLITE_DONE){
    int n = sqlite3_column_int(stmt.ptr(), 1);

    const int *str = (int *)sqlite3_column_blob(stmt.ptr(), 2);
    for (int i = 0; i < n; i++){
      stcheck.bind(1,str[i]);
      stcheck.step();
      if (sqlite3_column_int(stcheck.ptr(),0) == 0)
        os << "STRUCTURES (" + std::to_string(str[i]) + ") in Properties (" + std::string((char *) sqlite3_column_text(stmt.ptr(), 0)) + ") not found" << std::endl;
      stcheck.reset();
    }
  }  
}

// Write input files for a database set
void sqldb::write_structures(std::unordered_map<std::string,std::string> &kmap, const acp &a){
  if (!db) 
    throw std::runtime_error("Error reading connected database");
  
  // unpack the gaussian keyword into a map for this method
  bool havemethod = (kmap.find("METHOD") != kmap.end());
  std::unordered_map<std::string,std::string> gmap = {};
  if (havemethod) gmap = get_gaussian_map(kmap["METHOD"]);

  // set
  bool haveset = (kmap.find("SET") != kmap.end());
  int setid = 0;
  if (haveset){
    std::string setname = kmap["SET"];
    setid = find_id_from_key(setname,statement::STMT_QUERY_SET);
    if (setid == 0)
      throw std::runtime_error("Unknown SET in write_structures");
  }

  // directory and pack number
  std::string dir = fetch_directory(kmap);
  int npack = 0;
  if (kmap.find("PACK") != kmap.end()) npack = std::stoi(kmap["PACK"]);

  // collect the structure indices for this set (better here than in
  // the Sets table).
  std::unordered_map<int,std::string> smap;
  if (havemethod){
    if (haveset){
      // method and set
      statement st(db,statement::STMT_CUSTOM,R"SQL(
SELECT Property_types.key, Properties.nstructures, Properties.structures
FROM Properties, Property_types
WHERE Properties.setid = ?1 AND Properties.property_type = Property_types.id )SQL");
      st.bind(1,setid);
      while (st.step() != SQLITE_DONE){
        int n = sqlite3_column_int(st.ptr(),1);
        const int *str = (int *)sqlite3_column_blob(st.ptr(), 2);
        for (int i = 0; i < n; i++)
          smap[str[i]] = (char *) sqlite3_column_text(st.ptr(), 0);
      }
    } else {
      // method but not set
      statement st(db,statement::STMT_CUSTOM,R"SQL(
SELECT Property_types.key, Properties.nstructures, Properties.structures
FROM Properties, Property_types
WHERE Properties.property_type = Property_types.id )SQL");
      while (st.step() != SQLITE_DONE){
        int n = sqlite3_column_int(st.ptr(),1);
        const int *str = (int *)sqlite3_column_blob(st.ptr(), 2);
        for (int i = 0; i < n; i++)
          smap[str[i]] = (char *) sqlite3_column_text(st.ptr(), 0);
      }
    }
  } else {
    if (haveset){
      // set but not method
      statement st(db,statement::STMT_CUSTOM,R"SQL(
SELECT Structures.id
FROM Structures, Sets
WHERE Structures.setid = Sets.id AND Sets.id = ?1;)SQL");
      st.bind(1,setid);
      while (st.step() != SQLITE_DONE)
        smap[sqlite3_column_int(st.ptr(),0)] = "xyz";
    } else {
      // neither method nor set
      statement st(db,statement::STMT_CUSTOM,"SELECT Structures.id FROM Structures;");
      while (st.step() != SQLITE_DONE)
        smap[sqlite3_column_int(st.ptr(),0)] = "xyz";
    }
  }
  
  // write the inputs
  write_many_structures(smap,gmap,dir,npack,a);
}

// Read data for the database or one of its subsets from a file, then
// compare to reference method refm.
void sqldb::read_and_compare(std::ostream &os, const std::string &file, const std::string &refm, std::unordered_map<std::string,std::string> &kmap){
  if (!db)
    throw std::runtime_error("A database file must be connected before using READ");

  // verify that we have a reference method
  if (refm.empty())
    throw std::runtime_error("The COMPARE keyword in READ must be followed by a known method");
  int methodid = find_id_from_key(refm,statement::STMT_QUERY_METHOD);
  if (!methodid)
    throw std::runtime_error("Unknown method in READ/COMPARE: " + refm);

  // set
  int sid = -1;
  if (kmap.find("SET") != kmap.end())
    sid = find_id_from_key(kmap["SET"],statement::STMT_QUERY_SET);

  // get the data from the file
  auto datmap = read_data_file(file,globals::ha_to_kcal);

  // fetch the reference method values from the DB and populate vectors
  std::vector<std::string> names_found;
  std::vector<std::string> names_missing_fromdb;
  std::vector<std::string> names_missing_fromdat;
  std::vector<double> refvalues, datvalues;
  statement stkey(db,statement::STMT_CUSTOM,"SELECT key FROM Structures WHERE id = ?1;");
  std::string sttext;
  if (sid >= 0){
    sttext = R"SQL(
SELECT Properties.key, Evaluations.value, Properties.nstructures, Properties.structures, Properties.coefficients
FROM Properties
LEFT OUTER JOIN Evaluations ON (Evaluations.propid = Properties.id AND Evaluations.methodid = :METHOD)
WHERE Properties.setid = :SET
ORDER BY Properties.id;)SQL";
  } else {
    sttext = R"SQL(
SELECT Properties.key, Evaluations.value, Properties.nstructures, Properties.structures, Properties.coefficients
FROM Properties
LEFT OUTER JOIN Evaluations ON (Evaluations.propid = Properties.id AND Evaluations.methodid = :METHOD)
ORDER BY Properties.id;)SQL";
  }
  statement st(db,statement::STMT_CUSTOM,sttext);
  if (sid >= 0)
    st.bind((char *) ":SET",sid);
  st.bind((char *) ":METHOD",methodid);
  while (st.step() != SQLITE_DONE){
    // check if the evaluation is available in the database
    std::string key = (char *) sqlite3_column_text(st.ptr(),0);
    if (sqlite3_column_type(st.ptr(),1) == SQLITE_NULL){
      names_missing_fromdb.push_back(key);
      continue;
    } 

    // check if the components are in the data file
    int nstr = sqlite3_column_int(st.ptr(),2);
    int *istr = (int *) sqlite3_column_blob(st.ptr(),3);
    double *coef = (double *) sqlite3_column_blob(st.ptr(),4);
    double value = 0;
    bool found = true;
    for (int i = 0; i < nstr; i++){
      stkey.reset();
      stkey.bind(1,istr[i]);
      stkey.step();
      std::string strname = (char *) sqlite3_column_text(stkey.ptr(),0);
      if (datmap.find(strname) == datmap.end()){
        found = false;
        break;
      }
      value += coef[i] * datmap[strname];
    }

    // populate the vectors
    if (!found){
      names_missing_fromdat.push_back(key);
    } else {
      names_found.push_back(key);
      refvalues.push_back(sqlite3_column_double(st.ptr(),1));
      datvalues.push_back(value);
    }
  }
  datmap.clear();

  // output the header and the statistics
  os << "# Evaluation: " << file << std::endl
     << "# Reference: " << refm << std::endl;
  if (!names_missing_fromdat.empty() || !names_missing_fromdat.empty())
    os << "# Statistics: " << "(partial, missing: " 
       << names_missing_fromdat.size() << " from file, "
       << names_missing_fromdb.size() << " from database)" << std::endl;
  else
    os << "# Statistics: " << std::endl;

  std::streamsize prec = os.precision(7);
  os << std::fixed;
  os.precision(8);
  if (refvalues.empty())
    os << "#   (not enough data for statistics)" << std::endl;
  else{
    // calculate the statistics for the given set
    double wrms, rms, mae, mse;
    calc_stats(datvalues,refvalues,{},wrms,rms,mae,mse);

    os << "# " << std::left << std::setw(10) << "all"
       << std::left << "  rms = " << std::right << std::setw(12) << rms
       << std::left << "  mae = " << std::right << std::setw(12) << mae
       << std::left << "  mse = " << std::right << std::setw(12) << mse
       << std::left << " wrms = " << std::right << std::setw(12) << wrms
       << std::endl;
  }
  os.precision(prec);

  // output the results
  output_eval(os,{},names_found,{},datvalues,file,refvalues,refm);
  if (!names_missing_fromdb.empty()){
    os << "## The following properties are missing from the DATABASE:" << std::endl;
    for (int i = 0; i < names_missing_fromdb.size(); i++)
      os << "## " << names_missing_fromdb[i] << std::endl;
  }
  if (!names_missing_fromdat.empty()){
    os << "## The following properties are missing from the FILE:" << std::endl;
    for (int i = 0; i < names_missing_fromdat.size(); i++)
      os << "## " << names_missing_fromdat[i] << std::endl;
  }
}

// Read data from a file, then insert as evaluation of the argument method.
void sqldb::read_and_insert(const std::string &file, const std::string &method){
  if (!db)
    throw std::runtime_error("A database file must be connected before using READ");

  // verify that we have a reference method
  if (method.empty())
    throw std::runtime_error("The COMPARE keyword in READ must be followed by a known method");
  int methodid = find_id_from_key(method,statement::STMT_QUERY_METHOD);
  if (!methodid)
    throw std::runtime_error("Unknown method in READ/COMPARE: " + method);

  // get the data from the file
  auto datmap = read_data_file(file,globals::ha_to_kcal);

  // build the property map
  std::unordered_map<int,double> propmap;
  statement stkey(db,statement::STMT_CUSTOM,"SELECT key FROM Structures WHERE id = ?1;");
  statement st(db,statement::STMT_CUSTOM,R"SQL(
SELECT Properties.id, Properties.nstructures, Properties.structures, Properties.coefficients
FROM Properties
ORDER BY Properties.id;)SQL");
  while (st.step() != SQLITE_DONE){
    int propid = sqlite3_column_int(st.ptr(),0);
    int nstr = sqlite3_column_int(st.ptr(),1);
    int *istr = (int *) sqlite3_column_blob(st.ptr(),2);
    double *coef = (double *) sqlite3_column_blob(st.ptr(),3);
    double value = 0;
    bool found = true;
    for (int i = 0; i < nstr; i++){
      stkey.reset();
      stkey.bind(1,istr[i]);
      stkey.step();
      std::string strname = (char *) sqlite3_column_text(stkey.ptr(),0);
      if (datmap.find(strname) == datmap.end()){
        found = false;
        break;
      }
      value += coef[i] * datmap[strname];
    }
    if (found)
      propmap[propid] = value;
  }
  datmap.clear();

  // begin the transaction
  begin_transaction();

  st.recycle(statement::STMT_CUSTOM,R"SQL(
INSERT INTO Evaluations (methodid,propid,value)
VALUES(:METHOD,:PROPID,:VALUE);
)SQL");
  for (auto it = propmap.begin(); it != propmap.end(); it++){
    st.reset();
    st.bind((char *) ":METHOD",methodid);
    st.bind((char *) ":PROPID",it->first);
    st.bind((char *) ":VALUE",it->second);
    if (st.step() != SQLITE_DONE){
      std::cout << "method = " << method << " (" << methodid << ")" << std::endl;
      std::cout << "propid = " << it->first << std::endl;
      std::cout << "value = " << it->second << std::endl;
      throw std::runtime_error("Failed inserting data in the database (READ INSERT)");
    }
  }

  // commit the transaction
  commit_transaction();
}

// Write the structures with IDs given by the keys in smap. The
// values of smap give the types (xyz for an xyz file or
// energy_difference, etc. for an input file).  gmap, dir, a: see
// write_one_structure.
void sqldb::write_many_structures(std::unordered_map<int,std::string> smap, const std::unordered_map<std::string,std::string> gmap/*={}*/, 
                                  const std::string &dir/*="./"*/, int npack/*=0*/, const acp &a/*={}*/){

  if (npack <= 0 || npack >= smap.size()){
    for (auto it = smap.begin(); it != smap.end(); it++)
      write_one_structure(it->first,it->second,gmap,dir,a);
  } else {
    unsigned long div = smap.size() / (unsigned long) npack;
    if (smap.size() % npack != 0) div++;
    int slen = digits((int) div);

    // build the random vector
    int n = 0;
    std::vector<int> srand;
    srand.resize(smap.size());
    for (auto it = smap.begin(); it != smap.end(); it++)
      srand[n++] = it->first;
    std::random_shuffle(srand.begin(),srand.end());

    n = 0;
    int ipack = 0;
    std::list<fs::path> written;
    for (int i = 0; i < srand.size(); i++){
      written.push_back(fs::path(write_one_structure(srand[i],smap[srand[i]],gmap,dir,a)));

      // create a new package if written has npack items or we are about to finish
      if (++n % npack == 0 || i == srand.size()-1 && !written.empty()){
        std::string tarcmd = std::to_string(++ipack);
        tarcmd.insert(0,slen-tarcmd.size(),'0');
        tarcmd = "tar cJf " + dir + "/pack_" + tarcmd + ".tar.xz -C " + dir;
        for (auto iw = written.begin(); iw != written.end(); iw++)
          tarcmd = tarcmd + " " + iw->string();

        if (system(tarcmd.c_str()))
          throw std::runtime_error("Error running tar command on input files");
        for (auto iw = written.begin(); iw != written.end(); iw++)
          remove(dir / *iw);
        written.clear();
      }
    }
  }
}

// Write the structure id in the database. Options have the same
// meaning as in write_many_structures. Returns the written filename. 
std::string sqldb::write_one_structure(int id, const std::string type, const std::unordered_map<std::string,std::string> gmap/*={}*/, const std::string &dir/*="./"*/, const acp &a/*={}*/){

  // get the structure and build the file name
  statement st(db,statement::STMT_CUSTOM,R"SQL(
SELECT id, key, setid, ismolecule, charge, multiplicity, nat, cell, zatoms, coordinates
FROM Structures WHERE id = ?1;
)SQL");
  st.bind(1,id);
  st.step();
  std::string fileroot = std::string((char *) sqlite3_column_text(st.ptr(), 1));
  std::string name = dir + "/";
  name += fileroot;
  bool ismolecule = sqlite3_column_int(st.ptr(), 3);
  bool isxyz = equali_strings(type,"xyz");

  // build the molecule/crystal structure
  structure s;
  s.readdbrow(st.ptr());

  // append the extension and open the file stream
  std::string ext;
  if (ismolecule) {
    if (isxyz)
      ext = ".xyz";
    else
      ext = ".gjf";
  } else {
    throw std::runtime_error("Cannot do crystals yet");
  }
  name += ext;
  std::ofstream ofile(name,std::ios::trunc);

  // write the input file
  if (isxyz && ismolecule) {
    s.writexyz(ofile);
  } else if (equali_strings(type,"energy_difference") && ismolecule){
    if (gmap.find("METHOD") == gmap.end()) throw std::runtime_error("This method doesn ot have an associated Gaussian method keyword");
    std::string methodk = gmap.at("METHOD");
    std::string gbsk = "";
    if (gmap.find("GBS") != gmap.end()) gbsk = gmap.at("GBS");

    if (s.writegjf(ofile,methodk,gbsk,fileroot,a))
      throw std::runtime_error("Error writing input file: " + name);
  } else {
    throw std::runtime_error("Unknown combination of structure and property type");
  }

  // clean up and exit
  ofile.close();
  return (fileroot + ext);
}

