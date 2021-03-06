/*
 *                This source code is part of
 *
 *                     E  R  K  A  L  E
 *                             -
 *                       HF/DFT from Hel
 *
 * Written by Susi Lehtola, 2010-2014
 * Copyright (c) 2010-2014, Susi Lehtola
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "energy-opt.h"
#include "../unitary.h"
#include "../linalg.h"
#include "../tempered.h"
#include "../timer.h"
#include <cfloat>

#ifdef _OPENMP
#include <omp.h>
#endif

EnergyOptimizer::EnergyOptimizer(const std::string & elv, bool verb) {
  el=elv;
  verbose=verb;

  fd_h=1e-5;
  ls_h=1e-4;
#ifdef _OPENMP
  ntr=(size_t) omp_get_max_threads();
#else
  ntr=1;
#endif
}

EnergyOptimizer::~EnergyOptimizer() {
}

void EnergyOptimizer::set_params(const arma::ivec & am, const arma::uvec & nfv, const arma::uvec & nparv, const arma::uvec & optshv) {
  sham=am;
  nf=nfv;
  npar=nparv;
  optsh=optshv;
  
  if(am.n_elem!=nf.n_elem)
    throw std::runtime_error("sham and nf are not of same size!\n");
  if(nf.n_elem!=npar.n_elem)
    throw std::runtime_error("nf and npar are not of same size!\n");
  if(nf.n_elem!=optsh.n_elem)
    throw std::runtime_error("nf and optsh are not of same size!\n");
  for(arma::uword i=0;i<npar.n_elem;i++) {
    if(npar(i)>nf(i)) {
      std::ostringstream oss;
      oss << "The " << shell_types[i] << " shell has more parameters than functions!\n";
      throw std::runtime_error(oss.str());
    }
    if(npar(i)==1 && nf(i)>1) {
      nf.t().print("nf");
      npar.t().print("npar");
      std::ostringstream oss;
      oss << "The " << shell_types[i] << " shell has more than one function but only a single parameter!\n";
      throw std::runtime_error(oss.str());
    }
  }
  for(arma::uword i=0;i<optsh.n_elem;i++)
    if(optsh(i))
      optsh(i)=1;
}

std::string EnergyOptimizer::get_el() const {
  return el;
}

void EnergyOptimizer::toggle_init(bool in) {
  init=in;
}

std::vector<arma::vec> EnergyOptimizer::get_exps(const arma::vec & x) const {
  // Consistency check
  if(x.n_elem != arma::sum(npar)) {
    ERROR_INFO();
    npar.t().print("npar");
    x.t().print("pars");
    fflush(stdout);
    throw std::runtime_error("Error - amount of parameters and input doesn't match!\n");
  }

  // Exponent vector
  std::vector<arma::vec> exps(arma::max(arma::abs(sham))+1);

  size_t ioff=0;
  for(arma::uword i=0;i<nf.n_elem;i++) {
    // Get exponents
    arma::vec shexp=legendre_set(x.subvec(ioff,ioff+npar(i)-1),nf(i));

    // Concatenate to full set
    size_t minam=sham(i);
    if(sham[i]<0)
      minam=0;

    for(size_t am=minam;am<=abs(sham(i));am++) {
      if(exps[am].n_elem) {
	// Get original exponents
	arma::vec ox=exps[am];

	// Resize list
	exps[am].zeros(ox.n_elem+shexp.n_elem);
	exps[am].subvec(0,ox.n_elem-1)=ox;
	exps[am].subvec(ox.n_elem,exps[am].n_elem-1)=shexp;

	exps[am]=sort(exps[am],"descend");
      } else
	exps[am]=shexp;
    }

    ioff+=npar(i);
  }

  return exps;
}

arma::uvec EnergyOptimizer::sh_vec() const {
  // Amount of shells is
  arma::uword n=arma::sum(optsh);

  // Collect indices
  arma::uvec idx(n);
  arma::uword j=0;
  for(arma::uword i=0;i<optsh.n_elem;i++)
    if(optsh(i))
      idx(j++)=i;

  return idx;
}

arma::uvec EnergyOptimizer::idx_vec(arma::sword am) const {
  // Get a parameter index vector.
  // First count the amount of parameters for the am
  arma::uword n=0;
  for(arma::uword i=0;i<optsh.size();i++)
    // If we have a family shells, we also need to consider their
    // effect
    if(optsh(i) && (sham(i)==am || (sham(i)<0 && -sham(i)>=am)))
      n+=npar(i);

  // Index vector
  arma::uvec idx(n);
  arma::uword ioff=0;
  arma::uword j=0;

  for(arma::uword i=0;i<npar.n_elem;i++) {
    if(optsh(i) && (sham(i)==am || (sham(i)<0 && -sham(i)>=am)))
      for(arma::uword ip=0;ip<npar(i);ip++)
	idx(j++)=ioff+ip;
    ioff+=npar(i);
  }

  return idx;
}

arma::uvec EnergyOptimizer::idx_vec() const {
  // Get a parameter index vector.
  // First count the amount of parameters for the am
  arma::uword n=arma::dot(npar,optsh);

  // Index vector
  arma::uvec idx(n);
  arma::uword ioff=0;
  arma::uword j=0;

  for(arma::uword i=0;i<npar.n_elem;i++) {
    if(optsh(i))
      for(arma::uword ip=0;ip<npar(i);ip++)
	idx(j++)=ioff+ip;
    ioff+=npar(i);
  }

  return idx;
}

arma::vec EnergyOptimizer::pad_vec(const arma::vec & sd, arma::sword am) const {
  arma::vec x(arma::sum(npar));
  x.zeros();

  arma::uvec idx(idx_vec(am));
  if(idx.n_elem != sd.n_elem)
    throw std::runtime_error("Inconsistent input!\n");

  for(arma::uword i=0;i<idx.n_elem;i++)
    x(idx(i))=sd(i);

  return x;
}

arma::vec EnergyOptimizer::pad_vec(const arma::vec & sd) const {
  arma::vec x(arma::sum(npar));
  x.zeros();

  arma::uvec idx(idx_vec());
  if(idx.n_elem != sd.n_elem)
    throw std::runtime_error("Inconsistent input!\n");

  for(arma::uword i=0;i<idx.n_elem;i++)
    x(idx(i))=sd(i);

  return x;
}

arma::vec EnergyOptimizer::calcG(const arma::vec & x, arma::sword am, bool check) {
  // Parameter indices
  arma::uvec idx=idx_vec(am);

  // Gradient
  arma::vec g(idx.n_elem);

  // Form trials
  std::vector<BasisSetLibrary> trials;
  for(size_t i=0;i<g.n_elem;i++) {
    // RH value
    arma::vec xrh(x);
    xrh(idx(i))+=fd_h;
    trials.push_back(form_basis(xrh));

    // LH value
    arma::vec xlh(x);
    xlh(idx(i))-=fd_h;
    trials.push_back(form_basis(xlh));
  }

  // Get energies
  std::vector<double> Etr=calcE(trials);

  // Get gradients
  for(size_t i=0;i<g.n_elem;i++) {
    g(i)=(Etr[2*i]-Etr[2*i+1])/(2*fd_h);
  }

  if(check)
    for(size_t i=0;i<g.n_elem;i++) {
      // Check that gradient is sane
      if(!std::isnormal(g(i)))
	g(i)=0.0;
    }

  return g;
}

arma::vec EnergyOptimizer::calcG(const arma::vec & x, bool check) {
  // Parameter indices
  arma::uvec idx=idx_vec();

  // Gradient
  arma::vec g(idx.n_elem);

  // Form trials
  std::vector<BasisSetLibrary> trials;
  for(size_t i=0;i<g.n_elem;i++) {
    // RH value
    arma::vec xrh(x);
    xrh(idx(i))+=fd_h;
    trials.push_back(form_basis(xrh));

    // LH value
    arma::vec xlh(x);
    xlh(idx(i))-=fd_h;
    trials.push_back(form_basis(xlh));
  }

  // Get energies
  std::vector<double> Etr=calcE(trials);

  // Get gradients
  for(size_t i=0;i<g.n_elem;i++) {
    g(i)=(Etr[2*i]-Etr[2*i+1])/(2*fd_h);
  }

  if(check)
    for(size_t i=0;i<g.n_elem;i++) {
      // Check that gradient is sane
      if(!std::isnormal(g(i)))
	g(i)=0.0;
    }

  return g;
}

arma::mat EnergyOptimizer::calcH(const arma::vec & x, bool check) {
  // Parameter indices
  arma::uvec idx=idx_vec();

  // Form trials
  std::vector<BasisSetLibrary> trials;
  for(arma::uword i=0;i<idx.n_elem;i++) {
    for(arma::uword j=0;j<=i;j++) {
      // RH,RH value
      arma::vec xrhrh(x);
      xrhrh(idx(i))+=fd_h;
      xrhrh(idx(j))+=fd_h;
      trials.push_back(form_basis(xrhrh));
      
      // RH,LH value
      arma::vec xrhlh(x);
      xrhlh(idx(i))+=fd_h;
      xrhlh(idx(j))-=fd_h;
      trials.push_back(form_basis(xrhlh));

      // LH,RH value
      arma::vec xlhrh(x);
      xlhrh(idx(i))-=fd_h;
      xlhrh(idx(j))+=fd_h;
      trials.push_back(form_basis(xlhrh));

      // LH,LH value
      arma::vec xlhlh(x);
      xlhlh(idx(i))-=fd_h;
      xlhlh(idx(j))-=fd_h;
      trials.push_back(form_basis(xlhlh));
    }
  }
    
  // Get energies
  std::vector<double> Etr=calcE(trials);
  
  // Hessian
  arma::vec h(idx.n_elem,idx.n_elem);
  for(arma::uword i=0;i<idx.n_elem;i++)
    for(arma::uword j=0;j<=i;j++) {
      // Offset is 4 * i(i+1)/2
      size_t ioff=4*(i*(i+1)/2+j);

      // Collect values
      double rhrh=Etr[ioff  ];
      double rhlh=Etr[ioff+1];
      double lhrh=Etr[ioff+2];
      double lhlh=Etr[ioff+3];
      
      // Values
      h(i,j)=(rhrh - rhlh - lhrh + lhlh)/(4.0*fd_h*fd_h);
      // Symmetrize
      h(j,i)=h(i,j);
    }

  if(check)
    for(size_t i=0;i<h.n_rows;i++)
      for(size_t j=0;j<h.n_cols;j++) {
	// Check that Hessian is sane
	if(!std::isnormal(h(i,j)))
	  h(i,j)=0.0;
      }
  
  return h;
}

arma::mat EnergyOptimizer::calcH(const arma::vec & x, arma::sword am, bool check) {
  // Parameter indices
  arma::uvec idx=idx_vec(am);

  // Form trials
  std::vector<BasisSetLibrary> trials;
  for(arma::uword i=0;i<idx.n_elem;i++) {
    for(arma::uword j=0;j<=i;j++) {
      // RH,RH value
      arma::vec xrhrh(x);
      xrhrh(idx(i))+=fd_h;
      xrhrh(idx(j))+=fd_h;
      trials.push_back(form_basis(xrhrh));
      
      // RH,LH value
      arma::vec xrhlh(x);
      xrhlh(idx(i))+=fd_h;
      xrhlh(idx(j))-=fd_h;
      trials.push_back(form_basis(xrhlh));

      // LH,RH value
      arma::vec xlhrh(x);
      xlhrh(idx(i))-=fd_h;
      xlhrh(idx(j))+=fd_h;
      trials.push_back(form_basis(xlhrh));

      // LH,LH value
      arma::vec xlhlh(x);
      xlhlh(idx(i))-=fd_h;
      xlhlh(idx(j))-=fd_h;
      trials.push_back(form_basis(xlhlh));
    }
  }
    
  // Get energies
  std::vector<double> Etr=calcE(trials);
  
  // Hessian
  arma::mat h(idx.n_elem,idx.n_elem);
  for(arma::uword i=0;i<idx.n_elem;i++)
    for(arma::uword j=0;j<=i;j++) {
      // Offset is 4 * i(i+1)/2
      size_t ioff=4*(i*(i+1)/2+j);

      // Collect values
      double rhrh=Etr[ioff  ];
      double rhlh=Etr[ioff+1];
      double lhrh=Etr[ioff+2];
      double lhlh=Etr[ioff+3];
      
      // Values
      h(i,j)=(rhrh - rhlh - lhrh + lhlh)/(4.0*fd_h*fd_h);
      // Symmetrize
      h(j,i)=h(i,j);
    }

  if(check)
    for(size_t i=0;i<h.n_rows;i++)
      for(size_t j=0;j<h.n_cols;j++) {
	// Check that Hessian is sane
	if(!std::isnormal(h(i,j)))
	  h(i,j)=0.0;
      }
  
  return h;
}

BasisSetLibrary EnergyOptimizer::form_basis(const arma::vec & x) const {
  return form_basis(get_exps(x));
}

BasisSetLibrary EnergyOptimizer::form_basis(const std::vector<arma::vec> & exps) const {

  // Form element basis set
  ElementBasisSet elbas(el);
  for(size_t am=0;am<exps.size();am++)
    for(size_t ix=0;ix<exps[am].size();ix++) {
      FunctionShell sh(am);
      sh.add_exponent(1.0,exps[am][ix]);
      elbas.add_function(sh);
    }
  elbas.sort();

  // Returned basis set
  BasisSetLibrary baslib;
  baslib.add_element(elbas);
  return baslib;
}

double EnergyOptimizer::optimize(arma::vec & x, size_t maxiter, double nrthr, double gthr) {
  // Initial energy
  double Einit=0.0;

  // Energy value
  double Eval=0.0;
      
  if(verbose)
    print_info(x,"Input parameters");

  // Convergence threshold
  double cthr=std::max(nrthr,gthr);
  while(true) {
    
    // Loop over angular momentum
    for(size_t iamloop=0;iamloop<maxiter;iamloop++) {
      // Is any gradient significant?
      bool changed=false;
      
      // Initial energy for macroiteration
      double Emacro=0.0;
      
      if(verbose)
	printf("\n****** Macroiteration %3i *******\n\n",(int) iamloop+1);
      
      for(arma::sword am=0;am<=arma::max(arma::abs(sham(sh_vec())));am++) {
	// Current and old gradient
	arma::vec g, gold;
	
	// Search direction
	arma::vec sd;
	
	// Minimization loop
	for(size_t iloop=0;iloop<maxiter;iloop++) {
	  
	  // Get gradient
	  Timer t;
	  gold=g;
	  g=calcG(x,am);
	
	  if(verbose) {
	    printf("%c microiteration %i gradient, norm %e (%s)\n",shell_types[am],(int) iloop+1,arma::norm(g,2),t.elapsed().c_str());
	    g.t().print();
	    fflush(stdout);
	    t.set();
	  }
	
	  if(arma::norm(g,2)<cthr) {
	    if(verbose)
	      printf("Gradient is small, converged.\n\n");
	    break;
	  } else
	    changed=true;

	  bool nr;
	  if(arma::norm(g,2)>nrthr) {
	    nr=false;
	    // Update search direction
	    if(iloop % g.n_elem == 0) {
	      // Steepest descent
	      sd=-g;
	    
	      if(verbose)
		printf("Using SD step.\n");
	    } else {
	      // Update factor
	      double gamma;
	    
	      // Polak-Ribiere
	      gamma=arma::dot(g,g-gold)/arma::dot(gold,gold);
	      // Fletcher-Reeves
	      //gamma=arma::dot(g,g)/arma::dot(gold,gold);
	    
	      // Update search direction
	      sd=-g + gamma*sd;
	    
	      if(verbose)
		printf("Using CG step.\n");
	    }
	  } else {
	    nr=true;
	    // Get Hessian
	    arma::mat h=calcH(x,am);
	    if(verbose) {
	      printf("Hessian (%s)\n",t.elapsed().c_str());
	      h.print();
	      fflush(stdout);
	    }
	  
	    // Get eigenvalues of h
	    arma::vec hval;
	    arma::mat hvec;
	    eig_sym_ordered(hval,hvec,h);
	  
	    if(verbose)
	      hval.t().print("Eigenvalues of Hessian");
	  
	    // Inverse matrix
	    arma::mat hinv(h);
	    hinv.zeros();
	    for(arma::uword i=0;i<hval.n_elem;i++)
	      // Shift negative eigenvalues to positive to assure
	      // convergence to minimum
	      hinv+=hvec.col(i)*arma::trans(hvec.col(i))/fabs(hval(i));
	  
	    // Search direction is
	    sd=-hinv*g;
	  }
	
	  std::vector<double> step, E;
	
	  // Trial step sizes
	  std::vector<double> stepbatch;
	  stepbatch.push_back(0.0);
	
	  // Index of optimum
	  size_t optidx;
	
	  if(verbose)
	    printf("Line search\n");
	
	  while(true) {
	    // Generate new trials
	    while(stepbatch.size() < ntr) {
	      // Step size is
	      if(!nr) {
		double ss=std::pow(2.0,step.size()+stepbatch.size()-1)*ls_h;
		stepbatch.push_back(ss);
	      } else {
		// If Newton-Raphson, a unit step size should take us to the
		// minimum. Use backtracking line search to find optimal
		// value.
		const double tau=0.7;
		double ss=std::pow(tau,step.size()+stepbatch.size()-1);
		stepbatch.push_back(ss);
	      }
	    }
	  
	    // Generate basis sets
	    std::vector<BasisSetLibrary> basbatch(stepbatch.size());
	    for(size_t i=0;i<stepbatch.size();i++)
	      basbatch[i]=form_basis(x+pad_vec(stepbatch[i]*sd,am));
	  
	    // Get trial energies
	    std::vector<double> batchE=calcE(basbatch);
	  
	    // Print out info
	    if(verbose) {
	      double refE=(E.size()==0) ? batchE[0] : E[0];
	      for(size_t i=0;i<batchE.size();i++)
		if(batchE[i]>=0.0)
		  printf("%2i %e % 18.5e % e\n",(int) (i+step.size()),stepbatch[i],batchE[i],batchE[i]-refE);
		else
		  printf("%2i %e % 18.10f % e\n",(int) (i+step.size()),stepbatch[i],batchE[i],batchE[i]-refE);
	      fflush(stdout);
	    }
	  
	    // Initial energy?
	    if(iloop==0 && iamloop==0 && am==0 && E.size()==0)
	      Einit=batchE[0];
	    if(iamloop==0 && Emacro==0.0 && E.size()==0)
	      Emacro=batchE[0];
	    // Add to total list
	    for(size_t i=0;i<batchE.size();i++) {
	      step.push_back(stepbatch[i]);
	      E.push_back(batchE[i]);
	    }
	    stepbatch.clear();
	    
	    // Check if converged
	    bool convd=false;
	    if(nr) {
	      // Parameter value
	      const double c=0.5;
	    
	      // m value is
	      double m=arma::dot(sd,g);
	      // t value is
	      double tv=-c*m;
	    
	      for(size_t i=1;i<E.size();i++) {
		if(E[0]-E[i] >= step[i]*tv) {
		  convd=true;
		  break;
		}
	      }
	    } else {
	      for(size_t i=1;i<E.size();i++)
		if(E[i]>E[i-1]) {
		  convd=true;
		  break;
		}
	    }

	    // Check for too small step
	    if(step[step.size()-1]>0.0 && step[step.size()-1]*arma::norm(sd,2)<DBL_EPSILON*arma::norm(x,2))
	      convd=true;
	    
	    if(convd)
	      break;
	  }
	
	  // Find the optimal step size
	  optidx=0;
	  for(size_t i=1;i<E.size();i++)
	    if(E[i]<E[optidx])
	      optidx=i;
	  if(optidx==0) {
	    printf("Error in %c line search - could not go downhill!\n",shell_types[am]);
	    break;
	  }
	
	  // Optimal energy
	  Eval=E[optidx];
	
	  // Step size to use
	  double ss=step[optidx];
	
	  x+=pad_vec(ss*sd,am);
	  if(verbose) {
	    printf("Optimal step size is %e, reducing the energy by %e.\n",ss,E[optidx]-E[0]);
	    print_info(x,"Current parameters");
	  }
	}
      }

      if(verbose && Emacro!=0.0)
	printf("Macroiteration reduced the energy by %e.\n",Eval-Emacro);
    
      if(!changed)
	break;
    }

    // Converged?
    if(cthr==gthr)
      break;
    else
      cthr=gthr;
  }
  
  if(verbose)
    printf("Optimization reduced the energy by %e.\n",Eval-Einit);

  // Check that energy is at least initialized properly
  if(Eval==0.0) {
    std::vector<BasisSetLibrary> blib;
    blib.push_back(form_basis(x));
    Eval=calcE(blib)[0];
  }
  
  return Eval;
}

double EnergyOptimizer::optimize_full(arma::vec & x, size_t maxiter, double nrthr, double gthr) {
  // Current and old gradient
  arma::vec g, gold;

  // Search direction
  arma::vec sd;

  // Energy value
  double Eval=0.0;
  // Initial energy
  double Einit=0.0;

  if(verbose)
    print_info(x,"Input parameters");

  // Convergence threshold
  double cthr=std::max(nrthr,gthr);
  while(true) {
    
    // Minimization loop
    for(size_t iloop=0;iloop<maxiter;iloop++) {
      // Get gradient
      Timer t;
      gold=g;
      g=calcG(x);

      if(verbose) {
	printf("Iteration %i gradient, norm %e (%s)\n",(int) iloop+1,arma::norm(g,2),t.elapsed().c_str());
	g.t().print();
	fflush(stdout);
	t.set();
      }

      if(arma::norm(g,2)<gthr) {
	if(verbose)
	  printf("Gradient is small, converged.\n\n");
	break;
      }

      bool nr;
      if(arma::norm(g,2)>nrthr) {
	nr=false;
	// Update search direction
	if(iloop % g.n_elem == 0) {
	  // Steepest descent
	  sd=-g;
	
	  if(verbose)
	    printf("Using SD step.\n");
	} else {
	  // Update factor
	  double gamma;
	
	  // Polak-Ribiere
	  gamma=arma::dot(g,g-gold)/arma::dot(gold,gold);
	  // Fletcher-Reeves
	  //gamma=arma::dot(g,g)/arma::dot(gold,gold);
	
	  // Update search direction
	  sd=-g + gamma*sd;
	
	  if(verbose)
	    printf("Using CG step.\n");
	}
      } else {
	nr=true;
	// Get Hessian
	arma::mat h=calcH(x);
	if(verbose) {
	  printf("Hessian (%s)\n",t.elapsed().c_str());
	  h.print();
	  fflush(stdout);
	}
      
	// Get eigenvalues of h
	arma::vec hval;
	arma::mat hvec;
	eig_sym_ordered(hval,hvec,h);
      
	hval.t().print("Eigenvalues of Hessian");
      
	// Inverse matrix
	arma::mat hinv(h);
	hinv.zeros();
	for(arma::uword i=0;i<hval.n_elem;i++)
	  // Shift negative eigenvalues to positive to assure
	  // convergence to minimum
	  hinv+=hvec.col(i)*arma::trans(hvec.col(i))/fabs(hval(i));
      
	// Search direction is
	sd=-hinv*g;
      }

      std::vector<double> step, E;

      // Trial step sizes
      std::vector<double> stepbatch;
      stepbatch.push_back(0.0);

      // Index of optimum
      size_t optidx;

      if(verbose)
	printf("Line search\n");

      while(true) {
	// Generate new trials
	while(stepbatch.size() < ntr) {
	  // Step size is
	  if(!nr) {
	    double ss=std::pow(2.0,step.size()+stepbatch.size()-1)*ls_h;
	    stepbatch.push_back(ss);
	  } else {
	    // If Newton-Raphson, a unit step size should take us to the
	    // minimum. Use backtracking line search to find optimal
	    // value.
	    const double tau=0.7;
	    double ss=std::pow(tau,step.size()+stepbatch.size()-1);
	    stepbatch.push_back(ss);
	  }
	}

	// Generate basis sets
	std::vector<BasisSetLibrary> basbatch(stepbatch.size());
	for(size_t i=0;i<stepbatch.size();i++)
	  basbatch[i]=form_basis(x+pad_vec(stepbatch[i]*sd));

	// Get trial energies
	std::vector<double> batchE=calcE(basbatch);

	// Print out info
	if(verbose) {
	  double refE=(E.size()==0) ? batchE[0] : E[0];
	  for(size_t i=0;i<batchE.size();i++)
	    if(batchE[i]>=0.0)
	      printf("%2i %e % 18.5e % e\n",(int) (i+step.size()),stepbatch[i],batchE[i],batchE[i]-refE);
	    else
	      printf("%2i %e % 18.10f % e\n",(int) (i+step.size()),stepbatch[i],batchE[i],batchE[i]-refE);
	  fflush(stdout);
	}

	// Initial energy?
	if(iloop==0 && E.size()==0)
	  Einit=batchE[0];
	// Add to total list
	for(size_t i=0;i<batchE.size();i++) {
	  step.push_back(stepbatch[i]);
	  E.push_back(batchE[i]);
	}
	stepbatch.clear();

	// Check if converged
	bool convd=false;
	if(nr) {
	  // Parameter value
	  const double c=0.5;

	  // m value is
	  double m=arma::dot(sd,g);
	  // t value is
	  double tv=-c*m;

	  for(size_t i=1;i<E.size();i++) {
	    if(E[0]-E[i] >= step[i]*tv) {
	      convd=true;
	      break;
	    }
	  }
	} else {
	  for(size_t i=1;i<E.size();i++)
	    if(E[i]>E[i-1]) {
	      convd=true;
	      break;
	    }
	}
	if(convd)
	  break;
      }

      // Find the optimal step size
      optidx=0;
      for(size_t i=1;i<E.size();i++)
	if(E[i]<E[optidx])
	  optidx=i;
      if(optidx==0) {
	printf("Error in line search - could not go downhill!\n");
	break;
      }

      // Optimal energy
      Eval=E[optidx];

      // Step size to use
      double ss=step[optidx];
      x+=pad_vec(ss*sd);
      if(verbose) {
	printf("Optimal step size is %e, reducing the energy by %e.\n",ss,E[optidx]-E[0]);
	print_info(x,"Current parameters");
      }
    }

    if(cthr==gthr)
      break;
    else
      cthr=gthr;
  }
  
  if(verbose)
    printf("Optimization reduced the energy by %e.\n",Eval-Einit);

  // Check that energy is at least initialized properly
  if(Eval==0.0) {
    std::vector<BasisSetLibrary> blib;
    blib.push_back(form_basis(x));
    Eval=calcE(blib)[0];
  }
  
  return Eval;
}

double EnergyOptimizer::scan(arma::vec & x, double min, double max, double dx) {
  // Polarization exponent search
  size_t Ntr=ceil((max-min)/dx);
  std::vector<BasisSetLibrary> trbas(Ntr);
  // Sanity check
  if(npar(npar.n_elem-1)!=1)
    throw std::runtime_error("Scan requires a single parameter on the last shell!\n");

  if(verbose)
    printf("Scanning from % e to % e, with %i trials.\n",min,max,(int) Ntr);

  for(size_t i=0;i<Ntr;i++) {
    // Dummy vector
    arma::vec xdum(x);
    xdum(xdum.n_elem-1)=min+i*dx;
    trbas[i]=form_basis(xdum);
  }
  // Value w/o polarization function
  {
    std::vector<arma::vec> exps=get_exps(x);
    exps.erase(exps.end()-1);
    trbas.push_back(form_basis(exps));
  }

  // Get energies
  std::vector<double> trE=calcE(trbas);

  if(verbose) {
    printf("\nValue w/o polarization shell % .10e\n",trE[Ntr]);
    printf("Trial energies\n");
    for(size_t i=0;i<Ntr;i++)
      printf("%3i/%-3i % e % .10e % e\n",(int) i+1,(int) trE.size(),min+i*dx,trE[i],trE[i]-trE[Ntr]);
  }

  // Get minimum
  arma::vec E=arma::conv_to<arma::vec>::from(trE);
  arma::uword minind;
  double Emin=E.min(minind);

  if(verbose) {
    printf("Polarization shell reduces energy by % .10e to % .10e\n",Emin-trE[Ntr],Emin);
    printf("\n");
  }

  // Store value
  x(x.n_elem-1)=min+minind*dx;

  return Emin-trE[Ntr];
}

void EnergyOptimizer::print_info(const arma::vec & x, const std::string & msg) const {
  printf("%s\n",msg.c_str());

  for(arma::uword i=0;i<x.n_elem;i++)
    printf("% 7.3f ",x(i));
  printf("\n");

  // Print current exponents
  std::vector<arma::vec> exps=get_exps(x);
  for(size_t am=0;am<exps.size();am++) {
    arma::vec ame=arma::log10(exps[am]);
    
    printf("%c exponents\n",shell_types[am]);
    for(arma::uword ix=0;ix<ame.n_elem;ix++)
      printf("% 7.3f ",ame(ix));
    printf("\n");
  }
  printf("\n");

  fflush(stdout);
}
