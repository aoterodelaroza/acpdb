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

// Find the property type ID corresponding to the key in the database table.
// If toupper, uppercase the key before fetching the ID from the table. If
// no such key is found in the table, return 0.
int sqldb::find_id_from_key(const std::string &key,const std::string &table,bool toupper/*=false*/){
  statement st(db,statement::STMT_CUSTOM,"SELECT id FROM " + table + " WHERE key = ?1;");
  if (toupper){
    std::string ukey = key;
    uppercase(ukey);
    st.bind(1,ukey);
  } else {
    st.bind(1,key);
  }
  st.step();
  int rc = sqlite3_column_int(st.ptr(),0);
  st.reset();
  return rc;
}

// Find the key corresponding to the ID in the database table. If
// toupper, the key is returned in uppercase. If no such ID is found
// in the table, return an empty string.
std::string sqldb::find_key_from_id(const int id,const std::string &table,bool toupper/*=false*/){
  statement st(db,statement::STMT_CUSTOM,"SELECT key FROM " + table + " WHERE id = ?1;");
  st.bind(1,id);
  st.step();
  const unsigned char *result = sqlite3_column_text(st.ptr(),0);
  if (!result)
    return "";

  std::string key = (char *) result;
  if (toupper)
    uppercase(key);
  return key;
}

// Parse the input string input and find if it is a number or a
// key. If it is a key, find the corresponding ID in the table and
// return both the key and id. If it is a number, find the key in the
// table and return the key and id. If toupperi, uppercase the input
// before parsing it. If touppero, uppercase the output key. Returns the
// ID if succeeded, 0 if failed.
int sqldb::get_key_and_id(const std::string &input, const std::string &table,
                          std::string &key, int &id, bool toupperi/*=false*/, bool touppero/*=false*/){

  if (isinteger(input)){
    id = std::stoi(input);
    key = find_key_from_id(id,table,touppero);
    if (key.length() == 0)
      return 0;
  } else {
    key = input;
    id = find_id_from_key(key,table,toupperi);
    if (touppero)
      uppercase(key);
  }
  return id;
}

// Get the Gaussian map from the method key
std::unordered_map<std::string,std::string> sqldb::get_program_map(const std::string &methodkey, const std::string &program){
  std::string text;
  if (equali_strings(program,"gaussian"))
    text = "SELECT gaussian_keyword FROM Methods WHERE key=?1;";
  else if (equali_strings(program,"psi4"))
    text = "SELECT psi4_keyword FROM Methods WHERE key=?1;";
  else
    throw std::runtime_error("Unknown program: " + program);

  statement st(db,statement::STMT_CUSTOM,text);
  st.bind(1,methodkey);
  st.step();
  if (sqlite3_column_type(st.ptr(),0) == SQLITE_NULL)
    throw std::runtime_error("METHOD is unknown or has no associated " + program + " keyword: " + methodkey);
  std::string gkeyw = (char *) sqlite3_column_text(st.ptr(), 0);
  std::unordered_map<std::string,std::string> gmap = map_keyword_pairs(gkeyw,';',true);

  gmap["PROGRAM"] = program;
  uppercase(gmap["PROGRAM"]);

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
    throw std::runtime_error("Need a database file name to connect");

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

// Insert a literature reference by manually giving the data
void sqldb::insert_litref(std::ostream &os, const std::string &key, std::unordered_map<std::string,std::string> &kmap) {
  if (!db) throw std::runtime_error("A database file must be connected before using INSERT LITREF");
  if (key.empty())
    throw std::runtime_error("Empty key in INSERT LITREF");

  // statement
  statement st(db,statement::STMT_CUSTOM,R"SQL(
INSERT INTO Literature_refs (key,authors,title,journal,volume,page,year,doi,description)
       VALUES(:KEY,:AUTHORS,:TITLE,:JOURNAL,:VOLUME,:PAGE,:YEAR,:DOI,:DESCRIPTION);
)SQL");

  // bind
  st.bind((char *) ":KEY",key,false);
  std::forward_list<std::string> vlist = {"AUTHORS","TITLE","JOURNAL","VOLUME","PAGE","YEAR","DOI","DESCRIPTION"};

  for (auto it = vlist.begin(); it != vlist.end(); ++it){
    auto im = kmap.find(*it);
    if (im != kmap.end())
      st.bind(":" + *it,im->second);
  }

  os << "# INSERT LITREF " << key << std::endl;

  // submit
  st.step();
}

// Insert a set by manually giving the data
void sqldb::insert_set(std::ostream &os, const std::string &key, std::unordered_map<std::string,std::string> &kmap) {
  if (!db) throw std::runtime_error("A database file must be connected before using INSERT SET");
  if (key.empty())
    throw std::runtime_error("Empty key in INSERT SET");
  if (kmap.find("XYZ") != kmap.end() && kmap.find("DIN") != kmap.end())
    throw std::runtime_error("XYZ and DIN options in SET are incompatible");

  // statement
  statement st(db,statement::STMT_CUSTOM,R"SQL(
INSERT INTO Sets (key,litrefs,description)
       VALUES(:KEY,:LITREFS,:DESCRIPTION);
)SQL");

  // bind
  st.bind((char *) ":KEY",key);
  std::unordered_map<std::string,std::string>::const_iterator im;
  if ((im = kmap.find("DESCRIPTION")) != kmap.end())
    st.bind((char *) ":DESCRIPTION",im->second);
  if ((im = kmap.find("LITREFS")) != kmap.end()){
    std::list<std::string> tokens = list_all_words(im->second);
    std::string str = "";
    for (auto it = tokens.begin(); it != tokens.end(); it++){
      int idx = find_id_from_key(*it,"Literature_refs");
      if (!idx)
        throw std::runtime_error("Litref not found: " + *it);
      str = str + *it + " ";
    }
    st.bind((char *) ":LITREFS",str);
  }

  os << "# INSERT SET " << key << std::endl;

  // submit
  st.step();

  // interpret the xyz keyword
  if (kmap.find("XYZ") != kmap.end())
    insert_set_xyz(os, key, kmap);

  // interpret the din/directory/method keyword combination
  if (kmap.find("DIN") != kmap.end())
    insert_set_din(os, key, kmap);
}

// Insert a method by manually giving the data
void sqldb::insert_method(std::ostream &os, const std::string &key, std::unordered_map<std::string,std::string> &kmap){
  if (!db) throw std::runtime_error("A database file must be connected before using INSERT METHOD");
  if (key.empty())
    throw std::runtime_error("Empty key in INSERT METHOD");

  // statement
  statement st(db,statement::STMT_CUSTOM,R"SQL(
INSERT INTO Methods (key,gaussian_keyword,psi4_keyword,litrefs,description)
       VALUES(:KEY,:GAUSSIAN_KEYWORD,:PSI4_KEYWORD,:LITREFS,:DESCRIPTION);
)SQL");

  // bind
  st.bind((char *) ":KEY",key,false);
  std::forward_list<std::string> vlist = {"GAUSSIAN_KEYWORD","PSI4_KEYWORD","LITREFS","DESCRIPTION"};
  for (auto it = vlist.begin(); it != vlist.end(); ++it){
    auto im = kmap.find(*it);
    if (im != kmap.end())
      st.bind(":" + *it,im->second);
  }

  os << "# INSERT METHOD " << key << std::endl;

  // submit
  st.step();
}

// Insert a property by manually giving the data
void sqldb::insert_structure(std::ostream &os, const std::string &key, std::unordered_map<std::string,std::string> &kmap){
  if (!db) throw std::runtime_error("A database file must be connected before using INSERT STRUCTURE");
  if (key.empty())
    throw std::runtime_error("Empty key in INSERT STRUCTURE");

  // read the molecular structure
  structure s;
  std::unordered_map<std::string,std::string>::const_iterator im;
  if ((im = kmap.find("FILE")) != kmap.end()){
    if (s.readfile(im->second))
      throw std::runtime_error("Error reading file: " + im->second);
  } else if (kmap.find("XYZ") != kmap.end() && kmap.find("POSCAR") != kmap.end()) {
    throw std::runtime_error("XYZ and POSCAR are both present in INSERT STRUCTURE");
  } else if ((im = kmap.find("XYZ")) != kmap.end()){
    if (s.readxyz(im->second))
      throw std::runtime_error("Error reading xyz file: " + im->second);
  } else if ((im = kmap.find("POSCAR")) != kmap.end()){
    if (s.readposcar(im->second))
      throw std::runtime_error("Error reading POSCAR file: " + im->second);
  } else {
    throw std::runtime_error("A structure must be given in INSERT STRUCTURE");
  }

  // bind
  statement st(db,statement::STMT_CUSTOM,R"SQL(
INSERT INTO Structures (key,setid,ismolecule,charge,multiplicity,nat,cell,zatoms,coordinates)
       VALUES(:KEY,:SETID,:ISMOLECULE,:CHARGE,:MULTIPLICITY,:NAT,:CELL,:ZATOMS,:COORDINATES);
)SQL");

  int nat = s.get_nat();
  st.bind((char *) ":KEY",key,false);
  st.bind((char *) ":ISMOLECULE",s.ismolecule()?1:0,false);
  st.bind((char *) ":NAT",nat,false);
  st.bind((char *) ":CHARGE",s.get_charge(),false);
  st.bind((char *) ":MULTIPLICITY",s.get_mult(),false);
  if ((im = kmap.find("SET")) != kmap.end()){
    int setid;
    std::string setkey;
    if (get_key_and_id(im->second,"Sets",setkey,setid))
      st.bind((char *) ":SETID",setid);
    else
      throw std::runtime_error("Invalid set ID or key in INSERT STRUCTURE");
  } else
    throw std::runtime_error("A set is required in INSERT STRUCTURE");
  if (!s.ismolecule())
    st.bind((char *) ":CELL",(void *) s.get_r(),false,9 * sizeof(double));
  st.bind((char *) ":ZATOMS",(void *) s.get_z(),false,nat * sizeof(unsigned char));
  st.bind((char *) ":COORDINATES",(void *) s.get_x(),false,3 * nat * sizeof(double));

  os << "# INSERT STRUCTURE " << key << std::endl;

  // submit
  st.step();
}

// Insert a property by manually giving the data
void sqldb::insert_property(std::ostream &os, const std::string &key, std::unordered_map<std::string,std::string> &kmap){
  if (!db) throw std::runtime_error("A database file must be connected before using INSERT PROPERTY");
  if (key.empty())
    throw std::runtime_error("Empty key or prefix in INSERT PROPERTY");

  // some variables
  std::unordered_map<std::string,std::string>::const_iterator im1, im2;
  std::list<std::string> tok1;
  std::vector<double> tok2;

  // prepared statements
  statement st(db,statement::STMT_CUSTOM,R"SQL(
INSERT INTO Properties (id,key,property_type,setid,orderid,nstructures,structures,coefficients)
       VALUES(:ID,:KEY,:PROPERTY_TYPE,:SETID,:ORDERID,:NSTRUCTURES,:STRUCTURES,:COEFFICIENTS)
)SQL");
  statement ststr(db,statement::STMT_CUSTOM,R"SQL(
SELECT id, key FROM Structures WHERE setid = ?1 ORDER BY id;
)SQL");

  // property type
  int ppid = -1;
  if ((im1 = kmap.find("PROPERTY_TYPE")) != kmap.end()){
    std::string ppkey;
    if (!get_key_and_id(im1->second,"Property_types",ppkey,ppid,true,true))
      throw std::runtime_error("Invalid property_type ID or key in INSERT PROPERTY");
  } else
    throw std::runtime_error("A PROPERTY_TYPE is required in INSERT PROPERTY");
  st.bind((char *) ":PROPERTY_TYPE",ppid);

  // set ID
  int setid = -1;
  if ((im1 = kmap.find("SET")) != kmap.end()){
    std::string setkey;
    if (!get_key_and_id(im1->second,"Sets",setkey,setid))
      throw std::runtime_error("Invalid SET ID or key in INSERT PROPERTY");
  } else
    throw std::runtime_error("A SET is required in INSERT PROPERTY");
  st.bind((char *) ":SETID",setid);
  ststr.bind(1,setid);

  // whether we are inserting only one or prefixing; set the key and orderid
  bool justone = (kmap.find("ORDER") != kmap.end());
  if (justone){
    st.bind((char *) ":KEY",key,false);
    st.bind((char *) ":ORDERID",std::stoi(kmap.find("ORDER")->second));

    // parse structures and coefficients
    im1 = kmap.find("STRUCTURES");
    if (im1 == kmap.end())
      throw std::runtime_error("No structures given in INSERT PROPERTY");
    tok1 = list_all_words(im1->second);
    im2 = kmap.find("COEFFICIENTS");
    if (im2 != kmap.end())
      tok2 = list_all_doubles(im2->second);

    // number of structures
    int nstructures = tok1.size();
    st.bind((char *) ":NSTRUCTURES",nstructures);

    // bind the structures
    {
      int n = 0;
      int *str = new int[nstructures];
      for (auto it = tok1.begin(); it != tok1.end(); it++){
        int idx = 0;
        if (isinteger(*it))
          idx = std::stoi(*it);
        else
          idx = find_id_from_key(*it,"Structures");

        if (!idx)
          throw std::runtime_error("Structure not found: " + *it);
        str[n++] = idx;
      }
      st.bind((char *) ":STRUCTURES",(void *) str,true,nstructures * sizeof(int));
      delete str;
    }

    // bind the coefficients
    if (!tok2.empty()) {
      if (nstructures != tok2.size())
        throw std::runtime_error("Number of coefficients does not match number of structures in INSERT PROPERTY");
      st.bind((char *) ":COEFFICIENTS",(void *) &tok2.front(),true,nstructures * sizeof(double));
    }

    os << "# INSERT PROPERTY " << key << std::endl;

    // submit
    st.step();
  } else {
    // coefficients
    const double coef[1] = {1.0};

    // begin the transaction
    begin_transaction();

    int n = 0;
    while (ststr.step() != SQLITE_DONE){
      n++;

      // bind
      int id[1] = {sqlite3_column_int(ststr.ptr(),0)};
      std::string str = (char *) sqlite3_column_text(ststr.ptr(), 1);
      std::string newkey = key + str;
      st.bind((char *) ":KEY",newkey,false);
      st.bind((char *) ":PROPERTY_TYPE",ppid);
      st.bind((char *) ":SETID",setid);
      st.bind((char *) ":ORDERID",n);
      st.bind((char *) ":NSTRUCTURES",1);
      st.bind((char *) ":STRUCTURES",(void *) &id,false,1 * sizeof(int));
      st.bind((char *) ":COEFFICIENTS",(void *) &coef,false,sizeof(double));

      os << "# INSERT PROPERTY " << newkey << std::endl;
      st.step();
    }

    // commit the transaction
    commit_transaction();
  }
}

// Insert an evaluation by manually giving the data
void sqldb::insert_evaluation(std::ostream &os, std::unordered_map<std::string,std::string> &kmap){
  if (!db) throw std::runtime_error("A database file must be connected before using INSERT EVALUATION");

  // bind
  std::unordered_map<std::string,std::string>::const_iterator im;
  statement st(db,statement::STMT_CUSTOM,R"SQL(
INSERT INTO Evaluations (methodid,propid,value)
       VALUES(:METHODID,:PROPID,:VALUE)
)SQL");

  std::string methodkey;
  int methodid;
  if ((im = kmap.find("METHOD")) != kmap.end()){
    if (!get_key_and_id(im->second,"Methods",methodkey,methodid))
      throw std::runtime_error("Invalid METHOD ID or key in INSERT EVALUATION");
  } else
    throw std::runtime_error("A METHOD is required in INSERT EVALUATION");
  st.bind((char *) ":METHODID",methodid);

  std::string propkey;
  int propid;
  if ((im = kmap.find("PROPERTY")) != kmap.end()){
    if (!get_key_and_id(im->second,"Properties",propkey,propid))
      throw std::runtime_error("Invalid PROPERTY ID or key in INSERT EVALUATION");
  } else
    throw std::runtime_error("A PROPERTY is required in INSERT EVALUATION");
  st.bind((char *) ":PROPID",propid);

  if (kmap.find("VALUE") == kmap.end())
    throw std::runtime_error("A value must be given in INSERT EVALUATION");
  std::vector<double> value = list_all_doubles(kmap.find("VALUE")->second);
  st.bind((char *) ":VALUE",(void *) &value[0],false,value.size()*sizeof(double));

  os << "# INSERT EVALUATION (method=" << methodkey << ";property=" << propkey << ")" << std::endl;

  // submit
  st.step();
}

// Insert a term by manually giving the data
void sqldb::insert_term(std::ostream &os, std::unordered_map<std::string,std::string> &kmap){
  if (!db) throw std::runtime_error("A database file must be connected before using INSERT TERM");

  const std::unordered_map<char,int> angmom = {{'s',0},{'p',1},{'d',2},{'f',3},{'g',4},{'h',5}};

  // build command
  std::string cmd;
  if (kmap.find("VALUE") != kmap.end())
    cmd = R"SQL(INSERT INTO Terms (methodid,propid,atom,l,exponent,value,maxcoef)
       VALUES(:METHODID,:PROPID,:ATOM,:L,:EXPONENT,:VALUE,:MAXCOEF))SQL";
  else if (kmap.find("MAXCOEF") != kmap.end())
    cmd = R"SQL(UPDATE Terms SET maxcoef = :MAXCOEF WHERE methodid = :METHODID AND propid = :PROPID
                AND atom = :ATOM AND l = :L AND exponent = :EXPONENT)SQL";
  else
    throw std::runtime_error("A VALUE or MAXCOEF must be given in INSERT TERM");

  // bind
  std::unordered_map<std::string,std::string>::const_iterator im;
  statement st(db,statement::STMT_CUSTOM,cmd);

  std::string methodkey;
  int methodid;
  if ((im = kmap.find("METHOD")) != kmap.end()){
    if (!get_key_and_id(im->second,"Methods",methodkey,methodid))
      throw std::runtime_error("Invalid METHOD ID or key in INSERT EVALUATION");
  } else
    throw std::runtime_error("A METHOD is required in INSERT EVALUATION");
  st.bind((char *) ":METHODID",methodid);

  std::string propkey;
  int propid;
  if ((im = kmap.find("PROPERTY")) != kmap.end()){
    if (!get_key_and_id(im->second,"Properties",propkey,propid))
      throw std::runtime_error("Invalid PROPERTY ID or key in INSERT EVALUATION");
  } else
    throw std::runtime_error("A PROPERTY is required in INSERT EVALUATION");
  st.bind((char *) ":PROPID",propid);

  if ((im = kmap.find("ATOM")) != kmap.end())
    if (isinteger(im->second))
      st.bind((char *) ":ATOM",std::stoi(im->second));
    else{
      int iz = zatguess(im->second);
      if (!iz)
        throw std::runtime_error("Unknown atom in INSERT TERM");
      st.bind((char *) ":ATOM",iz);
    }
  else
    throw std::runtime_error("An atom must be given in INSERT TERM");
  if ((im = kmap.find("L")) != kmap.end())
    if (isinteger(im->second))
      st.bind((char *) ":L",std::stoi(im->second));
    else{
      char l = std::tolower(im->second[0]);
      if (angmom.find(l) == angmom.end())
        throw std::runtime_error("Unknown angular momentum label in INSERT TERM");
      st.bind((char *) ":L",angmom.at(l));
    }
  else
    throw std::runtime_error("An angular momentum (l) must be given in INSERT TERM");
  if ((im = kmap.find("EXPONENT")) != kmap.end())
    st.bind((char *) ":EXPONENT",std::stod(im->second));
  else
    throw std::runtime_error("An exponent must be given in INSERT TERM");
  if ((im = kmap.find("VALUE")) != kmap.end()){
    std::vector<double> tok = list_all_doubles(im->second);
    st.bind((char *) ":VALUE",(void *) &tok.front(),true,tok.size() * sizeof(double));
  }
  if ((im = kmap.find("MAXCOEF")) != kmap.end())
    st.bind((char *) ":MAXCOEF",std::stod(im->second));

  os << "# INSERT TERM (method=" << methodkey << ";property=" << propkey
     << ";atom=" << kmap.find("ATOM")->second << ";l=" << kmap.find("L")->second
     << ";exponent=" << kmap.find("EXPONENT")->second << ")" << std::endl;

  // submit
  st.step();
}

// Read data from a file, then insert as evaluation of the argument method.
void sqldb::insert_calc(std::ostream &os, std::unordered_map<std::string,std::string> &kmap){
  if (!db)
    throw std::runtime_error("A database file must be connected before using INSERT CALC");

  // some variables
  std::unordered_map<std::string,std::string>::const_iterator im;

  // get the property_type
  std::string ptkey;
  int ptid;
  if ((im = kmap.find("PROPERTY_TYPE")) != kmap.end()){
    if (!get_key_and_id(im->second,"Property_types",ptkey,ptid,true,true))
      throw std::runtime_error("Invalid property_type ID or key in INSERT CALC");
  } else
    throw std::runtime_error("The PROPERTY_TYPE must be given in INSERT CALC");

  // get the method id
  std::string methodkey;
  int methodid;
  if ((im = kmap.find("METHOD")) != kmap.end()){
    if (!get_key_and_id(im->second,"Methods",methodkey,methodid))
      throw std::runtime_error("Invalid method ID or key in INSERT CALC");
  } else
    throw std::runtime_error("The METHOD must be given in INSERT CALC");

  // get the file
  std::string file;
  if ((im = kmap.find("FILE")) != kmap.end())
    file = im->second;
  else
    throw std::runtime_error("The FILE must be given in INSERT CALC");

  // get the data from the file
 std::unordered_map<std::string,std::vector<double>> datmap;
 if (ptid == globals::ppty_energy_difference)
   datmap = read_data_file_vector(file,globals::ha_to_kcal);
 else
   datmap = read_data_file_vector(file,1.);

  // build the property map
  std::unordered_map<int,std::vector<double>> propmap;
  statement stkey(db,statement::STMT_CUSTOM,"SELECT key FROM Structures WHERE id = ?1;");
  statement st(db,statement::STMT_CUSTOM,R"SQL(
SELECT id, nstructures, structures, coefficients
FROM Properties
WHERE property_type = ?1
ORDER BY id;)SQL");
  st.bind(1,ptid);

  while (st.step() != SQLITE_DONE){
    int propid = sqlite3_column_int(st.ptr(),0);
    int nstr = sqlite3_column_int(st.ptr(),1);
    int *istr = (int *) sqlite3_column_blob(st.ptr(),2);
    double *coef = (double *) sqlite3_column_blob(st.ptr(),3);
    std::vector<double> value;
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
      if (i == 0)
        value.resize(datmap[strname].size(), 0.);
      else if (datmap[strname].size() != value.size())
        throw std::runtime_error("Incompatible number of values calculating evaluation in INSERT CALC");

      if (coef) {
        for (int j = 0; j < value.size(); j++)
          value[j] += coef[i] * datmap[strname][j];
      } else {
        for (int j = 0; j < value.size(); j++)
          value[j] += datmap[strname][j];
      }
    }
    if (found)
      propmap[propid] = value;
  }
  datmap.clear();

  // begin the transaction
  begin_transaction();

  st.recycle(statement::STMT_CUSTOM,"INSERT INTO Evaluations (methodid,propid,value) VALUES(:METHOD,:PROPID,:VALUE);");
  for (auto it = propmap.begin(); it != propmap.end(); it++){
    st.reset();
    st.bind((char *) ":METHOD",methodid);
    st.bind((char *) ":PROPID",it->first);
    st.bind((char *) ":VALUE",(void *) &it->second[0],false,(it->second).size()*sizeof(double));
    os << "# INSERT EVALUATION (method=" << methodkey << ";property=" << it->first << ";nvalue=" << it->second.size() << ")" << std::endl;
    if (st.step() != SQLITE_DONE){
      std::cout << "method = " << methodkey << std::endl;
      std::cout << "propid = " << it->first << std::endl;
      std::cout << "value = " << it->second[0] << "(" << it->second.size() << "elements)" << std::endl;
      throw std::runtime_error("Failed inserting data in the database (READ CALC)");
    }
  }

  // commit the transaction
  commit_transaction();
}

// Insert literature references into the database from a bibtex file
void sqldb::insert_litref_bibtex(std::ostream &os, std::list<std::string> &tokens){
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

  // prepare the statement
  statement st(db,statement::STMT_CUSTOM,R"SQL(
INSERT INTO Literature_refs (key,authors,title,journal,volume,page,year,doi,description)
       VALUES(:KEY,:AUTHORS,:TITLE,:JOURNAL,:VOLUME,:PAGE,:YEAR,:DOI,:DESCRIPTION);
)SQL");

  // loop over the contents of the bib file and add to the database
  boolean rc;
  while (AST *entry = bt_parse_entry(fp,filename,0,&rc)){
    if (rc && bt_entry_metatype(entry) == BTE_REGULAR) {
      std::string key = bt_entry_key(entry);
      std::string type = bt_entry_type(entry);

      if (equali_strings(type,"article")){
        // bind the key
        st.bind((char *) ":KEY",key,false);

        // bind the rest of the fields
        char *fname = NULL;
        AST *field = NULL;
        while (field = bt_next_field(entry,field,&fname)){
          // this prevents a memory leak if we throw an exception
          char *fvalue_ = bt_get_text(field);
          std::string fvalue(fvalue_);
          free(fvalue_);

          if (!strcmp(fname,"title"))
            st.bind((char *) ":TITLE",fvalue);
          else if (!strcmp(fname,"author") || !strcmp(fname,"authors"))
            st.bind((char *) ":AUTHORS",fvalue);
          else if (!strcmp(fname,"journal"))
            st.bind((char *) ":JOURNAL",fvalue);
          else if (!strcmp(fname,"volume"))
            st.bind((char *) ":VOLUME",fvalue);
          else if (!strcmp(fname,"page") || !strcmp(fname,"pages"))
            st.bind((char *) ":PAGE",fvalue);
          else if (!strcmp(fname,"year"))
            st.bind((char *) ":YEAR",fvalue);
          else if (!strcmp(fname,"doi"))
            st.bind((char *) ":DOI",fvalue);
          else if (!strcmp(fname,"description"))
            st.bind((char *) ":DESCRIPTION",fvalue);
        }

        os << "# INSERT LITREF " << key << std::endl;

        st.step();
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
void sqldb::insert_set_xyz(std::ostream &os, const std::string &key, std::unordered_map<std::string,std::string> &kmap){
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
        insert_structure(os,skey,smap);
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
        insert_structure(os,skey,smap);
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
void sqldb::insert_set_din(std::ostream &os, const std::string &key, std::unordered_map<std::string,std::string> &kmap){
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
        insert_structure(os,skey,smap);
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
    smap["PROPERTY_TYPE"] = "ENERGY_DIFFERENCE";
    smap["SET"] = key;
    smap["ORDER"] = std::to_string(k+1);
    smap["NSTRUCTURES"] = std::to_string(n);
    smap["STRUCTURES"] = "";
    smap["COEFFICIENTS"] = "";
    for (int i = 0; i < n; i++){
      smap["STRUCTURES"] = smap["STRUCTURES"] + key + "." + info[k].names[i] + " ";
      smap["COEFFICIENTS"] = smap["COEFFICIENTS"] + to_string_precise(info[k].coefs[i]) + " ";
    }
    insert_property(os,skey,smap);

    // insert the evaluation
    if (havemethod){
      smap.clear();
      smap["METHOD"] = kmap["METHOD"];
      smap["PROPERTY"] = skey;
      smap["VALUE"] = to_string_precise(info[k].ref);
      insert_evaluation(os,smap);
    }
  }

  // commit the transaction
  commit_transaction();
}

// Delete items from the database
void sqldb::erase(std::ostream &os, const std::string &category, std::list<std::string> &tokens) {
  if (!db) throw std::runtime_error("A database file must be connected before using DELETE");

  // pick the table
  std::string table;
  if (category == "LITREF")
    table = "Literature_refs";
  else if (category == "SET")
    table = "Sets";
  else if (category == "METHOD")
    table = "Methods";
  else if (category == "STRUCTURE")
    table = "Structures";
  else if (category == "PROPERTY")
    table = "Properties";
  else if (category == "EVALUATION")
    table = "Evaluations";
  else if (category == "TERM")
    table = "Terms";
  else
    throw std::runtime_error("Unknown keyword in DELETE");

  // execute
  if (tokens.empty()){
    statement st(db,statement::STMT_CUSTOM,"DELETE FROM " + table + ";");
    st.execute();
  } else if (category == "EVALUATION") {
    statement st(db,statement::STMT_CUSTOM,R"SQL(
DELETE FROM Evaluations WHERE methodid = (SELECT id FROM Methods WHERE key = ?1) AND propid = (SELECT id FROM Properties WHERE key = ?2);
)SQL");
    for (auto it = tokens.begin(); it != tokens.end(); it++){
      os << "# DELETE " << category << " (method=" << *it;
      st.bind(1,*it++);
      os << ";property=" << *it << ")" << std::endl;
      st.bind(2,*it);
      st.step();
    }
  } else if (category == "TERM") {
    statement st(db,statement::STMT_CUSTOM,R"SQL(
DELETE FROM Terms WHERE
  methodid = (SELECT id FROM Methods WHERE key = ?1) AND
  propid = (SELECT id FROM Properties WHERE key = ?2) AND
  atom = ?3 AND l = ?4 AND exponent = ?5;
)SQL");
    for (auto it = tokens.begin(); it != tokens.end(); it++){
      os << "# DELETE " << category << " (method=" << *it;
      st.bind(1,*it++);
      os << ";property=" << *it;
      st.bind(2,*it++);
      os << ";atom=" << *it;
      st.bind(3,std::stoi(*it++));
      os << ";l=" << *it;
      st.bind(4,std::stoi(*it++));
      os << ";exp=" << *it << ")" << std::endl;
      st.bind(5,std::stod(*it));
      st.step();
    }
  } else {
    statement st_id(db,statement::STMT_CUSTOM,"DELETE FROM " + table + " WHERE id = ?1;");
    statement st_key(db,statement::STMT_CUSTOM,"DELETE FROM " + table + " WHERE key = ?1;");
    for (auto it = tokens.begin(); it != tokens.end(); it++){
      os << "# DELETE " << category << " " << *it << std::endl;
      if (isinteger(*it)){
        st_id.bind(1,*it);
        st_id.step();
      } else {
        st_key.bind(1,*it);
        st_key.step();
      }
    }
  }
  os << std::endl;
}

// List items from the database
void sqldb::print(std::ostream &os, const std::string &category, bool dobib){
  if (!db) throw std::runtime_error("A database file must be connected before using LIST");

  enum entrytype { t_str, t_int, t_intsize, t_double, t_ptr_double };

  std::vector<entrytype> types;
  std::vector<std::string> headers;
  std::vector<int> cols, nelems;
  bool dobib_ = false;
  std::string stmt;
  if (category == "LITREF"){
    headers = {"id", "key","authors","title","journal","volume","page","year","doi","description"};
    types   = {t_int,t_str,    t_str,  t_str,    t_str,   t_str, t_str, t_str,t_str,        t_str};
    cols    = {    0,    1,        2,      3,        4,       5,     6,     7,    8,            9};
    dobib_ = dobib;
    stmt = R"SQL(
SELECT id,key,authors,title,journal,volume,page,year,doi,description
FROM Literature_refs
ORDER BY id;
)SQL";
  } else if (category == "SET"){
    headers = {"id", "key","litrefs","description"};
    types   = {t_int,t_str,    t_str,        t_str};
    cols    = {    0,    1,        2,            3};
    stmt = R"SQL(
SELECT id,key,litrefs,description
FROM Sets
ORDER BY id;
)SQL";
  } else if (category == "METHOD"){
    headers = { "id","key","gaussian_keyword","psi4_keyword","litrefs","description"};
    types   = {t_int,t_str,             t_str,         t_str,    t_str,        t_str};
    cols    = {    0,    1,                 2,             3,        4,            5};
    stmt = R"SQL(
SELECT id,key,gaussian_keyword,psi4_keyword,litrefs,description
FROM Methods
ORDER BY id;
)SQL";
  } else if (category == "STRUCTURE"){
    headers = { "id","key","set","ismolecule","charge","multiplicity","nat"};
    types   = {t_int,t_str,t_int,       t_int,   t_int,         t_int,t_int};
    cols    = {    0,    1,     2,          3,       4,             5,    6};
    stmt = R"SQL(
SELECT id,key,setid,ismolecule,charge,multiplicity,nat,cell,zatoms,coordinates
FROM Structures
ORDER BY id;
)SQL";
  } else if (category == "PROPERTY"){
    headers = { "id","key","property_type","setid","orderid","nstructures"};
    types   = {t_int,t_str,          t_int,  t_int,    t_int,        t_int};
    cols    = {    0,    1,              2,      3,        4,            5};
    stmt = R"SQL(
SELECT id,key,property_type,setid,orderid,nstructures
FROM Properties
ORDER BY id;
)SQL";
  } else if (category == "EVALUATION"){
    headers = {"methodid","propid",   "#values", "1st value"};
    types   = {     t_int,   t_int,   t_intsize,t_ptr_double};
    cols    = {         0,       1,           2,           3};
    stmt = R"SQL(
SELECT methodid,propid,length(value),value
FROM Evaluations;
)SQL";
  } else if (category == "TERM"){
    headers = {"methodid","propid", "atom",   "l","exponent",   "#values", "1st value","maxcoef"};
    types   = {     t_int,   t_int,  t_int, t_int,  t_double,   t_intsize,t_ptr_double, t_double};
    cols    = {         0,       1,      2,     3,         4,           5,           6,        7};
    stmt = R"SQL(
SELECT methodid,propid,atom,l,exponent,length(value),value,maxcoef
FROM Terms;
)SQL";
  } else {
    throw std::runtime_error("Unknown LIST category: " + category);
  }

  // make the statement
  statement st(db,statement::STMT_CUSTOM,stmt);

  // print table header
  int n = headers.size();
  if (!dobib_){
    for (int i = 0; i < n; i++)
      os << "| " << headers[i];
    os << "|" << std::endl;
  }

  // print table body
  std::streamsize prec = os.precision(10);
  while (st.step() != SQLITE_DONE){
    for (int i = 0; i < n; i++){
      if (types[i] == t_str){
        const unsigned char *field = sqlite3_column_text(st.ptr(), cols[i]);
        if (!dobib_)
          os << "| " << (field?field:(const unsigned char*) "");
        else{
          if (headers[i] == "key")
            os << "@article{" << field << std::endl;
          else if (field)
            os << " " << headers[i] << "={" << field << "}," << std::endl;
        }
      } else if (types[i] == t_int && !dobib_){
        os << "| " << sqlite3_column_int(st.ptr(), cols[i]);
      } else if (types[i] == t_intsize && !dobib_){
        os << "| " << sqlite3_column_int(st.ptr(), cols[i]) / sizeof(double);
      } else if (types[i] == t_double && !dobib_){
        os << "| " << sqlite3_column_double(st.ptr(), cols[i]);
      } else if (types[i] == t_ptr_double && !dobib_){
        double *ptr = (double *) sqlite3_column_blob(st.ptr(), cols[i]);
        os << "| " << *ptr;
      }
    }
    if (!dobib_)
      os << "|" << std::endl;
    else
      os << "}" << std::endl;
  }
  os.precision(prec);
  os << std::endl;
}

// Print a summary of the contents of the database. If full, print
// info about the evaluations and terms available.
void sqldb::printsummary(std::ostream &os, bool full){
  // property types
  statement st(db,statement::STMT_CUSTOM,"SELECT id, key, description FROM Property_types;");
  os << "# Table of property types" << std::endl;
  os << "| id | key | description |" << std::endl;
  while (st.step() != SQLITE_DONE){
    int id = sqlite3_column_int(st.ptr(),0);
    std::string key = (char *) sqlite3_column_text(st.ptr(),1);
    std::string desc = (char *) sqlite3_column_text(st.ptr(),2);
    os << "| " << id << " | " << key << " | " << desc << " |" << std::endl;
  }
  os << std::endl;

  // literature references
  st.recycle(statement::STMT_CUSTOM,"SELECT COUNT(id) FROM Literature_refs;");
  st.step();
  int num = sqlite3_column_int(st.ptr(),0);
  os << "# Number of literature references: " << num << std::endl;
  os << std::endl;

  // methods
  os << "# Table of methods" << std::endl;
  print(os,"METHOD",false);

  // sets
  os << "# Table of sets" << std::endl;
  print(os,"SET",false);

  // properties and structures in each set
  os << "# Number of properties and structures in each set" << std::endl;
  st.recycle(statement::STMT_CUSTOM,R"SQL(
SELECT Sets.id, Sets.key, prdx.cnt, srdx.cnt
FROM Sets
LEFT OUTER JOIN (SELECT setid, count(id) AS cnt FROM Properties GROUP BY setid) AS prdx ON prdx.setid = Sets.id
LEFT OUTER JOIN (SELECT setid, count(id) AS cnt FROM Structures GROUP BY setid) AS srdx ON srdx.setid = Sets.id
ORDER BY Sets.id;
)SQL");
  os << "| id | key | properties | structures |" << std::endl;
  while (st.step() != SQLITE_DONE){
    int id = sqlite3_column_int(st.ptr(),0);
    std::string key = (char *) sqlite3_column_text(st.ptr(),1);
    long int pcnt = sqlite3_column_int(st.ptr(),2);
    long int scnt = sqlite3_column_int(st.ptr(),3);
    os << "| " << id << " | " << key << " | " << pcnt << " | " << scnt << " |" << std::endl;
  }
  os << std::endl;

  // evaluations and terms
  if (full){
    os << "# Evaluations and terms for each combination of set & method" << std::endl;
    st.recycle(statement::STMT_CUSTOM,R"SQL(
SELECT Sets.id, Sets.key, Methods.id, Methods.key, eva.cnt, trm.cnt
FROM Methods, Sets
LEFT OUTER JOIN(
SELECT Evaluations.methodid AS mid, Sets.id AS sid, COUNT(Evaluations.value) AS cnt
FROM Evaluations, Properties, Sets
WHERE Evaluations.propid = Properties.id AND Properties.setid = Sets.id
GROUP BY Evaluations.methodid, Sets.id) AS eva ON Methods.id = eva.mid AND Sets.id = eva.sid
LEFT OUTER JOIN(
SELECT Terms.methodid AS mid, Sets.id AS sid, COUNT(Terms.value) AS cnt
FROM Terms, Properties, Sets
WHERE Properties.id = Terms.propid AND Properties.setid = Sets.id
GROUP BY Terms.methodid, Sets.id) AS trm ON Methods.id = trm.mid AND Sets.id = trm.sid
ORDER BY Sets.id, Methods.id;
)SQL");
    os << "| set-id | set-key | method-id | method-key | #evaluations | #terms |" << std::endl;
    while (st.step() != SQLITE_DONE){
      int sid = sqlite3_column_int(st.ptr(),0);
      std::string skey = (char *) sqlite3_column_text(st.ptr(),1);
      int mid = sqlite3_column_int(st.ptr(),2);
      std::string mkey = (char *) sqlite3_column_text(st.ptr(),3);
      long int ecnt = sqlite3_column_int(st.ptr(),4);
      long int tcnt = sqlite3_column_int(st.ptr(),5);
      os << "| " << sid << " | " << skey << " | " << mid << " | " << mkey << " |" << ecnt << " | " << tcnt << " |" << std::endl;
    }
    os << std::endl;
  }
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
        idset.push_back(find_id_from_key(*it,"Sets"));
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
      methodid = find_id_from_key(im->second,"Methods");

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
  os << "Checking the litrefs in sets are known" << std::endl;
  statement st(db,statement::STMT_CUSTOM,R"SQL(
SELECT litrefs,key FROM Sets;
)SQL");
  while (st.step() != SQLITE_DONE){
    const char *field_s = (char *) sqlite3_column_text(st.ptr(), 0);
    if (!field_s) continue;

    std::string field = std::string(field_s);
    std::list<std::string> tokens = list_all_words(field);
    for (auto it = tokens.begin(); it != tokens.end(); it++){
      if (!find_id_from_key(*it,"Literature_refs"))
        os << "LITREF (" + *it + ") in SET (" + std::string((char *) sqlite3_column_text(st.ptr(), 1)) + ") not found" << std::endl;
    }
  }

  // check litrefs in methods
  os << "Checking the litrefs in methods are known" << std::endl;
  st.recycle(statement::STMT_CUSTOM,R"SQL(
SELECT litrefs,key FROM Methods;
)SQL");
  while (st.step() != SQLITE_DONE){
    const char *field_s = (char *) sqlite3_column_text(st.ptr(), 0);
    if (!field_s) continue;

    std::string field = std::string(field_s);
    std::list<std::string> tokens = list_all_words(field);
    for (auto it = tokens.begin(); it != tokens.end(); it++){
      if (!find_id_from_key(*it,"Literature_refs"))
        os << "LITREF (" + *it + ") in METHODS (" + std::string((char *) sqlite3_column_text(st.ptr(), 1)) + ") not found" << std::endl;
    }
  }

  // check structures in properties
  os << "Checking the structures in properties are known" << std::endl;
  st.recycle(statement::STMT_CUSTOM,R"SQL(
SELECT key,nstructures,structures FROM Properties;
)SQL");
  statement stcheck(db,statement::STMT_CUSTOM,R"SQL(
SELECT id FROM Structures WHERE id = ?1;
)SQL");
  while (st.step() != SQLITE_DONE){
    int n = sqlite3_column_int(st.ptr(), 1);

    const int *str = (int *)sqlite3_column_blob(st.ptr(), 2);
    for (int i = 0; i < n; i++){
      stcheck.bind(1,str[i]);
      stcheck.step();
      if (sqlite3_column_int(stcheck.ptr(),0) == 0)
        os << "STRUCTURES (" + std::to_string(str[i]) + ") in Properties (" + std::string((char *) sqlite3_column_text(st.ptr(), 0)) + ") not found" << std::endl;
      stcheck.reset();
    }
  }

  // check the number of values and structures in evaluations
  os << "Checking the number of values and structures in the evaluations table" << std::endl;
  st.recycle(statement::STMT_CUSTOM,R"SQL(
SELECT Evaluations.methodid, Evaluations.propid, Properties.property_type, length(Evaluations.value), Properties.nstructures, Properties.structures
FROM Evaluations, Properties
WHERE Evaluations.propid = Properties.id
)SQL");
  stcheck.recycle(statement::STMT_CUSTOM,R"SQL(
SELECT nat FROM Structures WHERE id = ?1;
)SQL");
  while (st.step() != SQLITE_DONE){
    int methodid = sqlite3_column_int(st.ptr(), 0);
    int propid = sqlite3_column_int(st.ptr(), 1);
    int ppty = sqlite3_column_int(st.ptr(), 2);
    int nvalue = sqlite3_column_int(st.ptr(), 3) / sizeof(double);
    int nstr = sqlite3_column_int(st.ptr(), 4);

    // check the number of structures
    if (ppty != globals::ppty_energy_difference && nstr != 1){
      os << "EVALUATIONS (method=" << methodid << ";property=" << propid << ") should have one structure, but has " << nstr << std::endl;
      continue;
    }

    // check the number of values
    if (ppty == globals::ppty_energy_difference || ppty == globals::ppty_energy || ppty == globals::ppty_homo || ppty == globals::ppty_lumo){
      if (nvalue != 1)
        os << "EVALUATIONS (method=" << methodid << ";property=" << propid << ") should have one value, but has " << nvalue << std::endl;
    } else if (ppty == globals::ppty_dipole && nvalue != 3){
      os << "EVALUATIONS (method=" << methodid << ";property=" << propid << ") should have 3 values, but has " << nvalue << std::endl;
    } else if (ppty == globals::ppty_stress && nvalue != 6){
      os << "EVALUATIONS (method=" << methodid << ";property=" << propid << ") should have 6 values, but has " << nvalue << std::endl;
    } else if (ppty == globals::ppty_d1e || ppty == globals::ppty_d2e){
      int idstr = sqlite3_column_int(st.ptr(),5);
      stcheck.bind(1,idstr);
      stcheck.step();
      int nat = sqlite3_column_int(stcheck.ptr(),0);
      if (ppty == globals::ppty_d1e && nvalue != 3 * nat)
        os << "EVALUATIONS (method=" << methodid << ";property=" << propid << ") should have 3*nat values (nat=" << nat << "), but has " << nvalue << std::endl;
      else if (ppty == globals::ppty_d2e && nvalue != nat * (nat+1) / 2)
        os << "EVALUATIONS (method=" << methodid << ";property=" << propid << ") should have nat*(nat+1)/2 values (nat=" << nat << "), but has " << nvalue << std::endl;
      stcheck.reset();
    }
  }

  // check the number of values and structures in terms
  os << "Checking the number of values and structures in the terms table" << std::endl;
  st.recycle(statement::STMT_CUSTOM,R"SQL(
SELECT Terms.methodid, Terms.atom, Terms.l, Terms.exponent, Terms.propid, Properties.property_type, length(Terms.value), Properties.nstructures, Properties.structures
FROM Terms, Properties
WHERE Terms.propid = Properties.id
)SQL");
  stcheck.recycle(statement::STMT_CUSTOM,R"SQL(
SELECT nat FROM Structures WHERE id = ?1;
)SQL");
  while (st.step() != SQLITE_DONE){
    int methodid = sqlite3_column_int(st.ptr(), 0);
    int atom = sqlite3_column_int(st.ptr(), 1);
    int l = sqlite3_column_int(st.ptr(), 2);
    int exp = sqlite3_column_int(st.ptr(), 3);
    int propid = sqlite3_column_int(st.ptr(), 4);
    int ppty = sqlite3_column_int(st.ptr(), 5);
    int nvalue = sqlite3_column_int(st.ptr(), 6) / sizeof(double);
    int nstr = sqlite3_column_int(st.ptr(), 7);

    // check the number of structures
    if (ppty != globals::ppty_energy_difference && nstr != 1){
      os << "TERMS (method=" << methodid << ";atom=" << atom << ";l=" << l << ";exp=" << exp << ";property=" << propid
         << ") should have one structure, but has " << nstr << std::endl;
      continue;
    }

    // check the number of values
    if (ppty == globals::ppty_energy_difference || ppty == globals::ppty_energy || ppty == globals::ppty_homo || ppty == globals::ppty_lumo){
      if (nvalue != 1)
        os << "TERMS (method=" << methodid << ";atom=" << atom << ";l=" << l << ";exp=" << exp << ";property=" << propid
           << ") should have one value, but has " << nvalue << std::endl;
    } else if (ppty == globals::ppty_dipole && nvalue != 3){
      os << "TERMS (method=" << methodid << ";atom=" << atom << ";l=" << l << ";exp=" << exp << ";property=" << propid
         << ") should have 3 values, but has " << nvalue << std::endl;
    } else if (ppty == globals::ppty_stress && nvalue != 6){
      os << "TERMS (method=" << methodid << ";atom=" << atom << ";l=" << l << ";exp=" << exp << ";property=" << propid
         << ") should have 6 values, but has " << nvalue << std::endl;
    } else if (ppty == globals::ppty_d1e || ppty == globals::ppty_d2e){
      int idstr = sqlite3_column_int(st.ptr(),8);
      stcheck.bind(1,idstr);
      stcheck.step();
      int nat = sqlite3_column_int(stcheck.ptr(),0);
      if (ppty == globals::ppty_d1e && nvalue != 3 * nat)
        os << "TERMS (method=" << methodid << ";atom=" << atom << ";l=" << l << ";exp=" << exp << ";property=" << propid
           << ") should have 3*nat values (nat=" << nat << "), but has " << nvalue << std::endl;
      else if (ppty == globals::ppty_d2e && nvalue != nat * (nat+1) / 2)
        os << "TERMS (method=" << methodid << ";atom=" << atom << ";l=" << l << ";exp=" << exp << ";property=" << propid
           << ") should have nat*(nat+1)/2 values (nat=" << nat << "), but has " << nvalue << std::endl;
      stcheck.reset();
    }
  }

  os << std::endl;
}

// Write input files for a database set
void sqldb::write_structures(std::unordered_map<std::string,std::string> &kmap, const acp &a){
  if (!db)
    throw std::runtime_error("Error reading connected database");

  // program
  std::string program = "gaussian";
  if (kmap.find("PROGRAM") != kmap.end())
      program = kmap["PROGRAM"];

  // unpack the gaussian keyword into a map for this method
  bool havemethod = (kmap.find("METHOD") != kmap.end());
  std::unordered_map<std::string,std::string> gmap = {};
  if (havemethod) gmap = get_program_map(kmap["METHOD"],program);

  // set
  bool haveset = (kmap.find("SET") != kmap.end());
  int setid = 0;
  if (haveset){
    std::string setname = kmap["SET"];
    setid = find_id_from_key(setname,"Sets");
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
// compare to a reference method.
void sqldb::read_and_compare(std::ostream &os, std::unordered_map<std::string,std::string> &kmap){
  if (!db)
    throw std::runtime_error("A database file must be connected before using COMPARE");

  std::unordered_map<std::string,std::string>::const_iterator im;

  // property_type
  int ppid;
  std::string ppidname;
  if ((im = kmap.find("PROPERTY_TYPE")) != kmap.end()){
    if (!get_key_and_id(im->second,"Property_types",ppidname,ppid,true,true))
      throw std::runtime_error("Invalid PROPERTY_TYPE in COMPARE");
  } else
    throw std::runtime_error("A PROPERTY_TYPE is required when using COMPARE");

  // reference method
  int refm = -1;
  std::string refmethodname;
  if ((im = kmap.find("METHOD")) != kmap.end()){
    if (!get_key_and_id(im->second,"Methods",refmethodname,refm))
      throw std::runtime_error("Invalid METHOD in COMPARE");
  } else
    throw std::runtime_error("A METHOD is required when using COMPARE");

  // file
  im = kmap.find("FILE");
  if (im == kmap.end())
    throw std::runtime_error("A FILE is necessary when using COMPARE");
  std::string file = im->second;

  // set
  int sid = -1;
  if ((im = kmap.find("SET")) != kmap.end()){
    std::string setnamein;
    if (!get_key_and_id(im->second,"Sets",setnamein,sid))
      throw std::runtime_error("Invalid SET in COMPARE");
  }

  // read the file and build the data file
  std::unordered_map<std::string,std::vector<double>> datmap;
  datmap = read_data_file_vector(file,1.);

  // fetch the reference method values from the DB and populate vectors
  std::vector<std::string> names_found;
  std::vector<std::string> names_missing_fromdb;
  std::vector<std::string> names_missing_fromdat;
  std::vector<int> numvalues, setid;
  std::vector<double> refvalues, datvalues;
  std::map<int,std::string> setname;
  statement stkey(db,statement::STMT_CUSTOM,"SELECT key FROM Structures WHERE id = ?1;");
  std::string sttext;
  if (sid > 0){
    sttext = R"SQL(
SELECT Properties.key, length(Evaluations.value), Evaluations.value, Properties.nstructures, Properties.structures, Properties.coefficients, Properties.property_type, Sets.id, Sets.key
FROM Properties, Sets
LEFT OUTER JOIN Evaluations ON (Evaluations.propid = Properties.id AND Evaluations.methodid = :METHOD)
WHERE Properties.setid = :SET AND Properties.property_type = :PROPERTY_TYPE AND Sets.id = :SET
ORDER BY Properties.id;)SQL";
  } else {
    sttext = R"SQL(
SELECT Properties.key, length(Evaluations.value), Evaluations.value, Properties.nstructures, Properties.structures, Properties.coefficients, Properties.property_type, Sets.id, Sets.key
FROM Properties, Sets
LEFT OUTER JOIN Evaluations ON (Evaluations.propid = Properties.id AND Evaluations.methodid = :METHOD)
WHERE Properties.property_type = :PROPERTY_TYPE AND Sets.id = Properties.setid
ORDER BY Properties.id;)SQL";
  }
  statement st(db,statement::STMT_CUSTOM,sttext);
  if (sid > 0)
    st.bind((char *) ":SET",sid);
  st.bind((char *) ":METHOD",refm);
  st.bind((char *) ":PROPERTY_TYPE",ppid);
  while (st.step() != SQLITE_DONE){
    // check if the evaluation is available in the database
    std::string key = (char *) sqlite3_column_text(st.ptr(),0);
    if (sqlite3_column_type(st.ptr(),2) == SQLITE_NULL){
      names_missing_fromdb.push_back(key);
      continue;
    }

    // check if the components are in the data file
    int nvalue = sqlite3_column_int(st.ptr(),1) / sizeof(double);
    int nstr = sqlite3_column_int(st.ptr(),3);
    int *istr = (int *) sqlite3_column_blob(st.ptr(),4);
    double *coef = (double *) sqlite3_column_blob(st.ptr(),5);
    int ptid = sqlite3_column_int(st.ptr(),6);
    int thissetid = sqlite3_column_int(st.ptr(),7);
    std::string thissetname = (char *) sqlite3_column_text(st.ptr(), 8);
    std::vector<double> value(nvalue,0.0);
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
      for (int j = 0; j < nvalue; j++)
        value[j] += coef[i] * datmap[strname][j];
    }

    // conversion factor
    double scal = 1.0;
    if (ptid == globals::ppty_energy_difference)
      scal = globals::ha_to_kcal;

    // populate the vectors
    if (!found){
      names_missing_fromdat.push_back(key);
    } else {
      names_found.push_back(key);
      numvalues.push_back(nvalue);
      setid.push_back(thissetid);
      setname[thissetid] = thissetname;
      double *rval = (double *) sqlite3_column_blob(st.ptr(),2);
      for (int j = 0; j < nvalue; j++){
        refvalues.push_back(rval[j]);
        datvalues.push_back(value[j] * scal);
      }
    }
  }
  datmap.clear();

  // output the header and the statistics
  os << "# -- Evaluation of data from file -- " << std::endl
     << "# File: " << file << std::endl
     << "# Property type: " << ppidname << std::endl
     << "# Reference method: " << refmethodname << std::endl;
  if (!names_missing_fromdat.empty() || !names_missing_fromdat.empty())
    os << "# Statistics: " << "(partial, missing: "
       << names_missing_fromdat.size() << " from file, "
       << names_missing_fromdb.size() << " from database)" << std::endl;
  else
    os << "# Statistics: " << std::endl;

  std::streamsize prec = os.precision(8);
  os << std::fixed;
  if (refvalues.empty())
    os << "#   (no reference data for statistics)" << std::endl;
  else{
    // calculate statistics set-wise
    double wrms, rms, mae, mse;

    for (auto it = setname.begin(); it != setname.end(); it++){
      // calculate the statistics for the given set
      int ndat = calc_stats(datvalues,refvalues,{},wrms,rms,mae,mse,-1,-1,{},-1,-1,
                            setid,it->first);
      os << "# " << std::left << std::setw(15) << it->second
         << std::left << "  rms = " << std::right << std::setw(12) << rms << " "
         << std::left << "  mae = " << std::right << std::setw(12) << mae << " "
         << std::left << "  mse = " << std::right << std::setw(12) << mse << " "
         << std::left << " wrms = " << std::right << std::setw(12) << wrms << " "
         << std::left << " ndat = " << ndat
         << std::endl;
    }

    int ndat = calc_stats(datvalues,refvalues,{},wrms,rms,mae,mse);
    os << "# " << std::left << std::setw(15) << "ALL"
       << std::left << "  rms = " << std::right << std::setw(12) << rms << " "
       << std::left << "  mae = " << std::right << std::setw(12) << mae << " "
       << std::left << "  mse = " << std::right << std::setw(12) << mse << " "
       << std::left << " wrms = " << std::right << std::setw(12) << wrms << " "
       << std::left << " ndat = " << ndat
       << std::endl;
  }
  os << "#" << std::endl;
  os.precision(prec);

  // output the results
  const std::string approxname = "File";
  output_eval(os,{},names_found,numvalues,{},datvalues,approxname,refvalues,refmethodname);
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
  os << std::endl;
}

// Write the structures with IDs given by the keys in smap. The
// values of smap give the types (xyz for an xyz file, terms for a
// terms input file or energy_difference, etc. for a property input
// file). gmap: writer-dependent options for the structures. dir:
// output directory. npack = package and compress in packets of
// npack files (0 = no packing). a: ACP to use in the inputs.
// zat, l, exp: details for term inputs.
void sqldb::write_many_structures(std::unordered_map<int,std::string> &smap, const std::unordered_map<std::string,std::string> &gmap/*={}*/,
                                  const std::string &dir/*="./"*/, int npack/*=0*/,
                                  const acp &a/*={}*/,
                                  const std::vector<unsigned char> &zat/*={}*/, const std::vector<unsigned char> &lmax/*={}*/, const std::vector<double> &exp/*={}*/){

  if (npack <= 0 || npack >= smap.size()){
    for (auto it = smap.begin(); it != smap.end(); it++)
      write_one_structure(it->first,it->second,gmap,dir,a,zat,lmax,exp);
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
      written.push_back(fs::path(write_one_structure(srand[i],smap[srand[i]],gmap,dir,a,zat,lmax,exp)));

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
// meaning as in write_many_structures. Returns filename of the
// written file.
std::string sqldb::write_one_structure(int id, const std::string type, const std::unordered_map<std::string,std::string> gmap/*={}*/,
                                       const std::string &dir/*="./"*/,
                                       const acp &a/*={}*/,
                                       const std::vector<unsigned char> &zat/*={}*/, const std::vector<unsigned char> &lmax/*={}*/, const std::vector<double> &exp/*={}*/){

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

  // decisions, decisions
  bool isxyz = equali_strings(type,"xyz");
  bool isterms = equali_strings(type,"terms");
  bool isgaussian = ismolecule && (isterms || equali_strings(type,"energy_difference") && gmap.at("PROGRAM") == "GAUSSIAN");
  bool ispsi4 = ismolecule && !isterms && (equali_strings(type,"energy_difference") && gmap.at("PROGRAM") == "PSI4");
  if ((!isgaussian && !ispsi4) || (isgaussian && ispsi4))
    throw std::runtime_error("Unknown combination of property, structure, and program");

  // build the molecule/crystal structure
  structure s;
  s.readdbrow(st.ptr());

  // append the extension and open the file stream
  std::string ext;
  if (ismolecule) {
    if (isxyz)
      ext = ".xyz";
    else if (isgaussian)
      ext = ".gjf";
    else if (ispsi4)
      ext = ".inp";
  } else {
    throw std::runtime_error("Cannot do crystals yet");
  }
  name += ext;

  // write the input file to a memory stream for efficiency
  std::stringstream oss;
  if (isxyz) {
    s.writexyz(oss);
  } else if (isgaussian) {
    if (gmap.find("METHOD") == gmap.end()) throw std::runtime_error("This method does not have an associated Gaussian method keyword");
    std::string methodk = gmap.at("METHOD");
    std::string gbsk = "";
    if (gmap.find("GBS") != gmap.end()) gbsk = gmap.at("GBS");

    if (isterms){
      if (s.writegjf_terms(oss,methodk,gbsk,fileroot,zat,lmax,exp))
        throw std::runtime_error("Error writing input file: " + name);
    } else {
      if (s.writegjf(oss,methodk,gbsk,fileroot,a))
        throw std::runtime_error("Error writing input file: " + name);
    }
  } else if (ispsi4) {
    if (gmap.find("METHOD") == gmap.end()) throw std::runtime_error("This method does not have an associated psi4 method keyword");
    if (a) throw std::runtime_error("Cannot write psi4 inputs with ACPs");
    std::string method = gmap.at("METHOD");
    std::string basis = gmap.at("BASIS");
    if (s.writepsi4(oss,method,basis,fileroot))
      throw std::runtime_error("Error writing input file: " + name);

  } else {
    throw std::runtime_error("Unknown combination of structure, property type, and program");
  }

  // write the actual file and exit
  std::ofstream ofile(name,std::ios::trunc);
  ofile << oss.rdbuf();
  ofile.close();
  return (fileroot + ext);
}
