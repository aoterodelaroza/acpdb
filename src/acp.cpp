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

#include "acp.h"
#include "parseutils.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <limits>
#include <map>
#include <cmath>
#include <cstring>

const static std::unordered_map<std::string, int> ltoint {
   {"l",0}, {"s",1}, {"p",2}, {"d",3}, {"f",4}, {"g",5}, {"h",6},
};
const static std::vector<unsigned char> inttol = {'l','s','p','d','f','g','h'};

// ACP class
acp::acp(const std::string &name_, const std::string &filename){
  std::ifstream ifile(filename,std::ios::in);
  if (ifile.fail())
      throw std::runtime_error("Error opening ACP file " + filename);

  int count = 0;
  std::string line, str;
  while (get_next_line(ifile,line,'!','\0')){
    if (ifile.fail())
      throw std::runtime_error("Error reading ACP file " + filename);

    // process the atom
    std::istringstream iss(line);
    iss >> str;
    if (str[0] == '-')
      str = str.substr(1,str.size()-1);
    acp::term t_;
    t_.atom = zatguess(str);
    if (t_.atom == 0)
      throw std::runtime_error("Unknown atom: " + str);

    // get the number of blocks
    int nblock;
    ifile >> str >> nblock;
    ifile.ignore(std::numeric_limits<std::streamsize>::max(),'\n');
    std::strcpy(&(t_.sym[0]),str.substr(0,5).c_str());
    t_.block = count++;

    for (int i = 0; i <= nblock; i++){
      // read the block corresponding to angular momentum i
      ifile >> str;
      lowercase(str);
      if (ltoint.find(str) == ltoint.end())
	throw std::runtime_error("Unknown angular momentum symbol: " + str);
      t_.l = ltoint.at(str);

      int nterm;
      ifile >> nterm;
      for (int j = 0; j < nterm; j++){
	ifile >> str >> t_.exp >> t_.coef;
	t.push_back(t_);
      }
    }
  }
  name = name_;
}

acp::acp(const std::string &name_, std::istream &is){
  std::string line, str;
  while (get_next_line(is,line)){
    std::istringstream iss(line);
    iss >> str;
    uppercase(str);
    if (str == "END") break;

    acp::term t_;
    t_.atom = zatguess(str);
    if (t_.atom == 0)
      throw std::runtime_error("Unknown atom: " + str);

    iss >> str;
    if (ltoint.find(str) == ltoint.end())
      throw std::runtime_error("Unknown angular momentum symbol: " + str);
    t_.l = ltoint.at(str);

    iss >> t_.exp >> t_.coef;
    t.push_back(t_);
  }
  name = name_;
}

// Write the ACP to output stream os (human-readable version).
void acp::writeacp_text(std::ostream &os) const{
  if (t.empty()) return;

  os << "* Terms for ACP " + name << std::endl;

  os << "| id | atom | l | exponent | coefficient |" << std::endl;
  std::streamsize prec = os.precision(10);
  for (int i = 0; i < t.size(); i++){
    os << "| " << i << " | " << nameguess(t[i].atom) << " | " << inttol[t[i].l] << " | "
       << t[i].exp << " | " << t[i].coef << " |" << std::endl;
  }
  os << std::endl;
  os.precision(prec);
}

// Write the ACP to a file (Gaussian-style version). Final newline is omitted.
void acp::writeacp_gaussian(const std::string &filename,bool usenblock/*=false*/,bool usesym/*=false*/) const{
  if (t.empty()) return;

  std::ofstream ofile(filename,std::ios::trunc);
  if (ofile.fail())
    throw std::runtime_error("Error opening ACP file for write: " + filename);

  writeacp_gaussian(ofile,usenblock,usesym);
  if (ofile.fail())
    throw std::runtime_error("Error writing ACP file: " + filename);
}

// Write the ACP to an output stream (Gaussian-style version).
void acp::writeacp_gaussian(std::ostream &os, bool usenblock/*=false*/,bool usesym/*=false*/) const{
  if (t.empty()) return;

  // run over the terms in the ACP and write the atom types, lmax, and number of terms
  std::map<int,unsigned char> lmax;
  std::unordered_map<int,std::vector< std::vector<int> > > iterm;

  // classify the terms
  int nblock = 0;
  for (int i=0; i<t.size(); i++){
    if (lmax.find(t[i].block) == lmax.end())
      lmax[t[i].block] = t[i].l;
    else
      lmax[t[i].block] = std::max(lmax[t[i].block],t[i].l);
    if (iterm.find(t[i].block) == iterm.end())
      iterm[t[i].block] = {};
    if (iterm[t[i].block].size() <= t[i].l)
      iterm[t[i].block].resize(t[i].l+1,{});
    iterm[t[i].block][t[i].l].push_back(i);
    nblock = std::max(nblock,t[i].block+1);
  }

  // write the acp
  os << std::scientific;
  std::streamsize prec = os.precision(15);

  for (int i1 = 0; i1 < nblock; i1++){
    if (usenblock)
      os << i1+1 << " 0";
    else if (usesym)
      os << t[iterm[i1][0][0]].sym << " 0";
    else
      os << "-" << nameguess(t[iterm[i1][0][0]].atom) << " 0";
    os << std::endl << t[iterm[i1][0][0]].sym << " " << iterm[i1].size()-1 << " 0";
    for (int i2 = 0; i2 < iterm[i1].size(); i2++){
      os << std::endl << inttol[i2];
      os << std::endl << iterm[i1][i2].size();
      for (int i = 0; i < iterm[i1][i2].size(); i++)
	os << std::endl << "2 " << t[iterm[i1][i2][i]].exp << " " << t[iterm[i1][i2][i]].coef;
    }
    os << std::endl;
  }

  os.precision(prec);
  os << std::defaultfloat;
}

// Write the ACP to an output stream (crystal-style version).
void acp::writeacp_crystal(std::ostream &os) const{
  if (t.empty()) return;

  // run over the terms in the ACP and write the atom types, lmax, and number of terms
  std::map<unsigned char,unsigned char> lmax;
  std::unordered_map<unsigned char,std::vector< std::vector<int> > > iterm;

  // classify the terms
  for (int i=0; i<t.size(); i++){
    if (lmax.find(t[i].atom) == lmax.end())
      lmax[t[i].atom] = t[i].l;
    else
      lmax[t[i].atom] = std::max(lmax[t[i].atom],t[i].l);
    if (iterm.find(t[i].atom) == iterm.end())
      iterm[t[i].atom] = {};
    if (iterm[t[i].atom].size() <= t[i].l)
      iterm[t[i].atom].resize(t[i].l+1,{});
    iterm[t[i].atom][t[i].l].push_back(i);
  }

  // write the acp
  os << std::scientific;
  std::streamsize prec = os.precision(14);

  for (auto it = lmax.begin(); it != lmax.end(); it++){
    // header
    os << (int) it->first << ".";
    for (int i = 0; i < 6; i++){
      if (i <= it->second){
	if (iterm[it->first][i].size() > 0)
	  os << " " << iterm[it->first][i].size();
	else {
	  iterm[it->first][i].push_back(-1);
	  os << " " << 1;
	}
      } else
	os << " 0";
    }

    // body
    for (int l = 0; l <= it->second; l++){
      for (int i = 0; i < iterm[it->first][l].size(); i++){
	int idx = iterm[it->first][l][i];
	if (idx < 0)
	  os << std::endl << 1.0 << " " << 0.0 << " " << 0;
	else
	  os << std::endl << t[idx].exp << " " << t[idx].coef << " " << 0;
      }
    }
  }

  os.precision(prec);
  os << std::defaultfloat;
}

// Write info about the ACP to os
void acp::info(std::ostream &os) const{
  if (t.empty()) return;

  // run over the terms in the ACP and write the atom types, lmax, and number of terms
  std::map<unsigned char,unsigned char> lmax;
  std::unordered_map<unsigned char,std::vector<int> > nterm;

  // classify the terms
  double n1 = 0, n2 = 0, ninf = 0;
  for (int i=0; i<t.size(); i++){
    if (lmax.find(t[i].atom) == lmax.end())
      lmax[t[i].atom] = t[i].l;
    else
      lmax[t[i].atom] = std::max(lmax[t[i].atom],t[i].l);
    if (nterm.find(t[i].atom) == nterm.end())
      nterm[t[i].atom] = {};
    if (nterm[t[i].atom].size() <= t[i].l)
      nterm[t[i].atom].resize(t[i].l+1,0);
    nterm[t[i].atom][t[i].l]++;

    n1 += std::abs(t[i].coef);
    n2 += t[i].coef*t[i].coef;
    ninf = std::max(ninf,std::abs(t[i].coef));
  }
  n2 = std::sqrt(n2);

  std::streamsize prec = os.precision(5);
  os << std::scientific;
  os << "* INFO for ACP: " + name << std::endl;
  os << "+ Atoms (lmax) : ";
  for (auto it = lmax.begin(); it != lmax.end(); it++)
    os << nameguess(it->first) << "(" << inttol[it->second] << ") ";
  os << std::endl;
  os << "+ Number of terms : " << std::endl;
  for (auto it = lmax.begin(); it != lmax.end(); it++){
    os << nameguess(it->first) << ": ";
    for (int i = 0; i <= it->second; i++)
      os << inttol[i] << "=" << nterm[it->first][i] << " ";
    os << std::endl;
  }
  os << "1-norm = " << n1 << std::endl;
  os << "2-norm = " << n2 << std::endl;
  os << "inf-norm (max. abs(coef)) = " << ninf << std::endl;
  os << std::endl;
}

// Split the ACP into several ACPs, each with one term. Write them
// to files with template templ. If tokens contains the COEF
// keyword, use that coefficient for the new ACPs.
void acp::split(const std::string &templ, std::list<std::string> &tokens){
  if (t.empty()) return;

  bool havecoef = false;
  double coef;
  if (!tokens.empty()){
    havecoef = (popstring(tokens,true) == "COEF");
    if (havecoef) coef = std::stod(popstring(tokens));
  }

  // write the ACPs
  int d0 = digits(t.size()+1);
  for (int i = 0; i < t.size(); i++){
    int dd = digits(i+1);
    std::string name_ = templ + "-" + std::string(d0-dd,'0') + std::to_string(i+1);
    std::string filename_ = name_ + ".acp";

    if (havecoef){
      term t_ = t[i];
      t_.coef = coef;
      acp aa(name_, t_);
      aa.writeacp_gaussian(filename_);
    } else {
      acp aa(name_, t[i]);
      aa.writeacp_gaussian(filename_);
    }
  }
}
