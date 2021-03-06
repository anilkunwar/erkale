/*
 *                This source code is part of
 *
 *                     E  R  K  A  L  E
 *                             -
 *                       DFT from Hel
 *
 * Written by Susi Lehtola, 2010-2012
 * Copyright (c) 2010-2012, Susi Lehtola
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef ERKALE_EMDGTO
#define ERKALE_EMDGTO

#include "../basis.h"
#include "emd.h"

#include <complex>
#include <vector>

/// Gaussian radial function.
class RadialGaussian: public RadialFourier {
  /// The contraction
  std::vector<contr_t> c;
  /// The value of lambda
  int lambda;
 public:
  /// Constructor
  RadialGaussian(int lambda, int l);
  /// Destructor
  ~RadialGaussian();

  /// Add a term
  void add_term(const contr_t & term);
  /// Print expansion
  void print() const;

  /// Evaluate function at p
  std::complex<double> get(double p) const;
};

/// Construct list of equivalent functions
std::vector< std::vector<size_t> > find_identical_functions(const BasisSet & bas);

/// Construct lm decompositions
std::vector< std::vector<ylmcoeff_t> > form_clm(const BasisSet & bas);

/// Construct radial functions
std::vector< std::vector<RadialGaussian> > form_radial(const BasisSet & bas);

/// EMD in Gaussian basis set
class GaussianEMDEvaluator : public EMDEvaluator {
  /// The radial functions
  std::vector< std::vector<RadialGaussian> > radf;

  /// Update the pointer lists
  void update_pointers();
 public:
  /// Dummy constructor
  GaussianEMDEvaluator();
  /// Constructor
  GaussianEMDEvaluator(const BasisSet & bas, const arma::cx_mat & P, int l=0, int m=0);
  /// Constructor
  GaussianEMDEvaluator(const std::vector< std::vector<RadialGaussian> > & radf, const std::vector< std::vector<size_t> > & idfuncsv, const std::vector< std::vector<ylmcoeff_t> > & clm, const std::vector<size_t> & locv, const std::vector<coords_t> & coord, const arma::cx_mat & Pv, int l=0, int m=0);
  /// Destructor
  ~GaussianEMDEvaluator();

  /**
   * Assignment operator. This is necessary since EMDEvaluator
   * contains pointers to the memory locations of the radial
   * functions, which change whenever assignment takes place.
   */
  GaussianEMDEvaluator & operator=(const GaussianEMDEvaluator & rhs);
};

#endif
