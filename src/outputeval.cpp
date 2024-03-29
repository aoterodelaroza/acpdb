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
#include <cmath>

// Write a table comparing column approx with column ref to stream
// os. The rows have ids, names, and weights as indicated by the
// arguments. If add is present, write them as additional columns in
// the table. The name arguments are used in the header.  If id is
// empty, use integers starting at 0.
void output_eval(std::ostream &os,
                 const std::vector<int> &id, const std::vector<std::string> &name,
                 const std::vector<int> &num, const std::vector<double> &w,
                 const std::vector<double> &approx, const std::string &approxname,
                 const std::vector<double> &ref, const std::string &refname,
                 const std::vector<std::vector<double> > &add/*={}*/,
                 const std::vector<std::string> addname/*={}*/){

  // get the digits for this column
  int maxl = 0;
  for (int i = 0; i < ref.size(); i++)
    maxl = std::max(maxl,id.empty()?i:id[i]);
  int idwidth = digits(maxl);

  // write the header
  os << std::setw(idwidth) << std::left << "Id" << " "
     << std::setw(37) << std::left << "Name" << " ";

  if (!w.empty())
    os << std::setw(10) << std::right << "weight" << " ";

  for (int j = 0; j < add.size(); j++)
    os << std::setw(18) << std::right << addname[j] << " ";

  os << std::setw(18) << std::right << approxname << " "
     << std::setw(18) << std::right << refname << " "
     << std::setw(18) << std::right << "difference"
     << std::endl;

  // write the data
  std::streamsize prec = os.precision(7);
  os << std::fixed;
  int k = 0;
  for (int i = 0; i < num.size(); i++){
    for (int j = 0; j < num[i]; j++){
      os << std::setw(idwidth) << std::left << (id.empty()?i+1:id[i]) << " "
         << std::setw(40) << std::left << name[k];
      if (num[i] > 1){
        std::string attach = " (" + std::to_string(j+1) + ")";
        os << std::setw(4) << attach << " ";
      }

      if (!w.empty())
        os << std::setprecision(6) << std::setw(10) << std::right << w[i] << " ";

      for (int l = 0; l < add.size(); l++)
        os << std::setprecision(10) << std::setw(18) << std::right << add[l][k] << " ";

      os << std::setprecision(10) << std::setw(18) << std::right << approx[k] << " "
         << std::setprecision(10) << std::setw(18) << std::right << ref[k] << " "
         << std::setprecision(10) << std::setw(18) << std::right << approx[k]-ref[k];
      os << std::endl;
      k++;
    }
  }
  os.precision(prec);
}

// Calculate the statistics from columns x1 and x2 with weights
// w. Return the wrms, rms, mae, and mse. If setids and thissetid are
// present, process only the items i whose setid[i] is equal to
// thissetid. If w is empty, all weights are assumed to be
// one. Returns the number of items processed.
int calc_stats(const std::vector<double> &x1, const std::vector<double> &x2, const std::vector<double> &w,
               double &wrms, double &rms, double &mae, double &mse,
               const std::vector<int> &setids/*={}*/, const int thissetid/*=-1*/){

  // set the range and initialize
  int i0 = 0, i1 = x1.size();
  wrms = 0;
  rms = 0;
  mae = 0;
  mse = 0;

  // calculate statistics
  int n = 0;
  for (int i = i0; i < i1; i++){
    if (thissetid > 0 && !setids.empty() && setids[i] != thissetid)
      continue;
    n++;
    double xdiff = x1[i] - x2[i];
    mae += std::abs(xdiff);
    mse += xdiff;
    rms += xdiff * xdiff;
    if (!w.empty())
      wrms += w[i] * xdiff * xdiff;
    else
      wrms += xdiff * xdiff;
  }
  if (n > 0){
    mae /= n;
    mse /= n;
    rms = std::sqrt(rms/n);
    wrms = std::sqrt(wrms);
  }
  return n;
}
