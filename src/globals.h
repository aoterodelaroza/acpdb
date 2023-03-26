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

#include <vector>
#include <string>
#include <unordered_map>

#define M_PI 3.14159265358979323846

namespace globals {
  // global flags
  extern bool verbose; // verbose output

  // universal constants and conversion factors
  extern const double ha_to_kcal; // Hartree to kcal/mol
  extern const double ang_to_bohr; // angstrom to bohr

  // property type IDs
  extern const int ppty_energy_difference;
  extern const int ppty_energy;
  extern const int ppty_dipole;
  extern const int ppty_stress;
  extern const int ppty_d1e;
  extern const int ppty_d2e;
  extern const int ppty_homo;
  extern const int ppty_lumo;
  extern const int ppty_MAX;

  // atomic properties
  extern const double atmass[];

  // angular momentum conversion maps
  extern const std::unordered_map<std::string, int> ltoint;
  extern const std::vector<char> inttol;
}

#endif
