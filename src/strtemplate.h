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

#ifndef TEMPLATE_H
#define TEMPLATE_H

#include <string>
#include <vector>
#include <list>
#include <fstream>
#include "structure.h"

class strtemplate {

  public:

  // Token types for the template. These need to be coordinated with
  // tokenname and tokenstr in the implementation file.
  enum tokentypes { t_string, t_basename, t_cell, t_charge, t_multiplicity, t_nat, t_vaspxyz, t_xyz };
  // t_string: a string, passed literally to the file.
  // t_basename (%basename%): the name of the structure.
  // t_cell (%cell%): a 3x3 matrix with the lattice vectors (POSCAR format)
  // t_charge (%charge%): the molecular charge.
  // t_multiplicity (%multiplicity%): the molecular multiplicity.
  // t_nat (%nat%): the number of atoms.
  // t_vaspxyz (%vasp_xyz%): the species list (atomic symbols + number of atoms) followed by coordinate block.
  // t_xyz (%xyz%): the coordinate block, as "atom x y z" lines.

  strtemplate(const std::string &source); // construct from string

  // Apply a string to the template and write to an output stream
  std::string apply(const structure &s) const;

  // Print the contents of the template to stdout. For debugging purposes.
  void print();

  private:
  struct template_token {
    tokentypes token;
    std::string str;
  };
  std::list<template_token> tl;
};

#endif