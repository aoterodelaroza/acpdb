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

#ifndef GLOBALS_H
#define GLOBALS_H

namespace globals {
  //// global variables for use in the input generators ////
  extern int ncpu; // number of cores
  extern int mem;  // memory in GB

  // universal constants and conversion factors
  extern const double ha_to_kcal; // Hartree to kcal/mol

  // property type IDs
  extern const int ppty_energy_difference;
  extern const int ppty_energy;
  extern const int ppty_dipole;
  extern const int ppty_stress;
  extern const int ppty_d1e;
  extern const int ppty_d2e;
  extern const int ppty_homo;
  extern const int ppty_lumo;
}

#endif
