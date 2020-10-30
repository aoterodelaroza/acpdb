// -*- c++-mode -*-
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

#ifndef TRAINSET_H
#define TRAINSET_H

#include <string>
#include <list>
#include <vector>
#include <numeric>
#include "sqldb.h"
#include "sqlite3.h"

// A class for training sets.
class trainset {

 public:

  // Add atoms and max. angular momentum
  void addatoms(const std::list<std::string> &tokens);

  // Add exponents
  void addexp(const std::list<std::string> &tokens);

  // Add sets
  void addset(sqldb &db, const std::list<std::string> &tokens);

  // Set the reference method
  void setreference(sqldb &db, const std::list<std::string> &tokens);

  // Set the empty method
  void setempty(sqldb &db, const std::list<std::string> &tokens);

  // Add an additional method
  void addadditional(sqldb &db, const std::list<std::string> &tokens);

  // Set the weights
  void setweight(sqldb &db, const std::list<std::string> &tokens, std::unordered_map<std::string,std::string> &kmap);

  // Describe the current training set
  void describe(std::ostream &os, sqldb &db);

  // Is the training set defined?
  inline bool isdefined(){
    return !zat.empty() && !lmax.empty() && !exp.empty() && !setid.empty() && 
      !w.empty() && !methodid.empty() && !emptyname.empty();
  }

  // Write the structures in the training set as xyz files
  void write_xyz(sqldb &db, const std::list<std::string> &tokens);

  // Write the din files in the training set
  void write_din(sqldb &db, const std::list<std::string> &tokens);

 private:

  // Set the weights for one set from the indicated parameters.
  void setweight_onlyone(sqldb &db, int sid, double wglobal, std::vector<double> wpattern, bool norm_ref, bool norm_nitem, bool norm_nitemsqrt, std::vector<std::pair<int,double> > witem);

  std::vector<unsigned char> zat;  // atomic numbers for the atoms
  std::vector<unsigned char> lmax; // maximum angular momentum channel for the atoms
  std::vector<double> exp; // exponents

  std::vector<std::string> setname; // name of the sets
  std::vector<int> setid; // IDs of the sets

  std::vector<int> set_initial_idx; // initial index of each set
  std::vector<int> set_final_idx; // final index of each set
  std::vector<int> set_size; // size of each set
  std::vector<double> w; // weights for the set elements

  std::vector<std::string> methodname; // name of the reference method for each set
  std::vector<int> methodid; // ID of the reference method for each set

  std::string emptyname; // name of the empty method
  int emptyid; // ID of the empty method

  std::vector<std::string> addname; // names of the additional methods
  std::vector<bool> addisfit; // whether the additional method has FIT or not
  std::vector<int> addid; // IDs of the additional methods
};

#endif
