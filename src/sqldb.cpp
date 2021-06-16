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
#include <iterator>
#include <set>
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

//// database schema ////
static const std::string database_schema = R"SQL(
CREATE TABLE Literature_refs (
  id          INTEGER PRIMARY KEY AUTOINCREMENT,
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
  id          INTEGER PRIMARY KEY,
  key         TEXT UNIQUE NOT NULL,
  description TEXT
);
CREATE TABLE Sets (
  id            INTEGER PRIMARY KEY AUTOINCREMENT,
  key           TEXT UNIQUE NOT NULL,
  litrefs       TEXT,
  description   TEXT
);
CREATE TABLE Methods (
  id               INTEGER PRIMARY KEY AUTOINCREMENT,
  key              TEXT UNIQUE NOT NULL,
  litrefs          TEXT,
  description      TEXT
);
CREATE TABLE Structures (
  id            INTEGER PRIMARY KEY AUTOINCREMENT,
  key           TEXT UNIQUE NOT NULL,
  setid         INTEGER NOT NULL,
  ismolecule    INTEGER NOT NULL,
  charge        INTEGER,
  multiplicity  INTEGER,
  nat           INTEGER NOT NULL,
  cell          BLOB,
  zatoms        BLOB NOT NULL,
  coordinates   BLOB NOT NULL,
  FOREIGN KEY(setid) REFERENCES Sets(id) ON DELETE CASCADE
);
CREATE TABLE Properties (
  id            INTEGER PRIMARY KEY AUTOINCREMENT,
  key           TEXT UNIQUE NOT NULL,
  property_type INTEGER NOT NULL,
  setid         INTEGER NOT NULL,
  orderid       INTEGER NOT NULL,
  nstructures   INTEGER NOT NULL,
  structures    BLOB NOT NULL,
  coefficients  BLOB,
  FOREIGN KEY(property_type) REFERENCES Property_types(id) ON DELETE CASCADE,
  FOREIGN KEY(setid) REFERENCES Sets(id) ON DELETE CASCADE
);
CREATE TABLE Evaluations (
  methodid      INTEGER NOT NULL,
  propid        INTEGER NOT NULL,
  value         BLOB NOT NULL,
  PRIMARY KEY(methodid,propid)
  FOREIGN KEY(methodid) REFERENCES Methods(id) ON DELETE CASCADE,
  FOREIGN KEY(propid) REFERENCES Properties(id) ON DELETE CASCADE
);
CREATE TABLE Terms (
  methodid      INTEGER NOT NULL,
  atom          INTEGER NOT NULL,
  l             INTEGER NOT NULL,
  exponent      REAL NOT NULL,
  propid        INTEGER NOT NULL,
  value         BLOB NOT NULL,
  maxcoef       REAL,
  PRIMARY KEY(methodid,atom,l,exponent,propid),
  FOREIGN KEY(methodid) REFERENCES Methods(id) ON DELETE CASCADE,
  FOREIGN KEY(propid) REFERENCES Properties(id) ON DELETE CASCADE
);
CREATE TABLE Training_set_repo (
  key TEXT PRIMARY KEY,
  training_set BLOB NOT NULL
);
INSERT INTO Property_types (id,key,description)
       VALUES (1,'ENERGY_DIFFERENCE','A difference of molecular or crystal energies (reaction energy, binding energy, lattice energy, etc.)'),
              (2,'ENERGY','The total energy of a molecule or crystal'),
              (3,'DIPOLE','The electric dipole of a molecule'),
              (4,'STRESS','The stress tensor in a crystal'),
              (5,'D1E','The first derivatives of the energy wrt the atomic positions in a molecule or crystal'),
              (6,'D2E','The second derivatives of the energy wrt the atomic positions in a molecule or crystal'),
              (7,'HOMO','The orbital energy of the highest occupied molecular orbital'),
              (8,'LUMO','The orbital energy of the lowest unoccupied molecular orbital');
)SQL";
//// end of database schema ////

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
  statement st(db,"SELECT id FROM " + table + " WHERE key = ?1;");
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
  statement st(db,"SELECT key FROM " + table + " WHERE id = ?1;");
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

// Check if the DB is sane, empty, or not sane. If except_on_empty,
// raise exception on empty. Always raise excepton on error. Return
// 1 if sane, 0 if empty.
int sqldb::checksane(bool except_on_empty /*=false*/){
  if (!db)
    throw std::runtime_error("Error reading connected database");

  // query the database
  statement st(db,R"SQL(
SELECT COUNT(type)
FROM sqlite_master
WHERE type='table' AND name='Literature_refs';
)SQL");
  int rc = st.step();
  int icol = sqlite3_column_int(st.ptr(), 0);

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

  // initialize the database
  statement st(db,"PRAGMA foreign_keys = ON;");
  st.execute();
}

// Create the database skeleton.
void sqldb::create(){
  // skip if not open
  if (!db) throw std::runtime_error("A database file must be connected before using CREATE");

  // Create the table
  statement st(db,database_schema);
  st.execute();
}

// Close a database connection if open and reset the pointer to NULL
void sqldb::close(){
  if (!db) return;

  // close the database
  if (sqlite3_close_v2(db))
    throw std::runtime_error("Can't close database file " + dbfilename + " (" + sqlite3_errmsg(db) + ")");
  db = nullptr;
  dbfilename = "";
}

// Insert a literature reference by manually giving the data
void sqldb::insert_litref(std::ostream &os, const std::string &key, const std::unordered_map<std::string,std::string> &kmap) {
  if (!db) throw std::runtime_error("A database file must be connected before using INSERT LITREF");
  if (key.empty())
    throw std::runtime_error("Empty key in INSERT LITREF");

  // statement
  statement st(db,R"SQL(
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

  if (globals::verbose)
    os << "# INSERT LITREF " << key << std::endl;

  // submit
  st.step();
}

// Insert a set by manually giving the data
void sqldb::insert_set(std::ostream &os, const std::string &key, const std::unordered_map<std::string,std::string> &kmap) {
  if (!db) throw std::runtime_error("A database file must be connected before using INSERT SET");
  if (key.empty())
    throw std::runtime_error("Empty key in INSERT SET");
  if (kmap.find("XYZ") != kmap.end() && kmap.find("DIN") != kmap.end())
    throw std::runtime_error("XYZ and DIN options in SET are incompatible");

  // check the key
  if (key.find('@') != std::string::npos)
    throw std::runtime_error("Character @ is not allowed in set keys, in INSERT SET");

  // statement
  statement st(db,R"SQL(
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
      if (!find_id_from_key(*it,"Literature_refs"))
        throw std::runtime_error("Litref not found (" + *it + ") in INSERT SET");
      str = str + *it + " ";
    }
    st.bind((char *) ":LITREFS",str);
  }

  if (globals::verbose)
    os << "# INSERT SET " << key << std::endl;

  // submit
  st.step();

  // interpret the xyz keyword
  if (kmap.find("XYZ") != kmap.end() || kmap.find("POSCAR") != kmap.end())
    insert_set_xyz(os, key, kmap);

  // interpret the din/directory/method keyword combination
  if (kmap.find("DIN") != kmap.end())
    insert_set_din(os, key, kmap);
}

// Insert a method by manually giving the data
void sqldb::insert_method(std::ostream &os, const std::string &key, const std::unordered_map<std::string,std::string> &kmap){
  if (!db) throw std::runtime_error("A database file must be connected before using INSERT METHOD");
  if (key.empty())
    throw std::runtime_error("Empty key in INSERT METHOD");

  // statement
  statement st(db,R"SQL(
INSERT INTO Methods (key,litrefs,description)
       VALUES(:KEY,:LITREFS,:DESCRIPTION);
)SQL");

  // bind
  st.bind((char *) ":KEY",key,false);
  std::forward_list<std::string> vlist = {"LITREFS","DESCRIPTION"};
  for (auto it = vlist.begin(); it != vlist.end(); ++it){
    auto im = kmap.find(*it);
    if (im != kmap.end())
      st.bind(":" + *it,im->second);
  }

  if (globals::verbose)
    os << "# INSERT METHOD " << key << std::endl;

  // submit
  st.step();
}

// Insert a property by manually giving the data
void sqldb::insert_structure(std::ostream &os, const std::string &key, const std::unordered_map<std::string,std::string> &kmap){
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

  // check the key
  if (key.find('@') != std::string::npos)
    throw std::runtime_error("Character @ is not allowed in structure keys, in INSERT STRUCTURE");

  // bind
  statement st(db,R"SQL(
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

  if (globals::verbose)
    os << "# INSERT STRUCTURE " << key << std::endl;

  // submit
  st.step();
}

// Insert a property by manually giving the data
void sqldb::insert_property(std::ostream &os, const std::string &key, const std::unordered_map<std::string,std::string> &kmap){
  if (!db) throw std::runtime_error("A database file must be connected before using INSERT PROPERTY");
  if (key.empty())
    throw std::runtime_error("Empty key or prefix in INSERT PROPERTY");

  // some variables
  std::unordered_map<std::string,std::string>::const_iterator im1, im2;
  std::list<std::string> tok1;
  std::vector<double> tok2;

  // check the key
  if (key.find('@') != std::string::npos)
    throw std::runtime_error("Character @ is not allowed in property keys, in INSERT PROPERTY");

  // prepared statements
  statement st(db,R"SQL(
INSERT INTO Properties (id,key,property_type,setid,orderid,nstructures,structures,coefficients)
       VALUES(:ID,:KEY,:PROPERTY_TYPE,:SETID,:ORDERID,:NSTRUCTURES,:STRUCTURES,:COEFFICIENTS)
)SQL");
  statement ststr(db,"SELECT id, key FROM Structures WHERE setid = ?1 ORDER BY id;");

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
  bool justone = (kmap.find("ORDER") != kmap.end() && kmap.find("STRUCTURES") != kmap.end());
  if (justone){
    st.bind((char *) ":KEY",key,false);
    st.bind((char *) ":ORDERID",std::stoi(kmap.find("ORDER")->second));

    // parse structures and coefficients
    im1 = kmap.find("STRUCTURES");
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
          throw std::runtime_error("Structure not found (" + *it + ") in INSERT PROPERTY");
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

    if (globals::verbose)
      os << "# INSERT PROPERTY " << key << std::endl;

    // submit
    st.step();
  } else {
    if (kmap.find("ORDER") != kmap.end())
      throw std::runtime_error("ORDER is not allowed in bulk insertion, INSERT PROPERTY");
    if (kmap.find("STRUCTURES") != kmap.end())
      throw std::runtime_error("STRUCTURES is not allowed in bulk insertion, INSERT PROPERTY");

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

      if (globals::verbose)
        os << "# INSERT PROPERTY " << newkey << std::endl;
      st.step();
    }

    // commit the transaction
    commit_transaction();
  }
}

// Insert an evaluation by manually giving the data
void sqldb::insert_evaluation(std::ostream &os, const std::unordered_map<std::string,std::string> &kmap){
  if (!db) throw std::runtime_error("A database file must be connected before using INSERT EVALUATION");

  // bind
  std::unordered_map<std::string,std::string>::const_iterator im;
  statement st(db,"INSERT INTO Evaluations (methodid,propid,value) VALUES(:METHODID,:PROPID,:VALUE)");

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

  if (globals::verbose)
    os << "# INSERT EVALUATION (method=" << methodkey << ";property=" << propkey << ")" << std::endl;

  // submit
  st.step();
}

// Insert a term by manually giving the data
void sqldb::insert_term(std::ostream &os, const std::unordered_map<std::string,std::string> &kmap){
  if (!db) throw std::runtime_error("A database file must be connected before using INSERT TERM");

  // build command
  std::string cmd;
  bool reqpropty = true, isterm;
  if (kmap.find("VALUE") != kmap.end()) {
    isterm = true;
    cmd = "INSERT INTO Terms (methodid,propid,atom,l,exponent,value,maxcoef) VALUES(:METHODID,:PROPID,:ATOM,:L,:EXPONENT,:VALUE,:MAXCOEF)";
  } else if (kmap.find("MAXCOEF") != kmap.end()){
    isterm = false;
    if (kmap.find("PROPERTY") != kmap.end())
      cmd = "UPDATE Terms SET maxcoef = :MAXCOEF WHERE methodid = :METHODID AND propid = :PROPID AND atom = :ATOM AND l = :L AND exponent = :EXPONENT";
    else{
      cmd = "UPDATE Terms SET maxcoef = :MAXCOEF WHERE methodid = :METHODID AND atom = :ATOM AND l = :L AND exponent = :EXPONENT";
      reqpropty = false;
    }
  } else
    throw std::runtime_error("A VALUE or MAXCOEF must be given in INSERT TERM");

  // bind
  std::unordered_map<std::string,std::string>::const_iterator im;
  statement st(db,cmd);

  std::string methodkey;
  int methodid;
  if ((im = kmap.find("METHOD")) != kmap.end()){
    if (!get_key_and_id(im->second,"Methods",methodkey,methodid))
      throw std::runtime_error("Invalid METHOD ID or key in INSERT EVALUATION");
  } else
    throw std::runtime_error("A METHOD is required in INSERT EVALUATION");
  st.bind((char *) ":METHODID",methodid);

  std::string propkey = "";
  int propid = -1;
  if ((im = kmap.find("PROPERTY")) != kmap.end()){
    if (!get_key_and_id(im->second,"Properties",propkey,propid))
      throw std::runtime_error("Invalid PROPERTY ID or key in INSERT EVALUATION");
    st.bind((char *) ":PROPID",propid);
  } else if (reqpropty)
    throw std::runtime_error("A PROPERTY is required in INSERT EVALUATION");

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
      std::string l = im->second;
      lowercase(l);
      if (globals::ltoint.find(l) == globals::ltoint.end())
        throw std::runtime_error("Unknown angular momentum label in INSERT TERM");
      st.bind((char *) ":L",globals::ltoint.at(l));
    }
  else
    throw std::runtime_error("An angular momentum (l) must be given in INSERT TERM");
  if ((im = kmap.find("EXPONENT")) != kmap.end())
    st.bind((char *) ":EXPONENT",std::stod(im->second));
  else
    throw std::runtime_error("An exponent must be given in INSERT TERM");
  if ((im = kmap.find("VALUE")) != kmap.end()){
    std::vector<double> tok = list_all_doubles(im->second);

    if ((im = kmap.find("CALCSLOPE")) != kmap.end()){
      double c0 = std::stod(im->second);
      statement st2(db,R"SQL(
SELECT length(Evaluations.value), Evaluations.value
FROM Evaluations
WHERE Evaluations.propid = ?1 AND Evaluations.methodid = ?2;
)SQL");
      st2.bind(1,propid);
      st2.bind(2,methodid);
      st2.step();
      int len = sqlite3_column_int(st2.ptr(),0) / sizeof(double);
      double *rval = (double *) sqlite3_column_blob(st2.ptr(),1);
      if (!rval)
        throw std::runtime_error("To use CALCSLOPE in INSERT TERM, the evaluation for the corresponding method and property must be available");
      if (len != tok.size())
        throw std::runtime_error("The number of values in the evaluation does not match those in VALUE, in CALCSLOPE, INSERT TERM");
      for (int i = 0; i < tok.size(); i++)
        tok[i] = (tok[i] - rval[i]) / c0;
    }

    st.bind((char *) ":VALUE",(void *) &tok.front(),true,tok.size() * sizeof(double));
  }
  if ((im = kmap.find("MAXCOEF")) != kmap.end())
    st.bind((char *) ":MAXCOEF",std::stod(im->second));

  if (globals::verbose){
    if (isterm){
      os << "# INSERT TERM (method=" << methodkey << ";property=" << propkey
         << ";atom=" << kmap.find("ATOM")->second << ";l=" << kmap.find("L")->second
         << ";exponent=" << kmap.find("EXPONENT")->second << ")" << std::endl;
    } else {
      if (propid > 0)
        os << "# INSERT MAXCOEF (method=" << methodkey << ";property=" << propkey
           << ";atom=" << kmap.find("ATOM")->second << ";l=" << kmap.find("L")->second
           << ";exponent=" << kmap.find("EXPONENT")->second << ")" << std::endl;
      else
        os << "# INSERT MAXCOEF (method=" << methodkey 
           << ";atom=" << kmap.find("ATOM")->second << ";l=" << kmap.find("L")->second
           << ";exponent=" << kmap.find("EXPONENT")->second << ")" << std::endl;
    }
  }
  // submit
  st.step();
}

// Insert maxcoefs from a file
void sqldb::insert_maxcoef(std::ostream &os, const std::unordered_map<std::string,std::string> &kmap){
  if (!db) throw std::runtime_error("A database file must be connected before using INSERT MAXCOEF");

  // get the file
  std::unordered_map<std::string,std::string>::const_iterator im;
  std::string file;
  if ((im = kmap.find("FILE")) != kmap.end())
    file = im->second;
  else
    throw std::runtime_error("The FILE must be given in INSERT MAXCOEF");

  // get the method
  std::string method;
  if ((im = kmap.find("METHOD")) != kmap.end())
    method = im->second;
  else
    throw std::runtime_error("The METHOD must be given in INSERT MAXCOEF");

  // begin the transaction
  begin_transaction();

  // read the file and insert
  std::ifstream ifile(file,std::ios::in);
  if (ifile.fail())
    throw std::runtime_error("In INSERT MAXCOEF, error reading file: " + file);
  std::string line;
  while (std::getline(ifile,line)){
    std::string atom, l, exp, value, propkey;
    std::istringstream iss(line);
    iss >> atom >> l >> exp >> value;
    if (iss.fail())
      continue;
    iss >> propkey;
    if (iss.fail())
      propkey = "";

    std::unordered_map<std::string,std::string> smap;
    smap["METHOD"] = method;
    smap["ATOM"] = atom;
    smap["L"] = l;
    smap["EXPONENT"] = exp;
    smap["MAXCOEF"] = value;
    if (!propkey.empty())
      smap["PROPERTY"] = propkey;
    insert_term(os,smap);
  }
  ifile.close();

  // commit the transaction
  commit_transaction();
}

// Bulk insert: read data from a file, then insert as evaluation or terms.
void sqldb::insert_calc(std::ostream &os, const std::unordered_map<std::string,std::string> &kmap,
                        const std::vector<unsigned char> &zat/*={}*/, const std::vector<unsigned char> &lmax/*={}*/,
                        const std::vector<double> &exp/*={}*/){
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

  // get the term
  bool doterm = false, doslope = false, changename = false;
  double c0 = 1.;
  std::vector<unsigned char> zat_ = {0}, l_ = {0};
  std::vector<double> exp_ = {0.0};
  if ((im = kmap.find("TERM")) != kmap.end()){
    doterm = true;
    std::list<std::string> words = list_all_words(im->second);

    if (words.size() == 0){
      changename = true;
      exp_ = exp;
      zat_.clear();
      l_.clear();
      for (int izat = 0; izat < zat.size(); izat++){
        for (unsigned char il = 0; il <= lmax[izat]; il++){
          zat_.push_back(zat[izat]);
          l_.push_back(il);
        }
      }
    } else if (words.size() == 3){
      changename = false;
      std::string str = words.front();
      words.pop_front();
      if (isinteger(str))
        zat_[0] = std::stoi(str);
      else
        zat_[0] = zatguess(str);

      str = words.front();
      words.pop_front();
      if (isinteger(str))
        l_[0] = std::stoi(str);
      else{
        lowercase(str);
        if (globals::ltoint.find(str) == globals::ltoint.end())
          throw std::runtime_error("Invalid angular momentum " + str + " in WRITE/TERM");
        l_[0] = globals::ltoint.at(str);
      }

      str = words.front();
      words.pop_front();
      exp_[0] = std::stod(str);
    } else {
      throw std::runtime_error("Invalid number of tokens in INSERT/TERM");
    }

    if ((im = kmap.find("CALCSLOPE")) != kmap.end()){
      doslope = true;
      c0 = std::stod(im->second);
    }
  }

  // get all the data from the file
  std::unordered_map<std::string,std::vector<double>> datmap;
  if (ptid == globals::ppty_energy_difference)
    datmap = read_data_file_vector(file,globals::ha_to_kcal);
  else
    datmap = read_data_file_vector(file,1.);

  // consistency check
  if (zat_.size() != l_.size())
    throw std::runtime_error("Inconsistent zat and l arrays in insert_calc");

  // begin the transaction and prepare the statements
  begin_transaction();
  statement stkey(db,"SELECT key FROM Structures WHERE id = ?1;");
  statement ststruct(db,R"SQL(
SELECT id, nstructures, structures, coefficients
FROM Properties
WHERE property_type = ?1
ORDER BY id;)SQL");
  statement steval(db,R"SQL(
SELECT length(Evaluations.value), Evaluations.value
FROM Evaluations
WHERE Evaluations.propid = ?1 AND Evaluations.methodid = ?2;
)SQL");
  statement stinsert(db,"");
  if (doterm){
    stinsert.recycle(R"SQL(
INSERT INTO Terms (methodid,atom,l,exponent,propid,value) VALUES(:METHOD,:ATOM,:L,:EXP,:PROPID,:VALUE);
)SQL");
  } else {
    stinsert.recycle(R"SQL(
INSERT INTO Evaluations (methodid,propid,value) VALUES(:METHOD,:PROPID,:VALUE);
)SQL");
  }

  // run over all possible combinations of atom, l, and exponent
  for (int ii = 0; ii < zat_.size(); ii++){
    for (int iexp = 0; iexp < exp_.size(); iexp++){

      // build the property map
      std::unordered_map<int,std::vector<double>> propmap;
      ststruct.reset();
      ststruct.bind(1,ptid);
      while (ststruct.step() != SQLITE_DONE){
        int propid = sqlite3_column_int(ststruct.ptr(),0);
        int nstr = sqlite3_column_int(ststruct.ptr(),1);
        int *istr = (int *) sqlite3_column_blob(ststruct.ptr(),2);
        double *coef = (double *) sqlite3_column_blob(ststruct.ptr(),3);
        std::vector<double> value;
        bool found = true;
        for (int i = 0; i < nstr; i++){
          stkey.reset();
          stkey.bind(1,istr[i]);
          stkey.step();
          std::string strname = (char *) sqlite3_column_text(stkey.ptr(),0);

          if (doterm && changename){
            std::string atom = nameguess(zat_[ii]);
            lowercase(atom);
            strname += "@" + atom + "_" + globals::inttol[l_[ii]] + "_" + std::to_string(iexp+1);
          }

          std::cout << strname << std::endl;
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
        if (found){
          if (doterm && doslope){
            steval.reset();
            steval.bind(1,propid);
            steval.bind(2,methodid);
            steval.step();
            int len = sqlite3_column_int(steval.ptr(),0) / sizeof(double);
            double *rval = (double *) sqlite3_column_blob(steval.ptr(),1);
            if (!rval)
              throw std::runtime_error("To use CALCSLOPE in INSERT CALC, the evaluation for the corresponding method and property must be available");
            if (len != value.size())
              throw std::runtime_error("The number of values in the evaluation does not match those in VALUE, in CALCSLOPE, INSERT CALC");
            for (int i = 0; i < value.size(); i++)
              value[i] = (value[i] - rval[i]) / c0;
          }
          propmap[propid] = value;
        }
      }

      // insert into the database
      for (auto it = propmap.begin(); it != propmap.end(); it++){
        stinsert.reset();
        stinsert.bind((char *) ":METHOD",methodid);
        stinsert.bind((char *) ":PROPID",it->first);
        stinsert.bind((char *) ":VALUE",(void *) &it->second[0],false,(it->second).size()*sizeof(double));
        if (doterm){
          stinsert.bind((char *) ":ATOM",(int) zat_[ii]);
          stinsert.bind((char *) ":L",(int) l_[ii]);
          stinsert.bind((char *) ":EXP",exp_[iexp]);
          if (globals::verbose)
            os << "# INSERT TERM (method=" << methodkey << ";property=" << it->first << ";nvalue=" << it->second.size()
               << ";atom=" << (int) zat_[ii] << ";l=" << (int) l_[ii] << ";exp=" << exp[iexp]
               << ")" << std::endl;
        } else {
          if (globals::verbose)
            os << "# INSERT EVALUATION (method=" << methodkey << ";property=" << it->first << ";nvalue=" << it->second.size() << ")" << std::endl;
        }
        if (stinsert.step() != SQLITE_DONE){
          std::cout << "method = " << methodkey << std::endl;
          std::cout << "propid = " << it->first << std::endl;
          std::cout << "value = " << it->second[0] << "(" << it->second.size() << "elements)" << std::endl;
          throw std::runtime_error("Failed inserting data in the database (READ CALC)");
        }
      }
    }
  }
  datmap.clear();

  // commit the transaction
  commit_transaction();
}

// Insert literature references into the database from a bibtex file
void sqldb::insert_litref_bibtex(std::ostream &os, const std::list<std::string> &tokens){
  if (!db) throw std::runtime_error("A database file must be connected before using INSERT");

#ifdef BTPARSE_FOUND
  // check if the file name is empty
  if (tokens.empty())
    throw std::runtime_error("Need a bibtex file name in INSERT");

  // open the file name (need a char* and FILE* for btparse)
  std::string filename = &(tokens.front()[0]);
  FILE *fp = fopen(filename.c_str(),"r");
  if (!fp)
    throw std::runtime_error("Could not open bibtex file in INSERT: " + filename);
  char *filename_c = new char[filename.length() + 1];
  strcpy(filename_c, filename.c_str());

  // begin the transaction
  begin_transaction();

  // prepare the statement
  statement st(db,R"SQL(
INSERT INTO Literature_refs (key,authors,title,journal,volume,page,year,doi,description)
       VALUES(:KEY,:AUTHORS,:TITLE,:JOURNAL,:VOLUME,:PAGE,:YEAR,:DOI,:DESCRIPTION);
)SQL");

  // loop over the contents of the bib file and add to the database
  boolean rc;
  while (AST *entry = bt_parse_entry(fp,filename_c,0,&rc)){
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

        if (globals::verbose)
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

  // close the file and free memory
  fclose(fp);
  delete [] filename_c;

#else
  throw std::runtime_error("Cannot use INSERT LITREF BIBTEX: not compiled with bibtex support");
#endif
}

// Insert additional info from an INSERT SET command (xyz and POSCAR keywords)
void sqldb::insert_set_xyz(std::ostream &os, const std::string &key, const std::unordered_map<std::string,std::string> &kmap){
  if (!db) throw std::runtime_error("A database file must be connected before using INSERT SET");

  // prepare
  std::string skey;
  std::unordered_map<std::string,std::string> smap;

  // begin the transaction
  begin_transaction();

  for (int ixyz = 0; ixyz < 2; ixyz++){
    // tokenize the line following the xyz keyword
    std::list<std::string> tokens;
    if (ixyz == 0 && (kmap.find("XYZ") != kmap.end()))
      tokens = list_all_words(kmap.at("XYZ"));
    else if (kmap.find("POSCAR") != kmap.end())
      tokens = list_all_words(kmap.at("POSCAR"));
    if (tokens.empty())
      continue;

    if (fs::is_directory(tokens.front())){
      // add a directory //

      // interpret the input and build the regex
      auto it = tokens.begin();
      std::string dir = *it, rgx_s;
      if (std::next(it) != tokens.end())
        rgx_s = *(std::next(it));
      else{
        if (ixyz == 0)
          rgx_s = ".*\\.xyz$";
        else
          rgx_s = ".*\\.POSCAR";
      }
      std::regex rgx(rgx_s, std::regex::awk | std::regex::icase | std::regex::optimize);

      // sort the list of files (for reproducible outputs)
      std::set<fs::path> sortedfiles;
      for (auto &f : fs::directory_iterator(dir))
        sortedfiles.insert(f.path());

      // run over directory files and add the structures
      for (const auto& file : sortedfiles){
        std::string filename = file.filename();
        if (std::regex_match(filename.begin(),filename.end(),rgx)){
          skey = key + "." + std::string(file.stem());

          smap.clear();
          if (ixyz == 0)
            smap["XYZ"] = file.string();
          else
            smap["POSCAR"] = file.string();
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
          if (ixyz == 0)
            smap["XYZ"] = *it;
          else
            smap["POSCAR"] = *it;
          smap["SET"] = key;
          insert_structure(os,skey,smap);
        } else {
          throw std::runtime_error("File or directory not found: " + *it);
        }
      }
    } else {
      throw std::runtime_error("File or directory not found: " + tokens.front());
    }
  }

  // commit the transaction
  commit_transaction();
}

// Insert additional info from an INSERT SET command (xyz keyword)
void sqldb::insert_set_din(std::ostream &os, const std::string &key, const std::unordered_map<std::string,std::string> &kmap){
  if (!db) throw std::runtime_error("A database file must be connected before using INSERT");

  // Check sanity of the keywords
  bool havemethod = (kmap.find("METHOD") != kmap.end());

  std::string dir = fetch_directory(kmap);

  std::string din = kmap.at("DIN");
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
        if (!fs::is_regular_file(filename)){
          filename = dir + "/" + info[k].names[i] + ".POSCAR";
          if (!fs::is_regular_file(filename))
            throw std::runtime_error("xyz/POSCAR file not found (" + dir + "/" + info[k].names[i] + ") processing din file " + din);
          smap["POSCAR"] = filename;
        } else {
          smap["XYZ"] = filename;
        }
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
      smap["METHOD"] = kmap.at("METHOD");
      smap["PROPERTY"] = skey;
      smap["VALUE"] = to_string_precise(info[k].ref);
      insert_evaluation(os,smap);
    }
  }

  // commit the transaction
  commit_transaction();
}

// Calculate energy differences from total energies
void sqldb::calc_ediff(std::ostream &os){
  statement stedif(db,R"SQL(
SELECT id, nstructures, structures, coefficients
FROM Properties
WHERE property_type = 1;)SQL");
  statement stetot(db,R"SQL(
SELECT id
FROM Properties
WHERE property_type = 2 AND nstructures = 1 AND structures = :ID;)SQL");
  statement stinsert(db,R"SQL(
INSERT OR REPLACE into Evaluations (methodid,propid,value) VALUES (?1,?2,?3);
)SQL");

  // begin the transaction
  begin_transaction();

  // run over energy_difference properties
  while (stedif.step() != SQLITE_DONE){
    int propid = sqlite3_column_int(stedif.ptr(),0);
    int nstr = sqlite3_column_int(stedif.ptr(),1);
    int *istr = (int *) sqlite3_column_blob(stedif.ptr(),2);
    double *coef = (double *) sqlite3_column_blob(stedif.ptr(),3);
    bool found = true;

    // find the energy properties corresponding to this energy_difference
    int iprop[nstr];
    for (int i = 0; i < nstr; i++){
      stetot.reset();
      stetot.bind(1,(void *) &(istr[i]),false,sizeof(int));
      stetot.step();
      if (sqlite3_column_type(stetot.ptr(),0) == SQLITE_NULL){
        found = false;
        break;
      } else {
        iprop[i] = sqlite3_column_int(stetot.ptr(), 0);
      }
    }

    // if the corresponding energy properties were found
    if (found){
      // statement for fetching the values
      std::string cmd = "SELECT Eval1.methodid";
      for (int i = 0; i < nstr; i++)
        cmd += ", Eval" + std::to_string(i+1) + ".value";
      cmd += " FROM Evaluations as Eval1 ";
      for (int i = 1; i < nstr; i++)
        cmd += "INNER JOIN Evaluations as Eval" + std::to_string(i+1) + " ON Eval1.methodid = Eval" + std::to_string(i+1) + ".methodid ";
      cmd += "WHERE Eval1.propid = " + std::to_string(iprop[0]);
      for (int i = 1; i < nstr; i++)
        cmd += " AND Eval" + std::to_string(i+1) + ".propid = " + std::to_string(iprop[i]);
      statement st(db,cmd);

      while (st.step() != SQLITE_DONE){
        // get the energies and calculate the ediff
        int methodid = sqlite3_column_int(st.ptr(),0);
        double de = 0;
        for (int i = 0; i < nstr; i++){
          double *val = (double *) sqlite3_column_blob(st.ptr(), i+1);
          de += val[0] * coef[i];
        }

        // from Hartree to kcal/mol
        de = de * globals::ha_to_kcal;

        // insert or replace the energy_difference evaluation
        if (globals::verbose)
          os << "# INSERT EVALUATION (method=" << methodid << ";property=" << propid << ";de=" << de << ")" << std::endl;
        stinsert.reset();
        stinsert.bind(1,methodid);
        stinsert.bind(2,propid);
        stinsert.bind(3,(void *) &de,false,sizeof(double));
        if (stinsert.step() != SQLITE_DONE)
          throw std::runtime_error("Failed inserting evaluation in CALC_EDIFF");
      }
    }
  }

  // commit the transaction
  commit_transaction();
}

// Delete items from the database
void sqldb::erase(std::ostream &os, const std::string &category, const std::list<std::string> &tokens) {
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
    statement st(db,"DELETE FROM " + table + ";");
    st.execute();
  } else if (category == "EVALUATION") {
    statement st(db,"DELETE FROM Evaluations WHERE methodid = (SELECT id FROM Methods WHERE key = ?1) AND propid = (SELECT id FROM Properties WHERE key = ?2);");
    for (auto it = tokens.begin(); it != tokens.end(); it++){
      if (globals::verbose)
        os << "# DELETE " << category << " (method=" << *it;
      st.bind(1,*it++);
      if (globals::verbose)
        os << ";property=" << *it << ")" << std::endl;
      st.bind(2,*it);
      st.step();
    }
  } else if (category == "TERM") {
    statement st(db,R"SQL(
DELETE FROM Terms WHERE
  methodid = (SELECT id FROM Methods WHERE key = ?1) AND
  propid = (SELECT id FROM Properties WHERE key = ?2) AND
  atom = ?3 AND l = ?4 AND exponent = ?5;
)SQL");
    for (auto it = tokens.begin(); it != tokens.end(); it++){
      if (globals::verbose)
        os << "# DELETE " << category << " (method=" << *it;
      st.bind(1,*it++);
      if (globals::verbose)
        os << ";property=" << *it;
      st.bind(2,*it++);
      if (globals::verbose)
        os << ";atom=" << *it;
      st.bind(3,std::stoi(*it++));
      if (globals::verbose)
        os << ";l=" << *it;
      st.bind(4,std::stoi(*it++));
      if (globals::verbose)
        os << ";exp=" << *it << ")" << std::endl;
      st.bind(5,std::stod(*it));
      st.step();
    }
  } else {
    statement st_id(db,"DELETE FROM " + table + " WHERE id = ?1;");
    statement st_key(db,"DELETE FROM " + table + " WHERE key = ?1;");
    for (auto it = tokens.begin(); it != tokens.end(); it++){
      if (globals::verbose)
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
    headers = { "id","key","litrefs","description"};
    types   = {t_int,t_str,    t_str,        t_str};
    cols    = {    0,    1,        2,            3};
    stmt = R"SQL(
SELECT id,key,litrefs,description
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
  statement st(db,stmt);

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
  statement st(db,"SELECT id, key, description FROM Property_types;");
  os << "# Table of property types" << std::endl;
  os << "| id | key | description |" << std::endl;
  while (st.step() != SQLITE_DONE){
    int id = sqlite3_column_int(st.ptr(),0);
    const char *key = (const char *) sqlite3_column_text(st.ptr(),1);
    const char *desc = (const char *) sqlite3_column_text(st.ptr(),2);
    os << "| " << id << " | " << key << " | " << (desc?desc:"") << " |" << std::endl;
  }
  os << std::endl;

  // literature references
  st.recycle("SELECT COUNT(id) FROM Literature_refs;");
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
  st.recycle(R"SQL(
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
    st.recycle(R"SQL(
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
void sqldb::print_din(std::ostream &os, const std::unordered_map<std::string,std::string> &kmap){
  if (!db) throw std::runtime_error("A database file must be connected before using LIST DIN");

  std::unordered_map<std::string,std::string>::const_iterator im;

  // get the directory
  std::string dir = fetch_directory(kmap);

  // get the list of sets (IDs and names)
  std::vector<int> idset;
  std::vector<std::string> nameset;
  if ((im = kmap.find("SET")) != kmap.end()){
    std::list<std::string> vlist = list_all_words(im->second);
    for (auto it = vlist.begin(); it != vlist.end(); ++it){
      int idx;
      std::string key;
      if (get_key_and_id(*it,"Sets",key,idx)){
        idset.push_back(idx);
        nameset.push_back(key);
      } else
        throw std::runtime_error("Invalid set " + *it + " in PRINT DIN");
    }
  } else {
    statement st(db,"SELECT id, key FROM Sets ORDER BY id;");
    while (st.step() != SQLITE_DONE){
      idset.push_back(sqlite3_column_int(st.ptr(), 0));
      std::string name = (char *) sqlite3_column_text(st.ptr(), 1);
      nameset.push_back(name);
    }
  }
  if (idset.empty())
    throw std::runtime_error("No sets found in PRINT DIN");

  // get the method and prepare the statement
  statement st(db);
  int methodid = 0;
  std::string methodkey = "(none)";
  if ((im = kmap.find("METHOD")) != kmap.end()){
    if (!get_key_and_id(im->second,"Methods",methodkey,methodid))
      throw std::runtime_error("Invalid method (" + im->second + ") in PRINT DIN");

    // prepare the statement text
    std::string sttext = R"SQL(
SELECT Properties.nstructures, Properties.structures, Properties.coefficients, Evaluations.value
FROM Properties
INNER JOIN Evaluations ON (Properties.id = Evaluations.propid)
INNER JOIN Methods ON (Evaluations.methodid = Methods.id)
WHERE Properties.property_type = 1 AND Properties.setid = :SET AND Methods.id = )SQL";
    sttext = sttext + std::to_string(methodid) + " ORDER BY Properties.orderid;";
    st.recycle(sttext);
  } else {
    // no method was given - zeros as reference values
    std::string sttext = R"SQL(
SELECT Properties.nstructures, Properties.structures, Properties.coefficients
FROM Properties
WHERE Properties.property_type = 1 AND Properties.setid = :SET
ORDER BY Properties.orderid;
)SQL";
    st.recycle(sttext);
  }

  // the statement to fetch a structure name
  for (int i = 0; i < idset.size(); i++){
    // open and write the din header
    std::string fname = dir;
    if (fname.back() != '/')
      fname += "/";
    fname += nameset[i] + ".din";
    std::ofstream ofile(fname,std::ios::trunc);
    if (ofile.fail())
      throw std::runtime_error("Error writing din file " + fname);
    ofile << std::fixed << std::setprecision(10);

    os << "# PRINT DIN writing file: " << fname << std::endl;

    ofile << "# din file crated by acpdb" << std::endl;
    ofile << "# set = " << nameset[i] << std::endl;
    ofile << "# method = " << methodkey << std::endl;

    // step over the components of this set
    st.bind((char *) ":SET",idset[i]);
    while (st.step() != SQLITE_DONE){
      int nstr = sqlite3_column_int(st.ptr(),0);
      int *str = (int *) sqlite3_column_blob(st.ptr(),1);
      double *coef = (double *) sqlite3_column_blob(st.ptr(),2);

      for (int j = 0; j < nstr; j++){
        ofile << coef[j] << std::endl;
        ofile << find_key_from_id(str[j],"Structures") << std::endl;
      }
      ofile << "0" << std::endl;

      if (methodid > 0){
        double *value = (double *) sqlite3_column_blob(st.ptr(),3);
        ofile << value[0] << std::endl;
      } else
        ofile << "0.0" << std::endl;
    }
  }
  os << std::endl;
}

// Verify the consistency of the database
void sqldb::verify(std::ostream &os){
  if (!db) throw std::runtime_error("A database file must be connected before using VERIFY");

  // check litrefs in sets
  os << "Checking the litrefs in sets are known" << std::endl;
  statement st(db,"SELECT litrefs,key FROM Sets;");
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
  st.recycle("SELECT litrefs,key FROM Methods;");
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
  st.recycle("SELECT key,nstructures,structures FROM Properties;");
  statement stcheck(db,"SELECT id FROM Structures WHERE id = ?1;");
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
  st.recycle(R"SQL(
SELECT Evaluations.methodid, Evaluations.propid, Properties.property_type, length(Evaluations.value), Properties.nstructures, Properties.structures
FROM Evaluations, Properties
WHERE Evaluations.propid = Properties.id
)SQL");
  stcheck.recycle("SELECT nat FROM Structures WHERE id = ?1;");
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
  st.recycle(R"SQL(
SELECT Terms.methodid, Terms.atom, Terms.l, Terms.exponent, Terms.propid, Properties.property_type, length(Terms.value), Properties.nstructures, Properties.structures
FROM Terms, Properties
WHERE Terms.propid = Properties.id
)SQL");
  stcheck.recycle("SELECT nat FROM Structures WHERE id = ?1;");
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

// Read data from a file, and compare to the whole database data or
// one of its subsets. If usetrain = 0, assume the training set is
// defined and compare to the whole training set. If usetrain > 0,
// compare to the training set and restrict to set usetrain.
void sqldb::read_and_compare(std::ostream &os, const std::unordered_map<std::string,std::string> &kmap,
                             int usetrain/*=-1*/){
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

  // source
  im = kmap.find("SOURCE");
  if (im == kmap.end())
    throw std::runtime_error("A SOURCE is necessary when using COMPARE");
  std::string source = im->second;

  // set
  int sid = 0;
  if (usetrain >= 0){
    sid = usetrain;
  } else if ((im = kmap.find("SET")) != kmap.end()){
    std::string setnamein;
    if (!get_key_and_id(im->second,"Sets",setnamein,sid))
      throw std::runtime_error("Invalid SET in COMPARE");
  }

  // read the file and build the data file
  int approxm = 0;
  std::unordered_map<std::string,std::vector<double>> datmap;
  bool isfile = fs::is_regular_file(source);
  if (isfile)
    datmap = read_data_file_vector(source,1.);
  else {
    approxm = find_id_from_key(source,"Methods");
    if (!approxm)
      throw std::runtime_error("Invalid SOURCE in COMPARE (not a file or a method key)");
  }

  // fetch the reference method values from the DB and populate vectors
  std::vector<std::string> names_found;
  std::vector<std::string> names_missing_fromdb;
  std::vector<std::string> names_missing_fromdat;
  std::vector<int> numvalues, setid;
  std::vector<double> refvalues, datvalues;
  std::map<int,std::string> setname;
  statement stkey(db,"SELECT key FROM Structures WHERE id = ?1;");
  std::string sttext;

  // the statement text
  sttext = R"SQL(
SELECT Properties.key, Properties.nstructures, Properties.structures, Properties.coefficients, Properties.property_type, Sets.id, Sets.key,
       length(ref.value), ref.value
)SQL";
  if (approxm > 0)
    sttext += ", length(approx.value), approx.value";
  sttext += R"SQL(
FROM Properties
INNER JOIN Sets ON Properties.setid = Sets.id
)SQL";
  if (usetrain >= 0)
    sttext += R"SQL(
INNER JOIN Training_Set ON Training_set.propid = Properties.id
)SQL";
  sttext += R"SQL(
LEFT OUTER JOIN Evaluations AS ref ON (ref.propid = Properties.id AND ref.methodid = :METHOD)
)SQL";
  if (approxm > 0){
    sttext += R"SQL(
LEFT OUTER JOIN Evaluations AS approx ON (approx.propid = Properties.id AND approx.methodid = :AMETHOD)
)SQL";
  }
  sttext += R"SQL(
WHERE Properties.property_type = :PROPERTY_TYPE
)SQL";
  if (sid > 0)
    sttext += R"SQL(
AND Properties.setid = :SET
)SQL";
  sttext += R"SQL(
ORDER BY Properties.id
)SQL";

  statement st(db,sttext);
  if (sid > 0)
    st.bind((char *) ":SET",sid);
  if (approxm > 0)
    st.bind((char *) ":AMETHOD",approxm);
  st.bind((char *) ":METHOD",refm);
  st.bind((char *) ":PROPERTY_TYPE",ppid);

  while (st.step() != SQLITE_DONE){
    // check if the evaluation is available in the database
    std::string key = (char *) sqlite3_column_text(st.ptr(),0);
    if (sqlite3_column_type(st.ptr(),8) == SQLITE_NULL){
      names_missing_fromdb.push_back(key);
      continue;
    }

    // check if the components are in the data file or in the approximate method
    int nvalue = sqlite3_column_int(st.ptr(),7) / sizeof(double);
    int nstr = sqlite3_column_int(st.ptr(),1);
    int *istr = (int *) sqlite3_column_blob(st.ptr(),2);
    double *coef = (double *) sqlite3_column_blob(st.ptr(),3);
    int ptid = sqlite3_column_int(st.ptr(),4);
    int thissetid = sqlite3_column_int(st.ptr(),5);
    std::string thissetname = (char *) sqlite3_column_text(st.ptr(), 6);
    std::vector<double> value(nvalue,0.0);
    bool found = true;

    if (approxm > 0){
      int nvalue_a = sqlite3_column_int(st.ptr(),9) / sizeof(double);
      double *rval_a = (double *) sqlite3_column_blob(st.ptr(),10);
      if (!rval_a || nvalue_a != nvalue){
        found = false;
      } else {
        for (int j = 0; j < nvalue; j++)
          value[j] = rval_a[j];
      }
    } else {
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
      if (ptid == globals::ppty_energy_difference){
        for (int j = 0; j < nvalue; j++)
          value[j] *= globals::ha_to_kcal;
      }
    }

    // populate the vectors
    if (!found){
      names_missing_fromdat.push_back(key);
    } else {
      names_found.push_back(key);
      numvalues.push_back(nvalue);
      setid.push_back(thissetid);
      setname[thissetid] = thissetname;
      double *rval = (double *) sqlite3_column_blob(st.ptr(),8);
      for (int j = 0; j < nvalue; j++){
        refvalues.push_back(rval[j]);
        datvalues.push_back(value[j]);
      }
    }
  }
  datmap.clear();

  // output the header and the statistics
  if (isfile){
    os << "# -- Evaluation of data from file -- " << std::endl
       << "# File: " << source << std::endl;
  } else {
    os << "# -- Evaluation of data from method -- " << std::endl
       << "# Approximate method: " << source << std::endl;
  }
  os << "# Property type: " << ppidname << std::endl
     << "# Reference method: " << refmethodname << std::endl;
  if (!names_missing_fromdat.empty() || !names_missing_fromdat.empty())
    os << "# Statistics: " << "(partial, missing: "
       << names_missing_fromdat.size() << " from source, "
       << names_missing_fromdb.size() << " from reference)" << std::endl;
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
      int ndat = calc_stats(datvalues,refvalues,{},wrms,rms,mae,mse,setid,it->first);
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
  std::string approxname, refname;
  if (isfile)
    approxname = "File";
  else
    approxname = "Approx_method";
  refname = "Ref_method";

  output_eval(os,{},names_found,numvalues,{},datvalues,approxname,refvalues,refname);
  if (!names_missing_fromdb.empty()){
    os << "## The following properties are missing from the REFERENCE:" << std::endl;
    for (int i = 0; i < names_missing_fromdb.size(); i++)
      os << "## " << names_missing_fromdb[i] << std::endl;
  }
  if (!names_missing_fromdat.empty()){
    os << "## The following properties are missing from the " << (isfile?"FILE":"APPROX_METHOD") << ":" << std::endl;
    for (int i = 0; i < names_missing_fromdat.size(); i++)
      os << "## " << names_missing_fromdat[i] << std::endl;
  }
  os << std::endl;
}

// Write input files for a database set or the whole database. The
// options go in map kmap. If the ACP is present, it is passed down
// to the structure writer. If smapin is present, write only the
// structures that are keys in the map (the value of the map is 0 if
// crystal or 1 if molecule). zat, lmax, and exp are used to interpret
// the TERMS keyword.
void sqldb::write_structures(std::ostream &os, const std::unordered_map<std::string,std::string> &kmap, const acp &a,
                             const std::unordered_map<int,int> &smapin/*={}*/, const std::vector<unsigned char> &zat/*={}*/,
                             const std::vector<unsigned char> &lmax/*={}*/, const std::vector<double> &exp/*={}*/){
  if (!db)
    throw std::runtime_error("Error reading connected database");

  std::unordered_map<std::string,std::string>::const_iterator im;

  // directory and pack number
  std::string dir = fetch_directory(kmap);
  int npack = 0;
  if (kmap.find("PACK") != kmap.end()){
    if (isinteger(kmap.at("PACK")))
      npack = std::stoi(kmap.at("PACK"));
    else
      throw std::runtime_error("The argument to PACK must be an integer in WRITE");
  }

  // templates
  std::string template_m = R"SQL(%nat%
%charge% %mult%
%xyz%
)SQL";
  std::string ext_m = "xyz";
  std::string template_c = R"SQL(%basename%
1.0
%cell%
%vaspxyz%
)SQL";
  std::string ext_c = "POSCAR";
  if ((im = kmap.find("TEMPLATE")) != kmap.end()){
    std::ifstream is(im->second.c_str(),std::ios::in);
    template_c = template_m = std::string(std::istreambuf_iterator<char>(is),{});
    ext_c = ext_m = get_file_extension(im->second);
  }
  if ((im = kmap.find("TEMPLATE_MOL")) != kmap.end()){
    std::ifstream is(im->second.c_str(),std::ios::in);
    template_m = std::string(std::istreambuf_iterator<char>(is),{});
    ext_m = get_file_extension(im->second);
  }
  if ((im = kmap.find("TEMPLATE_CRYS")) != kmap.end()){
    std::ifstream is(im->second.c_str(),std::ios::in);
    template_c = std::string(std::istreambuf_iterator<char>(is),{});
    ext_c = get_file_extension(im->second);
  }

  // Collect the structure indices for this set
  std::unordered_map<int,int> smap;
  if (!smapin.empty())
    smap = smapin;
  else{
    // set
    int setid = 0;
    std::string setkey;
    if ((im = kmap.find("SET")) != kmap.end()){
      if (!get_key_and_id(im->second,"Sets",setkey,setid))
        throw std::runtime_error("Invalid SET in WRITE");
    }

    std::string sttext="SELECT Properties.nstructures, Properties.structures FROM Properties";
    if (setid > 0)
      sttext += " WHERE Properties.setid = ?1";
    sttext += ";";
    statement st(db,sttext);
    statement ststr(db,"SELECT ismolecule FROM Structures WHERE id = ?1;");
    if (setid > 0)
      st.bind(1,setid);
    while (st.step() != SQLITE_DONE){
      int n = sqlite3_column_int(st.ptr(),0);
      const int *str = (int *)sqlite3_column_blob(st.ptr(), 1);
      for (int i = 0; i < n; i++){
        ststr.bind(1,str[i]);
        ststr.step();
        smap[str[i]] = sqlite3_column_int(ststr.ptr(),0);
        ststr.reset();
      }
    }
  }

  // Terms
  std::vector<unsigned char> zat_ = {0}, l_ = {0};
  std::vector<double> exp_ = {0.0};
  bool rename = false;
  if ((im = kmap.find("TERM")) != kmap.end()){
    std::list<std::string> words = list_all_words(im->second);
    if (words.size() == 0){
      if (zat.empty() || lmax.empty() || exp.empty())
        throw std::runtime_error("The training set must be defined if using WRITE TERM with no additonal options");

      rename = true;
      exp_ = exp;
      zat_.clear();
      l_.clear();
      for (int izat = 0; izat < zat.size(); izat++){
        for (unsigned char il = 0; il <= lmax[izat]; il++){
          zat_.push_back(zat[izat]);
          l_.push_back(il);
        }
      }

    } else if (words.size() == 3){
      rename = false;
      std::string str = words.front();
      words.pop_front();
      if (isinteger(str))
        zat_[0] = std::stoi(str);
      else
        zat_[0] = zatguess(str);

      str = words.front();
      words.pop_front();
      if (isinteger(str))
        l_[0] = std::stoi(str);
      else{
        lowercase(str);
        if (globals::ltoint.find(str) == globals::ltoint.end())
          throw std::runtime_error("Invalid angular momentum " + str + " in WRITE/TERM");
        l_[0] = globals::ltoint.at(str);
      }

      str = words.front();
      words.pop_front();
      exp_[0] = std::stod(str);
    } else {
      throw std::runtime_error("Invalid number of tokens in WRITE/TERM");
    }
  }

  // write the inputs
  write_many_structures(os,template_m,template_c,ext_m,ext_c,a,smap,zat_,l_,exp_,rename,dir,npack);
  os << std::endl;
}

// Write the structures with IDs given by the keys in smap. The values
// of smap should be 1 if the structures are molecules or zero if they
// are crystals. Use template_m and template_c as templates for
// molecules and crystals. Use ext_m and ext_c as file extensions for
// molecules and crystals. dir: output directory. npack = package and
// compress in packets of npack files (0 = no packing).
void sqldb::write_many_structures(std::ostream &os,
                                  const std::string &template_m, const std::string &template_c,
                                  const std::string &ext_m, const std::string &ext_c,
                                  const acp &a,
                                  const std::unordered_map<int,int> &smap,
                                  const std::vector<unsigned char> &zat, const std::vector<unsigned char> &l, const std::vector<double> &exp,
                                  const bool rename,
                                  const std::string &dir/*="./"*/, int npack/*=0*/){

  // consistency check
  if (zat.size() != l.size())
    throw std::runtime_error("Inconsistent atom and l arrays in write_many_structures");

  // build the templates
  strtemplate tm(template_m);
  strtemplate tc(template_c);

  if (npack <= 0 || npack >= smap.size()){
    for (int ii = 0; ii < zat.size(); ii++){
      for (int iexp = 0; iexp < exp.size(); iexp++){
        for (auto it = smap.begin(); it != smap.end(); it++){
          write_one_structure(os,it->first, (it->second?tm:tc), (it->second?ext_m:ext_c), a,
                              zat[ii], l[ii], exp[iexp], iexp, rename, dir);
        }
      }
    }
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
      for (int ii = 0; ii < zat.size(); ii++){
        for (int iexp = 0; iexp < exp.size(); iexp++){
          written.push_back(fs::path(write_one_structure(os, srand[i], (smap.at(srand[i])?tm:tc), (smap.at(srand[i])?ext_m:ext_c),
                                                         a, zat[ii], l[ii], exp[iexp], iexp, rename, dir)));

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
  }
}

// Write the structure id in the database with template tmpl and
// extension ext. dir: output directory.
std::string sqldb::write_one_structure(std::ostream &os, int id, const strtemplate &tmpl,
                                       const std::string &ext, const acp& a,
                                       const unsigned char zat, const unsigned char l, const double exp, const int iexp,
                                       const bool rename,
                                       const std::string &dir/*="./"*/){

  // get the structure from the database
  statement st(db,R"SQL(
SELECT id, key, setid, ismolecule, charge, multiplicity, nat, cell, zatoms, coordinates
FROM Structures WHERE id = ?1;
)SQL");
  st.bind(1,id);
  st.step();

  // build the molecule/crystal structure
  structure s;
  s.readdbrow(st.ptr());

  // filename, extension
  std::string name;
  if (rename){
    std::string atom = nameguess(zat);
    lowercase(atom);
    name = s.get_name() + "@" + atom + "_" + globals::inttol[l] + "_" + std::to_string(iexp+1) + "." + ext;
  } else {
    name = s.get_name() + "." + ext;
  }

  // write the substitution of the template to a string
  std::string content = tmpl.apply(s,a,zat,l,exp);

  // write the actual file and exit
  if (globals::verbose)
    os << "# WRITE file " << dir << "/" << name << std::endl;
  std::ofstream ofile(dir + "/" + name,std::ios::trunc);
  ofile << content;
  ofile.close();

  return name;
}
