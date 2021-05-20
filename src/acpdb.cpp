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
#include "globals.h"

#ifdef BTPARSE_FOUND
#include "btparse.h"
#endif

// namespace alias
namespace fs = std::filesystem;

// global variables and constants (globals.h)
int globals::ncpu = 4;
int globals::mem = 2;
double globals::ha_to_kcal = 627.50947;

// variables for managing the input and output streams
static std::istream *is;
static std::ostream *os;
static std::stack< std::shared_ptr<std::ifstream> > ifile = {};
static std::shared_ptr<std::ofstream> ofile = nullptr;
static std::unordered_map<std::string,acp> nacp;

// database and training set
static sqldb db;
static trainset ts;

// some utility functions
static acp string_to_acp(const std::string &str){
  acp res;
  if (nacp.find(str) != nacp.end())
    res = nacp[str];
  else
    res = acp(str,str);
  return res;
}
static acp kmap_to_acp(const std::unordered_map<std::string,std::string> &kmap){
  if (kmap.find("ACP") != kmap.end())
    return string_to_acp(kmap.at("ACP"));
  else
    return acp();
}

int main(int argc, char *argv[]) {

  // Initial banner
  std::cout << "** ACPDB: database interface for ACP development **"<< std::endl;
  print_timestamp();

  // Check command parameters
  if (argc > 3 || (argc >= 2 && strcmp(argv[1],"-h") == 0) || (argc >= 3 && strcmp(argv[2],"-h") == 0)){
    std::cout << "Usage: " + std::string(argv[0]) + " [inputfile [outputfile]]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -h : show this message and exit" << std::endl;
    return 1;
  }

  // Connect the output file
  if (argc == 3) {
    ofile.reset(new std::ofstream(argv[2],std::ios::trunc));
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

      //// Set global variables ////

        //// NCPU ncpu.i
      if (keyw == "NCPU") {
        if (!isinteger(tokens.front()))
          throw std::runtime_error("NCPU must be an integer greater than zero");
        globals::ncpu = std::stoi(tokens.front());
        if (globals::ncpu <= 0)
          throw std::runtime_error("NCPU must be an integer greater than zero");
        *os << "* NCPU set to " << globals::ncpu << std::endl << std::endl;

        //// MEM mem.i
      } else if (keyw == "MEM") {
        if (!isinteger(tokens.front()))
          throw std::runtime_error("MEM must be an integer greater than zero");
        globals::mem = std::stoi(tokens.front());
        if (globals::mem <= 0)
          throw std::runtime_error("MEM must be an integer greater than zero");
        *os << "* MEM set to " << globals::mem << "GB" << std::endl << std::endl;

        //// SYSTEM command.s
      } else if (keyw == "SYSTEM") {
        std::string cmd = mergetokens(tokens);
        *os << "* SYSTEM: " << cmd << std::endl << std::endl;
        system(cmd.c_str());

      //// Global database operations ////

        //// CONNECT file.s
      } else if (keyw == "CONNECT") {
        *os << "* CONNECT " << std::endl << std::endl;

        // disconnect first
        *os << "Disconnecting previous database (if connected) " << std::endl;
        db.close();
        ts.setdb(nullptr);

        std::string file = popstring(tokens);
        if (fs::exists(file)){
          // connect to the database file
          if (!fs::is_regular_file(file))
            throw std::runtime_error("Object " + file + " exists but is not a file");
          *os << "Connecting database file " << file << std::endl;
          db.connect(file);
          if (!db.checksane(true))
            throw std::runtime_error("Database in file " + file + " is not sane");
          *os << "Connected database is sane" << std::endl;
        } else {
          // create the database file and connect to it
          *os << "Connecting database file " << file << std::endl;
          db.connect(file, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
          *os << "Creating skeleton database " << std::endl;
          db.create();
        }
        ts.setdb(&db);
        *os << std::endl;

        //// DISCONNECT file.s
      } else if (keyw == "DISCONNECT") {
        *os << "* DISCONNECT: disconnect the current database " << std::endl << std::endl;
        db.close();
        ts.setdb(nullptr);

        //// VERIFY
      } else if (keyw == "VERIFY") {
        *os << "* VERIFY: verify the consistency of the database " << std::endl << std::endl;
        db.verify(*os);

        //// PRINT
      } else if (keyw == "PRINT"){
        if (!db)
          throw std::runtime_error("The database needs to be defined before using PRINT");
        if (!db.checksane(true))
          throw std::runtime_error("The database is not sane");

        *os << "* PRINT: print the contents of the database " << std::endl << std::endl;

        std::string category = popstring(tokens,true);
        if (category.length() == 0){
          db.printsummary(*os,false);
          ts.describe(*os,false,false);
          ts.listdb(*os);
        } else if (category == "FULL") {
          db.printsummary(*os,true);
          ts.describe(*os,false,true);
          ts.listdb(*os);
        } else {
          db.print(*os,category,!tokens.empty() && equali_strings(tokens.front(),"BIBTEX"));
        }

        // if (category == "DIN_TRAINING")
        //   ts.write_din(tokens);
        // else if (category == "DIN"){
        //   std::unordered_map<std::string,std::string> kmap = map_keyword_pairs(*is,true);
        //   db.list_din(kmap);
        // } else
        //

        //// INSERT
      } else if (keyw == "INSERT") {
        std::string category = popstring(tokens,true);
        std::string key = popstring(tokens);
        *os << "* INSERT: insert data into the database (" << category << ")" << std::endl;

        if ((category == "LITREF") && equali_strings(key,"BIBTEX")) {
          db.insert_litref_bibtex(*os,tokens);
        // } else if (category == "OLDDAT"){
        //   ts.insert_olddat(key,tokens);
        } else {
          std::unordered_map<std::string,std::string> kmap = map_keyword_pairs(*is,true);
          if (category == "LITREF")
            db.insert_litref(*os,key,kmap);
          else if (category == "SET")
            db.insert_set(*os,key,kmap);
          else if (category == "METHOD")
            db.insert_method(*os,key,kmap);
          // else if (category == "STRUCTURE")
          //   db.insert_structure(key,kmap);
          // else if (category == "DAT")
          //   ts.insert_dat(kmap);
          else
            db.insert(category,key,kmap);
        }
        *os << std::endl;

        //// DELETE
      } else if (keyw == "DELETE") {
        std::string category = popstring(tokens,true);
        *os << "* DELETE: delete data from the database (" << category << ")" << std::endl << std::endl;
        db.erase(*os,category,tokens);

        ///////////////////////////////////////////////////

        //
      } else if (keyw == "ACP") {
        std::string name = popstring(tokens);
        if (tokens.empty())
          nacp[name] = acp(name,*is);
        else
          nacp[name] = acp(name,tokens.front());

        //
      } else if (keyw == "WRITE") {
        std::string category = popstring(tokens,true);
        if (category == "ACP"){
          // WRITE ACP keyword
          std::string key = popstring(tokens);
          if (key.empty() || nacp.find(key) == nacp.end())
            throw std::runtime_error("Unknown ACP name: " + key);

          std::string file = popstring(tokens);
          if (file.empty())
            nacp[key].writeacp_text(*os);
          else
            nacp[key].writeacp_gaussian(file);
        } else if (category == "TERMS"){
          // WRITE TERMS keyword
          std::unordered_map<std::string,std::string> kmap = map_keyword_pairs(*is,true);
          ts.write_structures(kmap,{},true);
        } else if (category.empty()) {
          // WRITE environment
          std::unordered_map<std::string,std::string> kmap = map_keyword_pairs(*is,true);
          acp a = kmap_to_acp(kmap);
          if (ts.isdefined() && (kmap.find("SET") == kmap.end() || ts.isalias(kmap["SET"])))
            ts.write_structures(kmap,a,false);
          else
            db.write_structures(kmap,a);
        } else {
          throw std::runtime_error("Unknown syntax in WRITE: " + category);
        }

      } else if (keyw == "READ") {
          std::string file = popstring(tokens);
          std::unordered_map<std::string,std::string> kmap = map_keyword_pairs(*is,true);
          if (equali_strings(file,"TERMS")){
            file = popstring(tokens);
            ts.read_terms(file,kmap);
          } else {
            if (file.empty())
              throw std::runtime_error("A data file is required for READ");
            acp a = kmap_to_acp(kmap);
            if (kmap.find("COMPARE") != kmap.end()){
              if (ts.isdefined() && (kmap.find("SET") == kmap.end() || ts.isalias(kmap["SET"]))){
                ts.read_and_compare(*os,file,kmap["COMPARE"],kmap);
              } else {
                if (kmap.find("COMPARE") != kmap.end())
                  db.read_and_compare(*os,file,kmap["COMPARE"],kmap);
              }
            }
            if (kmap.find("INSERT") != kmap.end())
              db.read_and_insert(file,kmap["INSERT"]);
          }

        //
      } else if (keyw == "ACPINFO") {
        acp a = string_to_acp(popstring(tokens));
        if (!a)
          throw std::runtime_error("Unknown ACP: " + a.get_name());
        else
          a.info(*os);

        //
      } else if (keyw == "ACPSPLIT") {
        std::string key = popstring(tokens);
        std::string templ = popstring(tokens);
        if (templ.empty())
          throw std::runtime_error("Empty template string for ACPSPLIT");

        acp a = string_to_acp(key);
        if (!a)
          throw std::runtime_error("Unknown ACP: " + a.get_name());
        else
          a.split(templ, tokens);

        //
      } else if (keyw == "ACPEVAL" || keyw == "EMPTYEVAL") {
        acp a;
        if (keyw == "ACPEVAL"){
          a = string_to_acp(popstring(tokens));
          if (!a)
            throw std::runtime_error("Unknown ACP: " + a.get_name());
        }

        if (!tokens.empty()){
          std::ofstream of(tokens.front(),std::ios::trunc);
          if (of.fail())
            throw std::runtime_error("Error opening file: " + tokens.front());
          ts.eval_acp(of,a);
        } else {
          ts.eval_acp(*os,a);
        }

        //
      } else if (keyw == "ATOM" || keyw == "ATOMS") {
        if (tokens.empty())
          ts.clearatoms();
        else
          ts.addatoms(tokens);

        //
      } else if (keyw == "EXP" || keyw == "EXPONENT" || keyw == "EXPONENTS") {
        ts.addexp(tokens);

        //
      } else if (keyw == "REFERENCE") {
        ts.setreference(tokens);

        //
      } else if (keyw == "EMPTY") {
        ts.setempty(tokens);

        //
      } else if (keyw == "ADD") {
        ts.addadditional(tokens);

        //
      } else if (keyw == "SUBSET") {
        std::string alias = popstring(tokens);
        std::unordered_map<std::string,std::string> kmap = map_keyword_pairs(*is,true);
        ts.addsubset(alias,kmap);

        //
      } else if (keyw == "DESCRIBE") {
        ts.describe(*os,false,true);

        //
      } else if (keyw == "TRAINING") {
        std::string key = popstring(tokens,true);
        std::string name = popstring(tokens);
        if (key == "SAVE")
          ts.savedb(name);
        else if (key == "LOAD")
          ts.loaddb(name);
        else if (key == "DELETE")
          ts.deletedb(name);
        else if (key == "LIST")
          ts.listdb(*os);
        else if (key == "CLEAR"){
          ts = trainset();
          ts.setdb(&db);
        } else
          throw std::runtime_error("Unknown command in TRAINING");

        //
      } else if (keyw == "DUMP") {
        ts.dump();

        //
      } else if (keyw == "SOURCE") {
        std::string filename = popstring(tokens);
        std::shared_ptr<std::ifstream> afile(new std::ifstream(filename,std::ios::in));
        if (afile->fail())
          throw std::runtime_error("Error opening file " + filename);
        ifile.push(afile);
        istack.push(ifile.top().get());
        icwd.push(fs::canonical(fs::path(filename)).parent_path().string());

        //
      } else if (keyw == "ECHO") {
        std::string aux = line.substr(5);
        deblank(aux);
        *os << aux << std::endl;

        //
      } else if (keyw == "END") {
        break;

        //
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

  // Final message
  std::cout << "ACPDB ended successfully" << std::endl;
  print_timestamp();

  return 0;
}
