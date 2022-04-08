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
#include "acp.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <unordered_map>
#include <cmath>
#include <filesystem>
#include <map>
#include <cstring>
#include <list>
#include <tuple>

namespace fs = std::filesystem;

// Register the database and create the Training_set table
void trainset::setdb(sqldb *db_){
  db = db_;

  // Drop the Training_set table if it exists, then create it
  if (db){
    statement st(db->ptr(),R"SQL(
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
}

// Add atoms and max. angular momentum.
void trainset::addatoms(const std::list<std::string> &tokens){

  auto it = tokens.begin();
  while (it != tokens.end()){
    std::string at = *(it++);
    std::string l = *(it++);

    unsigned char zat_ = zatguess(at);
    if (zat_ == 0)
      throw std::runtime_error("Invalid atom " + at + " in TRAINING ATOM");

    if (globals::ltoint.find(l) == globals::ltoint.end())
      throw std::runtime_error("Invalid lmax " + l + " in TRAINING ATOM");
    unsigned char lmax_ = globals::ltoint.at(l);
    zat.push_back(zat_);
    lmax.push_back(lmax_);
  }
  complete = c_unknown;
}

// Add exponents.
void trainset::addexp(const std::list<std::string> &tokens){
  for (auto it = tokens.begin(); it != tokens.end(); it++){
    double e_ = std::stod(*it);
    if (e_ <= 0.)
      throw std::runtime_error("Invalid exponent " + *it + " in TRAINING EXPONENT");

    exp.push_back(e_);
  }
  complete = c_unknown;
}

// Add a subset (combination of set, mask, weights)
void trainset::addsubset(const std::string &key, std::unordered_map<std::string,std::string> &kmap){
  if (!db || !(*db))
    throw std::runtime_error("A database file must be connected before in TRAINING SUBSET");
  if (kmap.find("SET") == kmap.end() || kmap["SET"].empty())
    throw std::runtime_error("The keyword SET is required in TRAINING SUBSET");

  // identify the set; add the name and the set index
  std::string name = kmap["SET"];
  int idx = db->find_id_from_key(name,"Sets");
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
  statement st(db->ptr(),"SELECT COUNT(id) FROM Properties WHERE setid = ?1;");
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

  //// mask and/mask or ////
  std::vector<bool> set_mask(size,true);
  bool imask_and = true;
  if (kmap.find("MASK_AND") != kmap.end())
    imask_and = true;
  if (kmap.find("MASK_OR") != kmap.end()){
    imask_and = false;
    std::fill(set_mask.begin(), set_mask.end(), false);
  }

  //// mask ////
  if (kmap.find("MASK_ATOMS") != kmap.end()){
    if (zat.empty())
      throw std::runtime_error("ATOMS in TRAINING/SUBSET/MASK_ATOMS is not possible if no atoms have been defined");

    // build the array of structures that contain only the atoms in the zat array
    std::unordered_map<int,bool> usest;
    statement st(db->ptr(),"SELECT id,nat,zatoms FROM Structures WHERE setid = " + std::to_string(setid[sid]) + ";");
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
    st.recycle("SELECT nstructures,structures FROM Properties WHERE setid = " +
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
      if (imask_and)
	set_mask[n] = set_mask[n] & !found;
      else
	set_mask[n] = set_mask[n] | !found;
      n++;
    }
  }
  if (kmap.find("MASK_PATTERN") != kmap.end()){
    std::list<std::string> tokens(list_all_words(kmap["MASK_PATTERN"]));
    if (tokens.empty())
      throw std::runtime_error("Empty pattern in TRAINING/SUBSET/MASK_PATTERN");
    std::vector<bool> pattern(tokens.size(),false);
    int n = 0;
    while (!tokens.empty())
      pattern[n++] = (popstring(tokens) != "0");
    for (int i = 0; i < set_mask.size(); i++){
      if (imask_and)
	set_mask[i] = set_mask[i] & pattern[i % pattern.size()];
      else
	set_mask[i] = set_mask[i] | pattern[i % pattern.size()];
    }
  }
  if (kmap.find("MASK_ITEMS") != kmap.end() || kmap.find("MASK_NOTITEMS") != kmap.end() ){
    bool pos = kmap.find("MASK_ITEMS") != kmap.end();
    std::list<std::string> tokens;
    if (pos)
      tokens = list_all_words(kmap["MASK_ITEMS"]);
    else
      tokens = list_all_words(kmap["MASK_NOTITEMS"]);
    if (tokens.empty())
      throw std::runtime_error("Empty item list in TRAINING/SUBSET/MASK_ITEMS");
    std::vector<bool> set_mask_local(size,pos?false:true);
    while (!tokens.empty()){
      int item = std::stoi(popstring(tokens)) - 1;
      if (item < 0 || item >= size)
	throw std::runtime_error("Item " + std::to_string(item+1) + " out of range in TRAINING/SUBSET/MASK_ITEMS");
      set_mask_local[item] = pos?true:false;
    }
    for (int i = 0; i < set_mask.size(); i++){
      if (imask_and)
	set_mask[i] = set_mask[i] & set_mask_local[i];
      else
	set_mask[i] = set_mask[i] | set_mask_local[i];
    }
  }
  if (kmap.find("MASK_RANGE") != kmap.end()){
    int istart = 0, iend = size, istep = 1;
    std::list<std::string> tokens(list_all_words(kmap["MASK_RANGE"]));

    // read the start, end, and step
    if (tokens.empty())
      throw std::runtime_error("Empty range in TRAINING/SUBSET/MASK_RANGE");
    else if (tokens.size() == 1)
      istep = std::stoi(tokens.front());
    else if (tokens.size() == 2){
      istart = std::stoi(popstring(tokens)) - 1;
      istep = std::stoi(tokens.front());
    } else if (tokens.size() == 3){
      istart = std::stoi(popstring(tokens)) - 1;
      istep = std::stoi(popstring(tokens));
      iend = std::stoi(tokens.front());
    }
    if (istart < 0 || istart >= size || iend < 1 || iend > size || istep < 0)
      throw std::runtime_error("Invalid range in TRAINING/SUBSET/MASK_RANGE");

    // reassign the mask and the size for this set
    std::vector<bool> set_mask_local(size,false);
    for (int i = istart; i < iend; i+=istep)
      set_mask_local[i] = true;
    for (int i = 0; i < set_mask.size(); i++){
      if (imask_and)
	set_mask[i] = set_mask[i] & set_mask_local[i];
      else
	set_mask[i] = set_mask[i] | set_mask_local[i];
    }
  }

  // build the propid, size, and final index of the set
  set_size.push_back(0);
  set_final_idx.push_back(ilast);
  st.recycle("SELECT id FROM Properties WHERE setid = ?1 ORDER BY orderid;");
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
  bool norm_refsqrt = (kmap.find("NORM_REFSQRT") != kmap.end());
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
  if (norm_nitem && set_size[sid] > 0)
    norm *= set_size[sid];
  if (norm_nitemsqrt && set_size[sid] > 0)
    norm *= std::sqrt(set_size[sid]);
  if ((norm_ref || norm_refsqrt) && set_size[sid] > 0){
    statement st(db->ptr(),R"SQL(
SELECT length(value), value FROM Training_set, Properties
LEFT OUTER JOIN Evaluations ON (Properties.id = Evaluations.propid AND Evaluations.methodid = :METHOD)
WHERE Properties.setid = :SETID AND Training_set.propid = Properties.id AND Training_set.isfit IS NOT NULL;
)SQL");
    st.bind((char *) ":SETID",setid[sid]);
    st.bind((char *) ":METHOD",refid);

    int ndat = 0;
    double dsum = 0.;
    while (st.step() != SQLITE_DONE){
      int nblob = sqlite3_column_int(st.ptr(),0) / sizeof(double);
      double *res = (double *) sqlite3_column_blob(st.ptr(),1);
      if (nblob == 0 || !res)
        throw std::runtime_error("Cannot use NORM_REF without having all reference method evaluations in TRAINING SUBSET");
      ndat += nblob;
      for (int i = 0; i < nblob; i++)
        dsum += std::abs(res[i]);
    }
    dsum = dsum / ndat;

    if (norm_ref && std::abs(dsum) > 1e-40)
      norm *= dsum;
    else if (norm_ref && std::abs(dsum) > 1e-40)
      norm *= std::sqrt(dsum);
    else
      throw std::runtime_error("Cannot use NORM_REF if the reference data averages to zero in TRAINING SUBSET");
  }

  // apply the normalization factor
  for (int i = set_initial_idx[sid]; i < set_final_idx[sid]; i++)
    w[i] /= norm;

  // set individual items
  for (int i = 0; i < witem.size(); i++){
    int id = set_initial_idx[sid]+witem[i].first-1;
    if (id < set_initial_idx[sid] || id >= set_final_idx[sid])
      throw std::runtime_error("Item weight out of bounds in TRAINING SUBSET");
    w[id] = witem[i].second;
  }
  complete = c_unknown;
}

// Set the reference method
void trainset::setreference(const std::list<std::string> &tokens){
  if (!db || !(*db))
    throw std::runtime_error("A database file must be connected before using TRAINING REFERENCE");
  if (tokens.empty())
    throw std::runtime_error("Need method key in TRAINING REFEENCE");

  // check if the method is known
  auto it = tokens.begin();
  refname = *it;
  refid = db->find_id_from_key(refname,"Methods");
  if (refid == 0)
    throw std::runtime_error("METHOD identifier not found in database (" + refname + ") in TRAINING REFRENCE");
  complete = c_unknown;
}

// Set the empty method
void trainset::setempty(const std::list<std::string> &tokens){
  if (!db || !(*db))
    throw std::runtime_error("A database file must be connected before using TRAINING EMPTY");
  if (tokens.empty())
    throw std::runtime_error("Need method key in TRAINING EMPTYx");

  std::string name = tokens.front();
  int idx = db->find_id_from_key(name,"Methods");
  if (idx == 0)
    throw std::runtime_error("METHOD identifier not found in database (" + name + ") in TRAINING EMPTY");

  emptyname = name;
  emptyid = idx;
  complete = c_unknown;
}

// Add an additional method
void trainset::addadditional(const std::list<std::string> &tokens){
  if (!db || !(*db))
    throw std::runtime_error("A database file must be connected before using TRAINING ADD");
  if (tokens.empty())
    throw std::runtime_error("Need method key in TRAINING ADD command");

  auto it = tokens.begin();

  std::string name = *it;
  int idx = db->find_id_from_key(name,"Methods");
  if (idx == 0)
    throw std::runtime_error("METHOD identifier not found in database (" + name + ") in TRAINING ADD");

  addname.push_back(name);
  addid.push_back(idx);
  addisfit.push_back(++it != tokens.end() && equali_strings(*it,"FIT"));
  complete = c_unknown;
}

// Describe the current training set
void trainset::describe(std::ostream &os, bool except_on_undefined, bool full, bool quiet) {
  if (!db || !(*db))
    throw std::runtime_error("A database file must be connected before using DESCRIBE");

  os << "## Description of the training set" << std::endl;
  if (!isdefined()){
    complete = c_no;
    os << "# The TRAINING SET is NOT DEFINED" << std::endl;
    if (zat.empty()) os << "--- No atoms found (ATOM) ---" << std::endl;
    if (lmax.empty()) os << "--- No angular momenta found (LMAX) ---" << std::endl;
    if (exp.empty()) os << "--- No exponents found (EXP) ---" << std::endl;
    if (setid.empty()) os << "--- No subsets found (SUBSET) ---" << std::endl;
    if (w.empty()) os << "--- No weights found (W) ---" << std::endl;
    if (emptyname.empty()) os << "--- No empty method found (EMPTY) ---" << std::endl;
    if (refname.empty()) os << "--- No reference method found (REFERENCE) ---" << std::endl;
    os << std::endl;
    if (except_on_undefined)
      throw std::runtime_error("The training set must be defined completely before using DESCRIBE");
    else
      return;
  }

  std::streamsize prec = os.precision(10);

  // Atoms and lmax //
  if (!quiet){
    os << "# List of atoms and maximum angular momentum channels (" << zat.size() << ")" << std::endl;
    os << "| Atom | lmax |" << std::endl;
    for (int i = 0; i < zat.size(); i++){
      os << "| " << nameguess(zat[i]) << " | " << globals::inttol[lmax[i]] << " |" << std::endl;
    }
    os << std::endl;
  }

  // Exponents //
  if (~quiet){
    os << "# List of exponents (" << exp.size() << ")" << std::endl;
    os << "| id | exp |" << std::endl;
    for (int i = 0; i < exp.size(); i++){
      os << "| " << i << " | " << exp[i] << " |" << std::endl;
    }
    os << std::endl;
  }

  // Sets //
  statement st(db->ptr(),"SELECT litrefs, description FROM Sets WHERE id = ?1;");
  if (!quiet){
    os << "# List of subsets (" << setname.size() << ")" << std::endl;
    os << "| id | alias | db-name | db-id | initial | final | size | dofit? | litref | description |" << std::endl;
    for (int i = 0; i < setname.size(); i++){
      st.reset();
      st.bind(1,setid[i]);
      st.step();
      const char *litref = (const char *) sqlite3_column_text(st.ptr(), 0);
      const char *description = (const char *) sqlite3_column_text(st.ptr(), 1);

      os << "| " << i << " | " << alias[i] << " | " << setname[i] << " | " << setid[i]
	 << " | " << set_initial_idx[i]+1
	 << " | " << set_final_idx[i] << " | " << set_size[i] << " | " << set_dofit[i]
	 << " | " << (litref?litref:"") << " | "
	 << (description?description:"") << " |" << std::endl;
    }
    os << std::endl;
  }

  // Methods //
  if (!quiet){
    os << "# List of methods" << std::endl;
    os << "| type | name | id | for fit? |" << std::endl;
    os << "| reference | " << refname << " | " << refid << " | n/a |" << std::endl;
    os << "| empty | " << emptyname << " | " << emptyid << " | n/a |" << std::endl;
    for (int i = 0; i < addname.size() ; i++){
      os << "| additional | " << addname[i] << " | " << addid[i] << " | " << (addisfit[i]?"yes":"no") << " |" << std::endl;
    }
    os << std::endl;
  }

  // the long lists
  if (full){
    // Properties //
    if (!quiet){
      os << "# List of properties (" << ntot << ")" << std::endl;
      os << "| fit? | id | property | propid | alias | db-set | proptype | nstruct | weight | refvalue |" << std::endl;
    }
    st.recycle(R"SQL(
SELECT Properties.id, Properties.key, Properties.nstructures, length(Evaluations.value), Evaluations.value, Property_types.key, Properties.setid, Training_set.isfit
FROM Properties
LEFT OUTER JOIN Evaluations ON (Properties.id = Evaluations.propid AND Evaluations.methodid = :METHOD)
INNER JOIN Property_types ON (Properties.property_type = Property_types.id)
INNER JOIN Training_set ON (Properties.id = Training_set.propid)
ORDER BY Training_set.id;
)SQL");
    st.bind((char *) ":METHOD",refid);
    int n = 0;
    while (st.step() != SQLITE_DONE){
      int nval = sqlite3_column_int(st.ptr(),3) / sizeof(double);
      double *val = (double *) sqlite3_column_blob(st.ptr(),4);

      std::string valstr;
      if (nval == 0)
        valstr = "n/a";
      else if (nval == 1)
        valstr = std::to_string(val[0]);
      else
        valstr = "<" + std::to_string(nval) + ">";

      bool isfit = (sqlite3_column_type(st.ptr(),7) != SQLITE_NULL);
      auto it = std::find(setid.begin(),setid.end(),sqlite3_column_int(st.ptr(),6));
      if (it == setid.end())
        throw std::runtime_error("Could not find set id in DESCRIBE");
      int sid = it - setid.begin();

      if (!quiet){
	os << "| " << (isfit?"yes":"no") << " | " << n+1 << " | " << sqlite3_column_text(st.ptr(), 1)
	   << " | " << sqlite3_column_int(st.ptr(), 0)
	   << " | " << alias[sid] << " | " << setname[sid] << " | " << sqlite3_column_text(st.ptr(), 5)
	   << " | " << sqlite3_column_int(st.ptr(), 2)
	   << " | " << w[n] << " | " << valstr << " |" << std::endl;
      }
      n++;
    }
    if (!quiet)
      os << std::endl;

    // Completion //
    if (!quiet)
      os << "# Calculation completion for the current training set" << std::endl;

    // number of evaluations to be done
    st.recycle("SELECT COUNT(DISTINCT Training_set.propid) FROM Training_set;");
    st.step();
    int ncalc_all = sqlite3_column_int(st.ptr(), 0);
    st.reset();

    // count reference, empty, and additional values
    st.recycle(R"SQL(
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
    if (!quiet){
      os << "# Reference: " << ncalc_ref << "/" << ncalc_all << (ncalc_ref==ncalc_all?" (complete)":" (missing)") << std::endl;
      os << "# Empty: " << ncalc_empty << "/" << ncalc_all << (ncalc_empty==ncalc_all?" (complete)":" (missing)") << std::endl;
      for (int j = 0; j < addid.size(); j++)
	os << "# Additional (" << addname[j] << "): " << ncalc_add[j] << "/" << ncalc_all << (ncalc_add[j]==ncalc_all?" (complete)":" (missing)") << std::endl;
    }

    // terms
    st.recycle(R"SQL(
SELECT COUNT(DISTINCT Training_set.propid)
FROM Terms
INNER JOIN Training_set ON Training_set.propid = Terms.propid
WHERE Terms.methodid = :METHOD AND Terms.atom = :ATOM AND Terms.l = :L AND Terms.exponent = :EXP;)SQL");
    int ncall = 0, ntall = 0;
    if (!quiet)
      os << "# Terms: " << std::endl;
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
	  if (!quiet){
	    os << "| " << nameguess(zat[iz]) << " | " << globals::inttol[il] << " | "
	       << exp[ie] << " | " << ncalc << "/" << ncalc_all << " |" << (ncalc==ncalc_all?" (complete)":" (missing)") << std::endl;
	  }
          ncall += ncalc;
          ntall += ncalc_all;
        }
      }
    }
    if (!quiet)
      os << "# Total terms: " << ncall << "/" << ntall << (ncall==ntall?" (complete)":" (missing)") << std::endl;

    // update the complete flag
    bool iscomplete = (ncall == ntall && ncalc_ref == ncalc_all && ncalc_empty == ncalc_all);
    for (int j = 0; j < addid.size(); j++)
      iscomplete = iscomplete && (ncalc_add[j]==ncalc_all);

    if (iscomplete){
      os << "# The training set is COMPLETE." << std::endl;
      complete = c_yes;
    } else {
      os << "# The training set is NOT COMPLETE." << std::endl;
      complete = c_no;
    }
  }
  os << std::endl;

  // clean up
  os.precision(prec);
}

// Write the din files in the training set
void trainset::write_din(const std::string &directory/*=""*/) const{
  if (!db || !(*db))
    throw std::runtime_error("A database file must be connected before using TRAINING WRITEDIN");
  if (setid.empty())
    throw std::runtime_error("Training set subsets must be defined before using TRAINING WRITEDIN");

  std::string dir = directory;
  if (dir.empty()) dir = ".";

  // define the query statements
  statement st(db->ptr(),R"SQL(
SELECT Properties.nstructures, Properties.structures, Properties.coefficients, Evaluations.value
FROM Properties, Evaluations, Methods, Training_set
WHERE Properties.id = Evaluations.propid AND Evaluations.methodid = Methods.id AND Properties.id = Training_set.id AND
      Properties.setid = :SET AND Methods.id = :METHOD AND Properties.property_type = 1 AND Evaluations.value IS NOT NULL
ORDER BY Properties.orderid;
)SQL");
  statement stname(db->ptr(),"SELECT key FROM Structures WHERE id = ?1;");

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
      double value = ((double *) sqlite3_column_blob(st.ptr(),3))[0];

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
void trainset::insert_olddat(std::ostream &os, const std::string &directory/*="./"*/){
  if (!db || !(*db))
    throw std::runtime_error("A database file must be connected before using TRAINING INSERT_OLD");
  if (!isdefined())
    throw std::runtime_error("The training set needs to be defined before using TRAINING INSERT_OLD");

  os << "* TRAINING: insert data in the old format " << std::endl << std::endl;

  // Check directory
  std::string dir = ".";
  if (!directory.empty()) dir = directory;
  if (!fs::is_directory(dir))
    throw std::runtime_error("In TRAINING INSERT_OLD, directory not found: " + dir);

  // Check that the names.dat matches and write down the property ids
  std::string name = dir + "/names.dat";
  if (!fs::is_regular_file(name))
    throw std::runtime_error("In TRAINING INSERT_OLD, names.dat file found: " + name);
  std::ifstream ifile(name,std::ios::in);
  if (ifile.fail())
    throw std::runtime_error("In TRAINING INSERT_OLD, error reading names.dat file: " + name);

  // build the mapping
  statement st(db->ptr(),R"SQL(
SELECT Properties.id
FROM Properties, Training_set
WHERE Properties.id = Training_set.propid AND Properties.key = ?1;
)SQL");
  std::string namedat;
  std::vector<int> pid;
  while (std::getline(ifile,namedat)){
    deblank(namedat);
    st.reset();
    st.bind(1,namedat);
    if (st.step() != SQLITE_ROW)
      throw std::runtime_error("In TRAINING INSERT_OLD, property name not found in training set: " + namedat);
    pid.push_back(sqlite3_column_int(st.ptr(),0));
  }
  ifile.close();

  // Start inserting data
  db->begin_transaction();

  // Insert data for reference method in ref.dat
  statement st1(db->ptr(),"INSERT OR IGNORE INTO Evaluations (methodid,propid,value) VALUES(:METHODID,:PROPID,:VALUE)");
  std::string knext = "";
  name = dir + "/ref.dat";
  ifile = std::ifstream(name,std::ios::in);
  if (ifile.fail())
    throw std::runtime_error("In TRAINING INSERT_OLD, error reading ref.dat file: " + name);
  for (int i = 0; i < pid.size(); i++){
    double value;
    ifile >> value;
    if (ifile.fail())
      throw std::runtime_error("In TRAINING INSERT_OLD, error reading ref.dat file: " + name);
    st1.bind((char *) ":METHODID",refid);
    st1.bind((char *) ":PROPID",pid[i]);
    st1.bind((char *) ":VALUE",(void *) &value,false,sizeof(double));
    st1.step();
  }

  // Insert data for empty method in empty.dat; save the empty for insertion of ACP terms
  int n = 0;
  std::vector<double> yempty(ntot,0.0);
  name = dir + "/empty.dat";
  ifile = std::ifstream(name,std::ios::in);
  if (ifile.fail())
    throw std::runtime_error("In TRAINING INSERT_OLD, error reading empty.dat file: " + name);
  for (int i = 0; i < pid.size(); i++){
    double value;
    ifile >> value;
    if (ifile.fail())
      throw std::runtime_error("In TRAINING INSERT_OLD, error reading empty.dat file: " + name);
    yempty[i] = value;
    st1.bind((char *) ":METHODID",emptyid);
    st1.bind((char *) ":PROPID",pid[i]);
    st1.bind((char *) ":VALUE",(void *) &value,false,sizeof(double));
    st1.step();
  }
  ifile.close();

  // Insert data for ACP terms
  st1.recycle("INSERT INTO Terms (methodid,propid,atom,l,exponent,value) VALUES(:METHODID,:PROPID,:ATOM,:L,:EXPONENT,:VALUE)");

  for (int iat = 0; iat < zat.size(); iat++){
    for (int il = 0; il <= lmax[iat]; il++){
      for (int iexp = 0; iexp < exp.size(); iexp++){
        std::string atom = nameguess(zat[iat]);
        lowercase(atom);
        name = dir + "/" + atom + "_" + globals::inttol[il] + "_" + std::to_string(iexp+1) + ".dat";
        ifile = std::ifstream(name,std::ios::in);
        if (ifile.fail())
          throw std::runtime_error("In TRAINING INSERT_OLD, error reading term file: " + name);

        for (int i = 0; i < pid.size(); i++){
          double value;
          ifile >> value;
          if (ifile.fail())
            throw std::runtime_error("In TRAINING INSERT_OLD, error reading term file: " + name);
          value = (value-yempty[i])/0.001;

          st1.bind((char *) ":METHODID",emptyid);
          st1.bind((char *) ":PROPID",pid[i]);
          st1.bind((char *) ":ATOM",(int) zat[iat]);
          st1.bind((char *) ":L",il);
          st1.bind((char *) ":EXPONENT",exp[iexp]);
          st1.bind((char *) ":VALUE",(void *) &value,false,sizeof(double));
          st1.step();
        }
        ifile.close();
      }
    }
  }

  // Insert maxcoef, if present
  st1.recycle("UPDATE Terms SET maxcoef = :MAXCOEF WHERE methodid = :METHODID AND atom = :ATOM AND l = :L AND exponent = :EXPONENT");
  name = dir + "/maxcoef.dat";
  if (ifile = std::ifstream(name,std::ios::in)){
    std::string line;
    while (std::getline(ifile,line)){
      std::string atom, l, idum, exp, value;
      std::istringstream iss(line);
      iss >> atom >> l >> idum >> exp >> value;
      if (iss.fail())
        continue;

      st1.bind((char *) ":METHODID",emptyid);
      st1.bind((char *) ":ATOM",(int) zatguess(atom));
      st1.bind((char *) ":L",globals::ltoint.at(l));
      st1.bind((char *) ":EXPONENT",exp);
      st1.bind((char *) ":MAXCOEF",value);
      st1.step();
    }
    ifile.close();
  }

  // Commit the transaction
  db->commit_transaction();
  os << std::endl;
  complete = c_unknown;
}

// Write training set data to data files in old-style format
void trainset::write_olddat(std::ostream &os, const std::string &directory/*="./"*/){
  if (!db || !(*db))
    throw std::runtime_error("A database file must be connected before using TRAINING WRITE_OLD");
  if (!isdefined())
    throw std::runtime_error("The training set needs to be defined before using TRAINING WRITE_OLD");

  os << "* TRAINING: write data in the old (acpfit) format " << std::endl << std::endl;

  if (complete == c_unknown)
    describe(os,false,true,true);
  if (complete == c_no)
    throw std::runtime_error("The training set needs to be complete before using TRAINING WRITE_OLD");

  // Check directory
  std::string dir = ".";
  if (!directory.empty()) dir = directory;
  if (!fs::is_directory(dir))
    throw std::runtime_error("In TRAINING WRITE_OLD, directory not found: " + dir);

  // write the names.dat and ref.dat
  unsigned long int nrows = 0;
  std::string fname = dir + "/names.dat";
  std::ofstream ofile(fname,std::ios::trunc);
  if (ofile.fail())
    throw std::runtime_error("Error writing file: " + fname);
  fname = dir + "/ref.dat";
  std::ofstream ofile2(fname,std::ios::trunc);
  if (ofile2.fail())
    throw std::runtime_error("Error writing file: " + fname);
  ofile2 << std::scientific;
  ofile2.precision(15);
  statement st(db->ptr(),R"SQL(
SELECT Properties.key, Evaluations.value
FROM Properties, Training_set, Evaluations
WHERE Properties.id = Training_set.propid AND Properties.id = Evaluations.propid AND Evaluations.methodid = ?1 AND Properties.property_type = 1
ORDER BY Training_set.id;
)SQL");
  st.bind(1,refid);
  while (st.step() != SQLITE_DONE){
    const unsigned char *key = sqlite3_column_text(st.ptr(), 0);
    ofile << key << std::endl;

    double *val = (double *) sqlite3_column_blob(st.ptr(),1);
    ofile2 << val[0] << std::endl;
    nrows++;
  }
  ofile.close();
  ofile2.close();

  // the empty.dat
  ofile.clear();
  fname = dir + "/empty.dat";
  ofile.open(fname);
  if (ofile.fail())
    throw std::runtime_error("Error writing file: " + fname);
  ofile << std::scientific;
  ofile.precision(15);
  st.reset();
  st.bind(1,emptyid);
  while (st.step() != SQLITE_DONE){
    double *val = (double *) sqlite3_column_blob(st.ptr(),1);
    ofile << val[0] << std::endl;
  }
  ofile.close();

  // the w.dat file
  ofile.clear();
  fname = dir + "/w.dat";
  ofile.open(fname);
  if (ofile.fail())
    throw std::runtime_error("Error writing file: " + fname);
  ofile << std::scientific;
  ofile.precision(15);
  unsigned long int n = 0;
  for (int i = 0; i < setid.size(); i++){
    for (int j = set_initial_idx[i]; j < set_final_idx[i]; j++){
      if (set_dofit[i])
	ofile << w[n] << std::endl;
      n++;
    }
  }
  ofile.close();

  // the empty.dat file
  ofile.clear();
  fname = dir + "/empty.dat";
  ofile.open(fname);
  if (ofile.fail())
    throw std::runtime_error("Error writing file: " + fname);
  ofile << std::scientific;
  ofile.precision(15);
  st.reset();
  st.bind(1,emptyid);
  while (st.step() != SQLITE_DONE){
    double *val = (double *) sqlite3_column_blob(st.ptr(),1);
    ofile << val[0] << std::endl;
  }
  ofile.close();


  // the x_y_z.dat files
  st.recycle(R"SQL(
SELECT Terms.value, Evaluations.value
FROM Terms, Training_set, Properties, Evaluations
WHERE Terms.methodid = :METHOD AND Terms.atom = :ATOM AND Terms.l = :L AND Terms.exponent = :EXP AND Terms.propid = Training_set.propid
      AND Training_set.propid = Properties.id AND Properties.property_type = 1
      AND Properties.id = Evaluations.propid AND Evaluations.methodid = Terms.methodid
ORDER BY Training_set.id;
)SQL");
  for (int iz = 0; iz < zat.size(); iz++){
    for (int il = 0; il <= lmax[iz]; il++){
      for (int ie = 0; ie < exp.size(); ie++){
	std::string atom = nameguess(zat[iz]);
	lowercase(atom);
	fname = dir + "/" + atom + "_" + globals::inttol[il] + "_" + std::to_string(ie+1) + ".dat";
	ofile.clear();
	ofile.open(fname);
	if (ofile.fail())
	  throw std::runtime_error("Error writing file: " + fname);
	ofile << std::scientific;
	ofile.precision(15);
	st.reset();
	st.bind((char *) ":METHOD",emptyid);
	st.bind((char *) ":ATOM",(int) zat[iz]);
	st.bind((char *) ":L",il);
	st.bind((char *) ":EXP",exp[ie]);
	int n = 0;
	while (st.step() != SQLITE_DONE){
	  if (n++ >= nrows)
	    throw std::runtime_error("Too many rows writing terms data");
	  double *val = (double *) sqlite3_column_blob(st.ptr(),0);
	  double *empty = (double *) sqlite3_column_blob(st.ptr(),1);
	  double e0 = empty[0] + 0.001 * val[0];
	  ofile << e0 << std::endl;
	}
	if (n != nrows)
	  throw std::runtime_error("Too few rows writing terms data. Is the training data complete?");
	ofile.close();
      }
    }
  }
}

// Evaluate an ACP on the current training set.
void trainset::eval_acp(std::ostream &os, const acp &a) {
  if (!db || !(*db))
    throw std::runtime_error("A database file must be connected before using TRAINING EVAL");
  if (!isdefined())
    throw std::runtime_error("The training set needs to be defined before using TRAINING EVAL");

  if (complete == c_unknown)
    describe(os,false,true,true);
  if (complete == c_no)
    throw std::runtime_error("The training set needs to be complete before using TRAINING EVAL");

  // get the number of items
  int nall = 0;
  std::vector<int> num, nsetid;
  statement st(db->ptr(),R"SQL(
SELECT length(Evaluations.value), Properties.setid
FROM Evaluations, Training_set, Properties
WHERE Evaluations.methodid = :METHOD AND Evaluations.propid = Training_set.propid AND
      Evaluations.propid = Properties.id
ORDER BY Training_set.id;
)SQL");
  st.bind((char *) ":METHOD",refid);
  while (st.step() != SQLITE_DONE){
    int nitem = sqlite3_column_int(st.ptr(),0) / sizeof(double);
    int sid = sqlite3_column_int(st.ptr(),1);
    num.push_back(nitem);
    nsetid.push_back(sid);
    nall += nitem;
  }
  // initialize container vectors
  std::vector<double> yempty(nall,0.0), yacp(nall,0.0), yadd(nall,0.0), ytotal(nall,0.0), yref(nall,0.0);
  std::vector<std::string> names(ntot,"");

  // get the names
  st.recycle(R"SQL(
SELECT Properties.key
FROM Properties, Training_set
WHERE Properties.id = Training_set.propid
ORDER BY Training_set.id;
)SQL");
  int n = 0;
  while (st.step() != SQLITE_DONE)
    names[n++] = std::string((char *) sqlite3_column_text(st.ptr(),0));
  if (n != nall)
    throw std::runtime_error("In TRAINING EVAL, unexpected end of the database column in names");

  // get the empty, reference, additional methods
  st.recycle(R"SQL(
SELECT length(Evaluations.value), Evaluations.value
FROM Evaluations, Training_set
WHERE Evaluations.methodid = :METHOD AND Evaluations.propid = Training_set.propid
ORDER BY Training_set.id;
)SQL");

  n = 0;
  st.bind((char *) ":METHOD",emptyid);
  while (st.step() != SQLITE_DONE){
    int nitem = sqlite3_column_int(st.ptr(),0) / sizeof(double);
    double *rval = (double *) sqlite3_column_blob(st.ptr(),1);
    if (nitem == 0 || !rval)
      throw std::runtime_error("In TRAINING EVAL, unexpected null element in evaluation search");
    for (int i = 0; i < nitem; i++)
      yempty[n++] = rval[i];
  }
  if (n != nall)
    throw std::runtime_error("In TRAINING EVAL, unexpected end of the database column in empty");

  n = 0;
  st.bind((char *) ":METHOD",refid);
  while (st.step() != SQLITE_DONE){
    int nitem = sqlite3_column_int(st.ptr(),0) / sizeof(double);
    double *rval = (double *) sqlite3_column_blob(st.ptr(),1);
    for (int i = 0; i < nitem; i++)
      yref[n++] = rval[i];
  }
  if (n != nall)
    throw std::runtime_error("In TRAINING EVAL, unexpected end of the database column in empty");

  for (int j = 0; j < addid.size(); j++){
    n = 0;
    st.bind((char *) ":METHOD",addid[j]);
    while (st.step() != SQLITE_DONE){
      int nitem = sqlite3_column_int(st.ptr(),0) / sizeof(double);
      double *rval = (double *) sqlite3_column_blob(st.ptr(),1);
      for (int i = 0; i < nitem; i++)
        yadd[n++] = rval[i];
    }
    if (n != nall)
      throw std::runtime_error("In TRAINING EVAL, unexpected end of the database column in empty");
  }

  // get the ACP contribution
  st.recycle(R"SQL(
SELECT length(Terms.value), Terms.value
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
    while (st.step() != SQLITE_DONE){
      int nitem = sqlite3_column_int(st.ptr(),0) / sizeof(double);
      double *rval = (double *) sqlite3_column_blob(st.ptr(),1);
      for (int j = 0; j < nitem; j++)
        yacp[n++] += rval[j] * t.coef;
    }
    if (n != nall){
      std::cout << "exponent: " << t.exp << " atom: " << (int) t.atom << " l: " << (int) t.l
                << " method: " << emptyid << " n: " << n << " nall: " << nall << std::endl;
      throw std::runtime_error("In TRAINING EVAL, unexpected end of the database column in ACP term number " + std::to_string(i));
    }
  }

  // calculate statistics
  for (int i = 0; i < nall; i++)
    ytotal[i] = yempty[i] + yacp[i] + yadd[i];
  int nset = setid.size();
  std::vector<double> rms(nset,0.0), mae(nset,0.0), mse(nset,0.0), wrms(nset,0.0);
  double rmst, maet, mset, wrmst, wrms_total_nofit = 0;
  int maxsetl = 0;
  std::vector<unsigned long int> ndat(nset,0);
  unsigned long int nsettot = 0;
  for (int i = 0; i < setid.size(); i++){
    ndat[i] = calc_stats(ytotal,yref,w,wrms[i],rms[i],mae[i],mse[i],nsetid,setid[i]);
    nsettot += ndat[i];
    if (set_dofit[i])
      wrms_total_nofit += wrms[i] * wrms[i];
    maxsetl = std::max(maxsetl,(int) alias[i].size());
  }
  wrms_total_nofit = std::sqrt(wrms_total_nofit);
  calc_stats(ytotal,yref,w,wrmst,rmst,maet,mset);

  // write the stats
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
  os << "#   wrms    =  " << wrms_total_nofit << std::endl;
  os << "#   wrmsall =  " << wrmst << " (including evaluation subsets)" << std::endl;

  for (int i = 0; i < setid.size(); i++){
    os << "# " << std::right << std::setw(maxsetl) << alias[i]
       << std::left << "  rms = " << std::right << std::setw(14) << rms[i]
       << std::left << "  mae = " << std::right << std::setw(14) << mae[i]
       << std::left << "  mse = " << std::right << std::setw(14) << mse[i]
       << std::left << "  ndat = " << std::right << ndat[i]
       << std::endl;
  }
  os << "# " << std::right << std::setw(maxsetl) << "all"
     << std::left << "  rms = " << std::right << std::setw(14) << rmst
     << std::left << "  mae = " << std::right << std::setw(14) << maet
     << std::left << "  mse = " << std::right << std::setw(14) << mset
     << std::left << "  ndat = " << std::right << nsettot
     << std::endl;

  // write the table of results
  output_eval(os,{},names,num,w,ytotal,"ytotal",yref,"yref",{yempty,yacp,yadd},{"yempty","yacp","yadd"});
  os << std::endl;
}

// Evaluate an ACP and compare to an external file; choose the systems
// with maximum deviation (non-linearity error) for each atom in the TS.
// Generate the maxcoef file.
void trainset::maxcoef(std::ostream &os, const std::unordered_map<std::string,std::string> &kmap,
		       const acp &a){
  if (!db || !(*db))
    throw std::runtime_error("A database file must be connected before using TRAINING MAXCOEF");
  if (!isdefined())
    throw std::runtime_error("The training set needs to be defined before using TRAINING MAXCOEF");

  if (complete == c_unknown)
    describe(os,false,true,true);
  if (complete == c_no)
    throw std::runtime_error("The training set needs to be complete before using TRAINING MAXCOEF");

  // header
  os << "* TRAINING MAXCOEF: calculate maximum coefficients for ACP development " << std::endl << std::endl;

  // parse input options
  std::unordered_map<std::string,std::string>::const_iterator im;
  bool iswrite;
  if ((im = kmap.find("WRITE")) != kmap.end())
    iswrite = true;
  else if ((im = kmap.find("CALC")) != kmap.end())
    iswrite = false;
  else
    throw std::runtime_error("Either WRITE or CALC is required in TRAINING MAXCOEF");

  im = kmap.find("SCF");
  if (im == kmap.end())
    throw std::runtime_error("The SCF evaluaions are required in TRAINING MAXCOEF");
  std::string file = kmap.at("SCF");

  int nprop_per_atom = 2;
  if ((im = kmap.find("NPROP_PER_ATOM")) != kmap.end())
    nprop_per_atom = std::stoi(im->second);

  // get the number of items
  int nall = 0;
  std::vector<int> num, nsetid;
  statement st(db->ptr(),R"SQL(
SELECT length(Evaluations.value), Properties.setid
FROM Evaluations, Training_set, Properties
WHERE Evaluations.methodid = :METHOD AND Evaluations.propid = Training_set.propid AND
      Evaluations.propid = Properties.id
ORDER BY Training_set.id;
)SQL");
  st.bind((char *) ":METHOD",refid);
  while (st.step() != SQLITE_DONE){
    int nitem = sqlite3_column_int(st.ptr(),0) / sizeof(double);
    int sid = sqlite3_column_int(st.ptr(),1);
    num.push_back(nitem);
    nsetid.push_back(sid);
    nall += nitem;
  }

  // initialize container vectors
  std::vector<double> yempty(nall,0.0), yacp(nall,0.0), yadd(nall,0.0), ytotal(nall,0.0), yscf(nall,0.0), yref(nall,0.0);
  std::vector<std::string> names(ntot,"");

  // get the names
  st.recycle(R"SQL(
SELECT Properties.key
FROM Properties, Training_set
WHERE Properties.id = Training_set.propid
ORDER BY Training_set.id;
)SQL");
  int n = 0;
  while (st.step() != SQLITE_DONE)
    names[n++] = std::string((char *) sqlite3_column_text(st.ptr(),0));
  if (n != nall)
    throw std::runtime_error("In TRAINING MAXCOEF, unexpected end of the database column in names");

  // get the empty, reference, additional methods
  st.recycle(R"SQL(
SELECT length(Evaluations.value), Evaluations.value
FROM Evaluations, Training_set
WHERE Evaluations.methodid = :METHOD AND Evaluations.propid = Training_set.propid
ORDER BY Training_set.id;
)SQL");

  n = 0;
  st.bind((char *) ":METHOD",emptyid);
  while (st.step() != SQLITE_DONE){
    int nitem = sqlite3_column_int(st.ptr(),0) / sizeof(double);
    double *rval = (double *) sqlite3_column_blob(st.ptr(),1);
    if (nitem == 0 || !rval)
      throw std::runtime_error("In TRAINING MAXCOEF, unexpected null element in evaluation search");
    for (int i = 0; i < nitem; i++)
      yempty[n++] = rval[i];
  }
  if (n != nall)
    throw std::runtime_error("In TRAINING MAXCOEF, unexpected end of the database column in empty");

  n = 0;
  st.bind((char *) ":METHOD",refid);
  while (st.step() != SQLITE_DONE){
    int nitem = sqlite3_column_int(st.ptr(),0) / sizeof(double);
    double *rval = (double *) sqlite3_column_blob(st.ptr(),1);
    for (int i = 0; i < nitem; i++)
      yref[n++] = rval[i];
  }
  if (n != nall)
    throw std::runtime_error("In TRAINING MAXCOEF, unexpected end of the database column in empty");

  for (int j = 0; j < addid.size(); j++){
    n = 0;
    st.bind((char *) ":METHOD",addid[j]);
    while (st.step() != SQLITE_DONE){
      int nitem = sqlite3_column_int(st.ptr(),0) / sizeof(double);
      double *rval = (double *) sqlite3_column_blob(st.ptr(),1);
      for (int i = 0; i < nitem; i++)
        yadd[n++] = rval[i];
    }
    if (n != nall)
      throw std::runtime_error("In TRAINING MAXCOEF, unexpected end of the database column in empty");
  }

  // get the ACP and total contributions
  st.recycle(R"SQL(
SELECT length(Terms.value), Terms.value
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
    while (st.step() != SQLITE_DONE){
      int nitem = sqlite3_column_int(st.ptr(),0) / sizeof(double);
      double *rval = (double *) sqlite3_column_blob(st.ptr(),1);
      for (int j = 0; j < nitem; j++)
        yacp[n++] += rval[j] * t.coef;
    }
    if (n != nall){
      std::cout << "exponent: " << t.exp << " atom: " << (int) t.atom << " l: " << (int) t.l
                << " method: " << emptyid << " n: " << n << " nall: " << nall << std::endl;
      throw std::runtime_error("In TRAINING MAXCOEF, unexpected end of the database column in ACP term number " + std::to_string(i));
    }
  }
  for (int i = 0; i < nall; i++)
    ytotal[i] = yempty[i] + yacp[i] + yadd[i];

  // read the file and build the data file
  if (!fs::is_regular_file(file))
    throw std::runtime_error("Invalid FILE in TRAINING MAXCOEF (not a file)");
  std::unordered_map<std::string,std::vector<double>> datmap = read_data_file_vector(file,1.);

  // fetch the reference method values from the DB and populate vectors
  std::vector<std::string> names_missing_fromdat;
  std::vector<double> datvalues;
  std::unordered_map<unsigned char,std::vector<int>> atmap;
  statement stkey(db->ptr(),"SELECT key, nat, zatoms FROM Structures WHERE id = ?1;");

  // the statement text
  st.recycle(R"SQL(
SELECT Properties.key, Properties.nstructures, Properties.structures, Properties.coefficients, Properties.property_type, Sets.id, Sets.key,
       length(ref.value), ref.value
FROM Properties
INNER JOIN Sets ON Properties.setid = Sets.id
INNER JOIN Training_Set ON Training_set.propid = Properties.id
LEFT OUTER JOIN Evaluations AS ref ON (ref.propid = Properties.id AND ref.methodid = :METHOD)
ORDER BY Properties.id
)SQL");
  st.bind((char *) ":METHOD",emptyid);
  n = 0;
  while (st.step() != SQLITE_DONE){
    // check if the evaluation is available in the database
    std::string key = (char *) sqlite3_column_text(st.ptr(),0);
    if (sqlite3_column_type(st.ptr(),8) == SQLITE_NULL)
      continue;
    n++;

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

    // fetch the structures and calculate value and atom map
    std::unordered_map<unsigned char,bool> atused;
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

      int nat = sqlite3_column_int(stkey.ptr(),1);
      unsigned char *zat_ = (unsigned char *) sqlite3_column_blob(stkey.ptr(),2);
      for (int j = 0; j < nat; j++)
	atused[zat_[j]] = true;
    }
    if (ptid == globals::ppty_energy_difference){
      for (int j = 0; j < nvalue; j++)
	value[j] *= globals::ha_to_kcal;
    }
    for (int j = 0; j < zat.size(); j++)
      if (atused.find(zat[j]) != atused.end())
	atmap[zat[j]].push_back(n);

    // populate the vectors
    if (!found){
      names_missing_fromdat.push_back(key);
    } else {
      double *rval = (double *) sqlite3_column_blob(st.ptr(),8);
      for (int j = 0; j < nvalue; j++)
        datvalues.push_back(value[j]);
    }
  }
  datmap.clear();
  if (!names_missing_fromdat.empty()){
    os << "## The following properties are missing from the FILE:" << std::endl;
    for (int i = 0; i < names_missing_fromdat.size(); i++)
      os << "## " << names_missing_fromdat[i] << std::endl;
    throw std::runtime_error("In TRAINING MAXCOEF, structures missing from file");
  }
  if (datvalues.size() != nall || n != nall)
    throw std::runtime_error("In TRAINING MAXCOEF, different number of values in file and DB");

  // calculate statistics
  double rmst, maet, mset, wrmst;
  calc_stats(ytotal,datvalues,w,wrmst,rmst,maet,mset);
  std::streamsize prec = os.precision(7);
  os << std::fixed;
  os << "# Non-linearity error evaluation" << std::endl;
  os << "# ACP: " << (a?a.get_name():emptyname) << std::endl;
  os << "# SCF from file: " << file << std::endl;
  os << "# Statistics: " << std::endl;
  os.precision(8);
  os << "#   wrms =  " << wrmst << std::endl;
  os << "# "
     << std::left << "  rms = " << std::right << std::setw(14) << rmst
     << std::left << "  mae = " << std::right << std::setw(14) << maet
     << std::left << "  mse = " << std::right << std::setw(14) << mset
     << std::left << "  ndat = " << std::right << nall
     << std::endl;
  os.precision(prec);

  // write the table of results
  output_eval(os,{},names,num,w,ytotal,"ACP (linear)",datvalues,"SCF (file)",{yref},{"Reference"});
  os << std::endl;

  // find the two figure out how to find per-system per-atom information
  os << "# LIST of properties on which to run the maxcoef calculation: " << std::endl;
  os << "| id | name | atom | nonlinear error (kcal/mol) |" << std::endl;
  std::unordered_map<int,bool> propused;
  std::unordered_map<unsigned char,std::vector<int>> atchosen;
  for (int i = 0; i < zat.size(); i++){
    std::vector<double> esave(nprop_per_atom,0.0);
    atchosen[zat[i]].resize(nprop_per_atom,0);
    if (atmap.find(zat[i]) != atmap.end()){
      for (int j = 0; j < atmap[zat[i]].size(); j++){
	int id = atmap[zat[i]][j];
	double e = std::abs(ytotal[id] - datvalues[id]);
	if (e > esave[0]){
	  int kthis = 0;
	  for (int k = 1; k < nprop_per_atom; k++){
	    if (e > esave[k])
	      kthis = k;
	    else
	      break;
	  }
	  for (int k = 0; k < kthis; k++){
	    esave[k] = esave[k+1];
	    atchosen[zat[i]][k] = atchosen[zat[i]][k+1];
	  }
	  esave[kthis] = e;
	  atchosen[zat[i]][kthis] = id;
	}
      }

      for (int j=0; j<atchosen[zat[i]].size(); j++){
	int id = atchosen[zat[i]][j];
	os << "|" << id << "| " << names[id] << " | " << nameguess(zat[i]) << " | " << esave[j] << "|" << std::endl;
      }
    }
  }
  os << std::endl;

  // coefficients
  std::vector<double> coef;
  for (int j = -6; j < 2; j++)
    coef.push_back(std::pow(10.0,j));

  if (iswrite){
    // WRITE
    std::unordered_map<std::string,std::string> kmap_new = kmap;
    kmap_new.erase("SET");
    kmap_new.erase("PACK");
    kmap_new["TERM"] = "";

    st.recycle(R"SQL(
SELECT Properties.nstructures, Properties.structures, Properties.key
FROM Properties, Training_set
WHERE Properties.id = Training_set.propid AND Training_set.id = ?1;
)SQL");
    stkey.recycle("SELECT ismolecule, key FROM Structures WHERE id = ?1;");
    for (int i = 0; i < zat.size(); i++){
      std::unordered_map<int,int> smap;

      // calculate the structure map
      for (int j = 0; j < atchosen[zat[i]].size(); j++){
	int id = atchosen[zat[i]][j];
	st.reset();
	st.bind(1,id);
	st.step();
	int nstr = sqlite3_column_int(st.ptr(),0);
	int *str = (int *) sqlite3_column_blob(st.ptr(),1);
	if (nstr == 0)
	  throw std::runtime_error("structures not found in TRAINING MAXCOEF");
	for (int k = 0; k < nstr; k++){
	  stkey.reset();
	  stkey.bind(1,str[k]);
	  stkey.step();
	  smap[str[k]] = sqlite3_column_int(stkey.ptr(),0);
	}
      }

      // write the structures
      std::string prefix = "maxcoef-" + nameguess(zat[i]) + "-";
      const std::vector<unsigned char> zat_ = {zat[i]};
      const std::vector<unsigned char> lmax_ = {lmax[i]};
      db->write_structures(os, kmap_new, a, smap, zat_, lmax_, exp, coef, prefix);
    }

  } else {
    // CALC

    // get the threshold
    double ethrs = 1.0;
    if (kmap.find("CALC") == kmap.end())
      throw std::runtime_error("The CALC keyword is required in TRAINING MAXCOEF + CALC");
    if (!kmap.at("CALC").empty())
      ethrs = std::stod(kmap.at("CALC"));

    // read the file and build the data file
    if (kmap.find("SOURCE") == kmap.end())
      throw std::runtime_error("The SOURCE file with the maxcoef are required in TRAINING MAXCOEF + CALC");
    std::string file = kmap.at("SOURCE");
    if (!fs::is_regular_file(file))
      throw std::runtime_error("Invalid SOURCE file in TRAINING MAXCOEF (not a file)");
    datmap = read_data_file_vector(file,1.);

    // initialize the maxcoefs
    int lmaxx = 0;
    for (int i = 0; i < zat.size(); i++)
      lmaxx = std::max((int) lmax[i]+1,lmaxx);
    std::vector<double> cmax(zat.size() * lmaxx * exp.size(),coef[coef.size()-1]);

    // If inserting, start transaction
    if (kmap.find("INSERT") != kmap.end())
      db->begin_transaction();

    // statements
    statement steval(db->ptr(),R"SQL(
SELECT length(Evaluations.value), Evaluations.value, length(Terms.value), Terms.value
FROM Properties, Evaluations, Training_Set, Terms
WHERE Training_set.propid = Properties.id AND Training_set.propid = Terms.propid AND Evaluations.propid = Properties.id AND
      Training_set.id = :ID AND Evaluations.methodid = :METHOD AND Terms.methodid = Evaluations.methodid AND
      Terms.atom = :ATOM AND Terms.l = :L AND Terms.exponent = :EXP
)SQL");
    statement stinsert(db->ptr(),R"SQL(
UPDATE OR REPLACE Terms
SET maxcoef = :MAXCOEF
WHERE methodid = :METHOD AND atom = :ATOM AND l = :L AND exponent = :EXP
      AND propid = (SELECT propid FROM Training_set WHERE id = :ID)
)SQL");
    st.recycle(R"SQL(
SELECT Properties.nstructures, Properties.structures, Properties.coefficients
FROM Properties, Training_set
WHERE Properties.id = Training_set.propid AND Training_set.id = ?1;
)SQL");
    stkey.recycle("SELECT key FROM Structures WHERE id = ?1;");

    // run over atoms and training IDs
    for (int i = 0; i < zat.size(); i++){
      for (int j = 0; j < atchosen[zat[i]].size(); j++){
	int id = atchosen[zat[i]][j];
	st.reset();
	st.bind(1,id);
	st.step();
	int nstr = sqlite3_column_int(st.ptr(),0);
	int *str = (int *) sqlite3_column_blob(st.ptr(),1);
	double *pcoef = (double *) sqlite3_column_blob(st.ptr(),2);
	if (nstr == 0)
	  throw std::runtime_error("structures not found in TRAINING MAXCOEF");

	// find the structure keys and file names
	std::vector<std::string> strfile;
	for (int k = 0; k < nstr; k++){
	  stkey.reset();
	  stkey.bind(1,str[k]);
	  stkey.step();
	  std::string name = (char *) sqlite3_column_text(stkey.ptr(), 0);
	  strfile.push_back("maxcoef-" + nameguess(zat[i]) + "-" + name);
	}

	int n = 0;
	for (int il = 0; il <= lmax[i]; il++){
	  for (int ie = 0; ie < exp.size(); ie++){
	    int idmax = ie + exp.size() * (il + lmaxx * i);

	    steval.reset();
	    steval.bind((char *) ":METHOD",emptyid);
	    steval.bind((char *) ":ID",id);
	    steval.bind((char *) ":ATOM",(int) zat[i]);
	    steval.bind((char *) ":L",il);
	    steval.bind((char *) ":EXP",exp[ie]);
	    steval.step();
	    int neval = sqlite3_column_int(steval.ptr(),0) / sizeof(double);
	    int nterm = sqlite3_column_int(steval.ptr(),2) / sizeof(double);
	    if (neval != nterm)
	      throw std::runtime_error("Incompatible neval and nterm in TRAINING MAXCOEF");
	    if (neval != 1)
	      throw std::runtime_error("Do not know how to handle neval != 1 in TRAINING MAXCOEF");
	    double *eval = (double *) sqlite3_column_blob(steval.ptr(),1);
	    double *tval = (double *) sqlite3_column_blob(steval.ptr(),3);

	    double coefid = coef[coef.size()-1];
	    double elast = 0.0;
	    bool found = false;
	    for (int ic = 0; ic < coef.size(); ic++){
	      n++;
	      double escf = 0;
	      for (int k = 0; k < nstr; k++){
		if (datmap.find(strfile[k]) == datmap.end())
		  throw std::runtime_error("In TRAINING MAXCOEF, structure in SOURCE file not found: " + strfile[k]);
		if (n > datmap[strfile[k]].size())
		  throw std::runtime_error("In TRAINING MAXCOEF, not enough energies for file: " + strfile[k]);

		escf += pcoef[k] * datmap[strfile[k]][n];
	      } // k, over structures
	      escf *= globals::ha_to_kcal;
	      double elin = eval[0] + tval[0] * coef[ic];
	      double edif = std::abs(elin - escf);

	      if (edif > ethrs && !found){
		if (ic == 0)
		  coefid = coef[0];
		else
		  coefid = coef[ic];
		found = true;
	      }
	      elast = edif;
	    } // ic, over coefficients

	    // update the cmax
	    cmax[idmax] = std::min(cmax[idmax],coefid);

	    // insert into the database
	    if (kmap.find("INSERT") != kmap.end()){
	      stinsert.reset();
	      stinsert.bind((char *) ":METHOD",emptyid);
	      stinsert.bind((char *) ":ATOM",(int) zat[i]);
	      stinsert.bind((char *) ":L",il);
	      stinsert.bind((char *) ":EXP",exp[ie]);
	      stinsert.bind((char *) ":ID",id);
	      stinsert.bind((char *) ":MAXCOEF",coefid);
	      stinsert.step();
	      // printf("inserted: %d %d %d %.2f %d %.10e\n",emptyid,(int) zat[i],(int) il,
	      // 	     exp[ie],id,coefid);
	    }

	  } // ie, over exponents
	} // il, over angular momenta

	// check that we had the correct number of energies in the datmap
	for (int k = 0; k < nstr; k++){
	  if (datmap[strfile[k]].size() != n)
	    throw std::runtime_error("In TRAINING MAXCOEF, too many energies for file: " + strfile[k]);
	}

      } // j, over training IDs for a given atom
    } // i, over atoms

    // If inserting, commit transaction
    if (kmap.find("INSERT") != kmap.end())
      db->commit_transaction();

    // write the final list, in maxcoef file format
    os << "# LIST of maximum coefficients: " << std::endl;
    for (int i = 0; i < zat.size(); i++){
      for (int il = 0; il <= lmax[i]; il++){
	for (int ie = 0; ie < exp.size(); ie++){
	  int idmax = ie + exp.size() * (il + lmaxx * i);
	  printf("%s %c %.6f %.10e\n",nameguess(zat[i]).c_str(),globals::inttol[il],exp[ie],cmax[idmax]);
	}
      }
    }
    os << std::endl;

  }
}

// Save the current training set to the database
void trainset::savedb(std::string &name) const{
#ifdef CEREAL_FOUND
  if (!db || !(*db))
    throw std::runtime_error("A database file must be connected before using TRAINING SAVE");
  if (!isdefined())
    throw std::runtime_error("The training set needs to be defined before using TRAINING SAVE");
  if (name.empty())
    throw std::runtime_error("TRAINING SAVE requires a name for the saved training set");

  // serialize
  std::stringstream ss;
  cereal::BinaryOutputArchive oarchive(ss);
  oarchive(*this);

  // insert into the database
  statement st(db->ptr(),"INSERT INTO Training_set_repo (key,training_set) VALUES(:KEY,:TRAINING_SET);");
 st.bind((char *) ":KEY",name);
 st.bind((char *) ":TRAINING_SET",(void *) ss.str().data(),true,ss.str().size());
 if (st.step() != SQLITE_DONE)
   throw std::runtime_error("Failed inserting training set into the database (TRAINING SAVE)");
#else
  throw std::runtime_error("Cannot use TRAINING SAVE: not compiled with cereal support");
#endif
}

// Load the training set from the database
void trainset::loaddb(std::string &name){
#ifdef CEREAL_FOUND
  if (!db || !(*db))
    throw std::runtime_error("A database file must be connected before using TRAINING LOAD");
  if (name.empty())
    throw std::runtime_error("TRAINING LOAD requires a name for the loaded training set");

  // fetch from the database
  statement st(db->ptr(),R"SQL(
SELECT length(training_set), training_set
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
  complete = c_unknown;
}

// Delete a training set from the database (or all the t.s.)
void trainset::deletedb(std::string &name) const{
  if (!db || !(*db))
    throw std::runtime_error("A database file must be connected before using TRAINING DELETE");

  statement st(db->ptr());
  if (name.empty()){
    st.recycle("DELETE FROM Training_set_repo;");
  } else {
    st.recycle("DELETE FROM Training_set_repo WHERE key = ?1;");
    st.bind(1,name);
  }
 if (st.step() != SQLITE_DONE)
   throw std::runtime_error("Failed deleting training set into the database (TRAINING DELETE)");
}

// List training sets from the database
void trainset::listdb(std::ostream &os) const {
  if (!db || !(*db))
    throw std::runtime_error("A database file must be connected before using TRAINING PRINT");

  statement st(db->ptr(),"SELECT key FROM Training_set_repo;");
  os << "## Table of saved training sets in the database" << std::endl;
  os << "| Name |" << std::endl;
  while (st.step() != SQLITE_DONE){
    std::string key = (char *) sqlite3_column_text(st.ptr(), 0);
    os << "| " << key << " |" << std::endl;
  }
  os << std::endl;
}

// Write the octavedump.dat file
void trainset::dump(std::ostream &os, const std::string &keyw/*=""*/) {
  if (!db || !(*db))
    throw std::runtime_error("A database file must be connected before using DUMP");
  if (!isdefined())
    throw std::runtime_error("The training set needs to be defined before using DUMP");

  os << "* TRAINING: dumping to an octave file " << std::endl;

  // check the completeness of the training set
  if (complete == c_unknown)
    describe(os,false,true,true);
  if (complete == c_no)
    throw std::runtime_error("The training set needs to be complete before using DUMP");

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
  // write the yref, yempty, and yadd columns
  statement st(db->ptr(),R"SQL(
SELECT length(Evaluations.value)
FROM Evaluations
WHERE Evaluations.methodid = :METHOD AND Evaluations.propid = :PROPID;
)SQL");
  std::vector<double> wtrain;
  unsigned long int nrows = 0, n = 0;
  for (int i = 0; i < setid.size(); i++){
    for (int j = set_initial_idx[i]; j < set_final_idx[i]; j++){
      if (set_dofit[i]){
        st.reset();
        st.bind((char *) ":METHOD",refid);
        st.bind((char *) ":PROPID",propid[j]);
        if (st.step() != SQLITE_ROW)
          throw std::runtime_error("Invalid evaluation in octave dump");

        int len = sqlite3_column_int(st.ptr(),0) / sizeof(double);
        for (int k = 0; k < len; k++)
          wtrain.push_back(w[n]);
        nrows += len;
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
  os << "# Dumped: " << 7 << " size integers " << std::endl;

  // write the atomic names
  std::string atoms = "";
  for (int iat = 0; iat < zat.size(); iat++){
    std::string str = nameguess(zat[iat]);
    if (str.size() == 1) str = str + " ";
    atoms += str;
  }
  const char *atoms_c = atoms.c_str();
  ofile.write(atoms_c,zat.size()*2*sizeof(char));
  os << "# Dumped: " << zat.size() << " atom names" << std::endl;

  // write the additional method names
  for (int i = 0; i < addname.size(); i++){
    std::string name = addname[iaddperm[i]] + std::string(addmaxl-addname[iaddperm[i]].size(),' ');
    const char *name_c = name.c_str();
    ofile.write(name_c,addmaxl*sizeof(char));
  }
  os << "# Dumped: " << addname.size() << " additional method names" << std::endl;

  // write the lmax vector
  char lmax_c[lmax.size()];
  for (int i = 0; i < lmax.size(); i++)
    lmax_c[i] = lmax[i] + 1;
  ofile.write((const char *) &lmax_c,lmax.size()*sizeof(unsigned char));
  os << "# Dumped: " << lmax.size() << " maximum angular momenta" << std::endl;

  // write the exponent vector
  const double *exp_c = exp.data();
  ofile.write((const char *) exp_c,exp.size()*sizeof(double));
  os << "# Dumped: " << exp.size() << " exponents" << std::endl;

  // write the w vector
  const double *w_c = wtrain.data();
  ofile.write((const char *) w_c,wtrain.size()*sizeof(double));
  os << "# Dumped: " << wtrain.size() << " weights" << std::endl;

  // write the x matrix
  st.recycle(R"SQL(
SELECT length(Terms.value), Terms.value
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
          int len = sqlite3_column_int(st.ptr(),0) / sizeof(double);
          double *value = (double *) sqlite3_column_blob(st.ptr(),1);
          ofile.write((const char *) value,len * sizeof(double));
        }
        if (n != nrows)
          throw std::runtime_error("Too few rows dumping terms data. Is the training data complete?");
      }
    }
  }
  os << "# Dumped: terms (x) with " << nrows << " rows and " << ncols << " columns" << std::endl;

  // write the yref, yempt, and yadd columns
  st.recycle(R"SQL(
SELECT length(Evaluations.value), Evaluations.value
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
      int len = sqlite3_column_int(st.ptr(),0) / sizeof(double);
      double *value = (double *) sqlite3_column_blob(st.ptr(),1);
      ofile.write((const char *) value,len * sizeof(double));
    }
    if (n != nrows)
      throw std::runtime_error("Too few rows dumping y data");
  }
  os << "# Dumped: evaluations (y) for " << ids.size() << " methods with " << nrows << " items each" << std::endl;

  // write the maxcoef vector
  std::vector<double> maxc;
  if (keyw != "NOMAXCOEF"){
    st.recycle(R"SQL(
SELECT MIN(Terms.maxcoef)
FROM Terms, Training_set
WHERE Terms.methodid = :METHOD AND Terms.atom = :ATOM AND Terms.l = :L AND Terms.exponent = :EXP
      AND Terms.propid = Training_set.propid AND Training_set.isfit IS NOT NULL;
)SQL");
    for (int iz = 0; iz < zat.size(); iz++){
      for (int il = 0; il <= lmax[iz]; il++){
	for (int ie = 0; ie < exp.size(); ie++){
	  st.reset();
	  st.bind((char *) ":METHOD",emptyid);
	  st.bind((char *) ":ATOM",(int) zat[iz]);
	  st.bind((char *) ":L",il);
	  st.bind((char *) ":EXP",exp[ie]);
	  st.step();
	  if (sqlite3_column_type(st.ptr(),0) != SQLITE_NULL){
	  } else {
	    maxc.clear();
	    goto exit_loop;
	  }
	  maxc.push_back(sqlite3_column_double(st.ptr(),0));
	}
      }
    }
  }
  exit_loop:
  uint64_t nmaxc;
  if (maxc.empty()){
    nmaxc = 0;
    ofile.write((const char *) &nmaxc,sizeof(uint64_t));
  } else {
    nmaxc = maxc.size();
    ofile.write((const char *) &nmaxc,sizeof(uint64_t));
    ofile.write((const char *) &maxc[0],maxc.size() * sizeof(double));
  }
  os << "# Dumped: " << maxc.size() << " maximum coefficients" << std::endl;

  // clean up
  ofile.close();
  os << "# DONE" << std::endl << std::endl;
}

// Write input files or structure files for the training set
// structures. Pass the options other than TRAINING and the ACP to the
// structure writers.
void trainset::write_structures(std::ostream &os, std::unordered_map<std::string,std::string> &kmap, const acp &a){
  if (!db || !(*db))
    throw std::runtime_error("A database file must be connected before using WRITE");
  if (!isdefined())
    throw std::runtime_error("The training set needs to be defined before using WRITE with TRAINING");

  // set
  int idini, idfin;
  auto im = kmap.find("TRAINING");
  if (im == kmap.end())
    throw std::runtime_error("write_structures in trainset called but no TRAINING keyword");
  if (!im->second.empty()){
    auto it = std::find(alias.begin(),alias.end(),im->second);
    if (it == alias.end())
      throw std::runtime_error("Unknown set alias passed to TRAINING in WRITE");
    int sid = it - alias.begin();
    idini = set_initial_idx[sid];
    idfin = set_final_idx[sid]-1;
  } else {
    idini = 0;
    idfin = ntot-1;
  }

  // collect the structure indices for the training set
  std::unordered_map<int,int> smap;
  statement st(db->ptr(),R"SQL(
SELECT Properties.nstructures, Properties.structures
FROM Properties, Training_set
WHERE Properties.id = Training_set.propid AND Training_set.id BETWEEN ?1 AND ?2;)SQL");
  statement ststr(db->ptr(),"SELECT ismolecule FROM Structures WHERE id = ?1;");
  st.bind(1,idini);
  st.bind(2,idfin);

  while (st.step() != SQLITE_DONE){
    int n = sqlite3_column_int(st.ptr(),0);
    const int *str = (int *)sqlite3_column_blob(st.ptr(),1);
    for (int i = 0; i < n; i++){
      ststr.bind(1,str[i]);
      ststr.step();
      smap[str[i]] = sqlite3_column_int(ststr.ptr(),0);
      ststr.reset();
    }
  }

  // write the inputs
  db->write_structures(os,kmap,a,smap,zat,lmax,exp);
}

// Read data for the training set or one of its subsets from a file,
// then compare to reference method refm.
void trainset::read_and_compare(std::ostream &os, std::unordered_map<std::string,std::string> &kmap){
  if (!db || !(*db))
    throw std::runtime_error("A database file must be connected before using COMPARE");
  if (!isdefined())
    throw std::runtime_error("The training set needs to be defined before using COMPARE with TRAINING");

  // set
  int sid = 0;
  auto im = kmap.find("TRAINING");
  if (im == kmap.end())
    throw std::runtime_error("write_structures in trainset called but no TRAINING keyword");
  if (!im->second.empty()){
    auto it = std::find(alias.begin(),alias.end(),im->second);
    if (it == alias.end())
      throw std::runtime_error("Unknown set alias passed to TRAINING in WRITE");
    sid = setid[it - alias.begin()];
  }

  // run the comparison with training set restriction
  db->read_and_compare(os,kmap,sid);
}


// Insert a subset into the Training_set table
void trainset::insert_subset_db(int sid){
  db->begin_transaction();
  statement st(db->ptr(),"INSERT INTO Training_set (id,propid,isfit) VALUES (:ID,:PROPID,:ISFIT);");
  for (int i = set_initial_idx[sid]; i < set_final_idx[sid]; i++){
    st.reset();
    st.bind((char *) ":ID", i);
    st.bind((char *) ":PROPID", propid[i]);
    if (set_dofit[sid])
      st.bind((char *) ":ISFIT", 1);
    st.step();
  }
  db->commit_transaction();
  complete = c_unknown;
}
