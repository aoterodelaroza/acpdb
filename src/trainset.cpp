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
#include <unordered_map>

const std::unordered_map<std::string, int> ltoint { 
   {"l",0}, {"s",1}, {"p",2}, {"d",3}, {"f",4}, {"g",5}, {"h",6}, 
};

void trainset::addatoms(std::list<std::string> &tokens){

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

