# acpdb

ACPDB is a database (SQLite) interface program for the development of
atom-centered potentials (ACPs).

| Section                                                                             | Keywords                                                                                                                                                                              |
|-------------------------------------------------------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| [Global variables and commands](#global-variables-and-commands)                     | NCPU, MEM, SOURCE, SYSTEM, ECHO, END                                                                                                                                                  |
| [Global database operations](#global-database-operations-connect-disconnect-verify) | CONNECT, DISCONNECT, VERIFY                                                                                                                                                           |
| [Print database information](#print-database-information)                           | PRINT ([Whole database](#whole-database), [Individual tables](#individual-tables))                                                                                                    |
| [Inserting data (elements)](#inserting-data-elements)                               | INSERT ([Lit. refs.](#literature-references), [Sets](#sets), [Methods](#methods), [Structures](#structures), [Properties](#properties), [Evaluations](#evaluations), [Terms](#terms)) |
| [Inserting data (bulk)](#inserting-data-bulk)                                       | INSERT ([Properties](#insert-several-properties-for-a-set), [Evaluations from Calculations](#insert-evaluations-from-a-file-with-calculated-values))                                  |
| [Deleting data](#deleting-data)                                                     | DELETE                                                                                                                                                                                |

## Command-Line Syntax
~~~
apcdb -h [inputfile [outputfile]]
~~~
The input file is a keyword-driven case-insensitive text
file. Comments start with # and continued lines end with a backslash
symbol. If the input or the output file are not present, stdin and
stdout are used. If the output file is not present, stdout is used.

## Definitions

- Property type: a type of calculated property. Can be scalar or a
  vector.

- Structure: the structural information about a molecule or crystal
  (atomic positions, unit cell, charge, multiplicity, etc.).

- Property: a specific instance of a property type calculable by
  running a few molecular or crystal calculations and that is linear
  in the total energies.

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

## Property types

The following are the supported property types, their units, and the
number of values they comprise:

| PROPERTY_TYPE       |            Values | Units          | Description                                                                          |
|---------------------|-------------------|----------------|--------------------------------------------------------------------------------------|
| `ENERGY_DIFFERENCE` |                 1 | kcal/mol       | Energy differences between structures                                                |
| `ENERGY`            |                 1 | Hartree        | Total energy                                                                         |
| `DIPOLE`            |                 3 | Debye          | Electric dipole (molecules only)                                                     |
| `STRESS`            |                 6 | GPa            | Stress tensor (crystals only; xx,yy,zz,yz,xz,xy)                                     |
| `D1E`               |             3*nat | Hartree/bohr   | First derivatives of energy wrt atomic positions (1x,1y,1z,2x,...)                   |
| `D2E`               | 3*nat*(3*nat-1)/2 | Hartree/bohr^2 | Second derivatives of energy wrt atomic positions (1x1x,1x1y,1x1z,1x2x,...,1y1y,...) |
| `HOMO`              |                 1 | Hartree        | Energy of highest occupied molecular orbitals (molecule only)                        |
| `LUMO`              |                 1 | Hartree        | Energy of lowest unoccupied molecular orbitals (molecule only)                       |

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
SOURCE file.s
~~~
Read and execute the commands in `file.s`. The current working
directory is changed to the location of the file.

~~~
SYSTEM cmd.s
~~~
Run the OS command `cmd.s`.

~~~
ECHO message.s
~~~
Write the message `message.s` to the output.

~~~
END
~~~
Terminate the run.

### Global Database Operations (Connect, Disconnect, Verify)

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

### Print Database Information

#### Whole Database

~~~
PRINT [FULL]
~~~
Write a summary of the contents of the current database and training
set to the  output. If the FULL keyword is used, include information
about the number of evaluations and terms available for each
combination of method and set, and about the completeness of the
training set (may be slow).

#### Individual Tables

~~~
PRINT LITREF [BIBTEX]
PRINT SET
PRINT METHOD
PRINT STRUCTURE
PRINT PROPERTY
PRINT EVALUATION
PRINT TERM
~~~
Print the individual tables in the database. In the case of the
literature references (LITREF), if the BIBTEX keyword is used, write
the list of literature references in bibtex format.

### Inserting Data (elements)

#### Literature References

~~~
INSERT LITREF ref.s
  AUTHORS ...
  TITLE ...
  JOURNAL ...
  VOLUME ...
  PAGE ...
  YEAR ...
  DOI ...
  DESCRIPTION ...
END
~~~
Insert a new literature reference, with key `ref.s`.

~~~
INSERT LITREF BIBTEX bibfile.s
~~~
Insert the literature references contained in bibtex file
`bibfile.s`. Only entries of type article are inserted. If there are
repeated keys, only the last entry will be inserted. Requires
compiling with the btparse library.

#### Sets

~~~
INSERT SET name.s
  DESCRIPTION ...
  LITREFS {ref1.s|ref1.i} [ref2.s|ref2.i] ...
END
~~~
Insert a set with name `name.s` in the database with the given
description. The set calculates property `prop.s` (alternatively, it
can be given by its ID from Property_types, `prop.i`). One or more
literature references associated with the set can be given by either
their key or numerical id.

~~~
INSERT SET name.s
  DESCRIPTION ...
  LITREFS {ref1.s|ref1.i} [ref2.s|ref2.i] ...
  XYZ xyz1.s [xyz2.s] [xyz3.s] ...
  ... or ...
  XYZ directory.s [regexp.s]
END
~~~
Add the set with name `name.s` in the same way as above. In addition,
belonging to this set, add structures corresponding to XYZ files
`xyz1.s`, etc. Alternatively, add all files in directory `directory.s`
that match the regular expression `regexp.s` (awk style). If
`regexp.s` is not given, add all files in the directory with xyz
extension.

~~~
INSERT SET name.s
  DESCRIPTION ...
  LITREFS {ref1.s|ref1.i} [ref2.s|ref2.i] ...
  DIN file.s
  [METHOD method.s]
  [DIRECTORY directory.s]
END
~~~
Add the set with name `name.s` in the same way as above. In addition,
belonging to this set, add the structures indicated in the din
file. The xyz files are found in directory `directory.s`. If no
directory is given, use the current directory. Also, add the
corresponding properties and the corresponding evaluations with method
`method.s` (the property type for the properties is assumed to be
ENERGY_DIFFERENCE). If no METHOD is given, the evaluations are not
inserted.

#### Methods

~~~
INSERT METHOD name.s
  GAUSSIAN_KEYWORD ...
  PSI4_KEYWORD ...
  LITREFS ref1.s [ref2.s] ...
  DESCRIPTION ...
END
~~~
Insert a computational method with name `name.s` in the
database. The method definition includes details regarding how to
write input files for it. At the moment, only two writer interfaces
are supported: Gaussian and psi4. The literature references (by key or
numerical ID) for the method are given in LITREFS and a description
can be provided.

The details for Gaussian calculations using this methods are given in
the GAUSSIAN\_KEYWORD. This should be a string of key=value entries
separated by ;. For GAUSSIAN\_KEYWORD, the following keys are
accepted:

- method: the method and basis set combination to put in the route
  section (can include other information). Example: method=b3lyp/6-31G*.

- gbs: the gbs file for a general basis set input. Example:
  gbs=minis.gbs.

For the PSI4\_KEYWORD, the accpetable keys are:

- method: the name of the method to be passed to the energy()
  call. Example: method=b3lyp.

- basis: the basis set. Example: basis=6-31G*.

#### Structures

~~~
INSERT STRUCTURE name.s
  FILE file.s
  XYZ file.s
  POSCAR file.s
  SET {set.s|set.i}
END
~~~
Insert a molecular structure with key `name.s`. The new structure
belongs in set given by key `set.s` or integer ID `set.i`. There are
three possible ways of passing the structure file with name
`file.s`. If the XYZ keyword is used, read the file as a molecular xyz
file. If POSCAR is used, read the file as a crystal POSCAR file. If
FILE is used, let acpdb detect the format (xyz or POSCAR) and read it
as a molecule or crystal depending on the result.

#### Properties
~~~
INSERT PROPERTY name.s
  PROPERTY_TYPE {prop.s|prop.i}
  SET {set.s|set.i}
  ORDER order.i
  STRUCTURES {s1.s|s1.i} {s2.s|s2.i} {s3.s|s3.i} ...
  COEFFICIENTS c1.r c2.r c3.r ...
END
~~~
Insert a property with key `name.s`. This property belongs in set
with key `set.s` or ID `set.i` and involves structures,
given by their keys (`s1.s`,...) or IDs (`s1.i`,...). The recipe for
the calculation of the property uses the energies from those
structures multiplied by coefficients `c1.r`, etc. This property is
number `order.i` in the set. The number of structures must be equal to
the number of coefficients.

#### Evaluations
~~~
INSERT EVALUATION
  METHOD {method.s|method.i}
  PROPERTY {prop.s|prop.i}
  VALUE value1.r [value2.r ...]
END
~~~
Insert an evaluation into the database. The evaluation is for property
given by key `prop.s` or ID `prop.i` with method key `method.s` or ID
`method.i`. The evaluation yields the values `value1.r`,... The number
of values and their units must be consistent with the corresponding
property type.

#### Terms
~~~
INSERT TERM
  METHOD {method.s|method.i}
  PROPERTY {prop.s|prop.i}
  ATOM z.i
  L l.i
  EXPONENT exp.r
  VALUE value1.r [value2.r ...]
  MAXCOEF maxcoef.r
END
~~~
Insert a term into the database. Corresponds to the ACP term
calculated with method `method.s` (given by key) or `method.i` (by ID)
on property `prop.s` (by key) or `prop.i` (by ID) for atom with atomic
number `z.i`, angular momentum channel with l = `l.i`, and exponent
`exp.r`.

This insert command has two purposes:

- If VALUE is given the value for this term is inserted in the
database as `value1.r`,... The number of values given and their units
must be consistent with the property type. Optionally, the maximum
coefficient for this term can also be given (`maxcoef.r`) if
available.

- If no VALUE is given but MAXCOEF is, then the term corresponding to
the given method, property, atom, l, and exponent is updated with the
MAXCOEF value (`maxcoef.r`).

### Inserting Data (bulk)

#### Insert Several Properties for a Set

~~~
INSERT PROPERTY prefix.s
  PROPERTY_TYPE {prop.s|prop.i}
  SET {set.s|set.i}
END
~~~
Insert a property for every structure in set `set.s` (key) or `set.i`
(ID). The key of the new property is the key of the structure
prefixed by `prefix.s`. The property type is given by `prop.s` (key)
or `prop.i` (ID) and it must not be `ENERGY_DIFFERENCE`. The
properties have the same order as the structures in the set.

#### Insert Evaluations from a File with Calculated Values

~~~
INSERT CALC
  PROPERTY_TYPE {prop.s|prop.i}
  FILE file.s
  METHOD {method.s|method.i}
END
~~~
Insert data in bulk from file `file.s`. The data corresponds to the
evaluation of all properties of type `prop.s` (key) or `prop.i` (ID) with
method `method.s` (key) or `method.i` (ID) that can be calculated
using the information for the corresponding structures contained in
the file. The file must have lines of the form:
~~~
structure1.s value1.r value2.r ...
structure2.s value3.r value4.r ...
...
~~~
where `structure<n>.s` are structure identifiers from the database
and `value<n>.r` are the calculated values. The structure names are
the same as the root of the file names generated using WRITE, so this
file can be easily generated with utilities such as grep or awk.
Blank lines and comments (#) are ignored. The number and units of
these calculated values must be consistent with the corresponding
property types. If a structure name is repeated in several lines, the
values are appended to the same vector.

### Deleting Data

~~~
DELETE LITREF [key.s|key.id] [key.s|key.id] ...
DELETE SET [set.s|set.i] [set.s|set.i] ...
DELETE METHOD [method.s|method.i] [method.s|method.i] ...
DELETE STRUCTURE [struct.s|struct.i] [struct.s|struct.i] ...
DELETE PROPERTY [prop.s|prop.i] [prop.s|prop.i] ...
DELETE EVALUATION [method.s prop.s] [method.s prop.s] ...
DELETE TERM [method.s prop.s atom.i l.i exp.r] [method.s prop.s] ...
~~~
Delete one or more entries from the database tables. Most entries
can be given by their key (if available) or by their
numerical ID. When deleting evaluations, the method and the property
must both be given by key. When deleting terms, the following should
be given in order: method key, property key, atomic number, angular
momentum channel (as an integer), and exponent.

In all cases, multiple entries can be deleted with the same DELETE
keyword. If no entries are passed to DELETE, all entries are deleted.

### Comparing to the Database Contents
~~~
COMPARE
  FILE file.s
  METHOD method.s
  [SET set.s]
END
~~~
Read calculated properties from `file.s` and compare them against the
database contents. The file must have lines of the form:
~~~
structure1.s value1.r value2.r ...
structure2.s value3.r value4.r ...
...
~~~
where `structure<n>.s` are structure identifiers from the database
and `value<n>.r` are the calculated values. The structure names are
the same as the root of the file names generated using WRITE, so this
file can be easily generated with utilities such as grep or awk.
Blank lines and comments (#) are ignored. The number and units of
these calculated values must be consistent with the corresponding
property types. If a structure name is repeated in several lines, the
values are appended to the same vector.

The data in the file are compared against the evaluations from method
`method.s`. If SET is present, restrict the comparison to the
properties that belong in set `set.s`. Otherwise, compare to all
evaluations available for the chosen method regardless of set.

