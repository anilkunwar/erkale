/*
 *                This source code is part of
 * 
 *                     E  R  K  A  L  E
 *                             -
 *                       DFT from Hel
 *
 * Written by Jussi Lehtola, 2010-2011
 * Copyright (c) 2010-2011, Jussi Lehtola
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */



#include "global.h"

#ifndef ERKALE_STRUTIL
#define ERKALE_STRUTIL

#include <armadillo>
#include <vector>
#include <string>
#include <istream>

/// Convert string to lower case
std::string tolower(const std::string & in);
/// Convert string to upper case
std::string toupper(const std::string & in);

/// Case insensitive comparison of two strings
int stricmp(const std::string & str1, const std::string & str2);

/// Read line from input (skip empty lines)
std::string readline(std::istream & in);

/// Get a line from the file (not skipping anything)
std::string readline(FILE *in);

/// Split line into words
std::vector<std::string> splitline(std::string line);

/// Read integer
int readint(std::string num);

/// Read a number in double precision
double readdouble(std::string num);

/// Print the energies wrt occupancies
void print_E(const arma::vec & E, const std::vector<double> & occ);

/// Convert memory requirement to readable text
std::string memory_size(size_t size);

/// Pretty-print symmetric matrix
void print_sym(const arma::mat &mat, bool floatformat=0, double cutoff=1e-3);

/// Print orbital energies and coefficients
void print_orb(const arma::mat & C, const arma::vec & E);

/// Parse string into pieces separated with a character in the separator
std::vector<std::string> parse(std::string in, const std::string & separator);

/// Parse input for a range of indices, e.g. 0,2-5,9,11-15,20
std::vector<size_t> parse_range(const std::string & in);

/// Parse input for a range of double precision numbers, e.g. 0:.01:10,20:30
std::vector<double> parse_range_double(const std::string & in);

/// Form wanted cube
void parse_cube(const std::string & sizes, std::vector<double> & x, std::vector<double> & y, std::vector<double> & z);

#endif
