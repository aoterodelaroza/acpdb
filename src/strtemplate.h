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
#include "acp.h"

class strtemplate {

  public:

  // Token types for the template. These need to be coordinated with
  // tokenname and tokenstr in the implementation file.
  enum tokentypes {
    t_string, t_basename, t_cell, t_cellbohr, t_cell_lengths, t_cell_angles,
    t_charge, t_mult, t_nat, t_ntyp, t_xyz,
    t_xyzatnum, t_xyzatnum200, t_vaspxyz, t_qexyz, t_fhixyz,
    t_acpgau, t_acpgaunum, t_acpgausym, t_acpcrys, t_term_id, t_term_string,
    t_term_atsymbol, t_term_atsymbol_lstr_gaussian, t_term_atnum, t_term_lstr, t_term_lnum, t_term_exp, t_term_exprn,
    t_term_coef, t_term_loop, t_term_endloop};
  // t_string: a string, passed literally to the file.
  // t_basename (%basename%): the name of the structure.
  // t_cell (%cell%): a 3x3 matrix with the lattice vectors (angstrom)
  // t_cell_lengths (%cell_lengths%): cell lengths in bohr.
  // t_cell_angles (%cell_angles%): cell angles in bohr.
  // t_cellbohr (%cellbohr%): a 3x3 matrix with the lattice vectors (bohr)
  // t_charge (%charge%): the molecular charge.
  // t_mult (%mult%): the molecular multiplicity.
  // t_nat (%nat%): the number of atoms.
  // t_ntyp (%ntyp%): the number of atomic species.
  // t_xyz (%xyz%): the coordinate block, as "atom x y z" lines.
  // t_xyzatnum (%xyzatnum%): the coordinate block, as "Z x y z" lines.
  // t_xyzatnum200 (%xyzatnum200%): the coordinate block, as "Z+200 x y z" lines.
  // t_vaspxyz (%vaspxyz%): the species list (atomic symbols + number of atoms) followed by coordinate block.
  // t_qexyz (%qexyz%): the ATOMIC_SPECIES and ATOMIC_COORDINATES block in QE format.
  // t_fhixyz (%fhixyz%): the FHIaims geometry.in.
  // t_acpgau (%acpgau%): the ACP in Gaussian format
  // t_acpgaunum (%acpgaunum%): the ACP in Gaussian format, with entry number instead of atomic names
  // t_acpgausym (%acpgausym%): the ACP in Gaussian format, with atomic symbols instead of atomic names
  // t_acpcrys (%acpcrys[%xx]%): the ACP in crystal format; optionally for only one atom.
  // t_term_id (%term_id%): ACP term numerical ID
  // t_term_string (%term_string%): ACP term string
  // t_term_atsymbol (%term_atsymbol%): ACP term, atomic symbol
  // t_term_atsymbol_lstr_gaussian (%term_atsymbol_lstr_gaussian%): ACP term, atomic symbol & l for Gaussian inputs
  // t_term_atnum (%term_atnum%): ACP term, atomic number
  // t_term_lstr (%term_lstr%): ACP term, angular momentum label
  // t_term_lnum (%term_lnum%): ACP term, angular momentum value
  // t_term_exp (%term_exp%): ACP term, exponent
  // t_term_exprn (%term_exprn%): ACP term, exponent r^n
  // t_term_coef (%term_coef%): ACP term, coefficient
  // t_term_loop (%term_loop%): start ACP term loop
  // t_term_endloop (%term_endloop%): end ACP term loop

  strtemplate() : tl{}, hasloop_(false) {};
  strtemplate(const std::string &source); // construct from string

  // Apply a string to the template and write to an output stream. The
  // substitutions are performed with the information from the structure (s),
  // the ACP (a), the list of atomic numbers (zat), angular momenta (l),
  // exponents (exp), and coefficients (coef).
  std::string apply(const structure &s, const acp& a, const int id,
		    const unsigned char zat,
		    const std::string &symbol, const std::string &termstring,
		    const unsigned char l,
		    const double exp, const int exprn, const double coef) const;

  // Apply a string to the template and write to an output stream, with
  // loop expansion. The information from the loop expansion comes from
  // the list of atomic numbers (zat), angular momenta (l), exponents
  // (exp), and coefficients (coef).
  void expand_loop(const std::vector<int> &atid,
		   const std::vector<unsigned char> &zat,
		   const std::vector<std::string> &symbol,
		   const std::vector<std::string> &termstring,
                   const std::vector<unsigned char> &l,
                   const std::vector<double> &exp,
                   const std::vector<int> &exprn,
                   const std::vector<double> &coef);

  // whether the template has loops
  bool hasloop() { return hasloop_; }

  // Print the contents of the template to stdout. For debugging purposes.
  void print();

  private:
  struct template_token {
    tokentypes token;
    std::string str;
  };
  bool hasloop_ = false;
  std::list<template_token> tl;

  // push a new token
  void push_back(const template_token &t){
    tl.push_back(t);
    if (t.token == t_term_loop) hasloop_ = true;
  }
};

#endif
