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

#include "statement.h"
#include "sqldb.h"
#include "trainset.h"
#include "parseutils.h"
#include <iostream>
#include <algorithm>
#include <unordered_map>
#include <cmath>

const std::unordered_map<std::string, int> ltoint { 
   {"l",0}, {"s",1}, {"p",2}, {"d",3}, {"f",4}, {"g",5}, {"h",6}, 
};

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

