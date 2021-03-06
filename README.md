# acpdb

Acpdb is a database (SQLite) interface program for the development of
atom-centered potentials (ACPs).

| Section                                                                                                 | Keywords                                                                                                                                                                                                             |
|---------------------------------------------------------------------------------------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| [Global commands](#global-commands)                                                                     | VERBOSE, QUIET, SOURCE, SYSTEM, ECHO, END                                                                                                                                                                            |
| [Global database operations](#global-database-operations-connect-disconnect-verify)                     | CONNECT, DISCONNECT, VERIFY                                                                                                                                                                                          |
| [Print database information](#print-database-information)                                               | PRINT ([Whole database](#whole-database), [Individual tables](#individual-tables), [DIN files](#din-files))                                                                                                          |
| [Inserting data (elements)](#inserting-data-elements)                                                   | INSERT ([Lit. refs.](#literature-references), [Sets](#sets), [Methods](#methods), [Structures](#structures), [Properties](#properties), [Evaluations](#evaluations), [Terms](#terms))                                |
| [Inserting data (bulk)](#inserting-data-bulk)                                                           | INSERT ([Properties](#insert-several-properties-for-a-set), [Evaluations from Calculations](#insert-evaluations-and-terms-from-a-file-with-calculated-values), [Maxcoefs](#insert-maximum-coefficients-from-a-file)) |
| [Deleting data](#deleting-data)                                                                         | DELETE                                                                                                                                                                                                               |
| [Comparing to database contents](#comparing-to-the-database-contents)                                   | COMPARE                                                                                                                                                                                                              |
| [Calculate energy differences from total energies](#calculating-energy-differences-from-total-energies) | CALC_EDIFF                                                                                                                                                                                                           |
| [Writing input and structure files](#writing-input-and-structure-files)                                 | WRITE ([Template format](#description-of-the-template-format))                                                                                                                                                       |
| [Working with ACPs](#working-with-acps)                                                                 | ACP ([LOAD](#load-an-acp), [INFO](#acp-information), [WRITE](#write-an-acp), [SPLIT](#split-an-acp))                                                                                                                 |
| [Defining the training set](#defining-the-training-set)                                                 | TRAINING                                                                                                                                                                                                             |
| [Simple training set operations](#simple-training-set-operations)                                       | TRAINING {DESCRIBE, SAVE, LOAD, DELETE, PRINT, CLEAR, WRITEDIN}                                                                                                                                                      |
| [Training set evaluations](#training-set-evaluations)                                                   | TRAINING EVAL                                                                                                                                                                                                        |
| [Insert training set data in old format](#insert-training-set-data-in-old-format)                       | TRAINING INSERT_OLD                                                                                                                                                                                                  |
| [Dumping the training set](#dumping-the-training-set)                                                   | TRAINING DUMP                                                                                                                                                                                                        |

## Command-Line Syntax
~~~
apcdb [-h] [inputfile [outputfile]]
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

The angular momentum integer labels are l=0, s=1, p=2, etc.

## Libraries

To get full functionality out of acpdb, the following optional libraries
are required:

- btparse (`libbtparse-dev` package on debian): for reading the
  references from a bib file.

- cereal (`libcereal-dev` package on debian): a serialization library
  that is used to load and save training sets.

Acpdb can work without these libraries, but it will be missing the
corresponding functionalities.

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

### Global Commands

~~~
VERBOSE
QUIET
~~~
Activate or deactivate verbose output. Default is quiet.

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

#### DIN files
~~~
PRINT DIN
 [DIRECTORY directory.s]
 [SET {set1.s|set1.i} {set2.s|set2.i} ...]
 [METHOD method.s|method.i]
END
~~~
Write din files for all the properties in the database. A din file is
written for each set and only properties with property type
`ENERGY_DIFFERENCE` are written. The base name of the din files is the
same as the name of the sets. If a DIRECTORY is given, write the new
din files in that directory. If one or more SETs are given, either by
key (`set.s`) or ID (`set.i`), write only the din files for those sets.
Use method `method.s` (key) or `method.i` (ID) to write the reference
values in the din files, or 0 if method is not given or evaluations
are not available with that method. The din file must have the
`fieldasrxn` keyword defined. See [DIN File Format](#din-file-format)
for details on the syntax of din files.

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
  LITREFS ref1.s [ref2.s] ...
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
  LITREFS ref1.s [ref2.s] ...
  XYZ xyz1.s [xyz2.s] [xyz3.s] ...
  ... or ...
  XYZ directory.s [regexp.s]
  POSCAR poscar1.s [poscar2.s] [poscar3.s] ...
  ... or ...
  POSCAR directory.s [regexp.s]
END
~~~
Add the set with name `name.s` in the same way as above. In addition,
belonging to this set, add structures corresponding to XYZ files
`xyz1.s`, etc. Alternatively, add all files in directory `directory.s`
that match the regular expression `regexp.s` (awk style). If
`regexp.s` is not given, add all files in the directory with xyz
extension. If the keyword POSCAR is present, also add structures
corresponding to POSCAR files `poscar1.s`, etc. Alternatively, add all
POSCAR files in directory `directory.s` that match the regular
expression `regexp.s` (awk style). If `regexp.s` is not given, add all
files in the directory with POSCAR extension.

~~~
INSERT SET name.s
  DESCRIPTION ...
  LITREFS ref1.s [ref2.s] ...
  DIN file.s
  [DIRECTORY directory.s]
  [METHOD method.s]
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
  LITREFS ref1.s [ref2.s] ...
  DESCRIPTION ...
END
~~~
Insert a computational method with name `name.s` in the
database. The literature references (by key or numerical ID) for the
method are given in LITREFS and a description can be provided.

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
  ATOM {z.s|z.i}
  L {l.s|l.i}
  EXPONENT exp.r
  VALUE value1.r [value2.r ...]
  [CALCSLOPE c0.r]
  MAXCOEF maxcoef.r
END
~~~
Insert a term into the database. Corresponds to the ACP term
calculated with method `method.s` (given by key) or `method.i` (by ID)
on property `prop.s` (by key) or `prop.i` (by ID) for atom with atomic
number `z.i` or symbol `z.s`, angular momentum channel with l = `l.i`
(number) or `l.s` (symbol), and exponent `exp.r`.

This insert command has two purposes:

- If VALUE is given the value for this term is inserted in the
database as `value1.r`,... The number of values given and their units
must be consistent with the property type. Optionally, the maximum
coefficient for this term can also be given (`maxcoef.r`) if
available.

- If no VALUE is given but MAXCOEF is, then the term corresponding to
the given method, property, atom, l, and exponent is updated with the
MAXCOEF value (`maxcoef.r`). In addition, if no PROPERTY is present,
insert the same MAXCOEF for all properties matching the rest of the
criteria.

By default, acpdb assumes that the term value is the slope of the ACP
contribution, i.e., the derivative of the property wrt the ACP
coefficient value. Sometimes, it is more convenient to calculate the
property when the ACP term is present and has certain coefficient. If
this data is available, you can have acpdb calculate the slope by
using CALCSLOPE `c0.r`. The term values are calculated as the numbers
given in VALUE minus the evaluation of method `method.s` in property
`prop.s`, and the result divided by `c0.r`. Using CALCSLOPE requires
having the corresponding evaluation in the database.

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

#### Insert Evaluations and Terms from a File with Calculated Values

~~~
INSERT CALC
  PROPERTY_TYPE {prop.s|prop.i}
  FILE file.s
  METHOD {method.s|method.i}
  [TERM [{zat.i|zat.s} {l.i|l.s} exp.r]]
  [CALCSLOPE c0.r]
END
~~~
Insert data in bulk from file `file.s`. If no TERM keyword is present,
the data corresponds to the evaluation of all properties of type
`prop.s` (key) or `prop.i` (ID) with method `method.s` (key) or
`method.i` (ID) that can be calculated using the information for the
corresponding structures contained in the file. The file must have
lines of the form:
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

If the TERM keyword is present and the atom (symbol `zat.s` or atomic
number `zat.i`), angular momentum (value `l.i` or label `l.s`), and
exponent (`exp.r`) are given, insert the data as the corresponding
term instead of the evaluation.

If TERM is used and no atom, angular momentum, and exponent are given,
then the training set must be defined. In this case, the data file
(`file.s`) must contain:
~~~
structure1@atom_l_exp.s value1.r value2.r ...
structure2@atom_l_exp.s value3.r value4.r ...
...
~~~
where the atomic symbol, angular momentum label, and exponent integer
ID are indicated after the structure name. The values are inserted in
the corresponding terms. Note that these names coincide with the file
names generated by WRITE with the TERM option.

If the TERM keyword is present, then CALCSLOPE can be used. By
default, acpdb assumes that the term value is the slope of the ACP
contribution, i.e., the derivative of the property wrt the ACP
coefficient value. Sometimes, it is more convenient to calculate the
property when the ACP term is present and has certain coefficient. If
this data is available, you can have acpdb calculate the slope by
using CALCSLOPE `c0.r`. The term values are calculated as the numbers
given in the file minus the evaluation of method `method.s` in
property `prop.s`, and the result divided by `c0.r`. Using CALCSLOPE
requires having the corresponding evaluation in the database.

#### Insert Maximum Coefficients from a File
~~~
INSERT MAXCOEF
  METHOD {method.s|method.i}
  FILE file.s
END
~~~
Insert maximum coefficients from file `file.s` for method `method.s`
(key) or `method.i` (integer ID). The file must have lines with the
structure:
~~~
atom.s  l.s  exp.r  maxcoef.r  property.s
~~~
where `atom.s` is the atomic symbol, `l.s` is the angular momentum
label, `exp.r` is the exponent, and `maxcoef.r` is the maximum
coefficient value. The last field (`property.s`) is optional. If it is
present, apply the maxcoef only to that property. Otherwise, apply it
to all properties that match the atom, angular momentum, and exponent.

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
  SOURCE {file.s|method.s}
  PROPERTY_TYPE {prop.s|prop.i}
  METHOD {method.s|method.i}
  [SET {set.s|set.i}]
  [TRAINING [alias.s]]
END
~~~
Read calculated properties from the source indicated by the SOURCE
keyword and compare them against the database contents. If the
argument to SOURCE is a file with name `file.s`, read them from the
file. The file must have lines of the form:
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

If the SOURCE argument is not a file, then acpdb assumes it is a
method key. The data is read from the available evaluations for that
method in the database.

The data from the source are compared against the evaluations from
method `method.s` (key) or `method.i` (ID). The evaluations are for
property type `prop.s` (key) or `prop.i` (ID).

Two different kinds of behavior are obtained depending on whether the
TRAINING or SET keywords are used. These two keywords are
incompatible. If neither SET nor TRAINING are present, compare to all
evaluations available for the chosen method regardless of set.  If SET
is present, restrict the comparison to the properties that belong in
set `set.s` (key) or `set.i` (ID). If TRAINING is present, the
training set must be defined, and the comparison is restricted to the
properties that belong in the training set. If a set alias `alias.s`
from the training set is given, compare only to the properties that
belong in that alias.

### Calculating Energy Differences from Total Energies
~~~
CALC_EDIFF
~~~
For all properties of type `ENERGY_DIFFERENCE`, see if there are
evaluations of type `ENERGY` for the corresponding structures. If all
structures have `ENERGY` evaluations with the same method, then
calculate and insert (or update) the corresponding `ENERGY_DIFFERENCE`
evaluations.

### Writing Input and Structure Files
~~~
WRITE
  [TEMPLATE file.s]
  [TEMPLATE_MOL filemol.s]
  [TEMPLATE_CRYS filecrys.s]
  [SET {set.s|set.i}]
  [DIRECTORY dir.s]
  [PACK ipack.i]
  [ACP {name.s|file.s}]
  [TRAINING [alias.s]]
  [TERM [{zat.i|zat.s} {l.i|l.s} exp.r]]
END
~~~
Write the structures in the database to input or structure files. If
no `TEMPLATE` is given, write the structure files (xyz format for
molecules, POSCAR format for crystals). Otherwise, write input files
according to the template (the template format is described below). If
`TEMPLATE` is given, `file.s` is used as template for both crystals
and molecules. If `TEMPLATE_MOL` is given, use `filemol.s` for
molecules. If `TEMPLATE_CRYS` is given, use `filecrys.s` for
crystals. The extensions of the generated input files are the same as
the extension of the template. Some template examples can be found in
the `templates/` directory.

Two different kinds of behavior are obtained depending on whether the
TRAINING or SET keywords are used. These two keywords are
incompatible. If neither SET nor TRAINING are present, write all the
structures in the database. If SET is present, write the structures in
the database set with that name (`set.s`) or ID (`set.i`). If TRAINING
is present, the training set must be defined, and the structures
belonging to the training set with alias `alias.s` are written. If no
alias is given, write all the structures in the training set.

The files are written to directory `dir.s` (default: `./`). If PACK is
present, create `tar.xz` compressed archives with at most `ipack.i`
structures each (this only works if the number of structures is
greater than `ipack.i`). The `PACK` keyword invokes the `tar` utility
through a `system()` call.

If the `ACP` keyword is present, use the ACP in file `file.s` or the
ACP with name `name.s` from the internal ACP database to substitute
the ACP-related template expansions (`%acpgau%`, etc.).

If the keyword TERM is present, write input files for the term
calculations. There are two possible ways of doing this. If the atom
(atomic number `zat.i` or symbol `zat.s`), angular momentum (symbol
`l.s` or value `l.i`), and exponent (`exp.r`) are given, then simply
pass this information to the template. Instead, if nothing other than
the TERM keyword is present, the training set must be defined. This
creates files with name `prefix_atom_l_expid.ext` where `prefix` is
the name of the structure, `atom` is the atomic symbol, `l`, is the
angular momentum symbol, and `expid` is the exponent integer ID. The
number of files generated corresponds to all possible combinations of
atom, l, and exponent from the training set.

#### Description of the Template Format

The template file for WRITE is a plain text file containing keywords
delimited by percent signs (`%keyword%`). These keywords are expanded
by the database program. The available keywords are:

- `%basename%`: the name of the structure in the database.

- `%cell%`: a 3x3 matrix with the lattice vectors in angstrom (same as
  in VASP/QE).

- `%cellbohr%`: a 3x3 matrix with the lattice vectors in bohr.

- `%cell_lengths%`: the cell lengths in angstrom.

- `%cell_angles%`: the cell angles in degrees.

- `%charge%`: the molecular charge.

- `%mult%`: the molecular multiplicity.

- `%nat%`: the number of atoms.

- `%ntyp%`: the number of atomic species.

- `%xyz%`: the block with the atomic coordinates. The format of each
  line is the atomic symbol followed by the x, y, and z coordinates.

- `%xyzatnum%`: the block with the atomic coordinates. The format of
  each line is the atomic number followed by the x, y, and z
  coordinates.

- `%xyzatnum200%`: the block with the atomic coordinates. The format
  of each line is the atomic number plus 200 followed by the x, y, and
  z coordinates. (Useful for crystal inputs with ACPs.)

- `%vaspxyz%`: the coordinates block in POSCAR format. The first line
  is the atomic symbols for the species in the system. The second line
  is the number of atoms belonging to each species. Lastly, the
  coordinate block is given, as the x, y, and z coordinates of each
  atom.

- `%qexyz%`: a combination of the `ATOMIC_SPECIES` and
  `ATOMIC_COORDINATES` blocks in QE format.

- `%acpgau%`, `%acpgau:xx%`: the ACP block in Gaussian format. If
  `%acpgau%` is used, print the whole ACP. Otherwise, print the ACP
  corresponding to atom with symbol `xx` only. Requires using the
  `ACP` keyword in `WRITE` for a correct expansion.

- `%acpcrys%`, `%acpcrys:xx%`: the ACP block in crystal format. If
  `%acpcrys%` is used, print the whole ACP. Otherwise, print the ACP
  corresponding to atom with symbol `xx` only. Requires using the
  `ACP` keyword in `WRITE` for a correct expansion.

- `%term_atsymbol%`: atomic symbol in ACP term.

- `%term_atnum%`: atomic number in ACP term.

- `%term_lstr%`: angular momentum label in ACP term.

- `%term_lnum%`: angular momentum value in ACP term.

- `%term_exp%`: exponent in ACP term.

Some template examples can be found in the `templates/` directory.

### Working with ACPs

#### Load an ACP
~~~
ACP LOAD name.s file.acp
ACP LOAD name.s
 atom.s l.s exp.r coef.r
 ...
END
~~~
Define an ACP with name `name.s` from file `file.acp` (in
Gaussian-style format). Alternatively, give all ACP terms one by one
by indicating the atomic symbol, angular momentum channel, exponent,
and coefficient.

#### ACP information
~~~
ACP INFO {name.s|file.s}
~~~
Print information about the ACP with name `name.s` or from file
`file.s`. This information includes the 1-norm, 2-norm, etc.

#### Write an ACP
~~~
ACP WRITE name.s [file.s]
~~~
Write the ACP `name.s` to file `file.s` (Gaussian-style format). If no
file is given, write it in human-readable form to the output.

#### Split an ACP
~~~
ACP SPLIT name.s prefix.s [COEF value.r]
~~~
Split the ACP `name.s` into several ACPs, with names given by
`prefix.s` followed by an integer and the extension `.acp`. Each new
ACP contains only one term from the original ACP. If COEF is given,
set the coefficients for the new ACPs to `value.r`.

### Defining the Training Set
~~~
TRAINING
 ATOM|ATOMS [at1.s l1.i at2.s l2.i ... ]
 EXP|EXPONENT|EXPONENTS exp1.r exp2.r ...
 EMPTY method.s
 REFERENCE method.s
 [ADD method.s [FIT]]
 SUBSET [alias.s]
  SET name.s
  NOFIT
  MASK RANGE start.i [end.i [step.i]]
  MASK ITEMS item1.i item2.i ...
  MASK PATTERN 0/1 0/1 ...
  MASK ATOMS
  WEIGHT_GLOBAL w.r
  WEIGHT_PATTERN w1.r w2.r w3.r w4.r...
  NORM_REF
  NORM_NITEM
  NORM_NITEMSQRT
  WEIGHT_ITEMS i1.i w1.r i2.i w2.r ...
 END
END
~~~
A training set in acpdb is composed of five elements: list of atoms,
list of exponents, reference method, empty method, and a list of
properties from the training set. Optionally, the training set can
also include one or more additional methods. The purpose of defining a
training set is to correct the empty method plus the additional
methods to look like the reference method by applying ACPs on the
given atoms. The ACPs have terms with exponents from the list. The
training set for the ACPs is composed of all the listed properties,
which are typically grouped by set. Each property has an associated
weight that measures its importance in the fit.

The `TRAINING` environment defines the training set for the current
run. Any previous training set present is discarded. Each sub-keyword
in the environment defines a component of the training set. To have a
properly defined training set, `ATOM`, `EXP`, `REFERENCE`, `EMPTY`,
and one or more `SUBSET` must be given. The meaning of these keywords
is as follows:

- `ATOM`: the atoms for which ACPs will be fitted, followed by the
  maximum angular momentum number for each (l, s, p, d, etc.).

- `EXP`: the list of exponents.

- `EMPTY`: the key for the empty method, the approximate method we
  want to fix with the ACPs.

- `REFERENCE`: the key for the reference method, the method we want to
  emulate.

- `ADD`: define an additional constant contribution to the energy from
  method with key `method.s`. If the `FIT` keyword follows the method
  key, then the contribution enters the fitting procedure and is
  treated as an additional scalable column in the least-squares
  fit. More than one `ADD` keywords can be given to include different
  additional methods.

- `SUBSET`: Add a subset to the training set with alias
  `alias.s`. The properties in this subset uses properties from
  database set with key `name.s`. If no alias is provided, the name
  from the corresponding database set (`name.s`) is used
  instead. Multiple `SUBSET` blocks can be given.

  If `NOFIT` is given, this subset is not passed on to the
  least-squares fitting routine, and is used only for evaluation
  purposes.

  The `MASK` commands apply a mask to remove some items from the
  database set. The items indicated in the `MASK` command are used
  in the training set and the others are deactivated. Four versions of
  the `MASK` command exist. `RANGE` indicates a range starting at
  `start.i` up to the end of the subset. If `end.i` is given, stop at
  `end.i`. If `setp.i` is given, use that as step. `ITEMS` indicates
  the items from the database set one by one. `PATTERN` repeats a
  pattern over the items of the set. If `itemn.i` is 0, it means the
  item is not used and 1 means it is used. `ATOMS` deactivates all
  items in the subset that have atoms other than those that are target
  of ACP development (requires using a previous `ATOM` command).

  The remaining commands are used to set the weights of the items in
  the subset. The keywords are:

  * The global weight (`WEIGHT_GLOBAL`) applies equally to all
    elements in the set. Default: 1.

  * The `WEIGHT_PATTERN` is a pattern applied to the elements of the
    set in sequence. For instance, a pattern of 1 5 4 applies a weight
    of 1 to the first element, 5 to the second, 4 to the third, 1 to
    the fourth, etc.

  * `NORM_REF`: divide all weights by the mean absolute reference
    value of each set.

  * `NORM_NITEM`: divide all weights by the number of items in each
    set.

  * `NORM_NITEMSQRT`: divide all weights by the square root of the
    number of items in each set.

  * `WEIGHT_ITEM i1.i w1.r ...` gives specific weights to individual
    items in the set (weight `w1.r` to item `i1.i`, etc.). Note that
    if a mask is also given, the item numbers correspond to the
    item indices after the mask is applied, in the order in which they
    appear.

  The final weight of an item is either the value given by the
  `WEIGHT_ITEM` keyword or the product of the `GLOBAL` weight, times
  the `PATTERN` weight corresponding to the item, divided by the
  normalization factors indicated by the corresponding keywords.

### Simple Training Set Operations
~~~
TRAINING DESCRIBE
~~~
Describe the current training set. This keyword calculates the number
of calculations still missing from the training set information to
carry out the ACP fit.
~~~
TRAINING SAVE name.s
~~~
Save the current training set to the connected database under name
`name.s`. Can be loaded in future sessions.
~~~
TRAINING LOAD name.s
~~~
Load the training set definition with name `name.s` from the connected
database.
~~~
TRAINING DELETE [name.s]
~~~
Delete the training set with name `name.s` from the connected
database. If no name is given, delete all training sets.
~~~
TRAINING PRINT
~~~
Print training sets from the database.
~~~
TRAINING CLEAR
~~~
Clear the current training set.
~~~
TRAINING WRITEDIN [directory.s]
~~~
Write din files for all subsets of the current training set (only the
properties that have `ENERGY_DIFFERENCE` type). If `directory.s` is
given, write the din files in this directory.

### Training Set Evaluations
~~~
TRAINING EVAL EMPTY [output.s]
~~~
Compare the empty method against the reference method for the current
training set. For this operation to work, the training set must be
defined. If `output.s` is given, write the output to that file instead
of the standard output.
~~~
TRAINING EVAL acp.s [output.s]
~~~
Evaluate the ACP with name `acp.s` against the reference method using
the linear model on the current training set. If an ACP with this name
does not exist, try to find an ACP file with that name and use it
instead. For this operation to work, the training set must be defined.
If `output.s` is given, write the output to that file instead
of the standard output.

### Insert Training Set Data in Old Format
~~~
TRAINING INSERT_OLD [directory.s]
~~~
Insert data in bulk from old-style ACP data files. Using this keyword
requires the training set being defined.

The data files to be inserted all reside in the directory
`directory.s` (`./` if not given). This command first searches for the
file called `names.dat`, and verifies that the names in it match with
the property names currently in the training set. This is done to
ensure the integrity of the database. After this operation is
complete, the following data is read from the corresponding files and
inserted:

- `ref.dat`: evaluation of the reference method. This data is only
  inserted if the evaluation is not already available in the
  database. If it is, the data is ignored.

- `empty.dat`: evaluation of the empty method. This data is only
  inserted if the evaluation is not already available in the
  database. If it is, the data is ignored.

- `x_y_z.dat`, where `x` is the atom (lowercase symbol), `y` is the
  angular momentum (lowercase symbol), and `z` is the exponent integer
  index. Insert the corresponding ACP term. This file contains the
  empty energy plus 0.001 times the term slope contribution.

- `maxcoef.dat`: if this file is present, insert the maxcoefs for each
  term. The file must have the rows with this structure:
~~~
atom l 2 exp.r maxcoef.r
~~~
  where `atom` is the atomic symbol or number, `l` is the angular
  momentum symbol or number, `2` is a constant, `exp.r` is the
  exponent, and `maxcoef.r` is the maximum coefficient. The maxcoeffs
  are applied to all terms that match the combination of atom, l, and
  exponent.

### Dumping the Training Set
~~~
TRAINING DUMP
~~~
Write the octavedump.dat file for the LASSO fit corresponding to the
current dataset.

## DIN File Format

A din file contains a number of `ENERGY_DIFFERENCE` properties and the
corresponding evaluations for a dataset. A din file is a text file
with a header (lines start with `#`) followed by a number of
consecutive blocks. Each block gives a property and an evaluation. The
structure of a block is:
```
3
a_struct
-1.5
b_struct
0
20.34
```
The property represented by this block is calculated as 3 times the
energy of structure `a_struct` plus -1.5 times the energy of structure
`b_struct`. The evaluation (reference energy) for this property is
`20.34` kcal/mol. The header file must contain the line:
```
#@ fieldasrxn n
```
where `n` is an integer. This line determines the name the
properties from the din file will have when inserted into the
database. If `n` is a (small) positive number, use the name of the nth
structure. If `n` is a negative number use the nth structure from the
end. If `n` is zero, combine all structure names. If `n` is 999, the
reference energy should be followed by the desired name for the
corresponding property:
```
3
a_struct
-1.5
b_struct
0
20.34 this_property_name
```
