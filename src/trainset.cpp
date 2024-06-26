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
#ifdef LASSO_LIB
#include "liblasso.h"
#endif

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
    at.resize(ATSYMBOL_LENGTH,ATSYMBOL_PAD);
    std::string l = *(it++);

    unsigned char zat_ = zatguess(at);
    if (zat_ == 0)
      throw std::runtime_error("Invalid atom " + at + " in TRAINING ATOM");

    if (globals::ltoint.find(l) == globals::ltoint.end())
      throw std::runtime_error("Invalid lmax " + l + " in TRAINING ATOM");
    unsigned char lmax_ = globals::ltoint.at(l);
    zat.push_back(zat_);
    lmax.push_back(lmax_);
    symbol.push_back(at);
    termstring.push_back("-" + nameguess(zat_));
    nat++;
  }
  complete = c_unknown;
}

// Set term strings.
void trainset::settermstring(const std::list<std::string> &tokens){
  if (!db || !(*db))
    throw std::runtime_error("A database file must be connected before using TRAINING REFERENCE");
  if (tokens.empty())
    throw std::runtime_error("Need method key in TRAINING REFEENCE");
  if (zat.empty())
    throw std::runtime_error("ATOM must be defined before using TERM_STRING");

  std::string atom_ = *(tokens.begin());
  atom_.resize(ATSYMBOL_LENGTH,ATSYMBOL_PAD);
  std::string str_ = "";
  for (auto it = std::next(tokens.begin()); it != tokens.end(); it++)
    str_ += *it + " ";

  int iatom = -1;
  for (int k = 0; k < symbol.size(); k++){
    if (atom_.compare(symbol[k]) == 0){
      iatom = k;
      break;
    }
  }
  if (iatom < 0)
    throw std::runtime_error("ATOM not found: " + atom_);
  termstring[iatom] = str_;
  complete = c_unknown;
}

// Add exponents.
void trainset::addexp(const std::list<std::string> &tokens){
  for (auto it = tokens.begin(); it != tokens.end(); it++){
    double e_ = std::stod(*it);
    if (e_ <= 0.)
      throw std::runtime_error("Invalid exponent " + *it + " in TRAINING EXPONENT");

    exp.push_back(e_);
    exprn.push_back(2);
  }
  complete = c_unknown;
}

// Add exponents.
void trainset::addexprn(const std::list<std::string> &tokens){
  exprn.clear();
  for (auto it = tokens.begin(); it != tokens.end(); it++){
    int n_ = std::stoi(*it);
    if (n_ < 0 || n_ > 2)
      throw std::runtime_error("Invalid exponent r^n " + *it + " in TRAINING EXPRN");
    exprn.push_back(n_);
  }
  if (exp.size() != exprn.size())
    throw std::runtime_error("Size of exponent r^n does not match size of exponent in TRAINING");

  complete = c_unknown;
}

// Add a subset (combination of set, mask, weights)
void trainset::addsubset(const std::string &key, std::unordered_map<std::string,std::string> &kmap){
  if (!db || !(*db))
    throw std::runtime_error("A database file must be connected before in TRAINING SUBSET");
  if (kmap.find("SET") == kmap.end() || kmap["SET"].empty())
    throw std::runtime_error("The keyword SET is required in TRAINING SUBSET");

  std::unordered_map<std::string,std::string>::const_iterator im;

  // identify the property type
  int ppid = -1;
  if ((im = kmap.find("PROPERTY_TYPE")) != kmap.end()){
    std::string ppidname;
    if (!db->get_key_and_id(im->second,"Property_types",ppidname,ppid,true,true))
      throw std::runtime_error("Invalid PROPERTY_TYPE in TRAINING SUBSET");
  }

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
  std::string str = "SELECT COUNT(id), COUNT(DISTINCT property_type) FROM Properties WHERE setid = ?1";
  if (ppid >= 0)
    str += "AND property_type = ?2";
  statement st(db->ptr(),str);
  st.bind(1,idx);
  if (ppid >= 0)
    st.bind(2,ppid);
  st.step();
  int size = sqlite3_column_int(st.ptr(),0);
  if (size == 0)
    throw std::runtime_error("SET " + name + " does not have any associated properties in TRAINING/SUBSET.");
  int nprop = sqlite3_column_int(st.ptr(),1);
  if (nprop > 1)
    throw std::runtime_error("The set " + name + " has more than one property type. Please use PROPERTY_TYPE in TRAINING/SUBSET.");
  if (ppid < 0){
    // get the property type if not provided
    st.recycle("SELECT property_type FROM Properties WHERE setid = ?1");
    st.bind(1,idx);
    st.step();
    ppid = sqlite3_column_int(st.ptr(),0);
  }
  setpptyid.push_back(ppid);

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
  if (kmap.find("MASK_ATOMS") != kmap.end() || kmap.find("MASK_NOANIONS") != kmap.end() || kmap.find("MASK_NOCHARGED") != kmap.end() ||
      kmap.find("MASK_SIZE") != kmap.end()){
    if (nat == 0)
      throw std::runtime_error("The selected MASK in TRAINING/SUBSET is not possible if no atoms have been defined");

    int msize = -1;
    std::string mcomp = "";
    if (kmap.find("MASK_SIZE") != kmap.end()){
      std::list<std::string> tokens(list_all_words(kmap["MASK_SIZE"]));
      if (tokens.size() != 2)
	throw std::runtime_error("Invalid MASK_SIZE in TRAINING/SUBSET");
      mcomp = popstring(tokens);
      msize = std::stoi(tokens.front());
      if (mcomp != "<" && mcomp != "<=" && mcomp != ">" && mcomp != ">=" &&
	  mcomp != "==" && mcomp != "!=")
	throw std::runtime_error("Invalid conditional operator in MASK_SIZE, TRAINING/SUBSET");
    }

    int n = -1;
    statement st(db->ptr(),"SELECT nstructures, structures FROM Properties WHERE setid = " + std::to_string(idx) + " ORDER BY orderid;");
    statement ststr(db->ptr(),"SELECT nat,zatoms,charge FROM Structures WHERE id = ?1");
    while (st.step() != SQLITE_DONE){
      bool accept = true;

      int nstr = sqlite3_column_int(st.ptr(),0);
      int *str = (int *) sqlite3_column_blob(st.ptr(),1);
      for (int i = 0; i < nstr; i++){
	ststr.reset();
	ststr.bind(1,str[i]);
	ststr.step();
	int nat = sqlite3_column_int(ststr.ptr(),0);
	unsigned char *zat_ = (unsigned char *) sqlite3_column_blob(ststr.ptr(),1);
	int charge = sqlite3_column_int(ststr.ptr(),2);

	if (kmap.find("MASK_ATOMS") != kmap.end()){
	  for (int j = 0; j < nat; j++){
	    bool found = false;
	    for (int k = 0; k < zat.size(); k++){
	      if (zat[k] == zat_[j]){
		found = true;
		break;
	      }
	    }
	    if (!found){
	      accept = false;
	      break;
	    }
	  }
	}
	if (kmap.find("MASK_NOANIONS") != kmap.end() && charge < 0) accept = false;
	if (kmap.find("MASK_NOCHARGED") != kmap.end() && charge != 0) accept = false;
	if (kmap.find("MASK_SIZE") != kmap.end()){
	  if (mcomp == "<"  && !(nat < msize))  accept = false;
	  if (mcomp == "<=" && !(nat <= msize)) accept = false;
	  if (mcomp == ">"  && !(nat > msize))  accept = false;
	  if (mcomp == ">=" && !(nat >= msize)) accept = false;
	  if (mcomp == "==" && !(nat == msize)) accept = false;
	  if (mcomp == "!=" && !(nat != msize)) accept = false;
	}
	if (!accept) break;
      }
      n++;
      if (imask_and)
	set_mask[n] = set_mask[n] & accept;
      else
	set_mask[n] = set_mask[n] | accept;
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
  if (kmap.find("MASK_RANDOM") != kmap.end()) {
    std::list<std::string> tokens(list_all_words(kmap["MASK_RANDOM"]));

    // read the entries
    int num;
    unsigned int seed = 0;
    bool seedgiven = false;
    if (tokens.empty())
      throw std::runtime_error("Empty number of items in MASK_RANDOM");
    else if (tokens.size() == 1)
      num = std::stoi(tokens.front());
    else if (tokens.size() == 2){
      num = std::stoi(popstring(tokens));
      seed = std::stoi(tokens.front());
      seedgiven = true;
    }

    // set the seed
    if (!seedgiven)
      seed = std::time(NULL);
    std::srand(seed);
    std::cout << std::endl << "  Setting RANDOM SEED = " << seed << std::endl << std::endl;

    // find the used IDs and make sure we have enough
    std::vector<int> ids;
    for (int i = 0; i < set_mask.size(); i++)
      if (set_mask[i]) ids.push_back(i);
    if (ids.size() < num)
      throw std::runtime_error("Not enough items remaining to satisfy the requested MASK_RANDOM");

    // shuffle and re-build the mask
    std::random_shuffle(ids.begin(), ids.end());
    std::fill(set_mask.begin(), set_mask.end(), false);
    for (int i = 0; i < num; i++)
      set_mask[ids[i]] = true;
  }

  // build the propid, size, and final index of the set
  set_size.push_back(0);
  set_final_idx.push_back(ilast);
  str = "SELECT id FROM Properties WHERE setid = ?1 AND property_type = ?2";
  str += "ORDER BY orderid";
  st.recycle(str);
  st.bind(1,setid[sid]);
  st.bind(2,ppid);

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
WHERE Properties.setid = :SETID AND Training_set.propid = Properties.id AND Training_set.isfit IS NOT NULL;)SQL");
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
    throw std::runtime_error("Need method key in TRAINING EMPTY");

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
    if (nat == 0) os << "--- No atoms found (ATOM) ---" << std::endl;
    if (lmax.empty()) os << "--- No angular momenta found (LMAX) ---" << std::endl;
    if (exp.empty()) os << "--- No exponents found (EXP) ---" << std::endl;
    if (exprn.empty()) os << "--- No exponent r^n found (EXPRN) ---" << std::endl;
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
    os << "# List of atoms and maximum angular momentum channels (" << nat << ")" << std::endl;
    os << "| Atom | lmax |" << std::endl;
    for (int i = 0; i < nat; i++){
      os << "| " << symbol[i] << " | " << globals::inttol[lmax[i]] << " |" << std::endl;
    }
    os << std::endl;
  }

  // Exponents //
  if (~quiet){
    os << "# List of exponents (" << exp.size() << ")" << std::endl;
    os << "| id | exp | n |" << std::endl;
    for (int i = 0; i < exp.size(); i++){
      os << "| " << i << " | " << exp[i] << " | " << exprn[i] << " |" << std::endl;
    }
    os << std::endl;
  }

  // Sets //
  statement st(db->ptr(),"SELECT litrefs, description FROM Sets WHERE id = ?1;");
  if (!quiet){
    os << "# List of subsets (" << setname.size() << ")" << std::endl;
    os << "| id | alias | db-name | db-id | ppty-type | initial | final | size | dofit? | litref | description |" << std::endl;
    for (int i = 0; i < setname.size(); i++){
      st.reset();
      st.bind(1,setid[i]);
      st.step();
      const char *litref = (const char *) sqlite3_column_text(st.ptr(), 0);
      const char *description = (const char *) sqlite3_column_text(st.ptr(), 1);

      os << "| " << i << " | " << alias[i] << " | " << setname[i] << " | " << setid[i]
	 << " | " << setpptyid[i]
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
SELECT Properties.id, Properties.key, Properties.nstructures, length(Evaluations.value), Evaluations.value, Property_types.key, Training_set.isfit, Training_set.id
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

      bool isfit = (sqlite3_column_type(st.ptr(),6) != SQLITE_NULL);

      int tid = sqlite3_column_int(st.ptr(),7);
      int sid = -1;
      for (int i = 0; i < setid.size(); i++){
	if (tid >= set_initial_idx[i] && tid < set_final_idx[i]){
	  sid = i;
	  break;
	}
      }
      if (sid < 0)
	throw std::runtime_error("Could not find set id in DESCRIBE");

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
WHERE Terms.methodid = :METHOD AND Terms.zatom = :ZATOM AND Terms.symbol = :SYMBOL AND Terms.l = :L AND Terms.exponent = :EXP AND Terms.exprn = :EXPRN;)SQL");
    int ncall = 0, ntall = 0;
    if (!quiet)
      os << "# Terms: " << std::endl;
    for (int iz = 0; iz < zat.size(); iz++){
      for (int il = 0; il <= lmax[iz]; il++){
	for (int ie = 0; ie < exp.size(); ie++){
	  st.reset();
	  st.bind((char *) ":METHOD",emptyid);
	  st.bind((char *) ":ZATOM",(int) zat[iz]);
	  st.bind((char *) ":SYMBOL",(char *) symbol[iz].c_str());
	  st.bind((char *) ":L",il);
	  st.bind((char *) ":EXP",exp[ie]);
	  st.bind((char *) ":EXPRN",exprn[ie]);
	  st.step();
	  int ncalc = sqlite3_column_int(st.ptr(), 0);
	  if (!quiet){
	    os << "| " << symbol[iz] << " | " << globals::inttol[il] << " | "
	       << exp[ie] << " | " << exprn[ie] << " | " << ncalc << "/" << ncalc_all << " |" << (ncalc==ncalc_all?" (complete)":" (missing)") << std::endl;
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

  int nall = 0;
  int n = -1;
  std::vector<int> num, nsetid;
  std::vector<double> wall;
  statement st(db->ptr(),R"SQL(
SELECT length(Evaluations.value), Properties.setid, Properties.property_type
FROM Evaluations, Training_set, Properties
WHERE Evaluations.methodid = :METHOD AND Evaluations.propid = Training_set.propid AND
      Evaluations.propid = Properties.id
ORDER BY Training_set.id;
)SQL");
  st.bind((char *) ":METHOD",refid);
  while (st.step() != SQLITE_DONE){
    n++;
    int nitem = sqlite3_column_int(st.ptr(),0) / sizeof(double);
    int sid = sqlite3_column_int(st.ptr(),1);
    int ppid = sqlite3_column_int(st.ptr(),2);
    int idx = sid * globals::ppty_MAX + ppid;

    num.push_back(nitem);
    for (int i = 0; i < nitem; i++){
      nsetid.push_back(idx);
      wall.push_back(w[n]);
    }
    nall += nitem;
  }
  // initialize container vectors
  std::vector<double> yempty(nall,0.0), yacp(nall,0.0), yadd(nall,0.0), ytotal(nall,0.0), yref(nall,0.0);
  std::vector<std::string> names(nall,"");

  // get the names
  st.recycle(R"SQL(
SELECT Properties.key
FROM Properties, Training_set
WHERE Properties.id = Training_set.propid
ORDER BY Training_set.id;
)SQL");
  int count = -1;
  n = 0;
  while (st.step() != SQLITE_DONE){
    count++;
    std::string str = std::string((char *) sqlite3_column_text(st.ptr(),0));
    for (int i = 0; i < num[count]; i++)
      names[n++] = str;
  }
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
WHERE Terms.methodid = :METHOD AND Terms.zatom = :ZATOM AND Terms.symbol = :SYMBOL AND Terms.l = :L AND Terms.exponent = :EXP 
      AND Terms.exprn = :EXPRN AND Terms.propid = Training_set.propid
ORDER BY Training_set.id;
)SQL");
  for (int i = 0; i < a.size(); i++){
    acp::term t = a.get_term(i);
    st.reset();
    st.bind((char *) ":METHOD",emptyid);
    st.bind((char *) ":ZATOM",(int) t.atom);
    st.bind((char *) ":SYMBOL",std::string(t.sym));
    st.bind((char *) ":L",(int) t.l);
    st.bind((char *) ":EXP",t.exp);
    st.bind((char *) ":EXPRN",t.exprn);

    n = 0;
    while (st.step() != SQLITE_DONE){
      int nitem = sqlite3_column_int(st.ptr(),0) / sizeof(double);
      double *rval = (double *) sqlite3_column_blob(st.ptr(),1);
      for (int j = 0; j < nitem; j++)
	yacp[n++] += rval[j] * t.coef;
    }
    if (n != nall){
      std::cout << "exponent: " << t.exp << "exprn: " << t.exprn << " atom: " << (int) t.atom << " sym: " << t.sym << " l: " << (int) t.l
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
    int idx = setid[i] * globals::ppty_MAX + setpptyid[i];

    ndat[i] = calc_stats(ytotal,yref,wall,wrms[i],rms[i],mae[i],mse[i],nsetid,idx);
    nsettot += ndat[i];
    if (set_dofit[i])
      wrms_total_nofit += wrms[i] * wrms[i];
    maxsetl = std::max(maxsetl,(int) alias[i].size());
  }
  wrms_total_nofit = std::sqrt(wrms_total_nofit);
  calc_stats(ytotal,yref,wall,wrmst,rmst,maet,mset,num);

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
void trainset::maxcoef(std::ostream &os, const std::unordered_map<std::string,std::string> &kmap){
  if (!db || !(*db))
    throw std::runtime_error("A database file must be connected before using TRAINING MAXCOEF");
  if (!isdefined())
    throw std::runtime_error("The training set needs to be defined before using TRAINING MAXCOEF");
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

  double ini = -6.0, end = 2.0;
  int npts = 9;
  if ((im = kmap.find("RANGE")) != kmap.end()){
    std::list<std::string> tokens(list_all_words(kmap.at("RANGE")));
    if (tokens.size() != 3)
      throw std::runtime_error("Erroneous syntax in RANGE, TRAINING/MAXCOEF");
    ini = std::stod(popstring(tokens));
    end = std::stod(popstring(tokens));
    npts = std::stoi(tokens.front());
  }

  // coefficients
  double step = (end-ini) / (npts-1);
  std::vector<double> coef;
  for (int i = 0; i < npts; i++){
    double x = ini + i * step;
    coef.push_back(std::pow(10.0,x));
  }
  printf("+ List of coefficients for the maxcoef interpolation:\n");
  for (int i = 0; i < coef.size(); i++){
    printf("%.10e ",coef[i]);
    if (i % 5 == 4) printf("\n");
  }
  printf("\n");

  if (iswrite){
    // WRITE
    std::unordered_map<std::string,std::string> kmap_new = kmap;
    kmap_new.erase("SET");
    kmap_new.erase("PACK");
    kmap_new["TERM"] = "";

    statement st(db->ptr(),R"SQL(
SELECT Properties.nstructures, Properties.structures, Properties.key, Properties.property_type
FROM Properties, Training_set
WHERE Properties.id = Training_set.propid;
)SQL");
    statement stkey(db->ptr(),"SELECT ismolecule, key FROM Structures WHERE id = ?1;");

    std::unordered_map<int,int> smap;
    while (st.step() != SQLITE_DONE){
      int ptid = sqlite3_column_int(st.ptr(),3);
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
    db->write_structures(os, kmap_new, {}, smap, zat, symbol, termstring, lmax, exp, exprn, coef, "maxcoef-");

  } else {
    // CALC

    // check that the mini-training set is complete
    if (complete == c_unknown)
      describe(os,false,true,true);
    if (complete == c_no)
      throw std::runtime_error("The training set needs to be complete before using TRAINING MAXCOEF");

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
    std::unordered_map<std::string,std::vector<double>> datmap = read_data_file_vector(file,1.);

    // verify the number of entries and decide whether the user gives the empty or not
    int ntotal = 0;
    for (int i = 0; i < zat.size(); i++)
      for (int il = 0; il <= lmax[i]; il++)
	ntotal += exp.size() * coef.size();
    int ncoef = 0;
    for (auto it = datmap.begin(); it != datmap.end(); it++){
      if (it->second.size() != ntotal && it->second.size() != ntotal+1){
	std::cout << "Structure = " << it->first << std::endl;
	std::cout << "Entries for structure = " << it->second.size() << std::endl;
	std::cout << "Entries expected = " << ntotal << " or " << ntotal+1 << std::endl;
	throw std::runtime_error("Invalid number of entries in TRAINING MAXCOEF for structure " + it->first);
      }
      if (it->second.size() != ncoef){
	if (ncoef == 0)
	  ncoef = it->second.size();
	else{
	  std::cout << "Structure = " << it->first << std::endl;
	  std::cout << "Entries for structure = " << it->second.size() << std::endl;
	  std::cout << "Entries for previous structures = " << ncoef << std::endl;
	  throw std::runtime_error("Inconsistent number of entries in TRAINING MAXCOEF");
	}
      }
    }
    bool skipempty = (ncoef == ntotal+1);

    // statements
    statement steval(db->ptr(),R"SQL(
SELECT Evaluations.propid, Evaluations.value, Terms.value, Properties.property_type
FROM Properties, Evaluations, Training_Set, Terms
WHERE Training_set.propid = Properties.id AND Training_set.propid = Terms.propid AND Evaluations.propid = Properties.id AND
      Evaluations.methodid = :METHOD AND Terms.methodid = Evaluations.methodid AND
      Terms.zatom = :ZATOM AND Terms.symbol = :SYMBOL AND Terms.l = :L AND Terms.exponent = :EXP AND Terms.exprn = :EXPRN
)SQL");
    statement st(db->ptr(),R"SQL(
SELECT Properties.nstructures, Properties.structures, Properties.coefficients
FROM Properties, Training_set
WHERE Properties.id = Training_set.propid AND Properties.id = ?1;
)SQL");
    statement stkey(db->ptr(),"SELECT key FROM Structures WHERE id = ?1;");

    // open the file
    FILE *fp = fopen("maxcoef.dat","w");

    // run over atoms
    os << "# LIST of maximum coefficients written to maxcoef.dat" << std::endl;
    int nbefore = 0;
    for (int i = 0; i < zat.size(); i++){
      for (int il = 0; il <= lmax[i]; il++){
	for (int ie = 0; ie < exp.size(); ie++){
	  // initialize the cmax
	  double cmax = coef[coef.size()-1];

	  steval.reset();
	  steval.bind((char *) ":METHOD",emptyid);
	  steval.bind((char *) ":ZATOM",(int) zat[i]);
	  steval.bind((char *) ":SYMBOL",symbol[i]);
	  steval.bind((char *) ":L",il);
	  steval.bind((char *) ":EXP",exp[ie]);
	  steval.bind((char *) ":EXPRN",exprn[ie]);
	  while (steval.step() != SQLITE_DONE){
	    // check property ID
	    int ptid = sqlite3_column_int(steval.ptr(),3);
	    if (ptid != globals::ppty_energy_difference && ptid != globals::ppty_energy)
	      throw std::runtime_error("properties other than ENERGY_DIFFERENCE and ENERGY cannot be used in TRAINING MAXCOEF");

	    // structure ID and linear and zero energies
	    int id = sqlite3_column_int(steval.ptr(),0);
	    double *res = (double *) sqlite3_column_blob(steval.ptr(),1);
	    double e0 = res[0];
	    res = (double *) sqlite3_column_blob(steval.ptr(),2);
	    double eslope = res[0];

	    // get the structures
	    st.reset();
	    st.bind(1,id);
	    st.step();
	    int nstr = sqlite3_column_int(st.ptr(),0);
	    int *str = (int *) sqlite3_column_blob(st.ptr(),1);
	    double *pcoef = (double *) sqlite3_column_blob(st.ptr(),2);

	    double elast = 0;
	    for (int ic = 0; ic < coef.size(); ic++){
	      // find the structure keys and file names
	      double escf = 0.;
	      std::vector<std::string> strfile;
	      for (int k = 0; k < nstr; k++){
		stkey.reset();
		stkey.bind(1,str[k]);
		stkey.step();
		std::string name = (char *) sqlite3_column_text(stkey.ptr(), 0);
		std::string strname = "maxcoef-" + name;
		int nthis;
		if (skipempty)
		  nthis = nbefore + ic + 1; // +1 to account for the empty calculation
		else
		  nthis = nbefore + ic;
		if (datmap.find(strname) == datmap.end())
		  throw std::runtime_error("Structure not found in source file: " + strname);
		escf += pcoef[k] * datmap[strname][nthis];
	      }
	      if (ptid == globals::ppty_energy_difference)
		escf *= globals::ha_to_kcal;
	      double elin = e0 + coef[ic] * eslope;
	      double edif = std::abs(escf - elin);
	      if (ptid == globals::ppty_energy)
		edif *= globals::ha_to_kcal;

	      if (edif > ethrs) {
		double cc;
		if (ic == 0)
		  cc = coef[0];
		else
		  cc = coef[ic-1] + (ethrs - elast) * (coef[ic] - coef[ic-1]) / (edif - elast);
		cmax = std::min(cmax,cc);
		break;
	      }
	      elast = edif;
	    } // ic, over coefficients
	  } // steval.step(), runs over properties
	  fprintf(fp,"%s %c %.6f %d %.10e\n",symbol[i].c_str(),globals::inttol[il],exp[ie],exprn[ie],cmax);
	  nbefore = nbefore + coef.size();
	} // ie, over exponents
      } // il, over angular momenta
    } // i, over atoms
    fclose(fp);
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
  if (addid.size() > 0)
    throw std::runtime_error("FIXME: additional terms not implemented yet");

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

  // write the atomic names
  std::string symbols = "";
  for (int iat = 0; iat < symbol.size(); iat++)
    symbols += symbol[iat];
  const char *symbols_c = symbols.c_str();
  ofile.write(symbols_c,symbol.size()*5*sizeof(char));
  os << "# Dumped: " << symbol.size() << " atom symbols" << std::endl;

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

  // write the exponent r^n vector
  const int *exprn_c = exprn.data();
  ofile.write((const char *) exprn_c,exprn.size()*sizeof(int));
  os << "# Dumped: " << exprn.size() << " exponent r^n" << std::endl;

  // write the w vector
  const double *w_c = wtrain.data();
  ofile.write((const char *) w_c,wtrain.size()*sizeof(double));
  os << "# Dumped: " << wtrain.size() << " weights" << std::endl;

  // write the x matrix
  st.recycle(R"SQL(
SELECT length(Terms.value), Terms.value
FROM Terms, Training_set
WHERE Terms.methodid = :METHOD AND Terms.zatom = :ZATOM AND Terms.symbol = :SYMBOL AND Terms.l = :L AND Terms.exponent = :EXP
      AND Terms.exprn = :EXPRN AND Terms.propid = Training_set.propid AND Training_set.isfit IS NOT NULL
ORDER BY Training_set.id;
)SQL");
  for (int iz = 0; iz < zat.size(); iz++){
    for (int il = 0; il <= lmax[iz]; il++){
      for (int ie = 0; ie < exp.size(); ie++){
	st.reset();
	st.bind((char *) ":METHOD",emptyid);
	st.bind((char *) ":ZATOM",(int) zat[iz]);
	st.bind((char *) ":SYMBOL",symbol[iz]);
	st.bind((char *) ":L",il);
	st.bind((char *) ":EXP",exp[ie]);
	st.bind((char *) ":EXPRN",exprn[ie]);
	int n = 0;
	while (st.step() != SQLITE_DONE){
	  int len = sqlite3_column_int(st.ptr(),0) / sizeof(double);
	  double *value = (double *) sqlite3_column_blob(st.ptr(),1);
	  n += len;
	  if (n > nrows)
	    throw std::runtime_error("Too many rows dumping terms data");
	  ofile.write((const char *) value,len * sizeof(double));
	}
	if (n != nrows)
	  throw std::runtime_error("Too few rows dumping terms data. Is the training data complete?");
      }
    }
  }
  os << "# Dumped: terms (x) with " << nrows << " rows and " << ncols << " columns" << std::endl;

  // write the yref, yempty, and yadd columns
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
      int len = sqlite3_column_int(st.ptr(),0) / sizeof(double);
      double *value = (double *) sqlite3_column_blob(st.ptr(),1);
      n += len;
      if (n > nrows)
	throw std::runtime_error("Too many rows dumping y data");
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
WHERE Terms.methodid = :METHOD AND Terms.zatom = :ZATOM AND Terms.symbol = :SYMBOL AND Terms.l = :L AND Terms.exponent = :EXP
      AND Terms.exprn = :EXPRN AND Terms.propid = Training_set.propid AND Training_set.isfit IS NOT NULL;
)SQL");
    for (int iz = 0; iz < zat.size(); iz++){
      for (int il = 0; il <= lmax[iz]; il++){
	for (int ie = 0; ie < exp.size(); ie++){
	  st.reset();
	  st.bind((char *) ":METHOD",emptyid);
	  st.bind((char *) ":ZATOM",(int) zat[iz]);
	  st.bind((char *) ":SYMBOL",symbol[iz]);
	  st.bind((char *) ":L",il);
	  st.bind((char *) ":EXP",exp[ie]);
	  st.bind((char *) ":EXPRN",exprn[ie]);
	  st.step();
	  if (sqlite3_column_type(st.ptr(),0) == SQLITE_NULL){
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

// Generate an ACP
void trainset::generate(std::ostream &os, const bool maxcoef0, const std::vector<double> lambdav){

#ifndef LASSO_LIB
  throw std::runtime_error("Cannot use TRAINING GENERATE without linking the external LASSO library.");
  return;
#else

  os << "* TRAINING: generating ACPs " << std::endl << std::endl;

  // build the lambda list
  std::vector<double> lam;
  double ini, end, step;
  if (lambdav.size() == 0 || lambdav.size() > 3)
    throw std::runtime_error("Incorrect number of elements in TRAINING GENERATE");
  ini = lambdav[0];
  if (lambdav.size() > 1)
    end = lambdav[1];
  else
    end = ini;
  if (lambdav.size() > 2)
    step = lambdav[2];
  else
    step = 1;
  for (double d = ini; d <= end; d += step)
    lam.push_back(d);
  if (lam.empty()) return;

  // check the completeness of the training set
  if (complete == c_unknown)
    describe(os,false,true,true);
  if (complete == c_no)
    throw std::runtime_error("The training set needs to be complete before using GENERATE");

  //// collect the information for the fit ////
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
  if (addid.size() > 0)
    throw std::runtime_error("FIXME: additional terms not implemented yet");

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

  // the dimension integers
  uint64_t ncols = 0, addmaxl = 0;
  for (int i = 0; i < zat.size(); i++)
    ncols += exp.size() * (lmax[i]+1);
  for (int i = 0; i < addname.size(); i++)
    addmaxl = std::max(addmaxl,(uint64_t) addname[i].size());

  // the w vector
  std::vector<double> wsqrt = wtrain;
  wsqrt.reserve(wtrain.size());
  for (int i = 0; i < wtrain.size(); i++)
    wsqrt[i] = std::sqrt(wtrain[i]);

  // the x matrix
  std::vector<double> x;
  st.recycle(R"SQL(
SELECT length(Terms.value), Terms.value
FROM Terms, Training_set
WHERE Terms.methodid = :METHOD AND Terms.zatom = :ZATOM AND Terms.symbol = :SYMBOL AND Terms.l = :L AND Terms.exponent = :EXP
      AND Terms.exprn = :EXPRN AND Terms.propid = Training_set.propid AND Training_set.isfit IS NOT NULL
ORDER BY Training_set.id;
)SQL");
  for (int iz = 0; iz < zat.size(); iz++){
    for (int il = 0; il <= lmax[iz]; il++){
      for (int ie = 0; ie < exp.size(); ie++){
	st.reset();
	st.bind((char *) ":METHOD",emptyid);
	st.bind((char *) ":ZATOM",(int) zat[iz]);
	st.bind((char *) ":SYMBOL",symbol[iz]);
	st.bind((char *) ":L",il);
	st.bind((char *) ":EXP",exp[ie]);
	st.bind((char *) ":EXPRN",exprn[ie]);
	int m = 0;
	while (st.step() != SQLITE_DONE){
	  int len = sqlite3_column_int(st.ptr(),0) / sizeof(double);
	  double *value = (double *) sqlite3_column_blob(st.ptr(),1);
	  for (int i = 0; i < len; i++)
	    x.push_back(value[i] * wsqrt[m++]);
	}
	if (m != nrows)
	  throw std::runtime_error("Too few rows in terms data. Is the training data complete?");
      }
    }
  }
  if (x.size() != nrows*ncols)
    throw std::runtime_error("Error count number of terms (nrows*ncols). Is the training data complete?");

  // calculate the y = yref - yempty - ynofit
  // crash if there are any yadd columns
  std::vector<double> y;
  st.recycle(R"SQL(
SELECT length(Evaluations.value), Evaluations.value
FROM Evaluations, Training_set
WHERE Evaluations.methodid = :METHOD
      AND Evaluations.propid = Training_set.propid AND Training_set.isfit IS NOT NULL
ORDER BY Training_set.id;
)SQL");
  st.bind((char *) ":METHOD",refid);
  n = 0;
  while (st.step() != SQLITE_DONE){
    int len = sqlite3_column_int(st.ptr(),0) / sizeof(double);
    double *value = (double *) sqlite3_column_blob(st.ptr(),1);
    for (int i = 0; i < len; i++)
      y.push_back(value[i]);
    n += len;
  }
  if (n != nrows)
    throw std::runtime_error("Too few rows dumping y data");
  st.bind((char *) ":METHOD",emptyid);
  n = 0;
  while (st.step() != SQLITE_DONE){
    int len = sqlite3_column_int(st.ptr(),0) / sizeof(double);
    double *value = (double *) sqlite3_column_blob(st.ptr(),1);
    for (int i = 0; i < len; i++){
      y[n] = (y[n] - value[i]) * wsqrt[n];
      n++;
    }
  }
  if (n != nrows)
    throw std::runtime_error("Too few rows dumping y data");

  // the maxcoef vector
  std::vector<double> maxc;
  bool maxcoef = false;
  if (maxcoef0){
    st.recycle(R"SQL(
SELECT MIN(Terms.maxcoef)
FROM Terms, Training_set
WHERE Terms.methodid = :METHOD AND Terms.zatom = :ZATOM AND Terms.symbol = :SYMBOL AND Terms.l = :L AND Terms.exponent = :EXP
      AND Terms.exprn = :EXPRN AND Terms.propid = Training_set.propid AND Training_set.isfit IS NOT NULL;
)SQL");
    for (int iz = 0; iz < zat.size(); iz++){
      for (int il = 0; il <= lmax[iz]; il++){
	for (int ie = 0; ie < exp.size(); ie++){
	  st.reset();
	  st.bind((char *) ":METHOD",emptyid);
	  st.bind((char *) ":ZATOM",(int) zat[iz]);
	  st.bind((char *) ":SYMBOL",symbol[iz]);
	  st.bind((char *) ":L",il);
	  st.bind((char *) ":EXP",exp[ie]);
	  st.bind((char *) ":EXPRN",exprn[ie]);
	  st.step();
	  if (sqlite3_column_type(st.ptr(),0) == SQLITE_NULL){
	    maxc.clear();
	    goto exit_loop;
	  }
	  maxc.push_back(sqlite3_column_double(st.ptr(),0));
	}
      }
    }
  }
 exit_loop:

  printf(" Id      lambda      norm-1      norm-2      norm-inf    wrms     nterm  filename\n");
  std::vector<double> beta;
  beta.reserve(ncols);
  // double *beta = new double[ncols];
  double wrms;
  for (int i = 0; i < lam.size(); i++){
    // run the lasso fit and generate the output line
    lasso_c(nrows,ncols,x.data(),y.data(),lam[i],beta.data(),&wrms,maxc.empty()?NULL:maxc.data());

    // make the ACP
    std::string name = "lasso-" + std::to_string(i+1);
    std::vector<acp::term> t;
    int n = 0;
    for (int iz = 0; iz < zat.size(); iz++){
      for (unsigned char il = 0; il <= lmax[iz]; il++){
	for (int ie = 0; ie < exp.size(); ie++){
	  if (std::abs(beta[n]) > 1e-20){
	    acp::term t_;
	    t_.block = iz;
	    t_.atom = zat[iz];
	    strcpy(&(t_.sym[0]),symbol[iz].c_str());
	    t_.l = il;
	    t_.exp = exp[ie];
	    t_.exprn = exprn[ie];
	    t_.coef = beta[n];
	    t.push_back(t_);
	  }
	  n++;
	}
      }
    }
    acp a(name,t);

    // generate the ACP
    std::ofstream fp(name + ".acp");
    if (!fp.is_open())
      throw std::runtime_error("Could not open output file: " + name + ".acp");

    // write the ACP header
    fp << "! This ACP was generated with acpdb" << std::endl
       << "! Atoms(lmax) ";
    for (int iz = 0; iz < zat.size(); iz++)
      fp << nameguess(zat[iz]) << "(" << globals::inttol[lmax[iz]] << ") ";
    fp << std::endl;
    fp << "! Exponents: " << std::fixed << std::setprecision(2);
    for (int ie = 0; ie < exp.size(); ie++)
      fp << exp[ie] << " ";
    fp << "! Exponent r^n: ";
    for (int ie = 0; ie < exprn.size(); ie++)
      fp << exprn[ie] << " ";
    fp << std::endl;
    fp << "! ACP terms in training set: " << a.size() << std::endl;
    fp << "! Data points in training set: " << nrows << std::endl;
    if (!maxc.empty())
      fp << "! Maximum coefficients applied" << std::endl;
    fp << std::fixed << std::setprecision(4);
    fp << "! norm-1 = " << a.norm1() << std::endl;
    fp << "! norm-2 = " << a.norm2() << std::endl;
    fp << "! norm-inf = " << a.norminf() << std::endl;
    fp << "! wrms = " << wrms << std::endl;

    // write the ACP body and close
    a.writeacp_gaussian(fp);
    fp.close();

    printf("%4d  %10.5f  %10.5f  %10.5f  %10.5f  %10.5f  %4d  %s.acp\n",i+1,lam[i],a.norm1(),
	   a.norm2(),a.norminf(),wrms,a.size(),name.c_str());
  }
  printf("\n");
#endif
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
  db->write_structures(os,kmap,a,smap,zat,symbol,termstring,lmax,exp,exprn);
}

// Read data for the training set or one of its subsets from a file,
// then compare to reference method refm.
void trainset::read_and_compare(std::ostream &os, std::unordered_map<std::string,std::string> &kmap){
  if (!db || !(*db))
    throw std::runtime_error("A database file must be connected before using COMPARE");
  if (!isdefined())
    throw std::runtime_error("The training set needs to be defined before using COMPARE with TRAINING");

  // set
  auto im = kmap.find("TRAINING");
  if (im == kmap.end())
    throw std::runtime_error("write_structures in trainset called but no TRAINING keyword");

  // run the comparison with training set restriction
  db->read_and_compare(os,kmap,0);
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
