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
#include <unordered_map>

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

// Register the database and create the Training_set table
void trainset::setdb(sqldb *db_){
  db = db_;

  // Drop the Training_set table if it exists, then create it
  statement st(db->ptr(),statement::STMT_CUSTOM,R"SQL(
DROP TABLE IF EXISTS Training_set;
CREATE TABLE Training_set (
  id INTEGER PRIMARY KEY,
  propid INTEGER NOT NULL,
  FOREIGN KEY(propid) REFERENCES Properties(id) ON DELETE CASCADE
);
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

  //// add the set ////

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
  st.reset();
  if (size == 0)
    throw std::runtime_error("SET does not have any associated properties: " + name);

  // write down the initial index, final index, and initialize the size
  int ilast = 0;
  if (!set_final_idx.empty()) ilast = set_final_idx.back();
  set_initial_idx.push_back(ilast);
  set_final_idx.push_back(ilast + size);

  // initialize the weights to one and the mask to false
  for (int i = 0; i < size; i++)
    w.push_back(1.);
  set_mask.push_back(std::vector(size,true));

  // is this set used in the fit? (nofit keyword)
  set_dofit.push_back(kmap.find("NOFIT") == kmap.end());

  //// mask ////
  if (kmap.find("MASK") != kmap.end()){
    // set all elements in the mask to zero
    for (int i = 0; i < set_mask[sid].size(); i++)
      set_mask[sid][i] = false;

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
        set_mask[sid][i] = true;

    } else if (keyw == "ITEMS") {
      if (tokens.empty())
        throw std::runtime_error("Empty item list in SUBSET/MASK");
      while (!tokens.empty()){
        int item = std::stoi(popstring(tokens)) - 1;
        if (item < 0 || item >= size)
          throw std::runtime_error("Item " + std::to_string(item+1) + " out of range in MASK");
        set_mask[sid][item] = true;
      }

    } else if (keyw == "PATTERN") {
      if (tokens.empty())
        throw std::runtime_error("Empty pattern in SUBSET/MASK");
      std::vector<bool> pattern(tokens.size(),false);
      int n = 0;
      while (!tokens.empty())
        pattern[n++] = (popstring(tokens) != "0");
      for (int i = 0; i < set_mask[sid].size(); i++)
        set_mask[sid][i] = pattern[i % pattern.size()];

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
        set_mask[sid][n++] = !found;
      }
    } else {
      throw std::runtime_error("Unknown category " + keyw + " in MASK");
    }
  }

  // calculate the size of the set, update the total size, and populate the Training_set table
  db->begin_transaction();
  st.recycle(statement::STMT_CUSTOM,R"SQL(
SELECT id FROM Properties WHERE setid = ?1 ORDER BY orderid;
)SQL");
  st.bind(1,setid[sid]);
  statement stinsert(db->ptr(),statement::STMT_CUSTOM,"INSERT INTO Training_set (id,propid) VALUES (:ID,:PROPID);");
  set_size.push_back(0);
  for (int i = 0; i < set_mask[sid].size(); i++){
    st.step();
    int propid = sqlite3_column_int(st.ptr(),0);
    if (set_mask[sid][i]){
      set_size[sid]++;
      ntot++;
      stinsert.bind((char *) ":ID", ntot);
      stinsert.bind((char *) ":PROPID", propid);
      stinsert.step();
    }
  }
  db->commit_transaction();

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
  if (kmap.find("WEIGHT_ITEM") != kmap.end()){
    std::list<std::string> wlist = list_all_words(kmap["WEIGHT_ITEM"]);

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
  setweight_onlyone(sid, wglobal, wpattern, norm_ref, norm_nitem, norm_nitemsqrt, witem);
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

// Set the weights for one set from the indicated parameters.
void trainset::setweight_onlyone(int sid, double wglobal, std::vector<double> wpattern, bool norm_ref, bool norm_nitem, bool norm_nitemsqrt, std::vector<std::pair<int,double> > witem){
  
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
    statement st(db->ptr(),statement::STMT_CUSTOM,text);

    st.bind((char *) ":METHOD",refid);
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
void trainset::describe(std::ostream &os){
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
       << " | " << set_initial_idx[i] 
       << " | " << set_final_idx[i] << " | " << set_size[i] << " | " << set_dofit[i] 
       << " | " << sqlite3_column_text(st.ptr(), 0) << " | " << sqlite3_column_text(st.ptr(), 1)
       << " |" << std::endl;
  }
  os << std::endl;

  // Methods //
  os << "+ List of methods" << std::endl;
  os << "| type | name | id | for fit? |" << std::endl;
  os << "| reference | " << refname << " | " << refid << " | |" << std::endl;
  os << "| empty | " << emptyname << " | " << emptyid << " | |" << std::endl;
  for (int i = 0; i < addname.size() ; i++){
    os << "| additional | " << addname[i] << " | " << addid[i] << " | " << addisfit[i] << " |" << std::endl;
  }
  os << std::endl;

  // Properties //
  int nall = std::accumulate(set_size.begin(),set_size.end(),0);
  os << "+ List of properties (" << nall << ")" << std::endl;
  os << "| fit? | id | property | propid | alias | db-set | proptype | nstruct | weight | refvalue |" << std::endl;
  st.recycle(statement::STMT_CUSTOM,R"SQL(
SELECT Properties.id, Properties.key, Properties.nstructures, Evaluations.value, Property_types.key
FROM Properties
LEFT OUTER JOIN Evaluations ON (Properties.id = Evaluations.propid AND Evaluations.methodid = :METHOD)
INNER JOIN Property_types ON (Properties.property_type = Property_types.id)
WHERE Properties.setid = :SET
ORDER BY Properties.orderid;
)SQL");

  int n = 0, nin = 0;
  for (int i = 0; i < setid.size(); i++){
    st.reset();
    st.bind((char *) ":SET",setid[i]);
    st.bind((char *) ":METHOD",refid);

    int rc, k = 0;
    std::string valstr;
    while ((rc = st.step()) != SQLITE_DONE){
      if (set_mask[i][k++]){
        if (sqlite3_column_type(st.ptr(),3) == SQLITE_NULL)
          valstr = "n/a";
        else
          valstr = std::to_string(sqlite3_column_double(st.ptr(), 3));
        os << "| " << (set_dofit[i]?"yes":"no") << " | " << ++nin << " | " << sqlite3_column_text(st.ptr(), 1) 
           << " | " << sqlite3_column_int(st.ptr(), 0)
           << " | " << alias[i] << " | " << setname[i] << " | " << sqlite3_column_text(st.ptr(), 4) 
           << " | " << sqlite3_column_int(st.ptr(), 2)
           << " | " << w[n] << " | " << valstr << " |" << std::endl;
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
    text = text + std::to_string(refid) + " AND " + set_constraint(i) + ";";
    st.recycle(statement::STMT_CUSTOM,text);
    st.step();
    ncalc += sqlite3_column_int(st.ptr(), 0);
  }
  os << "+ Reference: " << ncalc << "/" << nall << (ncalc==nall?" (complete)":" (missing)") << std::endl;
 
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
  os << "+ Empty: " << ncalc << "/" << nall << (ncalc==nall?" (complete)":" (missing)") << std::endl;

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
    os << "+ Additional (" << addname[j] << "): " << ncalc << "/" << nall << (ncalc==nall?" (complete)":" (missing)") << std::endl;
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
           << exp[ie] << " | " << ncalc << "/" << nall << " |" << (ncalc==nall?" (complete)":" (missing)") << std::endl;
        ncall += ncalc;
        ntall += nall;
      }
    }
  }
  os << "+ Total terms: " << ncall << "/" << ntall << (ncalc==nall?" (complete)":" (missing)") << std::endl;
  os << std::endl;

  // clean up
  os.precision(prec);
}

void trainset::write_xyz(const std::list<std::string> &tokens){
  if (!db || !(*db)) 
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
  statement st(db->ptr(),statement::STMT_CUSTOM,sttext);

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

void trainset::write_din(const std::list<std::string> &tokens){
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
WHERE Properties.setid = :SET AND Methods.id = :METHOD
ORDER BY Properties.orderid;
)SQL");
  statement stname(db->ptr(),statement::STMT_CUSTOM,R"SQL(
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
    throw std::runtime_error("The training set needs to be defined before using INSERT OLDDAT (use DESCRIBE to see what is missing)");

  std::string dir = ".";
  if (!directory.empty()) dir = directory;
  if (!fs::is_directory(dir))
    throw std::runtime_error("In INSERT OLDDAT, directory not found: " + dir);

  // Check that the names.dat matches. Build the list of property IDs.
  std::list<int> propid;
  std::string name = dir + "/names.dat";
  if (!fs::is_regular_file(name))
    throw std::runtime_error("In INSERT OLDDAT, names.dat file found: " + name);
  std::ifstream ifile(name,std::ios::in);
  if (ifile.fail()) 
    throw std::runtime_error("In INSERT OLDDAT, error reading names.dat file: " + name);
  
  statement st(db->ptr(),statement::STMT_CUSTOM,"SELECT Properties.key, Properties.id FROM Properties WHERE Properties.setid = :SET ORDER BY Properties.orderid;");
  for (int i = 0; i < setid.size(); i++){
    if (!set_dofit[i]) continue;
    st.bind((char *) ":SET",setid[i]);

    int n = 0;
    while (st.step() != SQLITE_DONE){
      if (set_mask[i][n++]){
        // check the name
        std::string name = (char *) sqlite3_column_text(st.ptr(), 0);
        std::string namedat;
        std::getline(ifile,namedat);
        deblank(namedat);
        if (name != namedat){
          std::cout << "In INSERT OLDDAT, names.dat and names for the training set do not match." << std::endl;
          std::cout << "The mismatch is:" << std::endl;
          std::cout << "  Set: " << i << " (db-name=" << setname[i] << ", alias=" << alias[i] << ")" << std::endl;
          std::cout << "  Item: " << n << std::endl;
          std::cout << "  Database name: " << name << std::endl;
          std::cout << "  names.dat name: " << namedat << std::endl;
          return;
        }

        // write down the property id
        propid.push_back(sqlite3_column_int(st.ptr(), 1));
      }
    }
  }
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

    for (auto it = propid.begin(); it != propid.end(); ++it){
      std::unordered_map<std::string,std::string> smap;
      std::string valstr;
      std::getline(ifile,valstr);
      if (ifile.fail()) 
        throw std::runtime_error("In INSERT OLDDAT, unexpected error or end of file in ref.dat file: " + name);

      smap["METHOD"] = std::to_string(refid);
      smap["PROPERTY"] = std::to_string(*it);
      smap["VALUE"] = valstr;
      db->insert("EVALUATION","",smap);
    }
    ifile.peek();
    if (!ifile.eof())
      throw std::runtime_error("In INSERT OLDDAT, the ref.dat file contains extra lines: " + name);
    ifile.close();
  }

  // Insert data for empty method in empty.dat
  name = dir + "/empty.dat";
  ifile = std::ifstream(name,std::ios::in);
  if (ifile.fail()) 
    throw std::runtime_error("In INSERT OLDDAT, error reading empty.dat file: " + name);

  for (auto it = propid.begin(); it != propid.end(); ++it){
    std::unordered_map<std::string,std::string> smap;
    std::string valstr;
    std::getline(ifile,valstr);
    if (ifile.fail()) 
      throw std::runtime_error("In INSERT OLDDAT, unexpected error or end of file in empty.dat file: " + name);

    smap["METHOD"] = std::to_string(emptyid);
    smap["PROPERTY"] = std::to_string(*it);
    smap["VALUE"] = valstr;
    db->insert("EVALUATION","",smap);
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

        for (auto it = propid.begin(); it != propid.end(); ++it){
          std::unordered_map<std::string,std::string> smap;
          std::string valstr;
          std::getline(ifile,valstr);
          if (ifile.fail()) 
            throw std::runtime_error("In INSERT OLDDAT, unexpected error or end of file in term file: " + name);

          smap["METHOD"] = std::to_string(emptyid);
          smap["PROPERTY"] = std::to_string(*it);
          smap["ATOM"] = std::to_string(zat[iat]);
          smap["L"] = std::to_string(il);
          smap["EXPONENT"] = to_string_precise(exp[iexp]);
          smap["VALUE"] = valstr;
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

// Evaluate an ACP on the current training set
void trainset::eval_acp(std::ostream &os, const acp &a){
  if (!db || !(*db)) 
    throw std::runtime_error("A database file must be connected before using ACPEVAL");

  std::vector< std::vector<double> > xempty, xref, xacp;
  std::vector< std::vector< std::vector<double> > > xadd;

  // Reserve space for the xadd
  xadd.reserve(addid.size());

  // prepare text for the terms
  std::string text = R"SQL(
SELECT Terms.propid, Terms.value
FROM Terms
WHERE Terms.methodid = :METHOD AND Terms.atom = :ATOM AND Terms.l = :L AND Terms.exponent = :EXP;
)SQL";
  statement st(db->ptr(),statement::STMT_CUSTOM,text);

  // the ACP terms
  for (int i = 0; i < a.size(); i++){
    acp::term t = a.get_term(i);
    st.reset();
    st.bind((char *) ":METHOD",emptyid);
    st.bind((char *) ":ATOM",(int) t.atom);
    st.bind((char *) ":L",(int) t.l);
    st.bind((char *) ":EXP",t.exp);
    
    std::cout << i << std::endl;
    while (st.step() != SQLITE_DONE){ }
  }

  
  // Fetch the values
//  for (int i = 0; i < setid.size(); i++){
//    // Empty, reference, additional
//    xempty.push_back(fetch_evaluation(db,emptyid,i));
//    xref.push_back(fetch_evaluation(db,emptyid,i));
//    for (int j = 0; j < addid.size(); j++)
//      xadd[j].push_back(fetch_evaluation(db,addid[j],i));
//
//    // ACP terms
//    for (int j = 0; j < a.size(); j++){
//      acp::term t = a.get_term(j);
//      std::cout << i << " " << j << std::endl;
//      fetch_term(db,emptyid,i,t.atom,t.l,t.exp);
//    }
//  }

  // std::vector<double> res = fetch_evaluation(db,1,1);
  // for (int i = 0 ; i < res.size(); i++)
  //   std::cout << i << " " << res[i] << std::endl;

// # Evaluation: final
// # Statistics: 
// #   2-norm  =    4.067088    
// #   1-norm  =    25.497848   
// #   maxcoef = 1.896049    
// #   wrms    =    72.28675158   
// #   s22        rms = 0.68878398     mae = 0.48504389     mse =   0.33183702  
// #   s66        rms = 0.55143257     mae = 0.41573574     mse =   0.27510574  
// #   s22x5      rms = 0.82793492     mae = 0.47061037     mse =   0.36455189  
// #   s66x8      rms = 0.59165961     mae = 0.37744554     mse =   0.26455244  
// #   BBI        rms = 0.89413580     mae = 0.85973223     mse =   0.85973223  
// #   SSI        rms = 0.35267871     mae = 0.22527576     mse =   0.17220130  
// #   dipep-conf rms = 1.48249924     mae = 1.18912068     mse =  -0.49992785  
// #   P26        rms = 1.40004061     mae = 1.10780464     mse =   0.04409143  
// #   ACHC       rms = 0.33171013     mae = 0.26852197     mse =  -0.13091791  
// #   defmol     rms = 1.93459865     mae = 1.11164817     mse =   0.24438685  
// #   blind15    rms = 0.90166798     mae = 0.57205963     mse =   0.01135845  
// #   chelic_c   rms = 4.77660334     mae = 4.76944690     mse =   4.76944690  
// #   chelic_r   rms = 0.26442527     mae = 0.20847002     mse =  -0.01466698  
// #   nhelic_c   rms = 4.90709263     mae = 4.89819692     mse =   4.89819692  
// #   nhelic_r   rms = 0.30783609     mae = 0.25527633     mse =   0.07665135  
// #   eep        rms = 0.34806911     mae = 0.30504250     mse =   0.01842110  
// #   eer        rms = 0.47919342     mae = 0.38216028     mse =  -0.29351462  
// #   wallachp   rms = 0.27071501     mae = 0.21858201     mse =  -0.00391333  
// #   wallachr   rms = 0.71405049     mae = 0.53525235     mse =   0.18881722  
// #   x23c       rms = 2.46825193     mae = 1.88508121     mse =  -0.90673550  
// #   x23p       rms = 0.95197033     mae = 0.66263153     mse =  -0.26939351  
// #   x23s       rms = 0.90989413     mae = 0.55755261     mse =  -0.03984996  
// #   all        rms = 1.51224007     mae = 0.84132163     mse =   0.12332794  
// Id                     Name                      wei             yempty                yscf                ytotal                yref                 diff        
// 1       db/s225_2pyridoxine2aminopyridin09     2.35740978     -14.4694653900        -5.6667046837       -14.5890484837       -15.1300000000         0.5409515163  
// 2       db/s225_2pyridoxine2aminopyridin10     0.47148196     -15.1898155200        -9.1293643227       -16.2316922027       -16.7000000000         0.4683077973  


//   // count reference, empty, and additional values
//   int ncalc = 0;
//   for (int i = 0; i < setid.size(); i++){
//     st.recycle(statement::STMT_CUSTOM,text);
//     st.step();
//     ncalc += sqlite3_column_int(st.ptr(), 0);
//   }
//   os << "+ Reference: " << ncalc << "/" << nall << (ncalc==nall?" (complete)":" (missing)") << std::endl;

  printf("hello!\n");
}

