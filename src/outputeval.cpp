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

#include "outputeval.h"
#include "parseutils.h"
#include <iomanip>

// Write a table comparing column approx with column ref to stream
// os. The rows have ids, names, and weights as indicated by the
// arguments. If add is present, write them as additional columns in
// the table. The name arguments are used in the header.
void output_eval(std::ostream &os,
                 std::vector<int> id,std::vector<std::string> name, std::vector<double> w,
                 std::vector<double> approx, const std::string &approxname,
                 std::vector<double> ref, const std::string &refname,
                 std::vector<std::vector<double> > add/*={}*/, const std::vector<std::string> addname/*={}*/){

  // get the digits for this column
  int maxl = 0;
  for (int i = 0; i < id.size(); i++)
    maxl = std::max(maxl,id[i]);
  int idwidth = digits(maxl);

  // write the header
  os << std::setw(idwidth) << std::left << "Id" << " "
     << std::setw(40) << std::left << "Name" << " ";

  if (!w.empty())
    os << std::setw(10) << std::right << "weight" << " ";

  for (int j = 0; j < add.size(); j++)
    os << std::setw(16) << std::right << addname[j] << " ";
        
  os << std::setw(16) << std::right << approxname << " "
     << std::setw(16) << std::right << refname << " "
     << std::setw(16) << std::right << "difference"
     << std::endl;

  // write the data
  std::streamsize prec = os.precision(7);
  os << std::fixed;
  for (int i = 0; i < id.size(); i++){
    os << std::setw(idwidth) << std::left << id[i] << " "
       << std::setw(40) << std::left << name[i] << " ";

    if (!w.empty())
      os << std::setprecision(6) << std::setw(10) << std::right << w[i] << " ";

    for (int j = 0; j < add.size(); j++)
      os << std::setprecision(10) << std::setw(16) << std::right << add[j][i] << " ";
        
    os << std::setprecision(10) << std::setw(16) << std::right << approx[i] << " "
       << std::setprecision(10) << std::setw(16) << std::right << ref[i] << " "
       << std::setprecision(10) << std::setw(16) << std::right << approx[i]-ref[i]
       << std::endl;
  }
  os.precision(prec);

}
