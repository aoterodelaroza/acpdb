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
#include <stack>
#include <memory>
#include <filesystem>

#include "acp.h"
#include "sqldb.h"
#include "trainset.h"
#include "parseutils.h"

#ifdef BTPARSE_FOUND  
#include "btparse.h"
#endif

namespace fs = std::filesystem;

// variables for managing the input and output streams
static std::istream *is;
static std::ostream *os;
static std::stack< std::shared_ptr<std::ifstream> > ifile = {};
static std::shared_ptr<std::ofstream> ofile = nullptr;
std::unordered_map<std::string,acp> nacp;

// database and training set
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

  // Connect the output file
  if (argc == 3) {
    ofile.reset(new std::ofstream(argv[2],std::ios::out));
    if (ofile->fail()) {
      std::cout << "Error opening file: " << argv[2] << std::endl;
      return 1;
    }
    os = ofile.get();
  } else {
    os = &std::cout;
  }    

  // Connect the input files; build the stack
  std::stack<std::istream *> istack;
  std::stack<std::string> icwd;
  if (argc >= 2) {
    std::shared_ptr<std::ifstream> afile(new std::ifstream(argv[1],std::ios::in));
    if (afile->fail()) {
      std::cout << "Error opening file: " << argv[1] << std::endl;
      return 1;
    }
    ifile.push(afile);
    is = ifile.top().get();
    icwd.push(fs::canonical(fs::path(argv[1])).parent_path().string());
  } else {
    is = &std::cin;
    icwd.push(fs::current_path());
  }    
  istack.push(is);

  // Initialize the btparse library, if present
#ifdef BTPARSE_FOUND  
  bt_initialize();
  bt_set_stringopts(BTE_REGULAR, BTO_CONVERT | BTO_EXPAND | BTO_PASTE | BTO_COLLAPSE);
#endif  

  // Parse the input file
  while(!istack.empty()){
    // work on the most recent input stream & directory
    is = istack.top();
    fs::current_path(icwd.top());

    // end of this stream
    if (is->eof()){
      if (!ifile.empty() && istack.top() == ifile.top().get()) ifile.pop();
      istack.pop();
      icwd.pop();
      continue;
    }

    // fetch a line
    std::string line;
    get_next_line(*is,line);
    if (line.empty() && is->eof()) 
      continue;
    if (is->fail())
      throw std::runtime_error("Error reading input");

    // Tokenize the line
    std::list<std::string> tokens(list_all_words(line));

    // Get the first keyword
    std::string keyw = popstring(tokens,true);

    // Interpret the keywords and call the appropriate routines
    try {
      if (keyw == "ACP") {
        std::string name = popstring(tokens);
        if (tokens.empty())
          nacp[name] = acp(name,*is);
        else
          nacp[name] = acp(name,tokens.front());
      } else if (keyw == "WRITE") {
        std::string category = popstring(tokens,true);
        if (category == "ACP"){
          std::string key = popstring(tokens);
          if (key.empty() || nacp.find(key) == nacp.end())
            throw std::runtime_error("Unknown ACP name: " + key);

          std::string file = popstring(tokens);
          if (file.empty())
            nacp[key].writeacp(*os);
          else
            nacp[key].writeacp(file);
        }
      } else if (keyw == "ACPINFO") {
        std::string key = popstring(tokens);
        if (key.empty() || nacp.find(key) == nacp.end())
          throw std::runtime_error("Unknown ACP name: " + key);
        nacp[key].info(*os);
      } else if (keyw == "ACPSPLIT") {
        std::string key = popstring(tokens);
        if (key.empty() || nacp.find(key) == nacp.end())
          throw std::runtime_error("Unknown ACP name: " + key);

        std::string templ = popstring(tokens);
        if (templ.empty())
          throw std::runtime_error("Empty template string for ACPSPLIT");

        nacp[key].split(templ, tokens);
      } else if (keyw == "ATOM" || keyw == "ATOMS") {
        ts.addatoms(tokens);
      } else if (keyw == "EXP" || keyw == "EXPONENT" || keyw == "EXPONENTS") {
        ts.addexp(tokens);
      } else if (keyw == "REFERENCE") {
        ts.setreference(db,tokens);
      } else if (keyw == "EMPTY") {
        ts.setempty(db,tokens);
      } else if (keyw == "ADD") {
        ts.addadditional(db,tokens);
      } else if (keyw == "SUBSET") {
        std::string alias = popstring(tokens);
        std::unordered_map<std::string,std::string> kmap = map_keyword_pairs(*is,true);
        ts.addsubset(db,alias,kmap);
      } else if (keyw == "DESCRIBE") {
        ts.describe(*os,db);
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
        if ((category == "LITREF") && equali_strings(key,"BIBTEX")) {
          db.insert_litref_bibtex(tokens);
        } else if (category == "OLDDAT"){
          ts.insert_olddat(db,key);
        } else {
          std::unordered_map<std::string,std::string> kmap = map_keyword_pairs(*is,true);
          db.insert(category,key,kmap);
        }
      } else if (keyw == "DELETE") {
        std::string category = popstring(tokens,true);
        db.erase(category,tokens);
      } else if (keyw == "LIST") {
        std::string category = popstring(tokens,true);
        if (category == "XYZ_TRAINING")
          ts.write_xyz(db,tokens);
        else if (category == "DIN_TRAINING")
          ts.write_din(db,tokens);
        else if (category == "XYZ"){
          std::unordered_map<std::string,std::string> kmap = map_keyword_pairs(*is,true);
          db.list_xyz(kmap);
        } else if (category == "DIN"){
          std::unordered_map<std::string,std::string> kmap = map_keyword_pairs(*is,true);
          db.list_din(kmap);
        } else
          db.list(*os,category,tokens);
      } else if (keyw == "VERIFY") {
        db.verify(*os);
      } else if (keyw == "SOURCE") {
        std::string filename = popstring(tokens);
        std::shared_ptr<std::ifstream> afile(new std::ifstream(filename,std::ios::in));
        if (afile->fail()) 
          throw std::runtime_error("Error opening file " + filename);
        ifile.push(afile);
        istack.push(ifile.top().get());
        icwd.push(fs::canonical(fs::path(filename)).parent_path().string());
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

  return 0;
}
