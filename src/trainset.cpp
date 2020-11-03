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

// return the set constraint for set id
std::string trainset::set_constraint(int id){
  if (set_mask[id].empty()) return "";

  bool alltrue = true;
  for (int i = 0; i < set_mask[id].size(); i++)
    alltrue = (alltrue && set_mask[id][i]);

  std::string res = " Properties.setid = " + std::to_string(setid[id]) + " AND Properties.orderid ";
  if (alltrue){
    res = res + "BETWEEN 1 AND " + std::to_string(set_mask[id].size());
  } else {
    res = res + "IN (";
    for (int i = 0; i < set_mask[id].size(); i++){
      if (set_mask[id][i])
        res = res + std::to_string(i+1) + ",";
    }
    res = res.substr(0,res.size()-1) + ")";
  }
  return res;
}

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
void trainset::addset(sqldb &db, const std::list<std::string> &tokens, bool dofit){
  if (!db) 
    throw std::runtime_error("A database file must be connected before using SET");
  if (tokens.size() < 1)
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
    set_dofit.push_back(dofit);
    set_mask.push_back(std::vector<bool>(size,true));

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

  // check if the method is known
  auto it = tokens.begin();
  methodname = *it;
  methodid = db.find_id_from_key(methodname,statement::STMT_QUERY_METHOD);
  if (methodid == 0)
    throw std::runtime_error("METHOD identifier not found in database: " + methodname);
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
  for (int i = set_initial_idx[sid]; i < set_final_idx[sid]; i++){
    if (set_mask[sid][i])
      w[i] = wglobal * wpattern[k++ % npat];
    else
      w[i] = 0;
  }

  // calculate the normalization factor
  double norm = 1.;
  if (norm_nitem)
    norm *= set_size[sid];
  if (norm_nitemsqrt)
    norm *= std::sqrt(set_size[sid]);
  if (norm_ref){
    std::string text = R"SQL(
SELECT AVG(abs(value)) FROM Evaluations
WHERE methodid = :METHOD AND propid IN 
  (SELECT id FROM Properties WHERE )SQL";
    text = text + set_constraint(sid) + ");";
    statement st(db.ptr(),statement::STMT_CUSTOM,text);

    st.bind((char *) ":METHOD",methodid);
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

// Set the masks
void trainset::setmask(sqldb &db, std::string &key, std::string &category, std::list<std::string> &tokens){
  if (!db) 
    throw std::runtime_error("A database file must be connected before using MASK");
  if (setid.empty())
    throw std::runtime_error("There are no sets in the training set (MASK)");

  // identify the set
  int sid;
  bool found;
  if (isinteger(key))
    sid = std::stoi(key);
  else
    sid = db.find_id_from_key(key,statement::STMT_QUERY_SET);
  for (int i = 0; i < setid.size(); i++){
    found = (setid[i] == sid);
    if (found){
      sid = i;
      break;
    }
  }
  if (!found)
    throw std::runtime_error("Could not find set " + key + " in MASK");

  // reset the mask
  for (int i = 0; i < set_mask[sid].size(); i++)
    set_mask[sid][i] = false;

  // interpret the category
  if (category == "RANGE"){
    int size = set_mask[sid].size();
    int istart = 0, iend = size, istep = 1;

    // read the start, end, and step
    if (tokens.empty())
      throw std::runtime_error("Empty range in MASK");
    istart = std::stoi(popstring(tokens)) - 1;
    if (!tokens.empty()){
      iend = std::stoi(popstring(tokens));
      if (!tokens.empty())
        istep = std::stoi(tokens.front());
    }
    if (istart < 0 || istart >= size || iend < 1 || iend > size || istep < 0) 
      throw std::runtime_error("Invalid range in MASK");

    // reassign the mask and the size for this set
    for (int i = istart; i < iend; i+=istep)
      set_mask[sid][i] = true;

  } else if (category == "ITEMS") {
    if (tokens.empty())
      throw std::runtime_error("Empty item list in MASK");
    while (!tokens.empty()){
      int item = std::stoi(popstring(tokens)) - 1;
      if (item < 0 || item >= set_mask[sid].size())
        throw std::runtime_error("Item " + std::to_string(item) + " out of range in MASK");
      set_mask[sid][item] = true;
    }

  } else {
    throw std::runtime_error("Unknown category " + category + " in MASK");
  }

  // recalculate set_size
  set_size[sid] = 0;
  for (int i = 0; i < set_mask[sid].size(); i++){
    if (set_mask[sid][i])
      set_size[sid]++;
  }
}

// Describe the current training set
void trainset::describe(std::ostream &os, sqldb &db){
  if (!db) 
    throw std::runtime_error("A database file must be connected before using DESCRIBE");
  if (!isdefined()){
    if (zat.empty()) os << "--- No atoms found (ATOM) ---" << std::endl;
    if (exp.empty()) os << "--- No exponents found (EXP) ---" << std::endl;
    if (setid.empty()) os << "--- No sets found (SET) ---" << std::endl;
    if (emptyname.empty()) os << "--- No empty method found (EMPTY) ---" << std::endl;
    if (methodname.empty()) os << "--- No reference method found (REFERENCE) ---" << std::endl;
    throw std::runtime_error("The training set must be defined completely before using DESCRIBE");
  }

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
  os << "| name | id | initial | final | size | dofit? | litref | description |" << std::endl;
  for (int i = 0; i < setname.size(); i++){
    st.reset();
    st.bind(1,setid[i]);
    st.step();
    os << "| " << setname[i] << " | " << setid[i] << " | " << set_initial_idx[i] 
       << " | " << set_final_idx[i] << " | " << set_size[i] << " | " << set_dofit[i] 
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
INNER JOIN Evaluations ON (Properties.id = Evaluations.propid)
INNER JOIN Property_types ON (Properties.property_type = Property_types.id)
INNER JOIN Methods ON (Evaluations.methodid = Methods.id)
WHERE Properties.setid = :SET AND Methods.id = :METHOD
ORDER BY Properties.orderid;
)SQL");

  int n = 0;
  for (int i = 0; i < setid.size(); i++){
    st.reset();
    st.bind((char *) ":SET",setid[i]);
    st.bind((char *) ":METHOD",methodid);

    int rc, k = 0;
    while ((rc = st.step()) != SQLITE_DONE){
      if (set_mask[i][k++]){
        os << "| " << n << " | " << sqlite3_column_text(st.ptr(), 1) << " | " << sqlite3_column_int(st.ptr(), 0)
           << " | " << setname[i] << " | " << sqlite3_column_text(st.ptr(), 4) << " | " << sqlite3_column_int(st.ptr(), 2)
           << " | " << w[n] << " | " << sqlite3_column_double(st.ptr(), 3) 
           << " |" << std::endl;
      }
      n++;
    }
  }
  os << std::endl;

  // Completion //
  os << "* Calculation completion for the current training set" << std::endl;

  // count reference, empty, and additional values
  int ncalc = 0;
  for (int i = 0; i < setid.size(); i++){
    std::string text = R"SQL(
SELECT COUNT(*)
FROM Evaluations
INNER JOIN Properties ON Properties.id = Evaluations.propid
WHERE Evaluations.methodid = )SQL";
    text = text + std::to_string(methodid) + " AND " + set_constraint(i) + ";";
    st.recycle(statement::STMT_CUSTOM,text);
    st.step();
    ncalc += sqlite3_column_int(st.ptr(), 0);
  }
  os << "+ Reference: " << ncalc << "/" << nall << std::endl;
 
  ncalc = 0;
  for (int i = 0; i < setid.size(); i++){
    std::string text = R"SQL(
SELECT COUNT(*)
FROM Evaluations
INNER JOIN Properties ON Properties.id = Evaluations.propid
WHERE Evaluations.methodid = )SQL";
    text = text + std::to_string(emptyid) + " AND " + set_constraint(i) + ";";
    st.recycle(statement::STMT_CUSTOM,text);
    st.step();
    ncalc += sqlite3_column_int(st.ptr(), 0);
  }
  os << "+ Empty: " << ncalc << "/" << nall << std::endl;

  for (int j = 0; j < addid.size(); j++){
    ncalc = 0;
    for (int i = 0; i < setid.size(); i++){
      std::string text = R"SQL(
SELECT COUNT(*)
FROM Evaluations
INNER JOIN Properties ON Properties.id = Evaluations.propid
WHERE Evaluations.methodid = )SQL";
      text = text + std::to_string(addid[j]) + " AND " + set_constraint(i) + ";";
      st.recycle(statement::STMT_CUSTOM,text);
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

  sttext = sttext + " AND ((" + set_constraint(0) + ")";
  for (int i = 1; i < setid.size(); i++)
    sttext = sttext + " OR (" + set_constraint(i) + ")";
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
    throw std::runtime_error("A database file must be connected before using WRITE XYZ");
  if (setid.empty())
    throw std::runtime_error("There are no sets in the training set (WRITE XYZ)");
  
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

void trainset::write_din(sqldb &db, const std::list<std::string> &tokens){
  if (!db) 
    throw std::runtime_error("A database file must be connected before using WRITE DIN");
  if (setid.empty())
    throw std::runtime_error("Training set subsets must be defined before using WRITE DIN");
  
  // check dir
  std::string dir = ".";
  if (!tokens.empty()) dir = tokens.front();
  if (!fs::is_directory(tokens.front()))
    throw std::runtime_error("In WRITE DIN, directory not found: " + dir);

  // define the query statements
  statement st(db.ptr(),statement::STMT_CUSTOM,R"SQL(
SELECT Properties.nstructures, Properties.structures, Properties.coefficients, Evaluations.value
FROM Properties
INNER JOIN Evaluations ON (Properties.id = Evaluations.propid)
INNER JOIN Methods ON (Evaluations.methodid = Methods.id)
WHERE Properties.setid = :SET AND Methods.id = :METHOD
ORDER BY Properties.orderid;
)SQL");
  statement stname(db.ptr(),statement::STMT_CUSTOM,R"SQL(
SELECT key FROM Structures WHERE id = ?1;
)SQL");

  for (int i = 0; i < setid.size(); i++){
    // open and write the din header
    std::string fname = dir + "/" + setname[i] + ".din";
    std::ofstream ofile(fname,std::ios::out);
    if (ofile.fail()) 
      throw std::runtime_error("Error writing din file " + fname);
    std::streamsize prec = ofile.precision(10);
    ofile << "# din file crated by acpdb" << std::endl;
    ofile << "# setid = " << setid[i] << std::endl;
    ofile << "# setname = " << setname[i] << std::endl;
    ofile << "# set_initial_idx = " << set_initial_idx[i] << std::endl;
    ofile << "# set_final_idx = " << set_final_idx[i] << std::endl;
    ofile << "# set_size = " << set_size[i] << std::endl;
    ofile << "# set used in fit? = " << set_dofit[i] << std::endl;
    ofile << "# reference method = " << methodname << std::endl;
    ofile << "# reference id = " << methodid << std::endl;
  
    // step over the components of this set
    st.bind((char *) ":METHOD",methodid);
    st.bind((char *) ":SET",setid[i]);
    while (st.step() != SQLITE_DONE){
      int nstr = sqlite3_column_int(st.ptr(),0);
      int *str = (int *) sqlite3_column_blob(st.ptr(),1);
      double *coef = (double *) sqlite3_column_blob(st.ptr(),2);
      double value = sqlite3_column_double(st.ptr(),3);

      for (int j = 0; j < nstr; j++){
        stname.bind(1,str[j]);
        stname.step();
        std::string name = (char *) sqlite3_column_text(stname.ptr(), 0);
        ofile << coef[j] << std::endl;
        ofile << name << std::endl;
        stname.reset();
      }
      ofile << "0" << std::endl;
      ofile << value << std::endl;
    }
  }
}

