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

#include <iostream>
#include <iomanip>
#include <sstream>
#include <map>
#include "strtemplate.h"
#include "parseutils.h"

// 1: t_string, t_basename, t_cell, t_charge, t_mult, t_nat, t_vaspxyz, t_xyz
static const std::vector<std::string> tokenname = { // keyword names for printing
  "string","basename","cell","charge","mult","nat","vaspxyz","xyz"
};
static const std::vector<std::string> tokenstr = { // strings for the keywords
  "","%basename%","%cell%","%charge%","%mult%","%nat%","%vaspxyz%","%xyz%"
};
static const int ntoken = tokenstr.size();

strtemplate::strtemplate(const std::string &source){
  // initialize the token list
  tl.push_back({t_string,source});

  // iterate over token types
  for (int i = 0; i < ntoken; i++){
    if (i == t_string) continue;
    std::string keyw = tokenstr[i];

    // iterate over list and break it up
    std::list<template_token>::iterator it;
    for (it = tl.begin(); it != tl.end(); it++){
      if (it->token != t_string) continue;

      size_t pos;
      while ((pos = it->str.find(keyw)) != std::string::npos){
        if (pos > 0)
          tl.emplace(it,template_token({t_string,it->str.substr(0,pos)}));
        tl.emplace(it,template_token({(tokentypes) i,""}));
        if (it->str.size() == pos+keyw.size()){
          it = tl.erase(it);
          break;
        } else
          it->str.erase(0,pos+keyw.size());
      }
    }
  }
}

// Apply a string to the template and write to an output stream
std::string strtemplate::apply(const structure &s) const {

  std::string result;

  // run over tokens
  for (auto it = tl.begin(); it != tl.end(); it++){
    if (it->token == t_string){
      result.append(it->str);

    } else if (it->token == t_basename){
      result.append(s.get_name());

    } else if (it->token == t_cell){
      const double *r = s.get_r();

      std::stringstream ss;
      ss << std::fixed << std::setprecision(8);
      for (int i = 0; i < 3; i++)
        ss << r[3*i+0] << " " << r[3*i+1] << " " << r[3*i+2] << std::endl;
      result.append(ss.str());

    } else if (it->token == t_charge){
      result.append(std::to_string(s.get_charge()));

    } else if (it->token == t_mult){
      result.append(std::to_string(s.get_mult()));

    } else if (it->token == t_nat){
      result.append(std::to_string(s.get_nat()));

    } else if (it->token == t_vaspxyz){
      int nat = s.get_nat();
      const unsigned char *z = s.get_z();
      const double *x = s.get_x();

      // build the maps for the atomic orderings
      std::map<unsigned char,int> ntyp;
      std::map<unsigned char,std::list<int>> ityp;
      for (int i = 0; i < nat; i++){
        ntyp[z[i]]++;
        ityp[z[i]].push_back(i);
      }

      std::stringstream ss;

      // write the atom types and numbers
      for (auto it = ntyp.begin(); it != ntyp.end(); it++)
        ss << nameguess(it->first) << (it != ntyp.end()?" ":"");
      ss << std::endl;
      for (auto it = ntyp.begin(); it != ntyp.end(); it++)
        ss << it->second << (it != ntyp.end()?" ":"");
      ss << std::endl;
      ss << "Direct" << std::endl;

      // write the atomic coordinates
      for (auto it = ityp.begin(); it != ityp.end(); it++){
        for (auto ia = it->second.begin(); ia != it->second.end(); ia++){
          ss << x[3*(*ia)+0] << " " << x[3*(*ia)+1] << " " << x[3*(*ia)+2];
          if (std::next(ia) != it->second.end()) ss << std::endl;
        }
      }

      result.append(ss.str());

    } else if (it->token == t_xyz){
      int nat = s.get_nat();
      const unsigned char *z = s.get_z();
      const double *x = s.get_x();

      std::stringstream ss;
      ss << std::fixed << std::setprecision(8);
      for (int i = 0; i < nat; i++){
        ss << nameguess(z[i]) << " " << x[3*i+0] << " " << x[3*i+1] << " " << x[3*i+2];
        if (i < nat-1) ss << std::endl;
      }
      result.append(ss.str());
    }
  }

  return result;
}

// Print the contents of the template to stdout. For debugging purposes.
void strtemplate::print(){
  std::cout << "#### dumping template contents ####" << std::endl;
  std::cout << "number of elements: " << tl.size() << std::endl;

  int n = 0;
  for (auto it = tl.begin(); it != tl.end(); it++){
    std::cout << "#token " << ++n << " : " << tokenname[it->token];
    if (it->token == t_string){
      std::cout << ", content-->" << it->str << "<--endcontent";
    }
    std::cout << std::endl;
  }
}
