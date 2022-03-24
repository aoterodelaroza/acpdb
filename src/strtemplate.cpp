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
#include <cmath>
#include "strtemplate.h"
#include "parseutils.h"
#include "globals.h"

// t_string, t_basename, t_cell, t_cellbohr, t_cell_lengths, t_cell_angles,
// t_charge, t_mult, t_nat, t_ntyp, t_xyz,
// t_xyzatnum, t_xyzatnum200, t_vaspxyz, t_qexyz,
// t_acpgau, t_acpcrys
// t_term_atsymbol, t_term_atsymbol_lstr_gaussian, t_term_atnum, t_term_lstr,
// t_term_lnum, t_term_exp, t_term_coef
// t_term_loop, t_term_endloop
static const std::vector<std::string> tokenname = { // keyword names for printing
  "string","basename","cell","cellbohr","cell_lengths","cell_angles",
  "charge","mult","nat","ntyp","xyz",
  "xyzatnum","xyzatnum200","vaspxyz","qexyz",
  "acpgau", "acpcrys",
  "term_atsymbol", "term_atsymbol_lstr_gaussian", "term_atnum",
  "term_lstr", "term_lnum", "term_exp", "term_coef",
  "term_loop", "term_endloop"
};
static const std::vector<std::string> tokenstr = { // strings for the keywords (if unterminated, optionally expect something else)
  "","%basename%","%cell%","%cellbohr%","%cell_lengths%","%cell_angles%",
  "%charge%","%mult%","%nat%","%ntyp%","%xyz%",
  "%xyzatnum%","%xyzatnum200%","%vaspxyz%","%qexyz%",
  "%acpgau","%acpcrys",
  "%term_atsymbol%","%term_atsymbol_lstr_gaussian%","%term_atnum%",
  "%term_lstr%","%term_lnum%","%term_exp%","%term_coef%",
  "%term_loop%","%term_endloop%"
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

      size_t pos0, pos1;
      while ((pos0 = it->str.find(keyw)) != std::string::npos){
        pos1 = pos0 + keyw.size();
        if (pos0 > 0)
          tl.emplace(it,template_token({t_string,it->str.substr(0,pos0)}));

        std::string arg = "";
        if (tokenstr[i][tokenstr[i].size()-1] != '%'){
	  size_t pos2 = it->str.find('%',pos1);
	  if (it->str[pos1] == ':'){
	    arg = it->str.substr(pos1+1,pos2-pos1-1);
	    pos1 = pos2+1;
	  } else {
	    pos1 = pos2+1;
	  }
        }

        tl.emplace(it,template_token({(tokentypes) i,arg}));
        if (i == t_term_loop) hasloop_ = true;
        if (it->str.size() == pos1){
          it = tl.erase(it);
          break;
        } else
          it->str.erase(0,pos1);
      }
    }
  }
}

// Apply a string to the template and write to an output stream
std::string strtemplate::apply(const structure &s, const acp& a, const unsigned char zat, const unsigned char l, 
			       const double exp, const double coef) const {

  std::string result;

  // run over tokens
  for (auto it = tl.begin(); it != tl.end(); it++){
    if (it->token == t_string){
      result.append(it->str);

    } else if (it->token == t_basename){
      result.append(s.get_name());

    } else if (it->token == t_cell || it->token == t_cellbohr){
      const double *r = s.get_r();

      double scale = 1.;
      if (it->token == t_cellbohr) scale = globals::ang_to_bohr;

      std::stringstream ss;
      ss << std::fixed << std::setprecision(8);
      for (int i = 0; i < 3; i++){
        ss << r[3*i+0]*scale << " " << r[3*i+1]*scale << " " << r[3*i+2]*scale;
        if (i < 2) ss << std::endl;
      }
      result.append(ss.str());

    } else if (it->token == t_cell_lengths) {
      const double *r = s.get_r();

      std::stringstream ss;
      ss << std::fixed << std::setprecision(8);
      for (int i = 0; i < 3; i++){
        double len2 = 0;
        for (int j = 0; j < 3; j++)
          len2 += r[3*i+j] * r[3*i+j];
        ss << std::sqrt(len2);
        if (i < 2) ss << " ";
      }
      result.append(ss.str());

    } else if (it->token == t_cell_angles) {
      const double *r = s.get_r();

      double len[3] = {0.0, 0.0, 0.0};
      for (int i = 0; i < 3; i++){
        for (int j = 0; j < 3; j++)
          len[i] += r[3*i+j] * r[3*i+j];
        len[i] = std::sqrt(len[i]);
      }
      double ang[3];
      ang[0] = acos((r[3*1+0] * r[3*2+0] + r[3*1+1] * r[3*2+1] + r[3*1+2] * r[3*2+2]) / len[1] / len[2]) * 180. / M_PI;
      ang[1] = acos((r[3*0+0] * r[3*2+0] + r[3*0+1] * r[3*2+1] + r[3*0+2] * r[3*2+2]) / len[0] / len[2]) * 180. / M_PI;
      ang[2] = acos((r[3*0+0] * r[3*1+0] + r[3*0+1] * r[3*1+1] + r[3*0+2] * r[3*1+2]) / len[0] / len[1]) * 180. / M_PI;

      std::stringstream ss;
      ss << std::fixed << std::setprecision(8);
      for (int i = 0; i < 3; i++){
        ss << ang[i];
        if (i < 2) ss << " ";
      }

      result.append(ss.str());

    } else if (it->token == t_charge){
      result.append(std::to_string(s.get_charge()));

    } else if (it->token == t_mult){
      result.append(std::to_string(s.get_mult()));

    } else if (it->token == t_nat){
      result.append(std::to_string(s.get_nat()));

    } else if (it->token == t_ntyp){
      std::map<unsigned char,int> ntyp;
      int nat = s.get_nat();
      const unsigned char *z = s.get_z();
      for (int i = 0; i < nat; i++)
        ntyp[z[i]] = 1;

      result.append(std::to_string(ntyp.size()));

    } else if (it->token == t_xyz || it->token == t_xyzatnum || it->token == t_xyzatnum200){
      int nat = s.get_nat();
      const unsigned char *z = s.get_z();
      const double *x = s.get_x();

      std::stringstream ss;
      ss << std::fixed << std::setprecision(8);
      for (int i = 0; i < nat; i++){
        if (it->token == t_xyz)
          ss << nameguess(z[i]);
        else if (it->token == t_xyzatnum)
          ss << (int) z[i];
        else if (it->token == t_xyzatnum200)
          ss << ((int) z[i]+200);
        ss << " " << x[3*i+0] << " " << x[3*i+1] << " " << x[3*i+2];
        if (i < nat-1) ss << std::endl;
      }
      result.append(ss.str());

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
      ss << std::fixed << std::setprecision(8);
      for (auto it = ityp.begin(); it != ityp.end(); it++){
        for (auto ia = it->second.begin(); ia != it->second.end(); ia++){
          ss << x[3*(*ia)+0] << " " << x[3*(*ia)+1] << " " << x[3*(*ia)+2];
          if (std::next(ia) != it->second.end() || std::next(it) != ityp.end()) ss << std::endl;
        }
      }

      result.append(ss.str());

    } else if (it->token == t_qexyz){
      int nat = s.get_nat();
      const unsigned char *z = s.get_z();
      const double *x = s.get_x();

      std::map<unsigned char,int> ntyp;
      for (int i = 0; i < nat; i++)
        ntyp[z[i]] = 1;

      std::stringstream ss;

      // write the atomic species block
      ss << "ATOMIC_SPECIES" << std::endl;
      for (auto it = ntyp.begin(); it != ntyp.end(); it++){
        std::string atsym = nameguess(it->first);
        ss << atsym << " " << std::fixed << std::setprecision(6) << globals::atmass[it->first] << " " << atsym << ".UPF" << std::endl;
      }
      ss << std::endl;

      // write the atomic positions block
      ss << "ATOMIC_POSITIONS crystal" << std::endl;
      ss << std::fixed << std::setprecision(8);
      for (int i = 0; i < nat; i++){
        ss << nameguess(z[i]) << " " << x[3*i+0] << " " << x[3*i+1] << " " << x[3*i+2];
        if (i < nat-1) ss << std::endl;
      }

      result.append(ss.str());
    } else if (it->token == t_acpgau || it->token == t_acpcrys){
      unsigned char zat;
      if (it->str.empty())
        zat = 0;
      else{
        zat = zatguess(it->str);
        if (zat <= 0)
          throw std::runtime_error("Unknown atom expanding %acpgau% template");
      }
      std::stringstream ss;
      if (it->token == t_acpgau)
        a.writeacp_gaussian(ss,zat);
      else
        a.writeacp_crystal(ss,zat);
      result.append(ss.str());
    } else if (it->token == t_term_atnum) {
      result.append(std::to_string(zat));
    } else if (it->token == t_term_atsymbol) {
      result.append(nameguess(zat));
    } else if (it->token == t_term_atsymbol_lstr_gaussian) {
      std::stringstream ss;
      ss << nameguess(zat) << " " << (int) l << " 0" << std::endl;
      for (unsigned char i = 0; i < l; i++){
	ss << globals::inttol[i] << std::endl;
	ss << "0" << std::endl;
      }
      ss << globals::inttol[l];
      result.append(ss.str());
    } else if (it->token == t_term_lnum) {
      result.append(std::to_string(l));
    } else if (it->token == t_term_lstr) {
      result += globals::inttol[l];
    } else if (it->token == t_term_exp) {
      std::stringstream ss;
      ss << std::fixed << std::setprecision(8) << exp;
      result.append(ss.str());
    } else if (it->token == t_term_coef) {
      std::stringstream ss;
      ss << std::fixed << std::setprecision(8) << coef;
      result.append(ss.str());
    } else if (it->token == t_term_loop) {
      throw std::runtime_error("Cannot use a loop in template.apply()");
    } else if (it->token == t_term_loop) {
      throw std::runtime_error("Cannot use a loop in template.apply()");
    }
  }

  return result;
}

// Apply a string to the template and write to an output stream, with loops
void strtemplate::expand_loop(const std::vector<unsigned char> &zat,
                              const std::vector<unsigned char> &l,
                              const std::vector<double> &exp,
                              const std::vector<double> &coef) {
  std::list<template_token> tl_loc, tl_repeat;
  bool inloop = false;

  // run over tokens
  for (auto it = tl.begin(); it != tl.end(); it++){
    // start the loop
    if (it->token == t_term_loop){
      if (inloop)
        throw std::runtime_error("Nested term loops are not allowed (found term_loop inside term_loop)");
      inloop = true;
      tl_repeat.clear();
    } else if (it->token == t_term_endloop){
      // end the loop
      if (!inloop)
        throw std::runtime_error("Tried to end term loop when not inside loop");
      // expand
      for (int iz = 0; iz < zat.size(); iz++){
        for (int iexp = 0; iexp < exp.size(); iexp++){
	  for (int icoef = 0; icoef < coef.size(); icoef++){
	    for (auto itr = tl_repeat.begin(); itr != tl_repeat.end(); itr++){
	      if (itr->token == t_term_atnum) {
		tl_loc.push_back(template_token({t_string,std::to_string(zat[iz])}));
	      } else if (itr->token == t_term_atsymbol) {
		tl_loc.push_back(template_token({t_string,nameguess(zat[iz])}));
	      } else if (itr->token == t_term_atsymbol_lstr_gaussian) {
		std::stringstream ss;
		ss << nameguess(zat[iz]) << " " << (int) l[iz] << " 0" << std::endl;
		for (unsigned char i = 0; i < l[iz]; i++){
		  ss << globals::inttol[i] << std::endl;
		  ss << "0" << std::endl;
		}
		ss << globals::inttol[l[iz]];
		tl_loc.push_back(template_token({t_string,ss.str()}));
	      } else if (itr->token == t_term_lnum) {
		tl_loc.push_back(template_token({t_string,std::to_string(l[iz])}));
	      } else if (itr->token == t_term_lstr) {
		tl_loc.push_back(template_token({t_string,std::string(1,globals::inttol[l[iz]])}));
	      } else if (itr->token == t_term_exp) {
		std::stringstream ss;
		ss << std::fixed << std::setprecision(8) << exp[iexp];
		tl_loc.push_back(template_token({t_string,ss.str()}));
	      } else if (itr->token == t_term_coef) {
		std::stringstream ss;
		ss << std::fixed << std::setprecision(8) << coef[icoef];
		tl_loc.push_back(template_token({t_string,ss.str()}));
	      } else {
		tl_loc.push_back(*itr);
	      }
	    }
          }
        }
      }
      tl_repeat.clear();
      inloop = false;
    } else if (inloop) {
      tl_repeat.push_back(*it);
    } else {
      tl_loc.push_back(*it);
    }
  }
  if (inloop)
    throw std::runtime_error("Term loop did not have a termination");

  // update with the new template
  tl = tl_loc;
  hasloop_ = false;
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
