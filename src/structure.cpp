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
#include <cmath>
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

// Read an POSCAR file. Return non-zero if error; 0 if correct.
int structure::readposcar(const std::string &filename){
  std::ifstream ifile(filename,std::ios::in);
  if (ifile.fail()) return 1;

  // this is a crystal
  ismol = false;
  charge = 0;
  mult = 1;

  // read POSCAR header
  std::string line;
  std::getline(ifile,line);
  if (ifile.fail()) return 2;

  // read the scale factor and the lattice vectors
  double scale, raux[9];
  ifile >> scale;
  if (ifile.fail()) return 2;
  if (std::abs(scale - 1.0) > 1e-10) {
    std::cout << "The scale factor in the POSCAR file must be one" << std::endl;
    return 2;
  }

  // read the lattice vectors
  for (int i=0; i<9; i++)
    ifile >> raux[i];
  if (ifile.fail()) return 2;

  // read the atomic symbols
  std::getline(ifile,line); // skip the remainder of the last line
  std::getline(ifile,line);
  std::list<std::string> attyp = list_all_words(line);
  int ntyp = attyp.size();
  if (ifile.fail()) return 2;

  // read the number of atomic types
  std::list<int> nis;
  nat = 0;
  for (int i = 0; i < ntyp; i++){
    int iaux;
    ifile >> iaux;
    nis.push_back(iaux);
    nat += iaux;
  }
  if (ifile.fail()) return 2;

  // read the conversion string
  std::string convstr;
  ifile >> convstr;
  if (ifile.fail()) return 2;
  if (convstr[0] != 'D' && convstr[0] != 'd'){
    std::cout << "The conversion string in the POSCAR must be Direct" << std::endl;
    return 2;
  }

  // allocate space for atomic symbols and coordinates
  deallocate();
  allocate();

  // read the atomic coordinates
  auto in = nis.begin();
  auto it = attyp.begin();
  for (int iat = 0; it != attyp.end(); it++, in++){
    for (int i = 0; i < *in; i++, iat++){
      ifile >> x[3*iat+0] >> x[3*iat+1] >> x[3*iat+2];
      z[iat] = zatguess(*it);
      if (z[iat] == 0) return 3;
    }
  }
  if (ifile.fail()) return 2;

  // copy over the lattice vectors
  for (int i=0; i<9; i++)
    r[i] = raux[i];

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

// Write a psi4 input file to output stream os. keyw = method
// keyword. root = root of the file name. Return non-zero if error;
// 0 if correct.
int structure::writepsi4(std::ostream &os, const std::string &method, const std::string &basis, const std::string &root) const{
  if (!ismol || !x || !z) return 1;

  // header
  os << "import os" << std::endl
     << "import psi4" << std::endl
     << "import psi4.core" << std::endl << std::endl
     << "memory " << globals::mem << "GB" << std::endl
     << "set_num_threads(" << globals::ncpu << ")" << std::endl << std::endl
     << "molecule mol {" << std::endl;
  if (os.fail()) return 2;

  // coordinate block
  int res = write_coordinate_block(os,true);
  if (res) return res;

  // end of molecule and options
  os << "units angstrom" << std::endl
     << "no_reorient" << std::endl
     << "}" << std::endl << std::endl
     << "psi4_io.set_specific_path(180, './')" << std::endl
     << "psi4_io.set_specific_retention(180, True)" << std::endl
     << "filename = core.get_writer_file_prefix(\"mol\") + '.180.npy'" << std::endl << std::endl
     << "set {" << std::endl
     << "  basis " << basis << std::endl
     << "  scf_type df " << std::endl
     << "  dft_spherical_points 590" << std::endl
     << "  dft_radial_points 99" << std::endl;

  // reference
  if (mult == 1)
    os << "  reference rks" << std::endl;
  else
    os << "  reference uks" << std::endl;
  os << "}" << std::endl << std::endl;

  // energy line and wavefunction file
  os << "E, wfn = energy(\"" << method << "\", return_wfn=True)" << std::endl;
  os << "molden(wfn, \"" << root << ".molden\", dovirtual=False)" << std::endl;
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
    for (unsigned char l = 0; l <= lmax[iz]; l++){
      t.l = l;
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
