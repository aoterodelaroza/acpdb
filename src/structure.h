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

#ifndef STRUCTURE_H
#define STRUCTURE_H

#include <string>
#include <cstring>
#include "sqlite3.h"
#include "acp.h"

// A class for molecular or crystal structures
class structure {

 public:

  //// Operators ////
  // constructors
  structure() : ismol(true), nat(0), charge(0), mult(1), r(nullptr), x(nullptr), z(nullptr) {};
  structure(bool ismol_, int nat_, int charge_, int mult_, const double *r_, const double *x_, const unsigned char *z_)
    : ismol(ismol_), nat(nat_), charge(charge_), mult(mult_) {
    allocate();
    if (r_)
      memcpy(r, r_, 9*sizeof(double));
    else
      memset(r, 0.0, 9*sizeof(double));
    memcpy(x, x_, 3*nat*sizeof(double));
    memcpy(z, z_, nat*sizeof(unsigned char));
  };
  structure(const std::string &filename) :
    ismol(true), nat(0), charge(0), mult(1), r(nullptr), x(nullptr), z(nullptr) {
    readxyz(filename);
  };
  structure(structure&& rhs) = delete; // move constructor (deleted)
  structure(const structure& rhs) = delete; // copy constructor (deleted)
  // destructors
  ~structure(){ deallocate(); };

  // bool operator
  operator bool() const { return x; }

  //// Public methods ////

  // Read an xyz file. Return non-zero if error; 0 if correct.
  int readxyz(const std::string &filename);

  // Write an xyz file to output stream os. Return non-zero if error; 0 if correct.
  int writexyz(std::ostream &os) const;

  // Read a POSCAR file. Return non-zero if error; 0 if correct.
  int readposcar(const std::string &filename);

  // Write a POSCAR file to output stream os. Return non-zero if error; 0 if correct.
  int writeposcar(std::ostream &os) const;

  // Write a Gaussian input file to output stream os. keyw = method
  // keyword. gbs = basis set file. root = root of the file name. a =
  // ACP used. Return non-zero if error; 0 if correct.
  int writegjf(std::ostream &os, const std::string &keyw, const std::string &gbs, const std::string &root, const acp &a) const;

  // Write a psi4 input file to output stream os. keyw = method
  // keyword. root = root of the file name. Return non-zero if error;
  // 0 if correct.
  int writepsi4(std::ostream &os, const std::string &method, const std::string &basis, const std::string &root) const;

  // Write a Gaussian input file for ACP term evaluation to output
  // stream os. keyw = method keyword. gbs = basis set file. root =
  // root of the file name. zat,l,exp = term details. Return non-zero
  // if error; 0 if correct.
  int writegjf_terms(std::ostream &os, const std::string &keyw, const std::string &gbs, const std::string &root,
                     const std::vector<unsigned char> &zat, const std::vector<unsigned char> &lmax, const std::vector<double> &exp) const;

  // Read the structure from a database row obtained via
  // SELECT. Non-zero if error, 0 if correct. The SELECT order is:
  // (id,key,setid,ismolecule,charge,multiplicity,nat,cell,zatoms,coordinates)
  int readdbrow (sqlite3_stmt *stmt);

  // c++ accessor functions
  bool ismolecule() const { return ismol; }
  int get_nat() const { return nat; }
  int get_charge() const { return charge; }
  int get_mult() const { return mult; }
  const double *get_r() const { return r; }
  const unsigned char *get_z() const { return z; }
  const double *get_x() const { return x; }

 private:
  //// Private methods ////

  // Write the atomic coordinates block as:
  //  symbol x y z
  //  ..
  // If withcm, precede the block with the charge and multiplicity.
  // Return non-zero if error, 0 if correct.
  int write_coordinate_block(std::ostream &os, bool withcm = false) const;

  // Delete the storage space
  inline void deallocate(){
    if (x) delete x;
    if (z) delete z;
    if (r) delete r;
  }

  // Allocate the storage space
  inline void allocate(){
    z = new unsigned char[nat];
    x = new double[3*nat];
    r = new double[3*3];
  }

  //// Private data ////

  bool ismol; // whether this is a molecule
  int nat; // the number of atoms
  int charge; // molecular charge
  int mult; // spin multiplicity
  double *r; // lattice vectors in angstrom (r[0:2] is the first vector, etc.)
  unsigned char *z; // atomic numbers
  double *x; // coordinates (in angstrom for a molecule; fractional coordinates for a crystal)
};

#endif
