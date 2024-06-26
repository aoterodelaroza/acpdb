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

#ifndef ACP_H
#define ACP_H

#include <vector>
#include <iostream>
#include <string>
#include <list>
#include <cmath>
#include <unordered_map>

// A class for ACPs.
class acp {

 public:

  typedef char symbol[6];

  // Structures
  struct term {
    int block;
    unsigned char atom;
    symbol sym;
    unsigned char l;
    double exp;
    int exprn;
    double coef;
  };

  //// Operators ////

  // constructors
  acp() : t{}, name{} {};
  acp(const std::string &name_, const std::string &filename);
  acp(const std::string &name_, std::vector<term> &t_) : name(name_), t(t_) {};
  acp(const std::string &name_, term t_) : name(name_), t{} { t.push_back(t_); };

  // bool operator
  operator bool() const { return (t.size() > 0); }

  // Write the ACP to an output stream (Gaussian-style version). If
  // usenblock, use the block number instead of the atomic name. If
  // usesym, use the atomic symbol. Final newline is omitted.
  void writeacp_gaussian(std::ostream &os, bool usenblock = false, bool usesym = false) const;

  // Write the ACP to a file (Gaussian-style version).
  void writeacp_gaussian(const std::string &filename, bool usenblock = false, bool usesym = false) const;

  // Write the ACP to an output stream (crystal-style version).
  void writeacp_crystal(std::ostream &os) const;

  // Calculate the 1-norm of the coefficients
  inline double norm1() const {
    double res = 0;
    for (int i=0; i<t.size(); i++)
      res += std::abs(t[i].coef);
    return res;
  };

  // Calculate the 2-norm of the coefficients
  inline double norm2() const {
    double res = 0;
    for (int i=0; i<t.size(); i++)
      res += t[i].coef*t[i].coef;
    return std::sqrt(res);
  };

  // Calculate the inf-norm of the coefficients
  inline double norminf() const {
    double res = 0;
    for (int i=0; i<t.size(); i++)
      res = std::max(res,std::abs(t[i].coef));
    return res;
  };

  // Get ACP size
  inline int size() const { return t.size(); };

  // Get ACP term i
  inline term get_term(int i) const { return t[i]; };

  // Get ACP name
  inline std::string get_name() const { return name; };

 private:
  std::string name;
  std::vector<term> t;
};

#endif
