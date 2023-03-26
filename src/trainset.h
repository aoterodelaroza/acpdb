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
#include "config.h"
#ifdef CEREAL_FOUND
#include <cereal/archives/binary.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/string.hpp>
#endif

// A class for training sets.
class trainset {

 public:

  // Constructor
  trainset() : db(nullptr), ntot(0), refid(0), emptyid(0) {};

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

  // Describe the current training set. If except_on_undefined, throw
  // an exception if the training set is not fully defined. If full,
  // check if all the data for the training set is available - in this
  // case, the routine sets the complete flag. If quiet, write only a
  // few messages to the output.
  void describe(std::ostream &os, bool except_on_undefined, bool full, bool quiet);

  // Insert data in bulk into the database using data files from
  // previous ACP development programs using this training set as
  // template
  void insert_olddat(std::ostream &os, const std::string &directory="./");

  // Write training set data to data files in old-style format
  void write_olddat(std::ostream &os, const std::string &directory="./");

  // Is the training set defined?
  inline bool isdefined() const{
    return !zat.empty() && !lmax.empty() && !exp.empty() && !setid.empty() && !setpptyid.empty() &&
      !w.empty() && !refname.empty() && !emptyname.empty();
  }

  // Write the din files in the training set, in the given directory.
  void write_din(const std::string &directory="") const;

  // Evaluate an ACP on the current training set
  void eval_acp(std::ostream &os, const acp &a);

  // Evaluate an ACP and compare to an external file; choose the systems
  // with maximum deviation (non-linearity error) for each atom in the TS.
  void maxcoef(std::ostream &os, const std::unordered_map<std::string,std::string> &kmap);

  // Save the current training set to the database
  void savedb(std::string &name) const;

  // Load the training set from the database
  void loaddb(std::string &name);

  // Delete a training set from the database (or all the t.s.)
  void deletedb(std::string &name) const;

  // List training sets from the database
  void listdb(std::ostream &os) const;

  // Write the octavedump.dat file
  void dump(std::ostream &os, const std::string &keyw="");

  // Write input files or structure files for the training set
  // structures. Pass the options other than TRAINING and the ACP to the
  // structure writers.
  void write_structures(std::ostream &os, std::unordered_map<std::string,std::string> &kmap, const acp &a);

  // Read data for the training set or one of its subsets from a file,
  // then compare to reference method refm.
  void read_and_compare(std::ostream &os, std::unordered_map<std::string,std::string> &kmap);

  // Returns true if the argument is a known set alias
  inline bool isalias(const std::string &str) const{
    return (std::find(alias.begin(),alias.end(),str) != alias.end());
  }

  // Returns the set name corresponding to the alias in argument
  // string, or empty string if no such alias exist.
  inline std::string alias_to_setname(const std::string &str) const{
    auto it = std::find(alias.begin(),alias.end(),str);
    if (it == alias.end())
      return "";
    else
      return setname[it - alias.begin()];
  }

  // accesors
  const std::vector<unsigned char> &get_zat() const { return zat; };
  const std::vector<unsigned char> &get_lmax() const { return lmax; };
  const std::vector<double> &get_exp() const { return exp; };

 private:

  // Insert a subset into the Training_set table
  void insert_subset_db(int sid);

  //// Variables ////

  sqldb *db; // Database pointer

  enum completetype { c_unknown, c_no, c_yes };
  completetype complete = c_unknown; // whether the training set is complete

  int ntot; // Total number of properties in the training set

  std::vector<int> propid; // propids for the elements in the training set

  std::vector<unsigned char> zat;  // atomic numbers for the atoms
  std::vector<unsigned char> lmax; // maximum angular momentum channel for the atoms
  std::vector<double> exp; // exponents

  std::vector<std::string> alias; // alias of the sets
  std::vector<std::string> setname; // name of the sets
  std::vector<int> setid; // IDs of the sets
  std::vector<int> setpptyid; // property_type of the sets

  std::vector<int> set_initial_idx; // initial index of each set
  std::vector<int> set_final_idx; // final index of each set
  std::vector<int> set_size; // size of each set (properties)
  std::vector<bool> set_dofit; // whether this set will be used in the least-squares fit
  std::vector<double> w; // weights for the set elements

  std::string refname; // name of the reference method
  int refid; // ID of the reference method

  std::string emptyname; // name of the empty method
  int emptyid; // ID of the empty method

  std::vector<std::string> addname; // names of the additional methods
  std::vector<bool> addisfit; // whether the additional method has FIT or not
  std::vector<int> addid; // IDs of the additional methods

  // serialization
#ifdef CEREAL_FOUND
  friend class cereal::access;
  template<class Archive> void serialize(Archive & archive){
    archive(ntot,propid,zat,lmax,exp,alias,setname,setid,setpptyid,set_initial_idx,set_final_idx,
            set_size,set_dofit,w,refname,refid,emptyname,emptyid,addname,addisfit,addid);
  }
#endif
};

#endif
