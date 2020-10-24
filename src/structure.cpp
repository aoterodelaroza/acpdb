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

#include "structure.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <algorithm>
#include <unordered_map>
#include <vector>
#include <cstring>

static unsigned char zatguess(std::string atsym){
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

  transform(atsym.begin(), atsym.begin()+2, atsym.begin(), ::toupper);

  auto it = an.find(atsym.substr(0,2));
  if (it != an.end())
    return it->second;
  else{
    it = an.find(atsym.substr(0,1));
    if (it != an.end())
      return it->second;
  }
  return 0;
}

static std::string nameguess(unsigned char z){
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

// Delete the storage space
void structure::deallocate(){
  if (x) delete x;
  if (z) delete z;
  if (r) delete r;
}

// Allocate the storage space
void structure::allocate(){
  z = new unsigned char[nat];
  x = new double[3*nat];
  r = new double[3*3];
}

// Read an xyz file. Return non-zero if error; 0 if correct.
int structure::readxyz(const std::string &filename){

  std::ifstream ifile(filename,std::ios::in);
  if (ifile.fail()) return 1;

  // read xyz header
  ifile >> nat;
  if (ifile.fail()) return 2;
  ifile >> charge >> mult;
  if (ifile.fail()) return 2;

  // allocate space for atomic symbols and coordinates
  deallocate();
  allocate();

  // initialize the lattice vectors
  std::fill(r, r+9, 0);

  // read the atomic symbols and coordinates
  for (int i = 0; i < nat; i++){
    std::string atsym;
    ifile >> atsym >> x[3*i+0] >> x[3*i+1] >> x[3*i+2];
    if (ifile.fail()) return 2;

    z[i] = zatguess(atsym);
    if (z[i] == 0) return 3;
  }

  // this is a molecule
  ismol = true;

  return 0;
}

// Write an xyz file to output stream os. Return non-zero if error; 0 if correct.
int structure::writexyz(std::ostream &os) const {
  if (!x || !z) return 1;
  os << nat << std::endl 
     << charge << " " << mult << std::endl;
  
  std::streamsize prec = os.precision(10);
  os << std::scientific;
  for (int i = 0; i < nat; i++){
    os << std::setw(2) << std::left << nameguess(z[i])
       << std::setw(18) << std::right << x[3*i+0]
       << std::setw(18) << std::right << x[3*i+1]
       << std::setw(18) << std::right << x[3*i+2] << std::endl;
  }
  os.precision(prec);
  os << std::defaultfloat;
  if (os.fail()) return 2;

  return 0;
}

// Read the structure from a database row obtained via SELECT. Non-zero if error, 0 if correct.
int structure::readdbrow(sqlite3_stmt *stmt){
  if (!stmt) return 1;
  
  ismol = sqlite3_column_int(stmt, 3);
  charge = sqlite3_column_int(stmt, 4);
  mult = sqlite3_column_int(stmt, 5);
  nat = sqlite3_column_int(stmt, 6);

  deallocate();
  allocate();

  const double *rptr = (double *) sqlite3_column_blob(stmt, 7);
  if (rptr)
    memcpy(r, rptr, 0 * sizeof(double));
  else
    std::fill(r, r+9, 0);
  memcpy(z, sqlite3_column_blob(stmt, 8), nat * sizeof(unsigned char));
  memcpy(x, sqlite3_column_blob(stmt, 9), 3*nat * sizeof(double));

  return 0;
}
