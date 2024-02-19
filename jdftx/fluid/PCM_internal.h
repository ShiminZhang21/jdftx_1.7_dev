/*-------------------------------------------------------------------
Copyright 2011 Ravishankar Sundararaman

This file is part of JDFTx.

JDFTx is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

JDFTx is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with JDFTx.  If not, see <http://www.gnu.org/licenses/>.
-------------------------------------------------------------------*/

#ifndef JDFTX_ELECTRONIC_PCM_INTERNAL_H
#define JDFTX_ELECTRONIC_PCM_INTERNAL_H

#include <core/matrix3.h>
#include <core/RadialFunction.h>

//! @addtogroup Solvation
//! @{
//! @file PCM_internal.h Internal implementation shared by solvation models

//----------- Common PCM functions (top level interface not seen by .cu files) ------------
#ifndef __in_a_cu_file__

#include <core/Operators.h>
#include <core/EnergyComponents.h>
#include <fluid/FluidSolverParams.h>

//! Original shape function from \cite JDFT, \cite PCM-Kendra and \cite NonlinearPCM
namespace ShapeFunction
{
	//! Compute the shape function (0 to 1) given the cavity-determining electron density
	void compute(const ScalarField& n, ScalarField& shape, double nc, double sigma);

	//! Propagate gradient w.r.t shape function to that w.r.t cavity-determining electron density (accumulate to E_n)
	void propagateGradient(const ScalarField& n, const ScalarField& E_shape, ScalarField& E_n, double nc, double sigma);
}

//! Shape function in CANDLE \cite CANDLE
namespace ShapeFunctionCANDLE
{
	//! Compute shape function that includes charge asymmetry from cavity-determining electron density and vacuum electric potential
	void compute(const ScalarField& n, const ScalarFieldTilde& phi,
		ScalarField& shape, double nc, double sigma, double pCavity);
	
	//! Propagate gradients w.r.t shape function to n, phi and pCavity (accumulate to E_n, E_phi, E_pCavity)
	void propagateGradient(const ScalarField& n, const ScalarFieldTilde& phi, const ScalarField& E_shape,
		ScalarField& E_n, ScalarFieldTilde& E_phi, double& E_pCavity, double nc, double sigma, double pCavity, matrix3<>* E_RRT=0);
}

//! Shape function for \cite CavityWDA
namespace ShapeFunctionSGA13
{
	//! Compute expanded density nEx from n, and optionally propagate gradients from nEx to n (accumulate to A_n)
	void expandDensity(const RadialFunctionG& w, double R, const ScalarField& n, ScalarField& nEx, const ScalarField* A_nEx=0, ScalarField* A_n=0, matrix3<>* E_RRT=0);
}

//! Shape function for the soft-sphere model \cite PCM-SoftSphere
namespace ShapeFunctionSoftSphere
{
	//! Compute the shape function (0 to 1) given list of atom lattice coordinates x and sphere radii
	void compute(const std::vector<vector3<>>& x, const std::vector<vector3<int>>& reps, const std::vector<double>& radius, ScalarField& shape, double sigma);

	//! Propagate gradient w.r.t shape function to that w.r.t atomic positions
	void propagateGradient(const std::vector<vector3<>>& x, const std::vector<vector3<int>>& reps, const std::vector<double>& radius,
		const ScalarField& shape, const ScalarField& E_shape, std::vector<vector3<>>& E_x, std::vector<double>& E_radius, double sigma);
}

//! Shape function for SCCS models \cite PCM-SCCS
namespace ShapeFunctionSCCS
{
	//! Compute the shape function (0 to 1) given the cavity-determining electron density
	void compute(const ScalarField& n, ScalarField& shape, double rhoMin, double rhoMax, double epsBulk);

	//! Propagate gradient w.r.t shape function to that w.r.t cavity-determining electron density (accumulate to E_n)
	void propagateGradient(const ScalarField& n, const ScalarField& E_shape, ScalarField& E_n, double rhoMin, double rhoMax, double epsBulk);
}

#endif


//--------- Compute kernels (shared by CPU and GPU implementations) --------
//! @cond

namespace ShapeFunction
{
	__hostanddev__ void compute_calc(int i, const double* nCavity, double* shape, const double nc, const double sigma)
	{	shape[i] = erfc(sqrt(0.5)*log(fabs(nCavity[i])/nc)/sigma)*0.5;
	}
	__hostanddev__ void propagateGradient_calc(int i, const double* nCavity, const double* grad_shape, double* grad_nCavity, const double nc, const double sigma)
	{	grad_nCavity[i] += (-1.0/(nc*sigma*sqrt(2*M_PI))) * grad_shape[i]
			* exp(0.5*(pow(sigma,2) - pow(log(fabs(nCavity[i])/nc)/sigma + sigma, 2)));
	}
}

namespace ShapeFunctionCANDLE
{
	//version with electric-field based charge asymmetry (combined compute and grad function)
	__hostanddev__ void compute_or_grad_calc(int i, bool grad,
		const double* nArr, vector3<const double*> DnArr, vector3<const double*> DphiArr, double* shape,
		const double* A_shape, double* A_n, vector3<double*> A_Dn, vector3<double*> A_Dphi, double* A_pCavity,
		const double nc, const double invSigmaSqrt2, const double pCavity)
	{	double n = nArr[i];
		if(n<1e-8) { if(!grad) shape[i]=1.; return; }
		//Regularized unit vector along Dn:
		vector3<> Dn = loadVector(DnArr,i);
		double normFac = 1./sqrt(Dn.length_squared() + 1e-4*nc*nc);
		vector3<> e = Dn * normFac;
		//Electric field along above unit vector, with saturation for stability:
		vector3<> E = -loadVector(DphiArr,i);
		double eDotE = dot(e,E);
		const double x_eDotE = -fabs(pCavity);
		double x = x_eDotE * eDotE;
		double asymm=0., asymm_x=0.;
		//modify cavity only for anion-like regions
		if(x > 4.) { asymm = 1.; asymm_x = 0.; } //avoid Inf/Inf error
		else if(x > 0.)
		{	double exp2x2 = exp(2.*x*x), den = 1./(1 + exp2x2);
			asymm = (exp2x2 - 1.) * den; //tanh(x^2)
			asymm_x = 8.*x * exp2x2 * den*den; //2x sech(x^2)
		}
		const double dlognMax = copysign(3., pCavity);
		double comb = log(n/nc) - dlognMax*asymm;
		if(!grad)
			shape[i] = 0.5*erfc(invSigmaSqrt2*comb);
		else
		{	double A_comb = (-invSigmaSqrt2/sqrt(M_PI)) * A_shape[i] * exp(-comb*comb*invSigmaSqrt2*invSigmaSqrt2);
			A_n[i] += A_comb/n;
			double A_x = A_comb*(-dlognMax)*asymm_x;
			accumVector((A_x*x_eDotE*normFac) * (E - e*eDotE), A_Dn,i);
			accumVector((A_x*x_eDotE*(-1.)) * e, A_Dphi,i);
			A_pCavity[i] += A_x*(-copysign(1.,pCavity))*eDotE;
		}
	}
}

namespace ShapeFunctionSGA13
{
	__hostanddev__ void expandDensity_calc(int i, double alpha, const double* nBar, const double* DnBarSq, double* nEx, double* nEx_nBar, double* nEx_DnBarSq)
	{	double n = nBar[i], D2 = DnBarSq[i];
		if(n < 1e-9) //Avoid numerical error in low density / gradient regions:
		{	nEx[i]=1e-9;
			if(nEx_nBar) nEx_nBar[i]=0.;
			if(nEx_DnBarSq) nEx_DnBarSq[i]=0.;
			return;
		}
		double nInv = 1./n;
		nEx[i] = alpha*n + D2*nInv;
		if(nEx_nBar) { nEx_nBar[i] = alpha - D2*nInv*nInv; }
		if(nEx_DnBarSq) { nEx_DnBarSq[i] = nInv; }
	}
}

//Cavity shape function and gradient for the soft-sphere model
namespace ShapeFunctionSoftSphere
{
	__hostanddev__ void compute_calc(int i, const vector3<int>& iv, const vector3<>& Sinv, const matrix3<>& RTR,
		int nAtoms, const vector3<>* x, int nReps, const vector3<int>* reps, const double* radius, double* shape, double sigmaInv)
	{	double s = 1.;
		for(int iAtom=0; iAtom<nAtoms; iAtom++)
		{	vector3<> dx0;
			for(int iDir=0; iDir<3; iDir++)
			{	dx0[iDir] = x[iAtom][iDir] - iv[iDir]*Sinv[iDir]; //lattice coodinate displacement
				dx0[iDir] -= floor(0.5+dx0[iDir]); //wrap to [-0.5,0.5]
			}
			for(int iRep=0; iRep<nReps; iRep++)
			{	vector3<> dx = dx0 + reps[iRep];
				double dr = sqrt(RTR.metric_length_squared(dx));
				s *= 0.5*erfc(sigmaInv*(radius[iAtom]-dr));
			}
		}
		shape[i] = s;
	}
	
	__hostanddev__ void propagateGradient_calc(int i, const vector3<int>& iv, const vector3<>& Sinv, const matrix3<>& RTR,
		const vector3<>& x, int nReps, const vector3<int>* reps, double radius, const double* shape,
		const double* E_shape, vector3<double*> E_x, double* E_radius, double sigmaInv)
	{	double s = shape[i];
		vector3<> dx0;
		for(int iDir=0; iDir<3; iDir++)
		{	dx0[iDir] = x[iDir] - iv[iDir]*Sinv[iDir]; //lattice coodinate displacement
			dx0[iDir] -= floor(0.5+dx0[iDir]); //wrap to [-0.5,0.5]
		}
		vector3<> E_xCur;
		double E_radiusCur = 0.;
		if(s > 1e-14) //avoid 0/0 below
		{	for(int iRep=0; iRep<nReps; iRep++)
			{	vector3<> dx = dx0 + reps[iRep];
				double dr = sqrt(RTR.metric_length_squared(dx));
				if(dr<1e-14) continue; //avoid 0/0 below
				double drComb = sigmaInv*(radius-dr);
				double sContrib = 0.5*erfc(drComb);
				double sContrib_dr = (sigmaInv/sqrt(M_PI)) * exp(-drComb*drComb);
				double E_dr = (E_shape[i]*s/sContrib) * sContrib_dr;
				E_xCur += (E_dr/dr) * (RTR*dx);
				E_radiusCur -= E_dr;
			}
		}
		storeVector(E_xCur, E_x, i);
		E_radius[i] = E_radiusCur;
	}
}

//Cavity shape function and gradient for the SCCS models
namespace ShapeFunctionSCCS
{
	__hostanddev__ void compute_calc(int i, const double* nCavity, double* shape,
		const double rhoMin, const double rhoMax, const double epsBulk)
	{	double rho = nCavity[i];
		if(rho >= rhoMax) { shape[i] = 0.; return; }
		if(rho <= rhoMin) { shape[i] = 1.; return; }
		const double logDen = log(rhoMax/rhoMin);
		double f = log(rhoMax/rho)/logDen;
		double t = f - sin(2*M_PI*f)/(2*M_PI);
		shape[i] = (pow(epsBulk,t) - 1.)/(epsBulk - 1.);
	}
	__hostanddev__ void propagateGradient_calc(int i, const double* nCavity, const double* grad_shape, double* grad_nCavity,
		const double rhoMin, const double rhoMax, const double epsBulk)
	{	double rho = nCavity[i];
		if(rho >= rhoMax) return;
		if(rho <= rhoMin) return;
		const double logDen = log(rhoMax/rhoMin);
		double f = log(rhoMax/rho)/logDen;
		double f_rho = -1./(rho*logDen); //df/drho
		double t = f - sin(2*M_PI*f)/(2*M_PI);
		double t_f = 1. - cos(2*M_PI*f); //dt/df
		double s_t = log(epsBulk) * pow(epsBulk,t)/(epsBulk - 1.); //dshape/dt
		grad_nCavity[i] += grad_shape[i] * s_t * t_f * f_rho; //chain rule
	}
}
//! @endcond


//! Helper classes for NonlinearPCM
namespace NonlinearPCMeval
{
	//!Helper class for ionic screening portion of NonlinearPCM
	struct Screening
	{
		bool linear; //!< whether ionic screening is linearized
		double NT, ZbyT, NZ; //!< where T=temperature, N=bulk ionic concentration, Z=charge (all assumed +/- symmetric)
		double x0plus, x0minus, x0; //!< anion, cation and total packing fractions
		
		Screening(bool linear, double T, double Nion, double Zion, double VhsPlus, double VhsMinus, double epsBulk); //epsBulk is used only for printing screening length
		
		#ifndef __in_a_cu_file__
		//! Compute the neutrality Lagrange multiplier mu0 and optionally its derivatives
		inline double neutralityConstraint(const ScalarField& muPlus, const ScalarField& muMinus, const ScalarField& shape, double Qexp,
			ScalarField* mu0_muPlus=0, ScalarField* mu0_muMinus=0, ScalarField* mu0_shape=0, double* mu0_Qexp=0)
		{
			if(linear)
			{	double Qsum = NZ * 2.*integral(shape);
				double Qdiff = NZ * integral(shape*(muPlus+muMinus));
				//Compute the constraint function and its derivatives w.r.t above moments:
				double mu0 = -(Qexp + Qdiff)/Qsum;
				double mu0_Qdiff = -1./Qsum;
				double mu0_Qsum = (Qexp + Qdiff)/(Qsum*Qsum);
				//Collect reuslt and optional gradients:
				if(mu0_muPlus) *mu0_muPlus = (mu0_Qdiff * NZ) * shape;
				if(mu0_muMinus) *mu0_muMinus = (mu0_Qdiff * NZ) * shape;
				if(mu0_shape) *mu0_shape = NZ * (mu0_Qdiff*(muPlus+muMinus) + mu0_Qsum*2.);
				if(mu0_Qexp) *mu0_Qexp = -1./Qsum;
				return mu0;
			}
			else
			{	ScalarField etaPlus  = exp(muPlus);
				ScalarField etaMinus = exp(-muMinus);
				double Qplus  = +NZ * integral(shape * etaPlus);
				double Qminus = -NZ * integral(shape * etaMinus);
				//Compute the constraint function and its derivatives w.r.t above moments:
				double mu0, mu0_Qplus, mu0_Qminus;
				double disc = sqrt(Qexp*Qexp - 4.*Qplus*Qminus); //discriminant for quadratic
				//Pick the numerically stable path (to avoid roundoff problems when |Qplus*Qminus| << Qexp^2):
				if(Qexp<0) 
				{	mu0 = log((disc-Qexp)/(2.*Qplus));
					mu0_Qplus  = -2.*Qminus/(disc*(disc-Qexp)) - 1./Qplus;
					mu0_Qminus = -2.*Qplus/(disc*(disc-Qexp));
				}
				else
				{	mu0 = log(-2.*Qminus/(disc+Qexp));
					mu0_Qplus  = 2.*Qminus/(disc*(disc+Qexp));
					mu0_Qminus = 2.*Qplus/(disc*(disc+Qexp)) + 1./Qminus;
				}
				//Collect result and optional gradients:
				if(mu0_muPlus)  *mu0_muPlus  = (mu0_Qplus * NZ)  * shape * etaPlus;
				if(mu0_muMinus) *mu0_muMinus = (mu0_Qminus * NZ) * shape * etaMinus;
				if(mu0_shape) *mu0_shape = NZ * (mu0_Qplus * etaPlus - mu0_Qminus * etaMinus);
				if(mu0_Qexp) *mu0_Qexp = -1./disc;
				return mu0;
			}
		}
		#endif
		
		//! Hard sphere free energy per particle and derivative, where x is total packing fraction
		__hostanddev__ double fHS(double xIn, double& f_xIn) const
		{	double x = xIn, x_xIn = 1.;
			if(xIn > 0.5) //soft packing: remap [0.5,infty) on to [0.5,1)
			{	double xInInv = 1./xIn;
				x = 1.-0.25*xInInv;
				x_xIn = 0.25*xInInv*xInInv;
			}
			double den = 1./(1-x), den0 = 1./(1-x0);
			double comb = (x-x0)*den*den0, comb_x = den*den;
			double prefac = (2./x0);
			double f = prefac * comb*comb;
			f_xIn = prefac * 2.*comb*comb_x * x_xIn;
			return f;
		}
		
		//! Compute the nonlinear functions in the free energy and charge density prior to scaling by shape function
		//! Note that each mu here is mu(r) + mu0, i.e. after imposing charge neutrality constraint
		__hostanddev__ void compute(double muPlus, double muMinus, double& F, double& F_muPlus, double& F_muMinus, double& Rho, double& Rho_muPlus, double& Rho_muMinus) const
		{	if(linear)
			{	F = NT * 0.5*(muPlus*muPlus + muMinus*muMinus);
				F_muPlus = NT * muPlus;
				F_muMinus = NT * muMinus;
				Rho = NZ * (muPlus + muMinus);
				Rho_muPlus = NZ;
				Rho_muMinus = NZ;
			}
			else
			{	double etaPlus = exp(muPlus), etaMinus=exp(-muMinus);
				double x = x0plus*etaPlus + x0minus*etaMinus; //packing fraction
				double f_x, f = fHS(x, f_x); //hard sphere free energy per particle
				F = NT * (2. + etaPlus*(muPlus-1.) + etaMinus*(-muMinus-1.) + f);
				F_muPlus  = NT * etaPlus *(muPlus  + f_x * x0plus);
				F_muMinus = NT * etaMinus*(muMinus - f_x * x0minus);
				Rho = NZ * (etaPlus - etaMinus);
				Rho_muPlus  = NZ * etaPlus;
				Rho_muMinus = NZ * etaMinus;
			}
		}
		
		//! Given shape function s and potential mu, compute induced charge rho, free energy density A and accumulate its derivatives
		__hostanddev__ void freeEnergy_calc(size_t i, double mu0, const double* muPlus, const double* muMinus, const double* s, double* rho, double* A, double* A_muPlus, double* A_muMinus, double* A_s) const
		{	double F, F_muPlus, F_muMinus, Rho, Rho_muPlus, Rho_muMinus;
			compute(muPlus[i]+mu0, muMinus[i]+mu0, F, F_muPlus, F_muMinus, Rho, Rho_muPlus, Rho_muMinus);
			rho[i] = s[i] * Rho;
			A[i] = s[i] * F;
			if(A_muPlus) A_muPlus[i] += s[i] * F_muPlus;
			if(A_muMinus) A_muMinus[i] += s[i] * F_muMinus;
			if(A_s) A_s[i] += F;
		}
		void freeEnergy(size_t N, double mu0, const double* muPlus, const double* muMinus, const double* s, double* rho, double* A, double* A_muPlus, double* A_muMinus, double* A_s) const;
		#ifdef GPU_ENABLED
		void freeEnergy_gpu(size_t N, double mu0, const double* muPlus, const double* muMinus, const double* s, double* rho, double* A, double* A_muPlus, double* A_muMinus, double* A_s) const;
		#endif
		
		//! Propagate derivative A_rho and accumulate to A_mu and A_s
		__hostanddev__ void convertDerivative_calc(size_t i, double mu0, const double* muPlus, const double* muMinus, const double* s, const double* A_rho, double* A_muPlus, double* A_muMinus, double* A_s) const
		{	double F, F_muPlus, F_muMinus, Rho, Rho_muPlus, Rho_muMinus;
			compute(muPlus[i]+mu0, muMinus[i]+mu0, F, F_muPlus, F_muMinus, Rho, Rho_muPlus, Rho_muMinus);
			A_muPlus[i] += s[i] * Rho_muPlus * A_rho[i];
			A_muMinus[i] += s[i] * Rho_muMinus * A_rho[i];
			if(A_s) A_s[i] += Rho * A_rho[i];
		}
		void convertDerivative(size_t N, double mu0, const double* muPlus, const double* muMinus, const double* s, const double* A_rho, double* A_muPlus, double* A_muMinus, double* A_s) const;
		#ifdef GPU_ENABLED
		void convertDerivative_gpu(size_t N, double mu0, const double* muPlus, const double* muMinus, const double* s, const double* A_rho, double* A_muPlus, double* A_muMinus, double* A_s) const;
		#endif
		
		//! Root function used for finding packing fraction x at a given dimensionless potential V = Z phi / T
		__hostanddev__ double rootFunc(double x, double V) const
		{	double f_x; fHS(x, f_x); //hard sphere potential
			return x - (x0plus*exp(-V-f_x*x0plus) + x0minus*exp(+V-f_x*x0minus));
		}
		
		//! Calculate self-consistent packing fraction x at given dimensionless potential V = Z phi / T using a bisection method
		__hostanddev__ double x_from_V(double V) const
		{	double xLo = x0; while(rootFunc(xLo, V) > 0.) xLo *= 0.5;
			double xHi = xLo; while(rootFunc(xHi, V) < 0.) xHi *= 2.;
			double x = 0.5*(xHi+xLo);
			double dx = x*1e-13;
			while(xHi-xLo > dx)
			{	if(rootFunc(x, V) < 0.)
					xLo = x;
				else
					xHi = x;
				x = 0.5*(xHi+xLo);
			}
			return x;
		}
		
		//! Given shape function s and phi, calculate state mu's if setState=true or effective kappaSq if setState=false
		__hostanddev__ void phiToState_calc(size_t i, const double* phi, const double* s, const RadialFunctionG& xLookup, bool setState, double* muPlus, double* muMinus, double* kappaSq) const
		{	double V = ZbyT * phi[i];
			if(!setState)
			{	//Avoid V=0 in calculating kappaSq below
				if(fabs(V) < 1e-7)
					V = copysign(1e-7, V);
			}
			double twoCbrtV= 2.*pow(fabs(V), 1./3);
			double Vmapped = copysign(twoCbrtV / (1. + sqrt(1. + twoCbrtV*twoCbrtV)), V);
			double xMapped = xLookup(1.+Vmapped);
			double x = 1./xMapped - 1.;
			double f_x; fHS(x, f_x); //hard sphere potential
			double logEtaPlus = -V - f_x*x0plus;
			double logEtaMinus = +V - f_x*x0minus;
			if(setState)
			{	muPlus[i] = logEtaPlus;
				muMinus[i] = -logEtaMinus;
			}
			else
				kappaSq[i] = (4*M_PI)*s[i]*(NZ*ZbyT)*(exp(logEtaMinus) - exp(logEtaPlus))/V;
		}
		void phiToState(size_t N, const double* phi, const double* s, const RadialFunctionG& xLookup, bool setState, double* muPlus, double* muMinus, double* kappaSq) const;
		#ifdef GPU_ENABLED
		void phiToState_gpu(size_t N, const double* phi, const double* s, const RadialFunctionG& xLookup, bool setState, double* muPlus, double* muMinus, double* kappaSq) const;
		#endif

	};
	
	//!Helper class for dielectric portion of NonlinearPCM
	struct Dielectric
	{
		bool linear; //!< whether dielectric is linearized
		double Np, pByT, NT; //!< N*p, p/T and N*T where N is molecular density and p is molecular dipole
		double alpha, X; //!< dipole correlation factor and chi*T/p^2 where chi is the molecular susceptibility
		
		Dielectric(bool linear, double T, double Nmol, double pMol, double epsBulk, double epsInf);
		
		//! Calculate the various nonlinear functions of epsilon used in calculating the free energy and its derivatives
		__hostanddev__ void calcFunctions(double eps, double& frac, double& logsinch) const
		{	double epsSq = eps*eps;
			if(linear)
			{	frac = 1.0/3;
				logsinch = epsSq*(1.0/6);
			}
			else
			{	if(eps < 1e-1) //Use series expansions
				{	frac = 1.0/3 + epsSq*(-1.0/45 + epsSq*(2.0/945 + epsSq*(-1.0/4725)));
					logsinch = epsSq*(1.0/6 + epsSq*(-1.0/180 + epsSq*(1.0/2835)));
				}
				else
				{	frac = (eps/tanh(eps)-1)/epsSq;
					logsinch = eps<20. ? log(sinh(eps)/eps) : eps - log(2.*eps);
				}
			}
		}
		
		//! Calculate x = pMol E / T given eps
		__hostanddev__ double x_from_eps(double eps) const
		{	double frac, logsinch;
			calcFunctions(eps, frac, logsinch);
			return eps*(1. - alpha*frac);
		}
		
		//! Invert x_from_eps() using a bisection method. Note that x must be positive and finite.
		__hostanddev__ double eps_from_x(double x) const
		{	if(!x) return 0.;
			double epsLo = x; while(x_from_eps(epsLo) > x) epsLo *= 0.95;
			double epsHi = epsLo; while(x_from_eps(epsHi) < x) epsHi *= 1.05;
			double eps = 0.5*(epsHi+epsLo);
			double deps = eps*1e-13;
			while(epsHi-epsLo > deps)
			{	if(x_from_eps(eps) < x)
					epsLo = eps;
				else
					epsHi = eps;
				eps = 0.5*(epsHi+epsLo);
			}
			return eps;
		}
		
		//! Apply nonlinear susceptibility and compute corresponding energy.
		//! The susceptibility is applied in-place on Dphi, and energy density is returned in A.
		__hostanddev__ void apply_calc(size_t i, const RadialFunctionG& dielEnergyLookup,
			const double* s, vector3<double*> Dphi, double* A) const
		{
			vector3<> Evec = loadVector(Dphi, i);
			double E = Evec.length();
			double x = pByT * E;
			double inv_x_plus_1 = 1.0 / (1 + x);
			double xMapped = x * inv_x_plus_1;
			double energy_by_x_sq = dielEnergyLookup(xMapped);
			double energy = energy_by_x_sq * (x * x);
			double energy_E_by_E = (
				dielEnergyLookup.deriv(xMapped) * xMapped * inv_x_plus_1
				+ 2.0 * energy_by_x_sq) * (pByT * pByT);
			constexpr double oneBy4pi = 1.0/(4 * M_PI);
			A[i] = (oneBy4pi * 0.5) * (E * E) + s[i] * energy;
			double A_E_by_E = oneBy4pi + s[i] * energy_E_by_E;
			storeVector(A_E_by_E * Evec, Dphi, i);
		}
		void apply(size_t N, const RadialFunctionG& dielEnergyLookup,
			const double* s, vector3<double*> Dphi, double* A) const;
		#ifdef GPU_ENABLED
		void apply_gpu(size_t N, const RadialFunctionG& dielEnergyLookup,
			const double* s, vector3<double*> Dphi, double* A) const;
		#endif

		//! Compute variational internal free energy of dielectric and its derivatives.
		//! Optionally also collect polarization density p (only needed for stress).
		__hostanddev__ void freeEnergy_calc(size_t i, const RadialFunctionG& gLookup,
				const double* s, vector3<const double*> Dphi,
				double* A, double* A_s, vector3<double*> p) const
		{
			//! Get eps from field:
			vector3<> Evec = loadVector(Dphi, i); //technically -E, but only magnitude matters (except in p below)
			double Esq = Evec.length_squared();
			double x = pByT * sqrt(Esq);
			double g = gLookup(x/(1.+x));
			double eps = x * g;
			
			//! Compute internal free energy and its derivatives:
			double frac, logsinch;
			calcFunctions(eps, frac, logsinch);
			double screen = 1 - alpha*frac; //correlation screening factor = (pE/T) / eps where E is the real electric field
			double F = NT * (eps*eps*(frac - 0.5*alpha*frac*frac + 0.5*X*screen*screen) - logsinch);
			A[i] = F * s[i];
			A_s[i] += F;
			
			//! Compute contributions through polarization:
			double chi = -pByT * g * Np * (frac + X*screen);  //ratio of p to Dphi (hence - sign)
			A_s[i] += chi * Esq;
			if(p[0]) storeVector((chi * s[i]) * Evec, p, i);
		}
		void freeEnergy(size_t N, const RadialFunctionG& gLookup,
				const double* s, vector3<const double*> Dphi,
				double* A, double* A_s, vector3<double*> p) const;
		#ifdef GPU_ENABLED
		void freeEnergy_gpu(size_t N, const RadialFunctionG& gLookup,
				const double* s, vector3<const double*> Dphi,
				double* A, double* A_s, vector3<double*> p) const;
		#endif
	};
}

//! Convenient macro for dumping scalar fields in dumpDensities() or dumpDebug()
#define FLUID_DUMP(object, suffix) \
		filename = filenamePattern; \
		filename.replace(filename.find("%s"), 2, suffix); \
		logPrintf("Dumping '%s'... ", filename.c_str());  logFlush(); \
		if(mpiWorld->isHead()) saveRawBinary(object, filename.c_str()); \
		logPrintf("done.\n"); logFlush();


//! @}
#endif // JDFTX_ELECTRONIC_PCM_INTERNAL_H
