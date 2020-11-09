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
#include "outputeval.h"
#include "globals.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <unordered_map>
#include <cmath>
#include <filesystem>
#include <unordered_map>
#include <cstring>

namespace fs = std::filesystem;

const static std::unordered_map<std::string, int> ltoint { 
   {"l",0}, {"s",1}, {"p",2}, {"d",3}, {"f",4}, {"g",5}, {"h",6}, 
};
const static std::vector<char> inttol = {'l','s','p','d','f','g','h'};

// Register the database and create the Training_set table
void trainset::setdb(sqldb *db_){
  db = db_;

  // Drop the Training_set table if it exists, then create it
  statement st(db->ptr(),statement::STMT_CUSTOM,R"SQL(
CREATE TABLE IF NOT EXISTS Training_set (
  id INTEGER PRIMARY KEY,
  propid INTEGER NOT NULL,
  isfit INTEGER,
  FOREIGN KEY(propid) REFERENCES Properties(id) ON DELETE CASCADE
);
DELETE FROM Training_set;
CREATE INDEX IF NOT EXISTS Training_set_idx ON Training_set (propid,isfit);
)SQL");
  st.execute();
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

// Add a subset (combination of set, mask, weights)
void trainset::addsubset(const std::string &key, std::unordered_map<std::string,std::string> &kmap){
  if (!db || !(*db)) 
    throw std::runtime_error("A database file must be connected before using SUBSET");
  if (kmap.find("SET") == kmap.end() || kmap["SET"].empty())
    throw std::runtime_error("The keyword SET is required inside SUBSET");

  // identify the set; add the name and the set index
  std::string name = kmap["SET"];
  int idx = db->find_id_from_key(name,statement::STMT_QUERY_SET);
  int sid = setid.size();
  if (idx == 0)
    throw std::runtime_error("SET identifier not found in database: " + name);
  setname.push_back(name);
  setid.push_back(idx);

  // add the alias
  std::string alias_ = key;
  if (alias_.empty()) alias_ = name;
  alias.push_back(alias_);

  // find the set size
  statement st(db->ptr(),statement::STMT_CUSTOM,R"SQL(
SELECT COUNT(id) FROM Properties WHERE setid = ?1;
)SQL");
  st.bind(1,idx);
  st.step();
  int size = sqlite3_column_int(st.ptr(),0);
  if (size == 0)
    throw std::runtime_error("SET does not have any associated properties: " + name);

  // write down the initial index
  int ilast = 0;
  if (!set_final_idx.empty()) ilast = set_final_idx.back();
  set_initial_idx.push_back(ilast);

  // is this set used in the fit? (nofit keyword)
  set_dofit.push_back(kmap.find("NOFIT") == kmap.end());

  //// mask ////
  std::vector<bool> set_mask(size,true);
  if (kmap.find("MASK") != kmap.end()){
    // set all elements in the mask to zero
    for (int i = 0; i < set_mask.size(); i++)
      set_mask[i] = false;

    std::list<std::string> tokens(list_all_words(kmap["MASK"]));
    std::string keyw = popstring(tokens,true);

    if (keyw == "RANGE"){
      int istart = 0, iend = size, istep = 1;

      // read the start, end, and step
      if (tokens.empty())
        throw std::runtime_error("Empty range in SUBSET/MASK");
      istart = std::stoi(popstring(tokens)) - 1;
      if (!tokens.empty()){
        iend = std::stoi(popstring(tokens));
        if (!tokens.empty())
          istep = std::stoi(tokens.front());
      }
      if (istart < 0 || istart >= size || iend < 1 || iend > size || istep < 0) 
        throw std::runtime_error("Invalid range in SUBSET/MASK");

      // reassign the mask and the size for this set
      for (int i = istart; i < iend; i+=istep)
        set_mask[i] = true;

    } else if (keyw == "ITEMS") {
      if (tokens.empty())
        throw std::runtime_error("Empty item list in SUBSET/MASK");
      while (!tokens.empty()){
        int item = std::stoi(popstring(tokens)) - 1;
        if (item < 0 || item >= size)
          throw std::runtime_error("Item " + std::to_string(item+1) + " out of range in MASK");
        set_mask[item] = true;
      }

    } else if (keyw == "PATTERN") {
      if (tokens.empty())
        throw std::runtime_error("Empty pattern in SUBSET/MASK");
      std::vector<bool> pattern(tokens.size(),false);
      int n = 0;
      while (!tokens.empty())
        pattern[n++] = (popstring(tokens) != "0");
      for (int i = 0; i < set_mask.size(); i++)
        set_mask[i] = pattern[i % pattern.size()];

    } else if (keyw == "ATOMS") {
      if (zat.empty())
        throw std::runtime_error("ATOMS in SUBSET/MASK is not possible if no atoms have been defined");
    
      // build the array of structures that contain only the atoms in the zat array
      std::unordered_map<int,bool> usest;
      statement st(db->ptr(),statement::STMT_CUSTOM,"SELECT id,nat,zatoms FROM Structures WHERE setid = " + std::to_string(setid[sid]) + ";");
      while (st.step() != SQLITE_DONE){
        int id = sqlite3_column_int(st.ptr(),0);
        int nat = sqlite3_column_int(st.ptr(),1);
        unsigned char *zat_ = (unsigned char *) sqlite3_column_blob(st.ptr(),2);

        bool res = true;
        for (int j = 0; j < nat; j++){
          bool found = false;
          for (int k = 0; k < zat.size(); k++){
            if (zat[k] == zat_[j]){
              found = true;
              break;
            }
          }
          if (!found){
            res = false;
            break;
          }
        }
        usest[id] = res;
      }

      // run over the properties in this set and write the mask
      st.recycle(statement::STMT_CUSTOM,"SELECT nstructures,structures FROM Properties WHERE setid = " + 
                 std::to_string(setid[sid]) + " ORDER BY orderid;");
      int n = 0;
      while (st.step() != SQLITE_DONE){
        int nstr = sqlite3_column_int(st.ptr(),0);
        int *str = (int *) sqlite3_column_blob(st.ptr(),1);
      
        bool found = false;
        for (int i = 0; i < nstr; i++){
          if (!usest[str[i]]){
            found = true;
            break;
          }
        }
        set_mask[n++] = !found;
      }
    } else {
      throw std::runtime_error("Unknown category " + keyw + " in MASK");
    }
  }

  // build the propid, size, total size, and final index of the set
  set_size.push_back(0);
  set_final_idx.push_back(ilast);
  st.recycle(statement::STMT_CUSTOM,R"SQL(
SELECT id FROM Properties WHERE setid = ?1 ORDER BY orderid;
)SQL");
  st.bind(1,setid[sid]);
  for (int i = 0; i < set_mask.size(); i++){
    st.step();
    int propid_ = sqlite3_column_int(st.ptr(),0);
    if (set_mask[i]){
      set_size[sid]++;
      set_final_idx[sid]++;
      ntot++;
      propid.push_back(propid_);
    }
  }

  // populate the Training_set table
  insert_subset_db(sid);

  //// weights ////
  // parse the keymap
  double wglobal = 1.;
  if (kmap.find("WEIGHT_GLOBAL") != kmap.end())
    wglobal = std::stod(kmap["WEIGHT_GLOBAL"]);

  std::vector<double> wpattern = {1};
  if (kmap.find("WEIGHT_PATTERN") != kmap.end()){
    wpattern.clear();
    std::list<std::string> wlist = list_all_words(kmap["WEIGHT_PATTERN"]);
    for (auto it = wlist.begin(); it != wlist.end(); ++it)
      wpattern.push_back(std::stod(*it));
  }

  bool norm_ref = (kmap.find("NORM_REF") != kmap.end());
  bool norm_nitem = (kmap.find("NORM_NITEM") != kmap.end());
  bool norm_nitemsqrt = (kmap.find("NORM_NITEMSQRT") != kmap.end());

  std::vector<std::pair<int, double> > witem = {};
  if (kmap.find("WEIGHT_ITEMS") != kmap.end()){
    std::list<std::string> wlist = list_all_words(kmap["WEIGHT_ITEMS"]);

    auto it = wlist.begin();
    while (it != wlist.end()){
      int idx = std::stoi(*(it++));
      if (it == wlist.end())
        throw std::runtime_error("Incorrect use of ITEM in SUBSET/WEIGHT keyword");
      double w = std::stod(*(it++));
      witem.push_back(std::pair(idx,w));
    }
  }

  // set the weights
  // calculate the weight from the global and pattern
  int k = 0;
  for (int i = set_initial_idx[sid]; i < set_final_idx[sid]; i++)
    w.push_back(wglobal * wpattern[k++ % wpattern.size()]);

  // calculate the normalization factor
  double norm = 1.;
  if (norm_nitem)
    norm *= set_size[sid];
  if (norm_nitemsqrt)
    norm *= std::sqrt(set_size[sid]);
  if (norm_ref){
    statement st(db->ptr(),statement::STMT_CUSTOM,R"SQL(
SELECT AVG(abs(value)) FROM Evaluations, Training_set, Properties
WHERE Properties.id = Evaluations.propid AND Properties.setid = :SETID AND
Evaluations.methodid = :METHOD AND 
Evaluations.propid = Training_set.propid AND Training_set.isfit IS NOT NULL;
)SQL");
    st.bind((char *) ":SETID",setid[sid]);
    st.bind((char *) ":METHOD",refid);
    st.step();
    double res = sqlite3_column_double(st.ptr(),0);
    if (std::abs(res) > 1e-40)
      norm *= res;
    else
      throw std::runtime_error("In SUBSET, cannot use NORM_REF without known reference data");
  }

  // apply the normalization factor
  for (int i = set_initial_idx[sid]; i < set_final_idx[sid]; i++)
    w[i] /= norm;

  // set individual items
  for (int i = 0; i < witem.size(); i++){
    int id = set_initial_idx[sid]+witem[i].first-1;
    if (id < set_initial_idx[sid] || id >= set_final_idx[sid])
      throw std::runtime_error("In SUBSET, WEIGHT ITEM out of bounds");
    w[id] = witem[i].second;
  }
}

// Set the reference method
void trainset::setreference(const std::list<std::string> &tokens){
  if (!db || !(*db)) 
    throw std::runtime_error("A database file must be connected before using REFERENCE");
  if (tokens.empty())
    throw std::runtime_error("Invalid REFEENCE command");

  // check if the method is known
  auto it = tokens.begin();
  refname = *it;
  refid = db->find_id_from_key(refname,statement::STMT_QUERY_METHOD);
  if (refid == 0)
    throw std::runtime_error("METHOD identifier not found in database: " + refname);
}

// Set the empty method
void trainset::setempty(const std::list<std::string> &tokens){
  if (!db || !(*db)) 
    throw std::runtime_error("A database file must be connected before using EMPTY");
  if (tokens.empty())
    throw std::runtime_error("Invalid EMPTY command");
  
  std::string name = tokens.front();
  int idx = db->find_id_from_key(name,statement::STMT_QUERY_METHOD);
  if (idx == 0)
    throw std::runtime_error("METHOD identifier not found in database: " + name);

  emptyname = name;
  emptyid = idx;
}

// Add an additional method
void trainset::addadditional(const std::list<std::string> &tokens){
  if (!db || !(*db)) 
    throw std::runtime_error("A database file must be connected before using ADD");
  if (tokens.empty())
    throw std::runtime_error("Invalid ADD command");
  
  auto it = tokens.begin();

  std::string name = *it;
  int idx = db->find_id_from_key(name,statement::STMT_QUERY_METHOD);
  if (idx == 0)
    throw std::runtime_error("METHOD identifier not found in database: " + name);

  addname.push_back(name);
  addid.push_back(idx);
  addisfit.push_back(++it != tokens.end() && equali_strings(*it,"FIT"));
}

// Describe the current training set
void trainset::describe(std::ostream &os) const {
  if (!db || !(*db)) 
    throw std::runtime_error("A database file must be connected before using DESCRIBE");
  if (!isdefined()){
    if (zat.empty()) os << "--- No atoms found (ATOM) ---" << std::endl;
    if (exp.empty()) os << "--- No exponents found (EXP) ---" << std::endl;
    if (setid.empty()) os << "--- No subsets found (SUBSET) ---" << std::endl;
    if (emptyname.empty()) os << "--- No empty method found (EMPTY) ---" << std::endl;
    if (refname.empty()) os << "--- No reference method found (REFERENCE) ---" << std::endl;
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
  statement st(db->ptr(),statement::STMT_CUSTOM,R"SQL(
SELECT litrefs, description FROM Sets WHERE id = ?1;
)SQL");
  os << "+ List of subsets (" << setname.size() << ")" << std::endl;
  os << "| id | alias | db-name | db-id | initial | final | size | dofit? | litref | description |" << std::endl;
  for (int i = 0; i < setname.size(); i++){
    st.reset();
    st.bind(1,setid[i]);
    st.step();
    os << "| " << i << " | " << alias[i] << " | " << setname[i] << " | " << setid[i] 
       << " | " << set_initial_idx[i]+1
       << " | " << set_final_idx[i] << " | " << set_size[i] << " | " << set_dofit[i] 
       << " | " << sqlite3_column_text(st.ptr(), 0) << " | " << sqlite3_column_text(st.ptr(), 1)
       << " |" << std::endl;
  }
  os << std::endl;

  // Methods //
  os << "+ List of methods" << std::endl;
  os << "| type | name | id | for fit? |" << std::endl;
  os << "| reference | " << refname << " | " << refid << " | n/a |" << std::endl;
  os << "| empty | " << emptyname << " | " << emptyid << " | n/a |" << std::endl;
  for (int i = 0; i < addname.size() ; i++){
    os << "| additional | " << addname[i] << " | " << addid[i] << " | " << (addisfit[i]?"yes":"no") << " |" << std::endl;
  }
  os << std::endl;

  // Properties //
  os << "+ List of properties (" << ntot << ")" << std::endl;
  os << "| fit? | id | property | propid | alias | db-set | proptype | nstruct | weight | refvalue |" << std::endl;
  st.recycle(statement::STMT_CUSTOM,R"SQL(
SELECT Properties.id, Properties.key, Properties.nstructures, Evaluations.value, Property_types.key, Properties.setid, Training_set.isfit
FROM Properties
LEFT OUTER JOIN Evaluations ON (Properties.id = Evaluations.propid AND Evaluations.methodid = :METHOD)
INNER JOIN Property_types ON (Properties.property_type = Property_types.id)
INNER JOIN Training_set ON (Properties.id = Training_set.propid)
ORDER BY Training_set.id;
)SQL");
  st.bind((char *) ":METHOD",refid);
  int n = 0;
  while (st.step() != SQLITE_DONE){
    std::string valstr;
    if (sqlite3_column_type(st.ptr(),3) == SQLITE_NULL)
      valstr = "n/a";
    else
      valstr = std::to_string(sqlite3_column_double(st.ptr(), 3));
    bool isfit = (sqlite3_column_type(st.ptr(),6) != SQLITE_NULL);
    auto it = std::find(setid.begin(),setid.end(),sqlite3_column_type(st.ptr(),5)); 
    if (it == setid.end())
      throw std::runtime_error("Could not find set id in DESCRIBE");
    int sid = it - setid.begin();
    
    os << "| " << (isfit?"yes":"no") << " | " << n+1 << " | " << sqlite3_column_text(st.ptr(), 1) 
       << " | " << sqlite3_column_int(st.ptr(), 0)
       << " | " << alias[sid] << " | " << setname[sid] << " | " << sqlite3_column_text(st.ptr(), 4) 
       << " | " << sqlite3_column_int(st.ptr(), 2)
       << " | " << w[n] << " | " << valstr << " |" << std::endl;
    n++;
  }
  os << std::endl;

  // Completion //
  os << "* Calculation completion for the current training set" << std::endl;

  // number of evaluations to be done
  st.recycle(statement::STMT_CUSTOM,R"SQL(
SELECT COUNT(DISTINCT Training_set.propid)
FROM Training_set;)SQL");
  st.step();
  int ncalc_all = sqlite3_column_int(st.ptr(), 0);
  st.reset();

  // count reference, empty, and additional values
  st.recycle(statement::STMT_CUSTOM,R"SQL(
SELECT COUNT(DISTINCT Training_set.propid)
FROM Evaluations, Training_set
WHERE Evaluations.methodid = :METHOD AND Evaluations.propid = Training_set.propid;)SQL");

  // reference
  st.bind((char *) ":METHOD",refid);
  st.step();
  int ncalc_ref = sqlite3_column_int(st.ptr(), 0);
  st.reset();

  // empty
  st.bind((char *) ":METHOD",emptyid);
  st.step();
  int ncalc_empty = sqlite3_column_int(st.ptr(), 0);
  st.reset();

  // additional
  std::vector<int> ncalc_add(addid.size(),0);
  for (int j = 0; j < addid.size(); j++){
    st.bind((char *) ":METHOD",addid[j]);
    st.step();
    ncalc_add[j] = sqlite3_column_int(st.ptr(), 0);
    st.reset();
  }
  os << "+ Reference: " << ncalc_ref << "/" << ncalc_all << (ncalc_ref==ncalc_all?" (complete)":" (missing)") << std::endl;
  os << "+ Empty: " << ncalc_empty << "/" << ncalc_all << (ncalc_empty==ncalc_all?" (complete)":" (missing)") << std::endl;
  for (int j = 0; j < addid.size(); j++)
    os << "+ Additional (" << addname[j] << "): " << ncalc_add[j] << "/" << ncalc_all << (ncalc_add[j]==ncalc_all?" (complete)":" (missing)") << std::endl;

  // terms
  st.recycle(statement::STMT_CUSTOM,R"SQL(
SELECT COUNT(DISTINCT Training_set.propid)
FROM Terms
INNER JOIN Training_set ON Training_set.propid = Terms.propid
WHERE Terms.methodid = :METHOD AND Terms.atom = :ATOM AND Terms.l = :L AND Terms.exponent = :EXP;)SQL");
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
        int ncalc = sqlite3_column_int(st.ptr(), 0);
        os << "| " << nameguess(zat[iz]) << " | " << inttol[il] << " | " 
           << exp[ie] << " | " << ncalc << "/" << ncalc_all << " |" << (ncalc==ncalc_all?" (complete)":" (missing)") << std::endl;
        ncall += ncalc;
        ntall += ncalc_all;
      }
    }
  }
  os << "+ Total terms: " << ncall << "/" << ntall << (ncall==ntall?" (complete)":" (missing)") << std::endl;
  os << std::endl;

  // clean up
  os.precision(prec);
}

// Write the din files in the training set
void trainset::write_din(const std::list<std::string> &tokens) const{
  if (!db || !(*db)) 
    throw std::runtime_error("A database file must be connected before using WRITE DIN");
  if (setid.empty())
    throw std::runtime_error("Training set subsets must be defined before using WRITE DIN");
  
  // check dir
  std::string dir = ".";
  if (!tokens.empty()) dir = tokens.front();
  if (!fs::is_directory(tokens.front()))
    throw std::runtime_error("In WRITE DIN, directory not found: " + dir);

  // define the query statements
  statement st(db->ptr(),statement::STMT_CUSTOM,R"SQL(
SELECT Properties.nstructures, Properties.structures, Properties.coefficients, Evaluations.value
FROM Properties
INNER JOIN Evaluations ON (Properties.id = Evaluations.propid)
INNER JOIN Methods ON (Evaluations.methodid = Methods.id)
INNER JOIN Training_set ON (Properties.id = Training_set.id)
WHERE Properties.setid = :SET AND Methods.id = :METHOD
ORDER BY Properties.orderid;
)SQL");
  statement stname(db->ptr(),statement::STMT_CUSTOM,R"SQL(
SELECT key FROM Structures WHERE id = ?1;
)SQL");

  for (int i = 0; i < setid.size(); i++){
    // open and write the din header
    std::string fname = dir + "/" + setname[i] + ".din";
    std::ofstream ofile(fname,std::ios::trunc);
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
    ofile << "# reference method = " << refname << std::endl;
    ofile << "# reference id = " << refid << std::endl;
  
    // step over the components of this set
    st.bind((char *) ":METHOD",refid);
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

// Insert data in bulk into the database using data files from
// previous ACP development programs using this training set as
// template
void trainset::insert_olddat(const std::string &directory, std::list<std::string> &tokens){
  if (!db || !(*db)) 
    throw std::runtime_error("A database file must be connected before using INSERT OLDDAT");
  if (!isdefined())
    throw std::runtime_error("The training set needs to be defined before using INSERT OLDDAT (use DESCRIBE to see missing data)");

  // Check directory
  std::string dir = ".";
  if (!directory.empty()) dir = directory;
  if (!fs::is_directory(dir))
    throw std::runtime_error("In INSERT OLDDAT, directory not found: " + dir);

  // Check that the names.dat matches and write down the property ids
  std::string name = dir + "/names.dat";
  if (!fs::is_regular_file(name))
    throw std::runtime_error("In INSERT OLDDAT, names.dat file found: " + name);
  std::ifstream ifile(name,std::ios::in);
  if (ifile.fail()) 
    throw std::runtime_error("In INSERT OLDDAT, error reading names.dat file: " + name);
  
  statement st(db->ptr(),statement::STMT_CUSTOM,R"SQL(
SELECT Properties.key, Properties.id
FROM Properties, Training_set
WHERE Properties.id = Training_set.propid
ORDER BY Training_set.id;
)SQL");
  while (st.step() != SQLITE_DONE){
    // check the name
    std::string namedb = (char *) sqlite3_column_text(st.ptr(), 0);
    std::string namedat;
    std::getline(ifile,namedat);
    deblank(namedat);
    if (namedb != namedat){
      std::cout << "In INSERT OLDDAT, names.dat and names for the training set do not match." << std::endl;
      std::cout << "The mismatch is:" << std::endl;
      std::cout << "  Database name: " << namedb << std::endl;
      std::cout << "  names.dat name: " << namedat << std::endl;
      throw std::runtime_error("In INSERT OLDDAT, non-matching names were found");
    }
  }
  ifile.peek();
  if (!ifile.eof())
    throw std::runtime_error("In INSERT OLDDAT, the names.dat file contains extra lines: " + name);
  ifile.close();
  
  // Start inserting data
  db->begin_transaction();

  // Insert data for reference method in ref.dat
  std::string knext = "";
  if (!tokens.empty()) knext = popstring(tokens,true);
  if (knext != "NOREFERENCE"){
    name = dir + "/ref.dat";
    ifile = std::ifstream(name,std::ios::in);
    if (ifile.fail()) 
      throw std::runtime_error("In INSERT OLDDAT, error reading ref.dat file: " + name);

    for (int i = 0; i < propid.size(); i++){
      std::unordered_map<std::string,std::string> smap;
      std::string valstr;
      std::getline(ifile,valstr);
      if (ifile.fail()) 
        throw std::runtime_error("In INSERT OLDDAT, unexpected error or end of file in ref.dat file: " + name);

      smap["METHOD"] = std::to_string(refid);
      smap["PROPERTY"] = std::to_string(propid[i]);
      smap["VALUE"] = valstr;
      db->insert("EVALUATION","",smap);
    }
    ifile.peek();
    if (!ifile.eof())
      throw std::runtime_error("In INSERT OLDDAT, the ref.dat file contains extra lines: " + name);
    ifile.close();
  }

  // Insert data for empty method in empty.dat; save the empty for insertion of ACP terms
  int n = 0;
  std::vector<double> yempty(ntot,0.0);
  name = dir + "/empty.dat";
  ifile = std::ifstream(name,std::ios::in);
  if (ifile.fail()) 
    throw std::runtime_error("In INSERT OLDDAT, error reading empty.dat file: " + name);

  for (int i = 0; i < propid.size(); i++){
    std::unordered_map<std::string,std::string> smap;
    std::string valstr;
    std::getline(ifile,valstr);
    if (ifile.fail()) 
      throw std::runtime_error("In INSERT OLDDAT, unexpected error or end of file in empty.dat file: " + name);

    smap["METHOD"] = std::to_string(emptyid);
    smap["PROPERTY"] = std::to_string(propid[i]);
    smap["VALUE"] = valstr;
    db->insert("EVALUATION","",smap);
    yempty[n++] = std::stod(valstr);
  }
  ifile.peek();
  if (!ifile.eof())
    throw std::runtime_error("In INSERT OLDDAT, the empty.dat file contains extra lines: " + name);
  ifile.close();

  // Insert data for ACP terms
  for (int iat = 0; iat < zat.size(); iat++){
    for (int il = 0; il <= lmax[iat]; il++){
      for (int iexp = 0; iexp < exp.size(); iexp++){
        std::string atom = nameguess(zat[iat]);
        lowercase(atom);
        name = dir + "/" + atom + "_" + inttol[il] + "_" + std::to_string(iexp+1) + ".dat";
        ifile = std::ifstream(name,std::ios::in);
        if (ifile.fail()) 
          throw std::runtime_error("In INSERT OLDDAT, error reading term file: " + name);

        n = 0;
        for (int i = 0; i < propid.size(); i++){
          std::unordered_map<std::string,std::string> smap;
          std::string valstr;
          std::getline(ifile,valstr);
          if (ifile.fail()) 
            throw std::runtime_error("In INSERT OLDDAT, unexpected error or end of file in term file: " + name);

          smap["METHOD"] = std::to_string(emptyid);
          smap["PROPERTY"] = std::to_string(propid[i]);
          smap["ATOM"] = std::to_string(zat[iat]);
          smap["L"] = std::to_string(il);
          smap["EXPONENT"] = to_string_precise(exp[iexp]);
          smap["VALUE"] = to_string_precise((std::stod(valstr)-yempty[n++])/0.001);
          db->insert("TERM","",smap);
        }

        ifile.peek();
        if (!ifile.eof())
          throw std::runtime_error("In INSERT OLDDAT, the term file contains extra lines: " + name);
        ifile.close();
      }
    }
  }

  // Commit the transaction
  db->commit_transaction();
}

// Insert data from a dat file into the database
void trainset::insert_dat(std::unordered_map<std::string,std::string> &kmap){
  if (!db || !(*db)) 
    throw std::runtime_error("A database file must be connected before using INSERT DAT");
  if (!isdefined())
    throw std::runtime_error("The training set needs to be defined before using INSERT DAT (use DESCRIBE to see missing data)");

  if (kmap.find("FILE") == kmap.end() || kmap["FILE"].empty())
    throw std::runtime_error("INSERT DAT requires a data file (use the FILE keyword)");
  std::string name = kmap["FILE"];

  if (kmap.find("METHOD") == kmap.end() || kmap["METHOD"].empty())
    throw std::runtime_error("INSERT DAT requires a method (use the METHOD keyword)");
  int methodid = db->find_id_from_key(kmap["METHOD"],statement::STMT_QUERY_METHOD);
  if (!methodid)
    throw std::runtime_error("Unknown method in INSERT DAT: " + kmap["METHOD"]);
    
  // Start inserting data
  db->begin_transaction();

  if (kmap.find("TERM") == kmap.end()){
    // The data file corresponds to an evaluation //
    std::ifstream ifile(name,std::ios::in);
    if (ifile.fail()) 
      throw std::runtime_error("In INSERT DAT, error reading data file: " + name);

    for (int i = 0; i < propid.size(); i++){
      std::unordered_map<std::string,std::string> smap;
      std::string valstr;
      std::getline(ifile,valstr);
      if (ifile.fail()) 
        throw std::runtime_error("In INSERT DAT, unexpected error or end of file in data file: " + name);

      smap["METHOD"] = std::to_string(methodid);
      smap["PROPERTY"] = std::to_string(propid[i]);
      smap["VALUE"] = valstr;
      db->insert("EVALUATION","",smap);
    }
    ifile.peek();
    if (!ifile.eof())
      throw std::runtime_error("In INSERT DAT, the data file contains extra lines: " + name);
    ifile.close();
  } else {
    // The data file corresponds to a term //
    std::list<std::string> tokens(list_all_words(kmap["TERM"]));

    // check the atom
    int izat_ = zatguess(popstring(tokens));
    if (izat_ == 0)
      throw std::runtime_error("In INSERT DAT, TERM keyword, unknown atom");
    int iat_ = -1;
    for (int i = 0; i < zat.size(); i++){
      if (izat_ == zat[i]){
        iat_ = i;
        break;
      }
    }
    if (iat_ < 0)
      throw std::runtime_error("In INSERT DAT, TERM keyword, atom not in training set");
    
    // check the l
    std::string l_ = popstring(tokens);
    if (!isinteger(l_))
      throw std::runtime_error("In INSERT DAT, TERM keyword, invalid angular momentum: " + l_ + " (should be an integer)");
    int il_ = std::stoi(l_);
    if (il_ < 0 || il_ >= lmax[iat_])
      throw std::runtime_error("In INSERT DAT, TERM keyword, lmax " + l_ + " not in range for given atom");

    // check the exponent
    std::string exp_ = popstring(tokens);
    if (exp_.empty())
      throw std::runtime_error("In INSERT DAT, TERM keyword, exponent not found");
    double xexp;
    try { 
      xexp = std::stod(exp_);
    } catch (const std::exception &e) {
      throw std::runtime_error("In INSERT DAT, TERM keyword, exponent is not a number");
    }
    int iexp_ = -1;
    for (int i = 0; i < exp.size(); i++){
      if (std::abs(xexp - exp[i]) < 1e-20){
        iexp_ = i;
        break;
      }
    }
    if (iexp_ < 0)
      throw std::runtime_error("In INSERT DAT, TERM keyword, exponent not in training set");

    std::ifstream ifile(name,std::ios::in);
    if (ifile.fail()) 
      throw std::runtime_error("In INSERT DAT, error reading data file: " + name);

    for (int i = 0; i < propid.size(); i++){
      std::unordered_map<std::string,std::string> smap;
      std::string valstr;
      std::getline(ifile,valstr);
      if (ifile.fail()) 
        throw std::runtime_error("In INSERT DAT, unexpected error or end of file in data file: " + name);

      smap["METHOD"] = std::to_string(methodid);
      smap["PROPERTY"] = std::to_string(propid[i]);
      smap["ATOM"] = std::to_string(izat_);
      smap["L"] = l_;
      smap["EXPONENT"] = to_string_precise(exp[iexp_]);
      smap["VALUE"] = valstr;
      db->insert("TERM","",smap);
    }
    ifile.peek();
    if (!ifile.eof())
      throw std::runtime_error("In INSERT DAT, the term file contains extra lines: " + name);
    ifile.close();
  }

  // Commit the transaction
  db->commit_transaction();
}

// Evaluate an ACP on the current training set
void trainset::eval_acp(std::ostream &os, const acp &a) const{
  if (!db || !(*db)) 
    throw std::runtime_error("A database file must be connected before using ACPEVAL");
  if (!isdefined())
    throw std::runtime_error("The training set needs to be defined before using INSERT OLDDAT (use DESCRIBE to see what is missing)");

  // initialize container vectors
  std::vector<double> yempty(ntot,0.0), yacp(ntot,0.0), yadd(ntot,0.0), ytotal(ntot,0.0), yref(ntot,0.0);
  std::vector<std::string> names(ntot,"");

  // get the names
  statement st(db->ptr(),statement::STMT_CUSTOM,R"SQL(
SELECT Properties.key
FROM Properties, Training_set
WHERE Properties.id = Training_set.propid
ORDER BY Training_set.id;
)SQL");
  int n = 0;
  while (st.step() != SQLITE_DONE)
    names[n++] = std::string((char *) sqlite3_column_text(st.ptr(),0));
  if (n != ntot)
    throw std::runtime_error("In ACPEVAL, unexpected end of the database column in names");

  // get the empty, reference, additional methods
  st.recycle(statement::STMT_CUSTOM,R"SQL(
SELECT Evaluations.value
FROM Evaluations, Training_set
WHERE Evaluations.methodid = :METHOD AND Evaluations.propid = Training_set.propid
ORDER BY Training_set.id;
)SQL");

  n = 0;
  st.bind((char *) ":METHOD",emptyid);
  while (st.step() != SQLITE_DONE)
    yempty[n++] = sqlite3_column_double(st.ptr(),0);
  if (n != ntot)
    throw std::runtime_error("In ACPEVAL, unexpected end of the database column in empty");

  n = 0;
  st.bind((char *) ":METHOD",refid);
  while (st.step() != SQLITE_DONE)
    yref[n++] = sqlite3_column_double(st.ptr(),0);
  if (n != ntot)
    throw std::runtime_error("In ACPEVAL, unexpected end of the database column in reference");

  for (int j = 0; j < addid.size(); j++){
    n = 0;
    st.bind((char *) ":METHOD",addid[j]);
    while (st.step() != SQLITE_DONE)
      yadd[n++] += sqlite3_column_double(st.ptr(),0);
    if (n != ntot)
      throw std::runtime_error("In ACPEVAL, unexpected end of the database column in additional method " + addname[j]);
  }

  // get the ACP contribution
  st.recycle(statement::STMT_CUSTOM,R"SQL(
SELECT Terms.value
FROM Terms, Training_set
WHERE Terms.methodid = :METHOD AND Terms.atom = :ATOM AND Terms.l = :L AND Terms.exponent = :EXP AND Terms.propid = Training_set.propid
ORDER BY Training_set.id;
)SQL");
  for (int i = 0; i < a.size(); i++){
    acp::term t = a.get_term(i);
    st.reset();
    st.bind((char *) ":METHOD",emptyid);
    st.bind((char *) ":ATOM",(int) t.atom);
    st.bind((char *) ":L",(int) t.l);
    st.bind((char *) ":EXP",t.exp);

    n = 0;
    while (st.step() != SQLITE_DONE)
      yacp[n++] += sqlite3_column_double(st.ptr(),0) * t.coef;
    if (n != ntot)
      throw std::runtime_error("In ACPEVAL, unexpected end of the database column in ACP term number " + std::to_string(i));
  }

  // calculate statistics
  int nset = setid.size();
  std::vector<double> rms(nset,0.0), mae(nset,0.0), mse(nset,0.0);
  double wrms = 0.0, wrmsall = 0.0, rmst = 0.0, maet = 0.0, mset = 0.0;
  int maxsetl = 0;
  n = 0;
  for (int i = 0; i < setid.size(); i++){
    for (int j = set_initial_idx[i]; j < set_final_idx[i]; j++){
      double xdiff = yempty[n] + yacp[n] + yadd[n] - yref[n];
      mae[i] += std::abs(xdiff);
      mse[i] += xdiff;
      rms[i] += xdiff * xdiff;
      maet += std::abs(xdiff);
      mset += xdiff;
      rmst += xdiff * xdiff;
      wrmsall += w[n] * xdiff * xdiff;
      if (set_dofit[i])
        wrms += w[n] * xdiff * xdiff;
      n++;
    }
    double nsetsize = set_size[i];
    mae[i] /= nsetsize;
    mse[i] /= nsetsize;
    rms[i] = std::sqrt(rms[i]/nsetsize);
    maxsetl = std::max(maxsetl,(int) alias[i].size());
  }
  maet /= ntot;
  mset /= ntot;
  rmst = std::sqrt(rmst/ntot);
  wrms = std::sqrt(wrms);
  wrmsall = std::sqrt(wrmsall);
  if (n != ntot)
    throw std::runtime_error("In ACPEVAL, inconsistent ntot, set_initial_idx, and set_final_idx");

  std::streamsize prec = os.precision(7);
  os << std::fixed;
  os << "# Evaluation: " << (a?a.get_name():emptyname) << std::endl;
  os << "# Statistics: " << std::endl;
  if (a){
    os << "#   2-norm  =  " << a.norm2() << std::endl;
    os << "#   1-norm  =  " << a.norm1() << std::endl;
    os << "#   maxcoef =  " << a.norminf() << std::endl;
  }

  os.precision(8);
  os << "#   wrms    =  " << wrms << std::endl;
  os << "#   wrmsall =  " << wrmsall << " (including evaluation subsets)" << std::endl;

  for (int i = 0; i < setid.size(); i++){
    os << "# " << std::right << std::setw(maxsetl) << alias[i]
       << std::left << "  rms = " << std::right << std::setw(14) << rms[i]
       << std::left << "  mae = " << std::right << std::setw(14) << mae[i]
       << std::left << "  mse = " << std::right << std::setw(14) << mse[i]
       << std::endl;
  }
  os << "# " << std::right << std::setw(maxsetl) << "all"
     << std::left << "  rms = " << std::right << std::setw(14) << rmst
     << std::left << "  mae = " << std::right << std::setw(14) << maet
     << std::left << "  mse = " << std::right << std::setw(14) << mset
     << std::endl;

  os << "Id           Name                                wei          yempty           yacp             yadd             ytotal           yref             diff" << std::endl;
  int idwidth = digits(ntot);
  for (int i = 0; i < ntot; i++){
    os << std::setw(idwidth) << std::left << i << " "
       << std::setw(40) << std::left << names[i] << " "
       << std::setprecision(6) << std::setw(10) << std::right << w[i] << " "
       << std::setprecision(10) << std::setw(16) << std::right << yempty[i] << " "
       << std::setprecision(10) << std::setw(16) << std::right << yacp[i] << " "
       << std::setprecision(10) << std::setw(16) << std::right << yadd[i] << " "
       << std::setprecision(10) << std::setw(16) << std::right << yempty[i]+yacp[i]+yadd[i] << " "
       << std::setprecision(10) << std::setw(16) << std::right << yref[i] << " "
       << std::setprecision(10) << std::setw(16) << std::right << yempty[i]+yacp[i]+yadd[i]-yref[i]
       << std::endl;
  }
  os.precision(prec);
}

// Save the current training set to the database
void trainset::savedb(std::string &name) const{
#ifdef CEREAL_FOUND
  if (!db || !(*db)) 
    throw std::runtime_error("A database file must be connected before using TRAINING");
  if (!isdefined())
    throw std::runtime_error("The training set needs to be defined before using TRAINING SAVE");
  if (name.empty())
    throw std::runtime_error("TRAINING SAVE requires a name for the saved training set");

  // serialize
  std::stringstream ss;
  cereal::BinaryOutputArchive oarchive(ss);
  oarchive(*this);
  int nsize = ss.str().size();

  // insert into the database
  statement st(db->ptr(),statement::STMT_CUSTOM,R"SQL(
INSERT INTO Training_set_repo (key,size,training_set) VALUES(:KEY,:SIZE,:TRAINING_SET);
)SQL");
 st.bind((char *) ":KEY",name);
 st.bind((char *) ":SIZE",nsize);
 st.bind((char *) ":TRAINING_SET",(void *) ss.str().data(),true,nsize);
 if (st.step() != SQLITE_DONE)
   throw std::runtime_error("Failed inserting training set in the database (TRAINING SAVE)");
#else
  throw std::runtime_error("Cannot use TRAINING SAVE: not compiled with cereal support");
#endif
}

// Load the training set from the database
void trainset::loaddb(std::string &name){
#ifdef CEREAL_FOUND
  if (!db || !(*db)) 
    throw std::runtime_error("A database file must be connected before using TRAINING");
  if (name.empty())
    throw std::runtime_error("TRAINING LOAD requires a name for the loaded training set");

  // fetch from the database
  statement st(db->ptr(),statement::STMT_CUSTOM,R"SQL(
SELECT size, training_set
FROM Training_set_repo
WHERE key = ?1;
)SQL");
  st.bind(1,name);
  if (st.step() != SQLITE_ROW)
    throw std::runtime_error("Failed retrieving training set from the database (TRAINING LOAD)");

  // de-searialize
  std::stringstream ss;
  int nsize = sqlite3_column_int(st.ptr(),0);
  const char *ptr = (const char *) sqlite3_column_blob(st.ptr(),1);
  ss.write(ptr,nsize);
  cereal::BinaryInputArchive iarchive(ss);
  iarchive(*this);

  // reset the Training_set table
  setdb(db);
  for (int i = 0; i < setid.size(); i++)
    insert_subset_db(i);

#else
  throw std::runtime_error("Cannot use TRAINING SAVE: not compiled with cereal support");
#endif
}

// Delete a training set from the database (or all the t.s.)
void trainset::deletedb(std::string &name) const{
  if (!db || !(*db)) 
    throw std::runtime_error("A database file must be connected before using TRAINING");

  statement st(db->ptr());
  if (name.empty()){
    st.recycle(statement::STMT_CUSTOM,R"SQL(
DELETE FROM Training_set_repo;
)SQL");
  } else {
    st.recycle(statement::STMT_CUSTOM,R"SQL(
DELETE FROM Training_set_repo WHERE key = ?1;
)SQL");
    st.bind(1,name);
  }
 if (st.step() != SQLITE_DONE)
   throw std::runtime_error("Failed deleting training set in the database (TRAINING DELETE)");
}

// List training sets from the database
void trainset::listdb(std::ostream &os) const {
  if (!db || !(*db)) 
    throw std::runtime_error("A database file must be connected before using TRAINING");
  
  statement st(db->ptr(),statement::STMT_CUSTOM,R"SQL(
SELECT key,size,training_set
FROM Training_set_repo;
)SQL");
  os << "| Name |" << std::endl;
  while (st.step() != SQLITE_DONE){
    std::string key = (char *) sqlite3_column_text(st.ptr(), 0);
    os << "| " << key << " |" << std::endl;
  }
}

// Write the octavedump.dat file
void trainset::dump() const {
  std::ofstream ofile("octavedump.dat",std::ios::trunc | std::ios::binary);

  // permutation for the additional methods (first fit, then nofit)
  uint64_t nyfit = 0;
  std::vector<int> iaddperm;
  for (int i = 0; i < addid.size(); i++){
    if (addisfit[i]){
      iaddperm.push_back(i);
      nyfit++;
    }
  }
  for (int i = 0; i < addid.size(); i++)
    if (!addisfit[i]) iaddperm.push_back(i);

  // calculate the number of rows and the weights with only the dofit sets
  std::vector<double> wtrain;
  unsigned long int nrows = 0;
  int n = 0;
  for (int i = 0; i < setid.size(); i++){
    for (int j = set_initial_idx[i]; j < set_final_idx[i]; j++){
      if (set_dofit[i]){
        wtrain.push_back(w[n]);
        nrows++;
      }
      n++;
    }
  }

  // write the dimension integers first
  uint64_t ncols = 0, addmaxl = 0;
  for (int i = 0; i < zat.size(); i++)
    ncols += exp.size() * (lmax[i]+1);
  for (int i = 0; i < addname.size(); i++)
    addmaxl = std::max(addmaxl,(uint64_t) addname[i].size());
  uint64_t sizes[7] = {zat.size(), exp.size(), nrows, ncols, addid.size(), nyfit, addmaxl};
  ofile.write((const char *) &sizes,7*sizeof(uint64_t));

  // write the atomic names
  std::string atoms = "";
  for (int iat = 0; iat < zat.size(); iat++){
    std::string str = nameguess(zat[iat]);
    if (str.size() == 1) str = str + " ";
    atoms += str;
  }
  const char *atoms_c = atoms.c_str();
  ofile.write(atoms_c,zat.size()*2*sizeof(char));
  
  // write the additional method names
  for (int i = 0; i < addname.size(); i++){
    std::string name = addname[iaddperm[i]] + std::string(addmaxl-addname[iaddperm[i]].size(),' ');
    const char *name_c = name.c_str();
    ofile.write(name_c,addmaxl*sizeof(char));
  }

  // write the lmax vector
  const unsigned char *lmax_c = lmax.data();
  ofile.write((const char *) lmax_c,lmax.size()*sizeof(unsigned char));

  // write the exponent vector
  const double *exp_c = exp.data();
  ofile.write((const char *) exp_c,exp.size()*sizeof(double));

  // write the w vector
  const double *w_c = wtrain.data();
  ofile.write((const char *) w_c,wtrain.size()*sizeof(double));

  // write the x matrix
  statement st(db->ptr(),statement::STMT_CUSTOM,R"SQL(
SELECT Terms.value
FROM Terms, Training_set
WHERE Terms.methodid = :METHOD AND Terms.atom = :ATOM AND Terms.l = :L AND Terms.exponent = :EXP 
      AND Terms.propid = Training_set.propid AND Training_set.isfit IS NOT NULL
ORDER BY Training_set.id;
)SQL");
  for (int iz = 0; iz < zat.size(); iz++){
    for (int il = 0; il <= lmax[iz]; il++){
      for (int ie = 0; ie < exp.size(); ie++){
        st.reset();
        st.bind((char *) ":METHOD",emptyid);
        st.bind((char *) ":ATOM",(int) zat[iz]);
        st.bind((char *) ":L",il);
        st.bind((char *) ":EXP",exp[ie]);
        int n = 0;
        while (st.step() != SQLITE_DONE){
          if (n++ >= nrows)
            throw std::runtime_error("Too many rows dumping terms data");
          double value = sqlite3_column_double(st.ptr(),0);
          ofile.write((const char *) &value,sizeof(double));
        }
        if (n != nrows)
          throw std::runtime_error("Too few rows dumping terms data");
      }
    }
  }

  // write the yref, yempt, and yadd columns
  st.recycle(statement::STMT_CUSTOM,R"SQL(
SELECT Evaluations.value
FROM Evaluations, Training_set
WHERE Evaluations.methodid = :METHOD 
      AND Evaluations.propid = Training_set.propid AND Training_set.isfit IS NOT NULL
ORDER BY Training_set.id;
)SQL");
  std::vector<int> ids = {refid,emptyid};
  for (int i = 0; i < addid.size(); i++)
    ids.push_back(addid[iaddperm[i]]);
  for (int i = 0; i < ids.size(); i++){
    st.bind((char *) ":METHOD", ids[i]);
    int n = 0;
    while (st.step() != SQLITE_DONE){
      if (n++ >= nrows)
        throw std::runtime_error("Too many rows dumping y data");
      double value = sqlite3_column_double(st.ptr(),0);
      ofile.write((const char *) &value,sizeof(double));
    }
    if (n != nrows)
      throw std::runtime_error("Too few rows dumping y data");
  }

  ofile.close();
}

// Write input files for the training set structures
void trainset::write_structures(std::unordered_map<std::string,std::string> &kmap, const acp &a){
  if (!db || !(*db)) 
    throw std::runtime_error("A database file must be connected before using WRITE");
  if (!isdefined())
    throw std::runtime_error("The training set needs to be defined before using WRITE");

  // unpack the gaussian keyword into a map for this method, if a method was given
  bool havemethod = (kmap.find("METHOD") != kmap.end());
  std::unordered_map<std::string,std::string> gmap = {};
  if (havemethod) gmap = db->get_gaussian_map(kmap["METHOD"]);

  // directory and pack number
  std::string dir = fetch_directory(kmap);
  int npack = 0;
  if (kmap.find("PACK") != kmap.end()) npack = std::stoi(kmap["PACK"]);

  // set
  int idini, idfin;
  if (kmap.find("SET") != kmap.end()){
    std::string setname = kmap["SET"];
    auto it = std::find(alias.begin(),alias.end(),setname);
    if (it == alias.end())
      throw std::runtime_error("Unknown SET in write_structures (no alias found)");
    int sid = it - alias.begin();
    idini = set_initial_idx[sid];
    idfin = set_final_idx[sid]-1;
  } else {
    idini = 0;
    idfin = ntot-1;
  }

  // collect the structure indices for the training set
  std::unordered_map<int,std::string> smap;
  if (havemethod){
      statement st(db->ptr(),statement::STMT_CUSTOM,R"SQL(
SELECT DISTINCT Property_types.key, Properties.nstructures, Properties.structures
FROM Properties, Property_types, Training_set
WHERE Properties.property_type = Property_types.id AND Properties.id = Training_set.propid
      AND Training_set.id BETWEEN ?1 AND ?2;
)SQL");
      st.bind(1,idini);
      st.bind(2,idfin);
      while (st.step() != SQLITE_DONE){
        int n = sqlite3_column_int(st.ptr(),1);
        const int *str = (int *)sqlite3_column_blob(st.ptr(), 2);
        for (int i = 0; i < n; i++)
          smap[str[i]] = (char *) sqlite3_column_text(st.ptr(), 0);
      }
  } else {
    statement st(db->ptr(),statement::STMT_CUSTOM,R"SQL(
SELECT DISTINCT Properties.nstructures, Properties.structures
FROM Properties, Training_set
WHERE Properties.id = Training_set.propid AND Training_set.id BETWEEN ?1 AND ?2;)SQL");
    st.bind(1,idini);
    st.bind(2,idfin);
    while (st.step() != SQLITE_DONE){
      int n = sqlite3_column_int(st.ptr(),0);
      const int *str = (int *)sqlite3_column_blob(st.ptr(),1);
      for (int i = 0; i < n; i++)
        smap[str[i]] = "xyz";
    }
  }

  // write the inputs
  db->write_many_structures(smap,gmap,dir,npack,a);
}

// Read data for the training set structures
void trainset::read_structures(std::ostream &os, const std::string &file, std::unordered_map<std::string,std::string> &kmap, const acp &a){
  if (!db || !(*db)) 
    throw std::runtime_error("A database file must be connected before using READ");
  if (!isdefined())
    throw std::runtime_error("The training set needs to be defined before using READ");

  // set
  bool haveset = (kmap.find("SET") != kmap.end());
  int idini, idfin;
  if (haveset){
    std::string setname = kmap["SET"];
    auto it = std::find(alias.begin(),alias.end(),setname);
    if (it == alias.end())
      throw std::runtime_error("Unknown SET in write_structures (no alias found)");
    int sid = it - alias.begin();
    idini = set_initial_idx[sid];
    idfin = set_final_idx[sid]-1;
  } else {
    idini = 0;
    idfin = ntot-1;
  }

  // get the data from the file
  auto datmap = read_data_file(file,globals::ha_to_kcal);

  if (kmap.find("COMPARE") != kmap.end()){
    // verify that we have a reference method
    std::string refm = kmap["COMPARE"];
    if (refm.empty())
      throw std::runtime_error("The COMPARE keyword in READ must be followed by a known method");
    int methodid = db->find_id_from_key(refm,statement::STMT_QUERY_METHOD);
    if (!methodid)
      throw std::runtime_error("Unknown method in READ/COMPARE: " + refm);
    
    // fetch the reference method values from the DB and populate vectors
    std::vector<std::string> names_found;
    std::vector<std::string> names_missing_fromdb;
    std::vector<std::string> names_missing_fromdat;
    std::vector<double> refvalues, datvalues, ws;
    std::vector<int> ids;
    statement stkey(db->ptr(),statement::STMT_CUSTOM,"SELECT key FROM Structures WHERE id = ?1;");
    statement st(db->ptr(),statement::STMT_CUSTOM,R"SQL(
SELECT Training_set.id, Properties.key, Evaluations.value, Properties.nstructures, Properties.structures, Properties.coefficients
FROM Training_set
INNER JOIN Properties ON (Training_set.propid = Properties.id)
LEFT OUTER JOIN  Evaluations ON (Evaluations.propid = Training_set.propid AND Evaluations.methodid = :METHOD)
WHERE Training_set.id BETWEEN :INI AND :END
ORDER BY Training_set.id;
)SQL");
    st.bind((char *) ":METHOD",methodid);
    st.bind((char *) ":INI",idini);
    st.bind((char *) ":END",idfin);
    while (st.step() != SQLITE_DONE){
      // check if the evaluation is available in the database
      int id = sqlite3_column_int(st.ptr(),0);
      std::string key = (char *) sqlite3_column_text(st.ptr(),1);
      if (sqlite3_column_type(st.ptr(),2) == SQLITE_NULL){
        names_missing_fromdb.push_back(key);
        continue;
      } 

      // check if the components are in the data file
      int nstr = sqlite3_column_int(st.ptr(),3);
      int *istr = (int *) sqlite3_column_blob(st.ptr(),4);
      double *coef = (double *) sqlite3_column_blob(st.ptr(),5);
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
        ids.push_back(id);
        refvalues.push_back(sqlite3_column_double(st.ptr(),2));
        datvalues.push_back(value);
        ws.push_back(w[id]);
      }
    }
    datmap.clear();

    // output the header and the statistics
    os << "# Evaluation: " << file << std::endl
       << "# Reference: " << refm << std::endl
       << "# Statistics: " << (names_missing_fromdat.empty() && names_missing_fromdat.empty()?"":"(partial data)")
       << std::endl;
    std::streamsize prec = os.precision(7);
    os << std::fixed;
    os.precision(8);
    if (ids.empty())
      os << "#   (not enough data for statistics)" << std::endl;
    else if (haveset){
      // calculate the statistics for the given set
      double wrms, rms, mae, mse;
      calc_stats(ids,datvalues,refvalues,ws,wrms,rms,mae,mse);

      os << "# " << std::left << std::setw(10) << "all"
         << std::left << "  rms = " << std::right << std::setw(12) << rms
         << std::left << "  mae = " << std::right << std::setw(12) << mae
         << std::left << "  mse = " << std::right << std::setw(12) << mse
         << std::left << " wrms = " << std::right << std::setw(12) << wrms
         << std::endl;
    } else {
      // calculate the statistics for all sets
      double wrms, rms, mae, mse;
      for (int i = 0; i < setid.size(); i++){
        int n = calc_stats(ids,datvalues,refvalues,ws,wrms,rms,mae,mse,-1,-1,set_initial_idx[i],set_final_idx[i]);
        if (n == 0){
          os << "# " << std::left << std::setw(10) << alias[i] << "   (no data)" << std::endl;
        } else{
          os << "# " << std::left << std::setw(10) << alias[i]
             << std::left << "  rms = " << std::right << std::setw(12) << rms
             << std::left << "  mae = " << std::right << std::setw(12) << mae
             << std::left << "  mse = " << std::right << std::setw(12) << mse
             << std::left << " wrms = " << std::right << std::setw(12) << wrms;
          if (n < set_size[i])
            os << "   (partial: " << n << " of " << set_size[i] << ")";
          os << std::endl;
        }
      }
    }
    os.precision(prec);

    // output the results
    output_eval(os,ids,names_found,ws,datvalues,file,refvalues,refm);
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
}

// Insert a subset into the Training_set table
void trainset::insert_subset_db(int sid){
  db->begin_transaction();
  statement st(db->ptr(),statement::STMT_CUSTOM,"INSERT INTO Training_set (id,propid,isfit) VALUES (:ID,:PROPID,:ISFIT);");
  for (int i = set_initial_idx[sid]; i < set_final_idx[sid]; i++){
    st.reset();
    st.bind((char *) ":ID", i);
    st.bind((char *) ":PROPID", propid[i]);
    if (set_dofit[sid])
      st.bind((char *) ":ISFIT", 1);
    st.step();
  }
  db->commit_transaction();
}

