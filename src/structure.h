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

class structure {
  
 public:

  //// Operators ////
  // constructors
  structure() : ismol(true), nat(0), charge(0), mult(1), r(nullptr), x(nullptr), z(nullptr) {};
  structure(const std::string &filename) : 
    ismol(true), nat(0), charge(0), mult(1), r(nullptr), x(nullptr), z(nullptr) { 
    readxyz(filename); 
  };
  structure(structure&& rhs) = delete; // move constructor (deleted)
  structure(const structure& rhs) = delete; // copy constructor (deleted)
  // destructors
  ~structure(){
    if (x) delete x;
    if (z) delete z;
    if (r) delete r;
  }
  // bool operator
  operator bool() const { return x; }

  //// Public methods ////

  // Read an xyz file. Return non-zero if error; 0 if correct.
  int readxyz(const std::string &filename);

  // Write an xyz file to output stream os. Return non-zero if error; 0 if correct.
  int writexyz (std::ostream &os) const;

  // c++ accessor functions
  bool ismolecule() const { return ismol; } 
  int get_nat() const { return nat; }
  int get_charge() const { return charge; }
  int get_mult() const { return mult; }
  const double *get_r() const { return r; }
  const unsigned char *get_z() const { return z; }
  const double *get_x() const { return x; }

 private:
  bool ismol; // whether this is a molecule
  int nat; // the number of atoms
  int charge; // molecular charge
  int mult; // spin multiplicity
  double *r; // lattice vectors
  unsigned char *z; // atomic numbers
  double *x; // coordinates
};

#endif

