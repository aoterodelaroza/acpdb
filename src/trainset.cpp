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

#include "structure.h"
#include "statement.h"
#include "sqldb.h"
#include "trainset.h"
#include "parseutils.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <unordered_map>
#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;

const static std::unordered_map<std::string, int> ltoint { 
   {"l",0}, {"s",1}, {"p",2}, {"d",3}, {"f",4}, {"g",5}, {"h",6}, 
};
const static std::vector<char> inttol = {'l','s','p','d','f','g','h'};

// Add atoms and max. angular momentum
void trainset::addatoms(const std::list<std::string> &tokens){

  auto it = tokens.begin();
  while (it != tokens.end()){
    std::string at = *(it++);
    std::string l = *(it++);

    unsigned char zat_ = zatguess(at);
    if (zat_ == 0)
      throw std::runtime_error("Invalid atom " + at);

    if (ltoint.find(l) == ltoint.end())
      throw std::runtime_error("Invalid lmax " + l);
    unsigned char lmax_ = ltoint.at(l);
    zat.push_back(zat_);
    lmax.push_back(lmax_);
  }
}

// Add exponents
void trainset::addexp(const std::list<std::string> &tokens){
  for (auto it = tokens.begin(); it != tokens.end(); it++){
    double e_ = std::stod(*it);
    if (e_ < 0)
      throw std::runtime_error("Invalid exponent " + *it);

    exp.push_back(e_);
  }
}

// Add sets
void trainset::addset(sqldb &db, const std::list<std::string> &tokens){
  if (!db) 
    throw std::runtime_error("A database file must be connected before using SET");
  if (tokens.size() < 2)
    throw std::runtime_error("Invalid SET command");

  statement st(db.ptr(),statement::STMT_CUSTOM,R"SQL(
SELECT COUNT(id) FROM Properties WHERE setid = ?1;
)SQL");

  for (auto it = tokens.begin(); it != tokens.end(); it++) {
    std::string name = *it;

    // add the name and the set index
    int idx = db.find_id_from_key(name,statement::STMT_QUERY_SET);
    if (idx == 0)
      throw std::runtime_error("SET identifier not found in database: " + name);
    setname.push_back(name);
    setid.push_back(idx);

    // find the set size
    st.bind(1,idx);
    st.step();
    int size = sqlite3_column_int(st.ptr(),0);
    st.reset();
    if (size == 0)
      throw std::runtime_error("SET does not have any associated properties: " + name);

    // write down the initial index, final index, and size of this set
    int ilast = 0;
    if (!set_final_idx.empty()) ilast = set_final_idx.back();
    set_size.push_back(size);
    set_initial_idx.push_back(ilast);
    set_final_idx.push_back(ilast + size);

    // initialize the weights to one
    for (int i = 0; i < size; i++)
      w.push_back(1.);
  }
}

// Set the reference method
void trainset::setreference(sqldb &db, const std::list<std::string> &tokens){
  if (!db) 
    throw std::runtime_error("A database file must be connected before using REFERENCE");
  if (tokens.empty())
    throw std::runtime_error("Invalid REFEENCE command");
  if (setid.empty())
    throw std::runtime_error("REFEENCE must come after SET(s)");

  auto it = tokens.begin();

  // check if the method is known
  std::string name = *it;
  int idx = db.find_id_from_key(name,statement::STMT_QUERY_METHOD);
  if (idx == 0)
    throw std::runtime_error("METHOD identifier not found in database: " + name);

  // resize it to method containers
  methodname.resize(setid.size());
  methodid.resize(setid.size());

  // write down the method
  if (++it == tokens.end()){
    // apply to all known sets
    for (int i = 0; i < setid.size(); i++){
      methodname[i] = name;
      methodid[i] = idx;
    }
  } else {
    // apply to one set only
    auto ires = find(setname.begin(),setname.end(),*it);
    if (ires == setname.end())
      throw std::runtime_error("SET in METHOD command not found: " + *it);

    const int id = ires - setname.begin();
    methodname[id] = name;
    methodid[id] = idx;
  }
}

// Set the empty method
void trainset::setempty(sqldb &db, const std::list<std::string> &tokens){
  if (!db) 
    throw std::runtime_error("A database file must be connected before using EMPTY");
  if (tokens.empty())
    throw std::runtime_error("Invalid EMPTY command");
  
  std::string name = tokens.front();
  int idx = db.find_id_from_key(name,statement::STMT_QUERY_METHOD);
  if (idx == 0)
    throw std::runtime_error("METHOD identifier not found in database: " + name);

  emptyname = name;
  emptyid = idx;
}

// Add an additional method
void trainset::addadditional(sqldb &db, const std::list<std::string> &tokens){
  if (!db) 
    throw std::runtime_error("A database file must be connected before using ADD");
  if (tokens.empty())
    throw std::runtime_error("Invalid ADD command");
  
  auto it = tokens.begin();

  std::string name = *it;
  int idx = db.find_id_from_key(name,statement::STMT_QUERY_METHOD);
  if (idx == 0)
    throw std::runtime_error("METHOD identifier not found in database: " + name);

  addname.push_back(name);
  addid.push_back(idx);
  addisfit.push_back(++it != tokens.end() && equali_strings(*it,"FIT"));
}

// Set the weights
void trainset::setweight(sqldb &db, const std::list<std::string> &tokens, std::unordered_map<std::string,std::string> &kmap){
  if (!db) 
    throw std::runtime_error("A database file must be connected before using WEIGHT");
  if (setid.empty())
    throw std::runtime_error("WEIGHT must come after SET(s)");

  double wglobal = 1.;
  std::vector<double> wpattern = {1};
  bool norm_ref = false;
  bool norm_nitem = false;
  bool norm_nitemsqrt = false;
  std::vector<std::pair<int, double> > witem = {};

  // parse the keymap
  std::unordered_map<std::string,std::string>::const_iterator im;
  if ((im = kmap.find("GLOBAL")) != kmap.end())
    wglobal = std::stod(im->second);
  if ((im = kmap.find("PATTERN")) != kmap.end()){
    wpattern.clear();
    std::list<std::string> wlist = list_all_words(im->second);
    for (auto it = wlist.begin(); it != wlist.end(); ++it)
      wpattern.push_back(std::stod(*it));
  }
  norm_ref = (kmap.find("NORM_REF") != kmap.end());
  norm_nitem = (kmap.find("NORM_NITEM") != kmap.end());
  norm_nitemsqrt = (kmap.find("NORM_NITEMSQRT") != kmap.end());
  if ((im = kmap.find("ITEM")) != kmap.end()){
    std::list<std::string> wlist = list_all_words(im->second);

    auto it = wlist.begin();
    while (it != wlist.end()){
      int idx = std::stoi(*(it++));
      if (it == wlist.end())
        throw std::runtime_error("Incorrect use of ITEM in WEIGHT keyword");
      double w = std::stod(*(it++));
      witem.push_back(std::pair(idx,w));
    }
  }

  if (tokens.empty()){
    // all sets
    for (int i = 0; i < setid.size(); i++)
      setweight_onlyone(db, i, wglobal, wpattern, norm_ref, norm_nitem, norm_nitemsqrt, witem);
  } else {
    // only the indicated set
    std::string sname = tokens.front();
    auto ires = find(setname.begin(),setname.end(),sname);
    if (ires == setname.end())
      throw std::runtime_error("SET in WEIGHT command not found: " + sname);
    const int sid = ires - setname.begin();
    setweight_onlyone(db, sid, wglobal, wpattern, norm_ref, norm_nitem, norm_nitemsqrt, witem);
  }
}

// Set the weights for one set from the indicated parameters.
void trainset::setweight_onlyone(sqldb &db, int sid, double wglobal, std::vector<double> wpattern, bool norm_ref, bool norm_nitem, bool norm_nitemsqrt, std::vector<std::pair<int,double> > witem){
  
  // calculate the weight from the global and pattern
  int k = 0;
  int npat = wpattern.size();
  for (int i = set_initial_idx[sid]; i < set_final_idx[sid]; i++)
    w[i] = wglobal * wpattern[k++ % npat];

  // calculate the normalization factor
  double norm = 1.;
  if (norm_nitem)
    norm *= set_size[sid];
  if (norm_nitemsqrt)
    norm *= std::sqrt(set_size[sid]);
  if (norm_ref){
    statement st(db.ptr(),statement::STMT_CUSTOM,R"SQL(
SELECT AVG(abs(value)) FROM Evaluations
WHERE methodid = :METHOD AND propid IN 
  (SELECT id FROM Properties WHERE setid = :SET);
)SQL");

    if (methodid.empty() || methodid.size() <= sid || methodid[sid] == 0)
      throw std::runtime_error("Use of NORM_REF requires setting the reference method");
    st.bind((char *) ":METHOD",methodid[sid]);
    st.bind((char *) ":SET",setid[sid]);
    st.step();
    norm *= sqlite3_column_double(st.ptr(),0);
  }

  // apply the normalization factor
  for (int i = set_initial_idx[sid]; i < set_final_idx[sid]; i++)
    w[i] /= norm;

  // set individual items
  for (int i = 0; i < witem.size(); i++)
    w[set_initial_idx[sid]+witem[i].first-1] = witem[i].second;
}

// Describe the current training set
void trainset::describe(std::ostream &os, sqldb &db){
  if (!db) 
    throw std::runtime_error("A database file must be connected before using DESCRIBE");
  if (!isdefined())
    throw std::runtime_error("The training set must be defined completely before using DESCRIBE");

  os << "* Description of the training set (DESCRIBE)" << std::endl << std::endl;
  std::streamsize prec = os.precision(10);

  // Atoms and lmax //
  os << "+ List of atoms and maximum angular momentum channels (" << zat.size() << ")" << std::endl;
  os << "| Atom | lmax |" << std::endl;
  for (int i = 0; i < zat.size(); i++){
    os << "| " << nameguess(zat[i]) << " | " << inttol[lmax[i]] << " |" << std::endl;
  }
  os << std::endl;

  // Exponents //
  os << "+ List of exponents (" << exp.size() << ")" << std::endl;
  os << "| id | exp |" << std::endl;
  for (int i = 0; i < exp.size(); i++){
    os << "| " << i << " | " << exp[i] << " |" << std::endl;
  }
  os << std::endl;

  // Sets //
  statement st(db.ptr(),statement::STMT_CUSTOM,R"SQL(
SELECT litrefs, description FROM Sets WHERE id = ?1;
)SQL");
  os << "+ List of sets (" << setname.size() << ")" << std::endl;
  os << "| name | id | initial | final | size | ref. method | ref. meth. id | litref | description |" << std::endl;
  for (int i = 0; i < setname.size(); i++){
    st.reset();
    st.bind(1,setid[i]);
    st.step();
    os << "| " << setname[i] << " | " << setid[i] << " | " << set_initial_idx[i] 
       << " | " << set_final_idx[i] << " | " << set_size[i] 
       << " | " << methodname[i] << " | " << methodid[i]
       << " | " << sqlite3_column_text(st.ptr(), 0) << " | " << sqlite3_column_text(st.ptr(), 1)
       << " |" << std::endl;
  }
  os << std::endl;

  // Methods //
  os << "+ List of methods (see above for reference)" << std::endl;
  os << "| type | name | id | for fit? |" << std::endl;
  os << "| empty | " << emptyname << " | " << emptyid << " | |" << std::endl;
  for (int i = 0; i < addname.size() ; i++){
    os << "| additional | " << addname[i] << " | " << addid[i] << " | " << addisfit[i] << " |" << std::endl;
  }
  os << std::endl;

  // Properties //
  int nall = std::accumulate(set_size.begin(),set_size.end(),0);
  os << "+ List of properties (" << nall << ")" << std::endl;
  os << "| id | property | propid | set | proptype | nstruct | weight | refvalue |" << std::endl;
  st.recycle(statement::STMT_CUSTOM,R"SQL(
SELECT Properties.id, Properties.key, Properties.nstructures, Evaluations.value, Property_types.key
FROM Properties
INNER JOIN Evaluations ON (Properties.id = Evaluations.propid AND Evaluations.methodid = 1)
INNER JOIN Property_types ON (Properties.property_type = Property_types.id)
INNER JOIN Methods ON (Evaluations.methodid = Methods.id)
WHERE Properties.setid = :SET AND Methods.id = :METHOD;
)SQL");

  int n = 0;
  for (int i = 0; i < setid.size(); i++){
    st.reset();
    st.bind((char *) ":SET",setid[i]);
    st.bind((char *) ":METHOD",methodid[i]);

    int rc;
    while ((rc = st.step()) != SQLITE_DONE){
      os << "| " << n << " | " << sqlite3_column_text(st.ptr(), 1) << " | " << sqlite3_column_int(st.ptr(), 0)
         << " | " << setname[i] << " | " << sqlite3_column_text(st.ptr(), 4) << " | " << sqlite3_column_int(st.ptr(), 2)
         << " | " << w[n] << " | " << sqlite3_column_double(st.ptr(), 3) 
         << " |" << std::endl;
      n++;
    }
  }
  os << std::endl;

  // Completion //
  os << "* Calculation completion for the current training set" << std::endl;
  st.recycle(statement::STMT_CUSTOM,R"SQL(
SELECT COUNT(*)
FROM Evaluations
INNER JOIN Properties ON Properties.id = Evaluations.propid
WHERE Properties.setid = :SET AND Evaluations.methodid = :METHOD;)SQL");

  // count reference, empty, and additional values
  int ncalc = 0;
  for (int i = 0; i < setid.size(); i++){
    st.reset();
    st.bind((char *) ":SET",setid[i]);
    st.bind((char *) ":METHOD",methodid[i]);
    st.step();
    ncalc += sqlite3_column_int(st.ptr(), 0);
  }
  os << "+ Reference: " << ncalc << "/" << nall << std::endl;

  ncalc = 0;
  for (int i = 0; i < setid.size(); i++){
    st.reset();
    st.bind((char *) ":SET",setid[i]);
    st.bind((char *) ":METHOD",emptyid);
    st.step();
    ncalc += sqlite3_column_int(st.ptr(), 0);
  }
  os << "+ Empty: " << ncalc << "/" << nall << std::endl;

  for (int j = 0; j < addid.size(); j++){
    ncalc = 0;
    for (int i = 0; i < setid.size(); i++){
      st.reset();
      st.bind((char *) ":SET",setid[i]);
      st.bind((char *) ":METHOD",addid[j]);
      st.step();
      ncalc += sqlite3_column_int(st.ptr(), 0);
    }
    os << "+ Additional (" << addname[j] << "): " << ncalc << "/" << nall << std::endl;
  }

  // terms
  std::string sttext = R"SQL(
SELECT COUNT(*)
FROM Terms
INNER JOIN Properties ON Properties.id = Terms.propid
WHERE Terms.methodid = :METHOD AND Terms.atom = :ATOM AND Terms.l = :L AND Terms.exponent = :EXP)SQL";
  sttext = sttext + " AND Properties.setid IN (" + std::to_string(setid[0]);
  for (int i = 1; i < setid.size(); i++)
    sttext = sttext + "," + std::to_string(setid[i]);
  sttext = sttext + ");";
  st.recycle(statement::STMT_CUSTOM,sttext);

  int ncall = 0, ntall = 0;
  os << "+ Terms: " << std::endl;
  for (int iz = 0; iz < zat.size(); iz++){
    for (int il = 0; il <= lmax[iz]; il++){
      for (int ie = 0; ie < exp.size(); ie++){
        st.reset();
        st.bind((char *) ":METHOD",emptyid);
        st.bind((char *) ":ATOM",(int) zat[iz]);
        st.bind((char *) ":L",il);
        st.bind((char *) ":EXP",exp[ie]);
        st.step();
        ncalc = sqlite3_column_int(st.ptr(), 0);
        os << "| " << nameguess(zat[iz]) << " | " << inttol[il] << " | " 
           << exp[ie] << " | " << ncalc << "/" << nall << " |" << std::endl;
        ncall += ncalc;
        ntall += nall;
      }
    }
  }
  os << "+ Total terms: " << ncalc << "/" << ntall << std::endl;
  os << std::endl;

  // clean up
  os.precision(prec);
}

void trainset::write_xyz(sqldb &db, const std::list<std::string> &tokens){
  if (!db) 
    throw std::runtime_error("A database file must be connected before using DESCRIBE");
  if (!isdefined())
    throw std::runtime_error("The training set must be defined completely before using WRITE_XYZ");
  
  std::string dir = ".";
  if (!tokens.empty()) dir = tokens.front();
  
  if (!fs::is_directory(tokens.front()))
    throw std::runtime_error("In WRITE XYZ, directory not found: " + dir);

   std::string sttext = R"SQL(
SELECT id, key, setid, ismolecule, charge, multiplicity, nat, cell, zatoms, coordinates
FROM Structures
WHERE setid IN ()SQL";
   sttext = sttext + std::to_string(setid[0]);
  for (int i = 1; i < setid.size(); i++)
    sttext = sttext + "," + std::to_string(setid[i]);
  sttext = sttext + ");";
  statement st(db.ptr(),statement::STMT_CUSTOM,sttext);

  while (st.step() != SQLITE_DONE){
    // readdbrow
    std::string key = (char *) sqlite3_column_text(st.ptr(), 1);
    std::string fname = dir + "/" + key + ".xyz";

    structure s;
    s.readdbrow(st.ptr());

    std::ofstream ofile(fname,std::ios::out);
    if (ofile.fail()) 
      throw std::runtime_error("Error writing xyz file " + fname);
    s.writexyz(ofile);
  }

}

