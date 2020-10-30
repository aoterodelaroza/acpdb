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

#include "parseutils.h"
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <algorithm>

// Transform a string to uppercase
void uppercase(std::string &s){
  transform(s.begin(), s.end(), s.begin(), ::toupper);
}

// Transform a string to lowercase
void lowercase(std::string &s){
  transform(s.begin(), s.end(), s.begin(), ::tolower);
}

// List the words in an input line. Skip the rest of the line if a
// comment character (#) is found as the first character in a token.
std::list<std::string> list_all_words(const std::string &line) {

  std::istringstream iss(line);
  std::list<std::string> result;
  std::string token;

  while (iss >> token){
    if (token[0] == '#') break;
    result.push_back(token);
  }

  return result;
}  

// Read lines from input stream is. Split each line into a key (first
// word) and content (rest of the line) pair. If toupper, capitalize
// the key. If the key END or the eof() is found, return the map.
std::unordered_map<std::string,std::string> map_keyword_pairs(std::istream *is, bool toupper){

  std::unordered_map<std::string,std::string> result;
  std::string ukeyw, keyw, line;
  while(*is >> keyw){
    line.clear();
    getline(*is,line);

    if (keyw.empty() || keyw[0] == '#') continue;
    ukeyw = keyw;
    uppercase(ukeyw);
    if (ukeyw == "END") return result;
    line.erase(line.begin(), std::find_if(line.begin(),line.end(), std::bind1st(std::not_equal_to<char>(),' ')));
    if (toupper)
      result[ukeyw] = line;
    else
      result[keyw] = line;
  }
  throw std::runtime_error("Error scanning for END keyword");
  
  return result;
}

// Get and remove front from list of strings. If toupper is true,
// convert the string to uppercase.
std::string popstring(std::list<std::string> &list, bool toupper){
  if (list.empty()) return "";

  std::string result = list.front();
  list.pop_front();
  if (toupper)
    uppercase(result);
  return result;
}

// Compare two strings for equality regardless of case. Adapted from:
// https://stackoverflow.com/questions/12568338/c-check-case-insensitive-equality-of-two-strings
bool equali_strings(const std::string& a, const std::string& b)
{
  return a.size() == b.size() &&
    std::equal(a.begin(), a.end(), b.begin(), 
               [](char cA, char cB) { return ::toupper(cA) == ::toupper(cB); });
}

// Write a double to a string very precisely
std::string to_string_precise(double a) {
    std::ostringstream out;
    out.precision(16);
    out << std::scientific << a;
    return out.str();
}

// Atomic number from atomic name
unsigned char zatguess(std::string atsym){
  const std::unordered_map<std::string, unsigned char> an = {
    {"H" , 1  }, {"HE", 2  }, {"LI", 3  }, {"BE", 4  }, {"B" , 5  }, {"C" , 6  }, {"N" , 7  },
    {"O" , 8  }, {"F" , 9  }, {"NE", 10 }, {"NA", 11 }, {"MG", 12 }, {"AL", 13 }, {"SI", 14 },
    {"P" , 15 }, {"S" , 16 }, {"CL", 17 }, {"AR", 18 }, {"K" , 19 }, {"CA", 20 }, {"SC", 21 },
    {"TI", 22 }, {"V" , 23 }, {"CR", 24 }, {"MN", 25 }, {"FE", 26 }, {"CO", 27 }, {"NI", 28 },
    {"CU", 29 }, {"ZN", 30 }, {"GA", 31 }, {"GE", 32 }, {"AS", 33 }, {"SE", 34 }, {"BR", 35 },
    {"KR", 36 }, {"RB", 37 }, {"SR", 38 }, {"Y" , 39 }, {"ZR", 40 }, {"NB", 41 }, {"MO", 42 },
    {"TC", 43 }, {"RU", 44 }, {"RH", 45 }, {"PD", 46 }, {"AG", 47 }, {"CD", 48 }, {"IN", 49 },
    {"SN", 50 }, {"SB", 51 }, {"TE", 52 }, {"I" , 53 }, {"XE", 54 }, {"CS", 55 }, {"BA", 56 },
    {"LA", 57 }, {"CE", 58 }, {"PR", 59 }, {"ND", 60 }, {"PM", 61 }, {"SM", 62 }, {"EU", 63 },
    {"GD", 64 }, {"TB", 65 }, {"DY", 66 }, {"HO", 67 }, {"ER", 68 }, {"TM", 69 }, {"YB", 70 },
    {"LU", 71 }, {"HF", 72 }, {"TA", 73 }, {"W" , 74 }, {"RE", 75 }, {"OS", 76 }, {"IR", 77 },
    {"PT", 78 }, {"AU", 79 }, {"HG", 80 }, {"TL", 81 }, {"PB", 82 }, {"BI", 83 }, {"PO", 84 },
    {"AT", 85 }, {"RN", 86 }, {"FR", 87 }, {"RA", 88 }, {"AC", 89 }, {"TH", 90 }, {"PA", 91 },
    {"U" , 92 }, {"NP", 93 }, {"PU", 94 }, {"AM", 95 }, {"CM", 96 }, {"BK", 97 }, {"CF", 98 },
    {"ES", 99 }, {"FM", 100}, {"MD", 101}, {"NO", 102}, {"LR", 103}, {"RF", 104}, {"DB", 105},
    {"SG", 106}, {"BH", 107}, {"HS", 108}, {"MT", 109}, {"DS", 110}, {"RG", 111}, {"CN", 112},
    {"NH", 113}, {"FL", 114}, {"MC", 115}, {"LV", 116}, {"TS", 117}, {"OG", 118}, {"XN", 119},
    {"XB", 120}, {"XR", 121}, {"XC", 122}, {"XZ", 123},   
  };

  atsym = atsym.substr(0,2);
  uppercase(atsym);

  auto it = an.find(atsym);
  if (it != an.end())
    return it->second;
  else{
    it = an.find(atsym.substr(0,1));
    if (it != an.end())
      return it->second;
  }
  return 0;
}

// Atomic name from atomic number
std::string nameguess(unsigned char z){
  const std::vector<std::string> an = {
    "H" ,"He","Li","Be","B" ,"C" ,"N" ,"O" ,"F" ,"Ne",
    "Na","Mg","Al","Si","P" ,"S" ,"Cl","Ar","K" ,"Ca",
    "Sc","Ti","V" ,"Cr","Mn","Fe","Co","Ni","Cu","Zn",
    "Ga","Ge","As","Se","Br","Kr","Rb","Sr","Y" ,"Zr",
    "Nb","Mo","Tc","Ru","Rh","Pd","Ag","Cd","In","Sn",
    "Sb","Te","I" ,"Xe","Cs","Ba","La","Ce","Pr","Nd",
    "Pm","Sm","Eu","Gd","Tb","Dy","Ho","Er","Tm","Yb",
    "Lu","Hf","Ta","W" ,"Re","Os","Ir","Pt","Au","Hg",
    "Tl","Pb","Bi","Po","At","Rn","Fr","Ra","Ac","Th",
    "Pa","U" ,"Np","Pu","Am","Cm","Bk","Cf","Es","Fm",
    "Md","No","Lr","Rf","Db","Sg","Bh","Hs","Mt","Ds",
    "Rg","Cn","Nh","Fl","Mc","Lv","Ts","Og","Xn","Xb",
    "Xr","Xc","Xz"};

  if (z < 1 || z > an.size()) return "X";
  return an[z-1];
}

// Read a line from stream and get the first keyword (str) and maybe
// double (res) from it. Skip lines that start with #. If there was a
// fail or eof, return 1. If no double could be read, return res = 0.
int line_get_double(std::istream &is, std::string &line, std::string &str, double &res){

  while (true){
    std::getline(is,line);
    if (is.fail() || is.eof()) return 1;

    std::istringstream iss(line);
    iss >> str;
  
    if (str[0] != '#') {
      try {
        res = std::stod(str);
      } catch (const std::invalid_argument &e) {
        res = 0;
      }
      break;
    }
  }
  return 0;
}
