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
#include "acp.h"
#include "sqldb.h"
#include "sqlite3.h"

// A class for training sets.
class trainset {

 public:

  // Constructor
  trainset() : db(nullptr), ntot(0) {};

  // Register the database and create the Training_set table
  void setdb(sqldb *db_);

  // Add atoms and max. angular momentum
  void addatoms(const std::list<std::string> &tokens);

  // Add exponents
  void addexp(const std::list<std::string> &tokens);

  // Add a subset (combination of set, mask, weights)
  void addsubset(const std::string &key, std::unordered_map<std::string,std::string> &kmap);

  // Set the reference method
  void setreference(const std::list<std::string> &tokens);

  // Set the empty method
  void setempty(const std::list<std::string> &tokens);

  // Add an additional method
  void addadditional(const std::list<std::string> &tokens);

  // Describe the current training set
  void describe(std::ostream &os);

  // Insert data in bulk into the database using data files from
  // previous ACP development programs using this training set as
  // template
  void insert_olddat(const std::string &directory, std::list<std::string> &tokens);

  // Is the training set defined?
  inline bool isdefined(){
    return !zat.empty() && !lmax.empty() && !exp.empty() && !setid.empty() && 
      !w.empty() && !refname.empty() && !emptyname.empty();
  }

  // Write the structures in the training set as xyz files
  void write_xyz(const std::list<std::string> &tokens);

  // Write the din files in the training set
  void write_din(const std::list<std::string> &tokens);

  // Evaluate an ACP on the current training set
  void eval_acp(std::ostream &os, const acp &a);
  

 private:

  // Set the weights for one set from the indicated parameters.
  void setweight_onlyone(int sid, double wglobal, std::vector<double> wpattern, bool norm_ref, bool norm_nitem, bool norm_nitemsqrt, std::vector<std::pair<int,double> > witem);

  //// Variables ////

  sqldb *db; // Database pointer

  int ntot; // Total number of properties in the training set

  std::vector<unsigned char> zat;  // atomic numbers for the atoms
  std::vector<unsigned char> lmax; // maximum angular momentum channel for the atoms
  std::vector<double> exp; // exponents

  std::vector<std::string> alias; // alias of the sets
  std::vector<std::string> setname; // name of the sets
  std::vector<int> setid; // IDs of the sets

  std::vector<int> set_initial_idx; // initial index of each set
  std::vector<int> set_final_idx; // final index of each set
  std::vector<int> set_size; // size of each set
  std::vector<bool> set_dofit; // whether this set will be used in the least-squares fit
  std::vector<double> w; // weights for the set elements

  std::string refname; // name of the reference method
  int refid; // ID of the reference method

  std::string emptyname; // name of the empty method
  int emptyid; // ID of the empty method

  std::vector<std::string> addname; // names of the additional methods
  std::vector<bool> addisfit; // whether the additional method has FIT or not
  std::vector<int> addid; // IDs of the additional methods
};

#endif
