from copy import deepcopy

# Dictionary containing the element data: atomic number and atomic mass
_elements = {"Bq": 0.0,"H": 1.00794,"He": 4.002602,"Li": 6.941,"Be": 9.012182,
             "B": 10.811,"C": 12.0107,"N": 14.0067,"O": 15.9994,"F": 18.9984032,
             "Ne": 20.1797,"Na": 22.989770,"Mg": 24.3050,"Al": 26.981538,
             "Si": 28.0855,"P": 30.973761,"S": 32.065,"Cl": 35.453,"Ar": 39.948,
             "K": 39.0983,"Ca": 40.078,"Sc": 44.955910,"Ti": 47.867,"V": 50.9415,
             "Cr": 51.9961,"Mn": 54.938049,"Fe": 55.845,"Co": 58.933200,"Ni": 58.6934,
             "Cu": 63.546,"Zn": 65.409,"Ga": 69.723,"Ge": 72.64,"As": 74.92160,
             "Se": 78.96,"Br": 79.904,"Kr": 83.798,"Rb": 85.4678,"Sr": 87.62,
             "Y": 88.90585,"Zr": 91.224,"Nb": 92.90638,"Mo": 95.94,"Tc": 97,
             "Ru": 101.07,"Rh": 102.90550,"Pd": 106.42,"Ag": 107.8682,"Cd": 112.411,
             "In": 114.818,"Sn": 118.710,"Sb": 121.760,"Te": 127.60,"I": 126.90447,
             "Xe": 131.293,"Cs": 132.90545,"Ba": 137.327,"La": 138.9055,
             "Ce": 140.116,"Pr": 140.90765,"Nd": 144.24,"Pm": 145,"Sm": 150.36,
             "Eu": 151.964,"Gd": 157.25,"Tb": 158.92534,"Dy": 162.500,
             "Ho": 164.93032,"Er": 167.259,"Tm": 168.93421,"Yb": 173.04,
             "Lu": 174.967,"Hf": 178.49,"Ta": 180.9479,"W": 183.84,"Re": 186.207,
             "Os": 190.23,"Ir": 192.217,"Pt": 195.078,"Au": 196.96655,"Hg": 200.59,
             "Tl": 204.3833,"Pb": 207.2,"Bi": 208.98038,"Po": 209,"At": 210,
             "Rn": 222,"Fr": 223,"Ra": 226,"Ac": 227,"Th": 232.04,"Pa": 231,
             "U": 238.03,"Np": 237,"Pu": 244,"Am": 243,"Cm": 247,"Bk": 247,"Cf": 251,
             "Es": 254,"Fm": 257,"Md": 258,"No": 255,"Lr": 260}

# dictionaries to transform between letter and angular momentum value
_ang2l = {"l":0,"s":1,"p":2,"d":3,"f":4,"g":5,"h":6}
_l2ang = {v: k for k, v in _ang2l.items()}

def _are_terms_equal(term1,term2):
    """Returns true if two terms are equal in their n and exponent to within 1e-12."""

    return term1['n'] == term2['n'] and abs(term1['exp']-term2['exp']) < 1e-12

class ACP:
    """A class representing a set of atom-centered potentials (ACP)."""

    # The data dictionary holds the ACP information.
    # data['H'] is the hydrogen ACP.
    # -> data['H'][0] is the l-channel for the hydrogen ACP
    # -> data['H'][0][3] is the fourth term in the l-channel of the hydrogen ACP
    # -> -> data['H'][0][3]['coef'] is the coefficient
    # -> -> data['H'][0][3]['exp'] is the exponent
    # -> -> data['H'][0][3]['n'] is the exponent for the r^n factor

    def __init__(self,data=None):
        """Initialize an ACP from the given data structure. If the
        data is not present, initalize and empty ACP. Creates a deep copy
        of the input argument."""
        if data is None:
            self._data = {}
        else:
            self._data = deepcopy(data)

    @classmethod
    def from_single_term(cls,sym,ang,n,exp,coef):
        """Initialize an ACP with a single term corresponding to atom
        sym, angular momentum ang (can be the letter or the number),
        n-exponent for the r^n factor, exponent, and coefficient."""

        # verify the atom is correct
        sym = sym.capitalize()
        if not sym in _elements:
            raise NameError(f"Atom string ({sym}) is not valid.")

        # verify the angular momentum label
        if type(ang) == str:
            if not ang in _ang2l:
                raise NameError(f"Angular momentum string ({ang}) is not valid.")
            ang = _ang2l[ang]

        # place the term in the data structure
        data = {}
        data[sym] = []
        for i in range(ang):
            data[sym].append([])
        data[sym].append([{'n':int(n), 'coef':float(coef), 'exp':float(exp)}])

        return cls(data)

    @classmethod
    def from_file(cls,filename):
        """Initialize the ACP by reading the data from a file
        containing a Gaussian-style effective core potential
        description."""

        data = {}

        with open(filename,'r') as fin:
            for line in fin:
                line = line.strip()
                if len(line) == 0 or line[0] == "!":
                    continue

                # parse the atomic symbol
                sym = line.split()[0]
                if (sym[0] == "-"):
                    sym = sym[1:]
                sym = sym.capitalize()
                if not sym in _elements:
                    raise NameError(f"Element string in input ACP ({sym}) is not valid.")
                data[sym] = []

                # the next line contains the number of angular momentum channels (blocks)
                nblock = int(fin.readline().split()[1]) + 1

                # iterate over the blocks
                for iblock in range(nblock):
                    # initialize the list for this angular momentum channel
                    data_block = []

                    # read the angular momentum label
                    ang = fin.readline().strip()
                    if not ang in _ang2l:
                        raise NameError(f"Angular momentum string in input ACP ({ang}) is not valid.")

                    # read the number of terms for this angular momentum
                    nterm = int(fin.readline())

                    for iterm in range(nterm):
                        # read the term info
                        n, exp, coef = fin.readline().split()
                        data_block.append({'n':int(n), 'coef':float(coef), 'exp':float(exp)})
                    data[sym].append(data_block)

        return cls(data)

    def sort_by_exponent(self):
        """In-place function. Sort all terms in the ACP by their exponent value."""
        for sym in self._data:
            for block in self._data[sym]:
                block.sort(key=lambda x: x['exp'])

    def __str__(self):
        """Returns a Gaussian-style string representation of the ACP."""

        # initialize and run over atoms
        sacp = ""
        for sym in self._data:

            # write the two-line header for this atom
            sacp += f"-{sym} 0\n"
            sacp += f"{sym} {len(self._data[sym])-1} 0\n"

            # run over angular momentum blocks
            for iblock,block in enumerate(self._data[sym]):

                # write the block header
                sacp += f"{_l2ang[iblock]}\n"
                sacp += f"{len(block)}\n"

                # run over terms and write them to the string
                for term in block:
                    sacp += f"{term['n']} {term['exp']:.14f} {term['coef']:.14f}\n"

        # remove the final endline on return
        return sacp.strip()

    def __add__(self,other):
        """Returns the sum of two ACPs."""

        # start with the left ACP info
        data = deepcopy(self._data)

        for sym in other._data:
            # If this atom is not known, add it
            if sym not in data:
                data[sym] = deepcopy(other._data[sym])
                continue

            # run over blocks and terms in the other ACP
            for iblock,block in enumerate(other._data[sym]):
                for term in block:

                    # add the necessary empty lists to accomodate the angular momentum
                    for i in range(iblock - len(data[sym]) + 1):
                        data[sym].append([])

                    # check if the target block contains the corresponding term, and add to it
                    found = False
                    for dterm in data[sym][iblock]:
                        if _are_terms_equal(term,dterm):
                            found = True
                            dterm['coef'] += term['coef']

                    # otherwise, append a new term
                    if not found:
                        data[sym][iblock].append(deepcopy(term))

        # build the return ACP from the data and sort by exponent
        acp = ACP(data)
        acp.sort_by_exponent()

        return acp

