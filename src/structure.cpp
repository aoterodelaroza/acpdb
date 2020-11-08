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
#include "parseutils.h"

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
  os << nat << std::endl;
  if (os.fail()) return 2;
  return write_coordinate_block(os,true);
}

// Write a Gaussian input file to output stream os. Return non-zero if
// error; 0 if correct.
int structure::writegjf(std::ostream &os, const std::string &root, const acp &a) const {
  if (!ismol || !x || !z) return 1;

  // header
  os << "%chk=" << root << ".chk" << std::endl
     << "%mem=" << 2 << "GB" << std::endl
     << "%nproc=" << 4 << std::endl
     << "#t " 
     << "B3LYP/sto-3g "
     << (a?"pseudo=read ":"")
     << "Symm=none int=(grid=ultrafine) guess=(read,tcheck) output=wfx" << std::endl
     << std::endl << "title" << std::endl << std::endl;
  if (os.fail()) return 2;
  
  // coordinate block
  int res = write_coordinate_block(os,true);
  if (res) return res;
  os << std::endl;

  if (a){
    // fixme
    os << std::endl;
// -H 0
// H 1 0
// l
// 7
//   2   0.080000000   -0.001888400
//   2   0.100000000   0.016035496
//   2   0.120000000   -0.025949883
//   2   0.180000000   0.020492803
//   2   0.200000000   0.008918412
//   2   0.300000000   -0.053514839
//   2   0.600000000   0.094516152
// s
// 3
//   2   0.080000000   -0.018800745
//   2   0.160000000   0.085154377
//   2   1.800000000   -0.435627212
// -B 0
// B 2 0
// l
// 3
//   2   0.080000000   0.001807291
//   2   0.140000000   -0.015254895
//   2   0.600000000   -0.015803107
// s
// 2
//   2   0.080000000   0.009451214
//   2   3.000000000   -0.014145559
// p
// 2
//   2   0.140000000   0.059070622
//   2   0.160000000   0.003487334
// -C 0
// C 2 0
// l
// 5
//   2   0.080000000   0.000297208
//   2   0.100000000   -0.003200997
//   2   0.140000000   0.002245758
//   2   0.300000000   -0.024350000
//   2   2.000000000   -0.036395699
// s
// 1
//   2   0.100000000   -0.009860735
// p
// 3
//   2   0.080000000   0.019940162
//   2   0.260000000   0.035253331
//   2   0.280000000   0.022895155
// -O 0
// O 2 0
// l
// 4
//   2   0.080000000   -0.003439305
//   2   0.100000000   0.003987536
//   2   0.200000000   -0.003397997
//   2   0.500000000   0.013876306
// s
// 3
//   2   0.080000000   0.099029657
//   2   0.400000000   -0.255692870
//   2   0.500000000   -0.360290615
// p
// 4
//   2   0.080000000   0.004773039
//   2   0.100000000   0.025539738
//   2   0.500000000   -0.366444102
//   2   4.000000000   0.929961297
// 
  }

  // wfx file
  os << root << ".wfx" << std::endl << std::endl;
  
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

// Write the atomic coordinates block as:
//  symbol x y z
//  ..
// If withcm, precede the block with the charge and multiplicity.
// Return non-zero if error, 0 if correct.
int structure::write_coordinate_block(std::ostream &os, bool withcm/*= false*/) const{
  if (!x || !z) return 1;
  if (withcm)
    os << charge << " " << mult << std::endl;
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

