#include <exception>
#include <iostream>
#include <fstream>
#include <ostream>
#include <string>
#include <sstream>
#include <iterator>
#include <algorithm>
#include <list>
#include <cstring>

#include "sqldb.h"

static std::istream *is;
static std::ostream *os;
static std::ifstream *ifile = nullptr;
static std::ofstream *ofile = nullptr;

static sqldb db;

// Error and usage message
static void print_error_usage(const char *prog = 0, const char *errmsg = 0) {
  if (errmsg) {
    printf("Error: %s\n",errmsg);
  }
  if (prog) {
    printf("Usage: %s [inputfile [outputfile]]\n",prog);
    printf("Options:\n");
    printf("  -h : show this message and exit\n");
  }
}

// List the words in an input line. Skip the rest of the line if a
// comment character (#) is found as the first character in a token.
static std::list<std::string> listwords(const std::string &line) {

  std::istringstream iss(line);
  std::list<std::string> result;
  std::string token;

  while (iss >> token){
    if (token[0] == '#') break;
    result.push_back(token);
  }

  return result;
}  

int main(int argc, char *argv[]) {

  // Check command parameters
  if (argc > 3 || (argc >= 2 && strcmp(argv[1],"-h") == 0) || (argc >= 3 && strcmp(argv[2],"-h") == 0)){
    print_error_usage(argv[0]);
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

  // Parse the input file
  std::string line;
  while(std::getline(*is, line)){
    // Tokenize the line
    std::list<std::string> tokens(listwords(line));

    // Skip blank lines
    if (tokens.empty()) continue;
    
    // Convert the token to uppercase
    std::string keyw = tokens.front();
    transform(keyw.begin(), keyw.end(), keyw.begin(), ::toupper);

    // Remove the first token from the list
    tokens.pop_front();

    // Interpret the keywords and call the appropriate routines
    try {
      if (keyw == "CREATE") {
        db.create(tokens);
      } else {
        throw std::runtime_error("Unknown keyword: " + keyw);
      }        
    } catch (const std::runtime_error &e) {
      std::cout << "Error: " << e.what() << std::endl;
      return 1;
    }
  }

  // Clean up
  db.close();
  if (ifile) ifile->close();
  if (ofile) ofile->close();

  return 0;
}
