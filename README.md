# acpdb

ACPDB is a database (SQLite) interface program for the development of
atom-centered potentials (ACPs).

## Command-Line Syntax

apcdb -h [inputfile [outputfile]]

The input file is a keyword-driven case-insensitive text
file. Comments start with # and continued lines end with a backslash
symbol. If the input or the output file are not present, stdin and
stdout are used. If the output file is not present, stdout is used.

## Definitions

- Structure: the structural information about a molecule or crystal
  (atomic positions, unit cell, charge, multiplicity, etc.).

- Property: a quantity calculable by running a few molecular or
  crystal calculations and that is linear in the total energies.

- Method: a method for the calculation of a property. Can be a
  "reference method" (expensive and/or from the literature), an
  approximate method (usually the target of ACP fitting), or an
  additional method (additional contributions to the energy, like a
  dispersion correction).

- Set: a group of properties, usually from an article in the
  literature and calculated with a reasonably good method.

- Traning set: the set of all properties and structures that are used
  for ACP development. Comprises one or more sets.

- Dataset: the training set plus additional information such as atoms,
  angular momentum channels, etc. The dataset completely determines
  the ACP fitting problem.

Whenver set names are required, an asterisk stands for the current
training set. When a method name is required, an asterisk stands for
the method used in the original reference.

## Libraries

To get full functionality of out ACP, the following optional libraries
are required:

- btparse (`libbtparse-dev` package on debian): for reading the
  references from a bib file.

- cereal (`libcereal-dev` package on debian): a serialization library
  that is used to load and save training sets.

ACPDB can work without these libraries, but it will be missing the
coresponding functionalities.

## Syntax

### Global Variables and Commands

These variables control the global behavior of ACPDB.

~~~
NCPU ncpu.i
~~~
Set the number of processors in all the input writers.

~~~
MEM mem.i
~~~
Set the amount of available memory (in GB) in all the input writers.

~~~
SYSTEM cmd.s
~~~
Run the OS command `cmd.s`.

### Global Database Operations (Connect, Disconnect, Verify, Print)

~~~
CONNECT file.s
~~~
Connect to database file `file.s`. If `file.s`, create a skeleton
database with no data in that file. If a previous database was
connected, disconnects that database first.

~~~
DISCONNECT
~~~
Disconnect the database.

~~~
VERIFY
~~~
Check the sanity and consistency of the current
database. Specifically, check that the unhandled BLOBs and TEXTs refer
to keys that exist.

~~~
PRINT [FULL]
~~~
Write a summary of the contents of the current database and training
set to the  output. If the FULL keyword is used, include information
about the number of evaluations and terms available for each
combination of method and set, and about the completeness of the
training set (may be slow).


