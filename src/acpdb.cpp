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

#include <iostream>
#include <fstream>
#include <cstring>

#include "sqldb.h"
#include "trainset.h"
#include "parseutils.h"

#ifdef BTPARSE_FOUND  
#include "btparse.h"
#endif

static std::istream *is;
static std::ostream *os;
static std::ifstream *ifile = nullptr;
static std::ofstream *ofile = nullptr;

static sqldb db;
static trainset ts;

int main(int argc, char *argv[]) {

  // Check command parameters
  if (argc > 3 || (argc >= 2 && strcmp(argv[1],"-h") == 0) || (argc >= 3 && strcmp(argv[2],"-h") == 0)){
    std::cout << "Usage: " + std::string(argv[0]) + " [inputfile [outputfile]]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -h : show this message and exit" << std::endl;
    return 1;
  }

  // Connect the input and output files
  if (argc == 3) {
    ofile = new std::ofstream(argv[2],std::ios::out);
    if (ofile->fail()) {
      std::cout << "Error opening file: " << argv[2] << std::endl;
      return 1;
    }
    os = ofile;
  } else {
    os = &std::cout;
  }    
  if (argc >= 2) {
    ifile = new std::ifstream(argv[1],std::ios::in);
    if (ifile->fail()) {
      std::cout << "Error opening file: " << argv[1] << std::endl;
      return 1;
    }
    is = ifile;
  } else {
    is = &std::cin;
  }    

  // Initialize the btparse library, if present
#ifdef BTPARSE_FOUND  
  bt_initialize();
  bt_set_stringopts(BTE_REGULAR, BTO_CONVERT | BTO_EXPAND | BTO_PASTE | BTO_COLLAPSE);
#endif  

  // Parse the input file
  std::string line;
  while(std::getline(*is, line)){
    if (is->fail()) 
      throw std::runtime_error("Error reading input file");

    // Tokenize the line
    std::list<std::string> tokens(list_all_words(line));

    // Skip blank lines
    if (tokens.empty()) continue;
    
    // Get the first keyword
    std::string keyw = popstring(tokens,true);

    // Interpret the keywords and call the appropriate routines
    try {
      if (keyw == "ATOM" || keyw == "ATOMS") {
        ts.addatoms(tokens);
      } else if (keyw == "CREATE") {
        db.connect(popstring(tokens), SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
        db.create();
      } else if (keyw == "CONNECT") {
        db.connect(popstring(tokens));
        db.checksane(true);
      } else if (keyw == "DISCONNECT") {
        db.close();
      } else if (keyw == "INSERT") {
        std::string category = popstring(tokens,true);
        std::string key = popstring(tokens);
        if (equali_strings(category,"LITREF") && equali_strings(key,"BIBTEX"))
          db.insert_litref_bibtex(tokens);
        else{
          std::unordered_map<std::string,std::string> kmap = map_keyword_pairs(is,true);
          db.insert(category,key,kmap);
        }
      } else if (keyw == "DELETE") {
        std::string category = popstring(tokens,true);
        db.erase(category,tokens);
      } else if (keyw == "LIST") {
        std::string category = popstring(tokens,true);
        db.list(*os,category,tokens);
      } else if (keyw == "VERIFY") {
        db.verify(*os);
      } else if (keyw == "END") {
        break;
      } else {
        throw std::runtime_error("Unknown keyword: " + keyw);
      }        
    } catch (const std::runtime_error &e) {
      std::cout << "Error: " << e.what() << std::endl;
      return 1;
    }
  }

  // Close the btparse library, if present
#ifdef BTPARSE_FOUND  
  bt_cleanup();
#endif  

  // Clean up
  db.close();
  if (ifile) ifile->close();
  if (ofile) ofile->close();

  return 0;
}
