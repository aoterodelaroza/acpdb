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

#include "globals.h"

// conversion factors
const double globals::ha_to_kcal = 627.50947;
const double globals::ang_to_bohr = 1. / 0.529177210903; // angstrom to bohr

// global variables and constants
const int globals::ppty_energy_difference = 1;
const int globals::ppty_energy = 2;
const int globals::ppty_dipole = 3;
const int globals::ppty_stress = 4;
const int globals::ppty_d1e = 5;
const int globals::ppty_d2e = 6;
const int globals::ppty_homo = 7;
const int globals::ppty_lumo = 8;

