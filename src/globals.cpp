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

// global flags
bool globals::verbose = false;

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
const int globals::ppty_MAX = 10; // higher than highest ppty

const double globals::atmass[] = {
    0.0,   1.00794, 4.002602,     6.941, 9.012182,    10.811, 12.0107,   14.0067, 15.9994, 18.9984032,
20.1797, 22.989770,  24.3050, 26.981538,  28.0855, 30.973761,  32.065,    35.453,  39.948,    39.0983,
 40.078, 44.955910,   47.867,   50.9415,  51.9961, 54.938049,  55.845, 58.933200, 58.6934,     63.546,
 65.409,    69.723,    72.64,  74.92160,    78.96,    79.904,  83.798,   85.4678,   87.62,   88.90585,
 91.224,  92.90638,    95.94,        97,   101.07, 102.90550,  106.42,  107.8682, 112.411,    114.818,
118.710,   121.760,   127.60, 126.90447,  131.293, 132.90545, 137.327,  138.9055, 140.116,  140.90765,
 144.24,       145,   150.36,   151.964,   157.25, 158.92534, 162.500, 164.93032, 167.259,  168.93421,
 173.04,   174.967,   178.49,  180.9479,   183.84,   186.207,  190.23,   192.217, 195.078,  196.96655,
 200.59,  204.3833,    207.2, 208.98038,      209,       210,     222,       223,     226,        227,
 232.04,       231,   238.03,       237,      244,       243,     247,       247,     251,        254,
    257,       258,      255,       260};

const std::unordered_map<std::string, int> globals::ltoint {
   {"l",0}, {"s",1}, {"p",2}, {"d",3}, {"f",4}, {"g",5}, {"h",6},
};
const std::vector<char> globals::inttol = {'l','s','p','d','f','g','h'};

