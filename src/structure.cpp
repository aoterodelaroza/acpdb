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
#include "globals.h"

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

// Write a Gaussian input file to output stream os. keyw = method
// keyword. gbs = basis set file. root = root of the file name. a =
// ACP used. Return non-zero if error; 0 if correct.
int structure::writegjf(std::ostream &os, const std::string &keyw, const std::string &gbs, const std::string &root, const acp &a) const{
  if (!ismol || !x || !z) return 1;

  // header
  os << "%chk=" << root << ".chk" << std::endl
     << "%mem=" << globals::mem << "GB" << std::endl
     << "%nproc=" << globals::ncpu << std::endl
     << "#t " 
     << keyw << " "
     << (a?"pseudo=read ":"")
     << "Symm=none int=(grid=ultrafine) guess=(read,tcheck) output=wfx" << std::endl
     << std::endl << "title" << std::endl << std::endl;
  if (os.fail()) return 2;
  
  // coordinate block
  int res = write_coordinate_block(os,true);
  if (res) return res;
  os << std::endl;

  // gen basis set
  if (!gbs.empty())
    os << "@" << gbs << "/N" << std::endl << std::endl;

  // acp
  if (a){
    a.writeacp_gaussian(os);
    os << std::endl;
  }

  // wfx file
  os << root << ".wfx" << std::endl << std::endl;
  if (os.fail()) return 2;
  
  return 0;
}

// Write a Gaussian input file for ACP term evaluation to output
// stream os. keyw = method keyword. gbs = basis set file. root =
// root of the file name. zat,l,exp = term details. Return non-zero
// if error; 0 if correct.
int structure::writegjf_terms(std::ostream &os, const std::string &keyw, const std::string &gbs, const std::string &root, 
                              const std::vector<unsigned char> &zat, const std::vector<unsigned char> &lmax, const std::vector<double> &exp) const {
  if (!ismol || !x || !z) return 1;

  // header
  os << "%chk=" << root << ".chk" << std::endl
     << "%mem=" << globals::mem << "GB" << std::endl
     << "%nproc=" << globals::ncpu << std::endl
     << "#t " 
     << keyw << " "
     << "Symm=none int=(grid=ultrafine) guess=(read,tcheck)" << std::endl
     << std::endl << "title" << std::endl << std::endl;
  if (os.fail()) return 2;
  
  // coordinate block
  int res = write_coordinate_block(os,true);
  if (res) return res;
  os << std::endl;

  // gen basis set
  if (!gbs.empty())
    os << "@" << gbs << "/N" << std::endl << std::endl;

  acp::term t;
  t.coef = 0.001;
  for (int iz = 0; iz < zat.size(); iz++){
    t.atom = zat[iz];
    for (int il = 0; il < lmax.size(); il++){
      t.l = lmax[il];
      for (int ie = 0; ie < exp.size(); ie++){
        t.exp = exp[ie];

        os << "--Link1--" << std::endl
           << "%chk=" << root << ".chk" << std::endl
           << "%mem=" << globals::mem << "GB" << std::endl
           << "%nproc=" << globals::ncpu << std::endl
           << "#t " 
           << keyw << " "
           << "pseudo=read iop(5/13=1,5/36=2,99/5=2) Symm=none geom=check" << std::endl
           << "int=(grid=ultrafine) guess=(read,tcheck) scf=(maxcycle=1)" << std::endl
           << std::endl << "title" << std::endl << std::endl
           << charge << " " << mult << std::endl << std::endl;

        if (!gbs.empty())
          os << "@" << gbs << "/N" << std::endl << std::endl;

        acp a({},t);
        a.writeacp_gaussian(os);
        os << std::endl;
      }
    }
  }
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

