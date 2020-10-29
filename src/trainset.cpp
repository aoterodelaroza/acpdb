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

#include "trainset.h"
#include "parseutils.h"
#include <iostream>
#include <algorithm>
#include <unordered_map>

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

  for (auto it = tokens.begin(); it != tokens.end(); it++) {
    std::string name = *it;

    int idx = db.find_id_from_key(name,statement::STMT_QUERY_SET);
    if (idx == 0)
      throw std::runtime_error("SET identifier not found in database: " + name);
    
    setname.push_back(name);
    setid.push_back(idx);
  }
}

// Set the reference method
void trainset::setreference(sqldb &db, const std::list<std::string> &tokens){
  if (!db) 
    throw std::runtime_error("A database file must be connected before using SET");
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

