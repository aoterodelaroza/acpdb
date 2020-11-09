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

#ifndef OUTPUTEVAL_H
#define OUTPUTEVAL_H

#include <vector>
#include <string>
#include <iostream>

// Write a table comparing column approx with column ref to stream
// os. The rows have ids, names, and weights as indicated by the
// arguments. If add is present, write them as additional columns in
// the table. The name arguments are used in the header.  If id is
// empty, use integers starting at 0.
void output_eval(std::ostream &os,
                 std::vector<int> id,std::vector<std::string> name, std::vector<double> w,
                 std::vector<double> approx, const std::string &approxname,
                 std::vector<double> ref, const std::string &refname,
                 std::vector<std::vector<double> > add={}, const std::vector<std::string> addname={});

// Calculate the statistics from columns x1 and x2 with weights w and
// ids id. Return the wrms, rms, mae, and mse. If istart and iend are
// given, restrict the stats calculation to the range istart:iend-1.
// If ids, idini and idend are given, restrict the stats calculation
// to the items with id between idini and idend -1. If w is empty, all
// weights are assumed to be one. Returns the number of items
// processed.
int calc_stats(const std::vector<double> x1, const std::vector<double> x2, const std::vector<double> w,
               double &wrms, double &rms, double &mae, double &mse, 
               const int istart = -1, const int iend = -1, 
               const std::vector<int> ids={}, const int idini = -1, const int idend = -1);

#endif

