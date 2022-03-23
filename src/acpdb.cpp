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
  bool intraining = false;
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
    deblank(line);
    if (line.empty() && is->eof())
      continue;
    if (is->fail())
      throw std::runtime_error("Error reading input");

    // Tokenize the line
    std::list<std::string> tokens(list_all_words(line));

    // Get the first keyword
    std::string keyw = popstring(tokens,true);

    if (keyw.empty()) continue;

    // Print out
    *os << "%% " << line << std::endl;

    //// Global commands ////

      //// SYSTEM
    if (keyw == "VERBOSE") {
      globals::verbose = true;
    } else if (keyw == "QUIET") {
      globals::verbose = false;
    } else if (keyw == "SYSTEM") {
      std::string cmd = mergetokens(tokens);
      *os << "* SYSTEM: " << cmd << std::endl << std::endl;
      system(cmd.c_str());

      //// SOURCE
    } else if (keyw == "SOURCE") {
      std::string filename = popstring(tokens);
      std::shared_ptr<std::ifstream> afile(new std::ifstream(filename,std::ios::in));
      if (afile->fail())
        throw std::runtime_error("Error opening file " + filename);
      ifile.push(afile);
      istack.push(ifile.top().get());
      icwd.push(fs::canonical(fs::path(filename)).parent_path().string());

      //// ECHO
    } else if (keyw == "ECHO") {
      std::string aux = line.substr(5);
      deblank(aux);
      *os << aux << std::endl;

      //// END
    } else if (keyw == "END") {
      if (intraining){
        intraining = false;
        *os << "* TRAINING: fininshed defining the training set " << std::endl << std::endl;
        ts.describe(*os,false,true,false);
      } else
        break;

      //// Global database operations ////

      //// CONNECT file.s
    } else if (keyw == "CONNECT") {
      *os << "* CONNECT " << std::endl << std::endl;

      // disconnect first
      *os << "Disconnecting previous database (if connected) " << std::endl;
      db.close();
      ts = trainset();
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
      ts = trainset();
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
        ts.describe(*os,false,false,false);
        ts.listdb(*os);
      } else if (category == "FULL") {
        db.printsummary(*os,true);
        ts.describe(*os,false,true,false);
        ts.listdb(*os);
      } else if (category == "DIN") {
        std::unordered_map<std::string,std::string> kmap = map_keyword_pairs(*is,true);
        *os << "# PRINT DIN: print the database to DIN files " << std::endl << std::endl;
        db.print_din(*os,kmap);
      } else {
        db.print(*os,category,!tokens.empty() && equali_strings(tokens.front(),"BIBTEX"));
      }

      //// INSERT
    } else if (keyw == "INSERT") {
      std::string category = popstring(tokens,true);
      std::string key = popstring(tokens);
      *os << "* INSERT: insert data into the database (" << category << ")" << std::endl;

      if ((category == "LITREF") && equali_strings(key,"BIBTEX")) {
        db.insert_litref_bibtex(*os,tokens);
      } else {
        std::unordered_map<std::string,std::string> kmap = map_keyword_pairs(*is,true);
        if (category == "LITREF")
          db.insert_litref(*os,key,kmap);
        else if (category == "SET")
          db.insert_set(*os,key,kmap);
        else if (category == "METHOD")
          db.insert_method(*os,key,kmap);
        else if (category == "STRUCTURE")
          db.insert_structure(*os,key,kmap);
        else if (category == "PROPERTY")
          db.insert_property(*os,key,kmap);
        else if (category == "EVALUATION")
          db.insert_evaluation(*os,kmap);
        else if (category == "TERM")
          db.insert_term(*os,kmap);
        else if (category == "MAXCOEF")
          db.insert_maxcoef(*os,kmap);
        else if (category == "CALC")
          db.insert_calc(*os,kmap,ts.get_zat(),ts.get_lmax(),ts.get_exp());
      }
      *os << std::endl;

      //// CALC_EDIFF
    } else if (keyw == "CALC_EDIFF") {
      *os << "* CALC_EDIFF: calculate and insert energy differences from total energies " << std::endl;
      db.calc_ediff(*os);
      *os << std::endl;

      //// DELETE
    } else if (keyw == "DELETE") {
      std::string category = popstring(tokens,true);
      *os << "* DELETE: delete data from the database (" << category << ")" << std::endl << std::endl;
      db.erase(*os,category,tokens);

      //// COMPARE
    } else if (keyw == "COMPARE") {
      *os << "* COMPARE: compare data to database evaluations" << std::endl << std::endl;
      std::unordered_map<std::string,std::string> kmap = map_keyword_pairs(*is,true);

      if ((kmap.find("SET") != kmap.end()) && (kmap.find("TRAINING") != kmap.end()))
        throw std::runtime_error("SET and TRAINING are incompatible keywords in COMPARE");
      else if (kmap.find("TRAINING") != kmap.end())
        ts.read_and_compare(*os,kmap);
      else
        db.read_and_compare(*os,kmap);

      //// WRITE
    } else if (keyw == "WRITE") {
      *os << "* WRITE: write input files for database structures" << std::endl << std::endl;
      std::unordered_map<std::string,std::string> kmap = map_keyword_pairs(*is,true);
      acp a = kmap_to_acp(kmap);
      if ((kmap.find("SET") != kmap.end()) && (kmap.find("TRAINING") != kmap.end()))
        throw std::runtime_error("SET and TRAINING are incompatible keywords in WRITE");

      if (kmap.find("TRAINING") != kmap.end())
        ts.write_structures(*os,kmap,a);
      else
        db.write_structures(*os,kmap,a,{},ts.get_zat(),ts.get_lmax(),ts.get_exp());

      //// ACP
    } else if (keyw == "ACP") {
      std::string category = popstring(tokens,true);
      std::string name = popstring(tokens);

      if (category == "LOAD"){
        *os << "* ACP LOAD " << name << std::endl << std::endl;
        if (tokens.empty())
          nacp[name] = acp(name,*is);
        else
          nacp[name] = acp(name,tokens.front());

      } else if (category == "INFO") {
        if (nacp.find(name) != nacp.end())
          nacp[name].info(*os);
        else {
          acp a = acp(name,name);
          a.info(*os);
        }

      } else if (category == "WRITE") {
        if (name.empty() || nacp.find(name) == nacp.end())
          throw std::runtime_error("Unknown ACP name: " + name);

        std::string file = popstring(tokens);
        if (file.empty())
          nacp[name].writeacp_text(*os);
        else{
          *os << "* ACP WRITE: writing ACP " << name << " to file " << file << std::endl << std::endl;
          nacp[name].writeacp_gaussian(file);
        }
      } else if (category == "SPLIT") {
        std::string prefix = popstring(tokens);
        if (prefix.empty())
          throw std::runtime_error("Empty prefix string for ACP SPLIT");

        *os << "* ACP SPLIT " << name << " creates files " << prefix << "-*.acp" << std::endl << std::endl;
        if (nacp.find(name) != nacp.end())
          nacp[name].split(prefix,tokens);
        else {
          acp a = acp(name,name);
          a.split(prefix,tokens);
        }
      }
      //// TRAINING (start environment)
    } else if (keyw == "TRAINING") {
      if (!db)
        throw std::runtime_error("The database needs to be defined before using TRAINING");
      if (!db.checksane(true))
        throw std::runtime_error("The database is not sane");
      std::string category = popstring(tokens,true);
      std::string name = popstring(tokens);

      // TRAINING subkeywords
      if (category.empty()){
        *os << "* TRAINING: started defining the training set " << std::endl << std::endl;
        intraining = true;
        ts = trainset();
        ts.setdb(&db);
      } else if (category == "DESCRIBE") {
        ts.describe(*os,false,true,false);
      } else if (category == "SAVE") {
        ts.savedb(name);
      } else if (category == "LOAD") {
        ts.loaddb(name);
      } else if (category == "DELETE") {
        ts.deletedb(name);
      } else if (category == "PRINT") {
        ts.listdb(*os);
      } else if (category == "CLEAR") {
        ts = trainset();
        ts.setdb(&db);
      } else if (category == "WRITEDIN") {
        ts.write_din(name);
      } else if (category == "EVAL") {
        std::string uname = name;
        uppercase(uname);

        acp a;
        if (uname != "EMPTY"){
          *os << "* TRAINING: evaluating ACP " << name << std::endl << std::endl;
          a = string_to_acp(name);
          if (!a)
            throw std::runtime_error("Unknown ACP " + name + " in TRAINING EVAL");
        } else {
          *os << "* TRAINING: evaluating the EMPTY method " << std::endl << std::endl;
        }

        if (!tokens.empty()){
          std::ofstream of(tokens.front(),std::ios::trunc);
          if (of.fail())
            throw std::runtime_error("Error opening file: " + tokens.front());
          ts.eval_acp(of,a);
        } else
          ts.eval_acp(*os,a);
      } else if (category == "MAXCOEF_SELECT") {
        acp a = string_to_acp(name);
	if (!a)
	  throw std::runtime_error("Unknown ACP " + name + " in TRAINING MAXCOEF_SELECT");

	if (tokens.empty())
	  throw std::runtime_error("Missing evaluation file name in TRAINING MAXCOEF_SELECT");
	std::string file = popstring(tokens);
	ts.maxcoef_select(*os,a,file);
      } else if (category == "INSERT_OLD"){
        ts.insert_olddat(*os,name);
      } else if (category == "WRITE_OLD"){
        ts.write_olddat(*os,name);
      } else if (category == "DUMP") {
        ts.dump(*os);
      } else {
        throw std::runtime_error("Unknown keyword after TRAINING");
      }

      //// TRAINING -> ATOM
    } else if (keyw == "ATOM" || keyw == "ATOMS") {
      if (!intraining)
        throw std::runtime_error("ATOM is not allowed outside the TRAINING environment");
      ts.addatoms(tokens);

      //// TRAINING -> EXP
    } else if (keyw == "EXP" || keyw == "EXPONENT" || keyw == "EXPONENTS") {
      ts.addexp(tokens);

      //// TRAINING -> REFERENCE
    } else if (keyw == "REFERENCE") {
      ts.setreference(tokens);

      //// TRAINING -> EMPTY
    } else if (keyw == "EMPTY") {
      ts.setempty(tokens);

      //// TRAINING -> ADD
    } else if (keyw == "ADD") {
      ts.addadditional(tokens);

      //// TRAINING -> SUBSET
    } else if (keyw == "SUBSET") {
      std::string alias = popstring(tokens);
      std::unordered_map<std::string,std::string> kmap = map_keyword_pairs(*is,true);
      ts.addsubset(alias,kmap);

    } else {
      throw std::runtime_error("Unknown keyword: -" + keyw + "-");
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
