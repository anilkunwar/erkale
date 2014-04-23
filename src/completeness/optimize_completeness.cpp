/*
 *                This source code is part of
 *
 *                     E  R  K  A  L  E
 *                             -
 *                       HF/DFT from Hel
 *
 * Written by Susi Lehtola, 2010-2012
 * Copyright (c) 2010-2012, Susi Lehtola
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "completeness_profile.h"
#include "optimize_completeness.h"
#include "../linalg.h"
#include "../timer.h"

extern "C" {
#include <gsl/gsl_blas.h>
#include <gsl/gsl_multimin.h>
#include <gsl/gsl_sf_legendre.h>
}

#ifdef _OPENMP
#include <omp.h>
#endif

// Maximum number of functions allowed in completeness optimization
#define NFMAX 50

arma::vec get_exponents(const gsl_vector *xv, const completeness_scan_t * p) {
  // Check parameter consistency
  size_t np=p->nfull;
  if(p->neven)
    np++;
  if(np != xv->size) {
    std::ostringstream oss;
    oss << "Expected " << np << " parameters but was given " << xv->size << "!\n";
    throw std::runtime_error(oss.str());
  }

  // One-sided exponents
  size_t nside=p->neven + p->nfull;
  arma::vec A(nside);
  A.zeros();
  
  // Plug in the even-tempered exponents
  if(p->neven) {
    double lge=gsl_vector_get(xv,0);

    if(p->odd)
      for(size_t i=0;i < p->neven;i++)
	A(i)=(i+1.0)*lge;
    else
      // In the even case, the first exponent is placed halfway
      for(size_t i=0;i < p->neven;i++)
	A(i)=(i+0.5)*lge;
  }

  // x index offset
  size_t xoff = p->neven ? 1 : 0;
  size_t eoff = p->neven;
  
  // Fully optimized exponents
  for(size_t i=0;i < p->nfull;i++) {
    A(i+eoff)=gsl_vector_get(xv,i+xoff);
  }

  //  A.subvec(0,eoff-1).t().print("Even-tempered");
  //  A.subvec(eoff,A.n_elem-1).t().print("Full");
  //  A.t().print("A");

  // Convert to natural logarithm
  A*=log(10.0);

  // Full set of exponents
  size_t nexp=2*A.n_elem;
  if(p->odd)
    nexp++;

  arma::vec exps(nexp);
  exps.subvec(0,A.n_elem-1)=arma::exp(-A);
  if(p->odd) {
    exps(A.n_elem)=1.0;
    exps.subvec(A.n_elem+1,2*A.n_elem)=arma::exp(A);
  } else 
    exps.subvec(A.n_elem,2*A.n_elem-1)=arma::exp(A);

  return exps;
}

arma::mat self_overlap(const arma::vec & z, int am) {
  // Compute self-overlap
  size_t N=z.n_elem;
  arma::mat Suv(N,N);
#ifdef _OPENMP
#pragma omp parallel for
#endif
  for(size_t i=0;i<N;i++)
    for(size_t j=0;j<=i;j++) {
      Suv(i,j)=pow(4.0*z(i)*z(j)/pow(z(i)+z(j),2),am/2.0+0.75);
      Suv(j,i)=Suv(i,j);
    }

  return Suv;
}

arma::vec completeness_profile(const gsl_vector * x, void * params) {
  // Get parameters
  completeness_scan_t *par=(completeness_scan_t *) params;

  // Get exponents
  arma::vec z=get_exponents(x,par);

  // Get self-overlap
  arma::mat Suv=self_overlap(z,par->am);

  // and its half inverse matrix
  arma::mat Sinvh=BasOrth(Suv,false);

  // Get overlap of primitives with scanning terms
  arma::mat amu=overlap(par->scanexp,z,par->am);

  // Compute intermediary result, Np x N
  arma::mat J=amu*Sinvh;

  // The completeness profile
  size_t N=par->scanexp.size();
  arma::vec Y(N);
  for(size_t i=0;i<N;i++)
    Y[i]=arma::dot(J.row(i),J.row(i));

  return Y;
}

double compl_mog(const gsl_vector * x, void * params) {
  // Get parameters
  completeness_scan_t *p=(completeness_scan_t *) params;

  // Get completeness profile
  arma::vec Y=completeness_profile(x,params);

  // Compute MOG.
  double phi=0.0;

  size_t nint=0;

  switch(p->n) {

  case(1):
    for(size_t i=1;i<Y.n_elem-1;i+=2) {
      // Compute differences from unity
      double ld=1.0-Y[i-1];
      double md=1.0-Y[i  ];
      double rd=1.0-Y[i+1];
      // Increment profile measure
      phi+=ld+4.0*md+rd;
      nint++;
    }
    break;

  case(2):
    for(size_t i=1;i<Y.n_elem-1;i+=2) {
      // Compute differences from unity
      double ld=1.0-Y[i-1];
      double md=1.0-Y[i  ];
      double rd=1.0-Y[i+1];
      // Increment profile measure
      phi+=ld*ld+4.0*md*md+rd*rd;
      nint++;
    }
    break;

  default:
    ERROR_INFO();
    throw std::runtime_error("Value of n not supported!\n");
  }
  // Plug in normalization factors
  phi/=6.0*nint;

  return phi;
}

void compl_mog_df(const gsl_vector * x, void * params, gsl_vector * g) {
  // Step size to use. Since we are working in logarithmic space, this
  // should be always OK.
  double h=1e-4;

  // It'd be nice to parallellize here, but it seems it causes
  // problems with large amounts of exponents... and I have no idea
  // why.

  // Helper for parameters
  gsl_vector *tmp=gsl_vector_alloc(x->size);

  // Compute gradient using central difference
  for(size_t ip=0; ip < x->size; ip++) {
    // Initialize helper vector
    gsl_vector_memcpy(tmp,x);

    // Original parameter value
    double x0=gsl_vector_get(x,ip);

    // RH value
    gsl_vector_set(tmp,ip,x0+h);
    double rh=compl_mog(tmp,params);

    // LH value
    gsl_vector_set(tmp,ip,x0-h);
    double lh=compl_mog(tmp,params);

    // Gradient value is
    gsl_vector_set(g,ip, (rh-lh)/(2.0*h));
  }

  // Free memory
  gsl_vector_free(tmp);
}

void compl_mog_fdf(const gsl_vector * x, void * params, double * f, gsl_vector * g) {
  *f=compl_mog(x,params);
  compl_mog_df(x,params,g);
}

arma::vec optimize_completeness_simplex(int am, double min, double max, int Nf, int n, bool verbose, double *mog, int nfull) {
  // Optimized exponents
  arma::vec exps;

  // Length of interval is
  double len=max-min;

  // Parameters for the optimization.
  completeness_scan_t pars;
  // Angular momentum
  pars.am=am;
  // Moment to optimize
  pars.n=n;
  // Scanning exponents
  pars.scanexp=get_scanning_exponents(-len/2.0,len/2.0,50*Nf+1);

  // Odd amount of exponents?
  pars.odd=Nf%2;
  // Amount of fully optimized exponents
  pars.nfull=std::min(Nf/2,nfull);
  // Amount of even-tempered exponents
  pars.neven=Nf/2-pars.nfull;

  if(verbose)
    printf("%i exponents fully optimized, %i even-tempered exponents.\n",(int)pars.nfull,(int)pars.neven);

  if(Nf<1) {
    throw std::runtime_error("Cannot completeness-optimize less than one primitive.\n");

  } else if(Nf==1) {
    // Only single exponent at exp(0) = 1.0.
    gsl_vector x;
    x.size=0;

    // Get exponent
    exps=get_exponents(&x,&pars);
    // and the mog
    if(mog!=NULL)
      *mog=compl_mog(&x,(void *) &pars);

  } else {
    // Time minimization
    Timer tmin;

    // Optimized profile will be always symmetric around the midpoint,
    // so we can use this to reduce the amount of degrees of freedom in
    // the optimization. For even amount of exponents, the mid exponent
    // is pinned to the midway of the interval.
    int Ndof=pars.nfull;
    if(pars.neven)
      Ndof++;

    // Maximum number of iterations
    size_t maxiter = 10000;

    // GSL stuff
    const gsl_multimin_fminimizer_type *T = gsl_multimin_fminimizer_nmsimplex2;
    gsl_multimin_fminimizer *s = NULL;
    gsl_multimin_function minfunc;

    size_t iter = 0;
    int status;
    double size;

    /* Starting point: even-tempered exponents */
    arma::vec sp(Nf/2);
    for(int i=0;i<Nf/2;i++)
      sp(i)=log(10.0)*((i+0.5)*len/(2.0*Nf));

    gsl_vector *x = gsl_vector_alloc (Ndof);
    if(pars.neven) {
      // Even-tempered parameter
      gsl_vector_set(x,0,sp(1)-sp(0));
      // Free exponents
      for(size_t fi=0;fi<pars.nfull;fi++)
        gsl_vector_set(x,1+fi,sp(pars.neven+fi));
    } else {
      // Free exponents
      for(size_t fi=0;fi<pars.nfull;fi++)
	gsl_vector_set(x,fi,sp(fi));
    }

    /* Set initial step sizes to 0.1 */
    gsl_vector *ss = gsl_vector_alloc (Ndof);
    gsl_vector_set_all (ss, 0.1);

    /* Initialize method and iterate */
    minfunc.n = Ndof;
    minfunc.f = compl_mog;
    minfunc.params = (void *) &pars;

    s = gsl_multimin_fminimizer_alloc (T, Ndof);
    gsl_multimin_fminimizer_set (s, &minfunc, x, ss);

    // Progress timer
    Timer t;

    // Legend
    if(verbose) {
      printf("Optimizing tau_%i for a=[%.3f ... %.3f] of %c shell with %i exponents.\n\n",n,min,max,shell_types[am],Nf);

      printf("iter ");
      char num[80];
      for(int i=0;i<Ndof;i++) {
	sprintf(num,"lg par%i",i+1);
	printf("%9s ",num);
      }
      printf(" %12s  %12s\n","tau","size");
    }

    do
      {
	iter++;
	status = gsl_multimin_fminimizer_iterate(s);

	if (status)
	  break;

	size = gsl_multimin_fminimizer_size (s);
	status = gsl_multimin_test_size (size, 1e-3);

	if (status == GSL_SUCCESS && verbose)
	  printf ("converged to minimum at\n");

	if(verbose) {
	  t.set();
	  printf("%4u ",(unsigned int) iter);
	  for(int i=0;i<Ndof;i++)
	    // Convert to 10-base logarithm
	    printf("% 9.5f ",log10(M_E)*gsl_vector_get(s->x,i));
	  printf(" %e  %e\n",pow(s->fval,1.0/n),size);

	  // print_gradient(s->x,(void *) &pars);
	}
      }
    while (status == GSL_CONTINUE && iter < maxiter);

    // Save mog
    if(mog!=NULL)
      *mog=pow(s->fval,1.0/n);

    // The optimized exponents in descending order
    exps=arma::sort(get_exponents(s->x,&pars),1);

    gsl_vector_free(x);
    gsl_vector_free(ss);
    gsl_multimin_fminimizer_free (s);

    if(verbose)
      printf("\nMinimization completed in %s.\n",tmin.elapsed().c_str());
  }

  // Move starting point from 0 to start
  exps*=std::pow(10.0,min+len/2.0);

  return exps;
}

arma::vec optimize_completeness(int am, double min, double max, int Nf, int n, bool verbose, double *mog, int nfull) {
  // Optimized exponents
  arma::vec exps;

  // Length of interval is
  double len=max-min;

  // Parameters for the optimization.
  completeness_scan_t pars;
  // Angular momentum
  pars.am=am;
  // Moment to optimize
  pars.n=n;
  // Scanning exponents
  pars.scanexp=get_scanning_exponents(-len/2.0,len/2.0,50*Nf+1);

  // Odd amount of exponents?
  pars.odd=Nf%2;
  // Amount of fully optimized exponents
  pars.nfull=std::min(Nf/2,nfull);
  // Amount of even-tempered exponents
  pars.neven=Nf/2-pars.nfull;

  if(verbose)
    printf("%i exponents fully optimized, %i even-tempered exponents.\n",(int)pars.nfull,(int)pars.neven);

  if(Nf<1) {
    throw std::runtime_error("Cannot completeness-optimize less than one primitive.\n");

  } else if(Nf==1) {
    // Only single exponent at exp(0) = 1.0.
    gsl_vector x;
    x.size=0;

    // Get exponent
    exps=get_exponents(&x,&pars);
    // and the mog
    if(mog!=NULL)
      *mog=compl_mog(&x,(void *) &pars);

  } else {
    // Time minimization
    Timer tmin;

    // Optimized profile will be always symmetric around the midpoint,
    // so we can use this to reduce the amount of degrees of freedom in
    // the optimization. For even amount of exponents, the mid exponent
    // is pinned to the midway of the interval.
    int Ndof=pars.nfull;
    if(pars.neven)
      Ndof++;

    // Maximum number of iterations
    size_t maxiter = 10000;

    // Use Fletcher-Reeves gradient, which works here better than Polak-Ribiere or BFGS.
    const gsl_multimin_fdfminimizer_type *T = gsl_multimin_fdfminimizer_conjugate_fr;
    gsl_multimin_fdfminimizer *s = NULL;
    gsl_multimin_function_fdf minfunc;

    size_t iter = 0;
    int status;

    /* Starting point: even-tempered exponents */
    arma::vec sp(Nf/2);
    for(int i=0;i<Nf/2;i++)
      sp(i)=log(10.0)*((i+0.5)*len/(2.0*Nf));

    gsl_vector *x = gsl_vector_alloc (Ndof);
    if(pars.neven) {
      // Even-tempered parameter
      gsl_vector_set(x,0,sp(1)-sp(0));
      // Free exponents
      for(size_t fi=0;fi<pars.nfull;fi++)
	gsl_vector_set(x,1+fi,sp(pars.neven+fi));
    } else {
      // Free exponents
      for(size_t fi=0;fi<pars.nfull;fi++)
	gsl_vector_set(x,fi,sp(fi));
    }

    get_exponents(x,&pars);

    /* Initialize method and iterate */
    minfunc.n = Ndof;
    minfunc.f = compl_mog;
    minfunc.df = compl_mog_df;
    minfunc.fdf = compl_mog_fdf;
    minfunc.params = (void *) &pars;

    s = gsl_multimin_fdfminimizer_alloc (T, Ndof);

    // Initial step size is 0.01, and the line minimization parameter is 1e-4
    gsl_multimin_fdfminimizer_set (s, &minfunc, x, 0.01, 1e-4);

    // Progress timer
    Timer t;

    // Legend
    if(verbose) {
      printf("Optimizing tau_%i for a=[%.3f ... %.3f] of %c shell with %i exponents.\n\n",n,min,max,shell_types[am],Nf);

      printf("iter ");
      char num[80];
      for(int i=0;i<Ndof;i++) {
	sprintf(num,"lg par%i",i+1);
	printf("%9s ",num);
      }
      printf(" %12s  %12s\n","tau","grad norm");
    }

    do
      {
	iter++;
	status = gsl_multimin_fdfminimizer_iterate(s);

	if (status)
	  break;

	status = gsl_multimin_test_gradient (s->gradient, 1e-8);

	if (status == GSL_SUCCESS && verbose)
	  printf ("converged to minimum at\n");

	if(verbose) {
	  t.set();
	  printf("%4u ",(unsigned int) iter);
	  for(int i=0;i<Ndof;i++)
	    // Convert to 10-base logarithm
	    printf("% 9.5f ",log10(M_E)*gsl_vector_get(s->x,i));
	  printf(" %e  %e\n",pow(s->f,1.0/n),gsl_blas_dnrm2(s->gradient));

	  // print_gradient(s->x,(void *) &pars);
	}
      }
    while (status == GSL_CONTINUE && iter < maxiter);

    /*
    if (status != GSL_SUCCESS)
      printf ("encountered error \"%s\"\n",gsl_strerror(status));
    */

    // Save mog
    if(mog!=NULL)
      *mog=pow(s->f,1.0/n);

    // The optimized exponents in descending order
    exps=arma::sort(get_exponents(s->x,&pars),1);

    gsl_vector_free(x);
    gsl_multimin_fdfminimizer_free (s);

    if(verbose)
      printf("\nMinimization completed in %s.\n",tmin.elapsed().c_str());
  }

  // Move starting point from 0 to start
  exps*=std::pow(10.0,min+len/2.0);

  return exps;
}

double maxwidth(int am, double tol, int nexp, int nval) {
  // Dummy value
  double width=-1.0;
  maxwidth_exps(am,tol,nexp,&width,nval);
  return width;
}

arma::vec maxwidth_exps(int am, double tol, int nexp, double *width, int nval) {
  // Error check
  if(nexp<=0) {
    arma::vec exps;
    return exps;
  }

  if(tol<MINTAU) {
    printf("Renormalized CO tolerance to %e.\n",MINTAU);
    tol=MINTAU;
  }

  // Left value - vanishing plateau width, i.e. tau too small
  double left=0.0;
  double lval=0.0;

  // Right value - pretty big plateau, i.e. tau too big
  double right=0.5*nexp;
  double rval;

  // Check that right value is OK
  arma::vec rexps=optimize_completeness(am,0.0,right,nexp,nval,false,&rval);
  while(rval<tol) {
    // Must have tau too big here
    left=right;
    right*=2.0;
    rexps=optimize_completeness(am,0.0,right,nexp,nval,false,&rval);
  }

  // Check for unitialized left value
  if(left==0.0) {
    left=right;
    lval=rval;
    arma::vec lexps;
    while(lval>tol) {
      // Must have tau too small here
      left/=2.0;
      lexps=optimize_completeness(am,0.0,left,nexp,nval,false,&lval);
    }
  }

  // Refine until in linear regime
  size_t ii;
  const size_t iimax=100;
  for(ii=0;ii<iimax;ii++) {
    // Compute midpoint value
    double middle=(right+left)/2.0;
    double mval;
    arma::vec mexps=optimize_completeness(am,0.0,middle,nexp,nval,false,&mval);

    // Check for linear regime: predicted value is
    double mpred=lval + (middle-left)/(right-left)*(rval-lval);
    //    printf("mpred accuracy: %e\n",fabs((mpred-mval)/mval));

    // and we are converged if the correction term is O(eps)
    bool converged = (fabs((mpred-mval)) <= sqrt(DBL_EPSILON)*fabs(mval));

    // Update limits
    if(mval<tol) {
      left=middle;
      lval=mval;
    } else if(mval>tol) {
      right=middle;
      rval=mval;
    }

    // Stop if converged
    if(converged)
      break;
  }

  if(ii==iimax)
    throw std::runtime_error("Error finding limits in maxwidth_exps.\n");

  // Interpolate to the exact value
  *width=left + (tol-lval)/(rval-lval)*(right-left);

  // Exponents and realized value of tau are
  double tau;
  arma::vec exps=optimize_completeness(am,0.0,*width,nexp,nval,false,&tau);

  //  printf("Interpolation from w = %e .. %e with tau = %e .. %e yielded w = %e, tau = %e.\n",left,right,lval,rval,*width,tau);

  return exps;
}


/// Perform completeness-optimization of exponents
arma::vec get_exponents(int am, double start, double end, double tol, int nval, bool verbose) {
  // Exponents
  arma::vec exps;
  bool succ=false;

  // Work array
  std::vector<arma::vec> expwrk;
  std::vector<double> mog;

  // Sanity check
  if(tol<MINTAU) {
    if(verbose)
      printf("Renormalized CO tolerance to %e.\n",MINTAU);
    tol=MINTAU;
  }

  // Allocate work memory
#ifdef _OPENMP
  int nth=omp_get_max_threads();
#else
  int nth=1;
#endif

  expwrk.resize(nth);
  mog.resize(nth);

  // Do completeness optimization
  int nf=1;
  if(verbose)
    printf("\tNf  tau_%i\n",nval);

  while(nf<=NFMAX) {

#ifdef _OPENMP
#pragma omp parallel
#endif
    {

#ifdef _OPENMP
      int ith=omp_get_thread_num();
#else
      int ith=0;
#endif

#ifdef _OPENMP
#pragma omp for ordered
#endif
      for(int mf=nf;mf<nf+nth;mf++) {
	// Get exponents.
	mog[ith]=-1.0;
	expwrk[ith]=optimize_completeness(am,start,end,mf,nval,false,&(mog[ith]));
#ifdef _OPENMP
#pragma omp ordered
#endif
	if(verbose) {
	  if(mog[ith]<(1+sqrt(DBL_EPSILON))*tol)
	    printf("\t%2i *%e\n",mf,mog[ith]);
	  else
	    printf("\t%2i  %e\n",mf,mog[ith]);
	}
      }
    }

    // Did we achieve the wanted mog?
    for(int i=0;i<nth;i++) {
      if(mog[i]<(1+sqrt(DBL_EPSILON))*tol) {
	// Tolerance achieved. Save exponents.
	exps=expwrk[i];
	succ=true;
	break;
      }
    }

    // Need another break clause here.
    if(succ)
      break;

    // Increase nf
    nf+=nth;
  }

  if(!succ) {
    fprintf(stderr,"Could not get exponents for %c shell with tol=%e.\n",shell_types[am],tol);
    throw std::runtime_error("Unable to achieve wanted tolerance.\n");
  } else if(verbose)
    printf("Wanted tolerance achieved with %i exponents.\n",(int) exps.size());

  return exps;
}
