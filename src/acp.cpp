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

const static std::unordered_map<std::string, int> ltoint { 
   {"l",0}, {"s",1}, {"p",2}, {"d",3}, {"f",4}, {"g",5}, {"h",6}, 
};
const static std::vector<char> inttol = {'l','s','p','d','f','g','h'};

static std::istream &get_next_line(std::istream &is, std::string &line, char skipchar){
  std::string str;
  while (std::getline(is,line)){
    std::istringstream iss(line);
    iss >> str;
    if (str[0] != skipchar && !str.empty()) break;
  }
  return is;
}

acp::acp(const std::string name_, const std::string &filename){
  std::ifstream ifile(filename,std::ios::in);
  if (ifile.fail()) 
      throw std::runtime_error("Error opening ACP file " + filename);
  
  std::string line, str;
  while (get_next_line(ifile,line,'!')){
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

acp::acp(const std::string name_, std::istream &is){
  std::string line, str;
  while (get_next_line(is,line,'#')){
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
void acp::writeacp(std::ostream &os) const{
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

