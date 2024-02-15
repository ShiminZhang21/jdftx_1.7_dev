/*-------------------------------------------------------------------
Copyright 2011 Ravishankar Sundararaman, Kendra Letchworth Weaver, Deniz Gunceler

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


#ifndef JDFTX_ELECTRONIC_NONLINEARPCM_H
#define JDFTX_ELECTRONIC_NONLINEARPCM_H

#include <fluid/PCM.h>
#include <fluid/NonlinearCommon.h>
#include <core/VectorField.h>
#include <core/Minimize.h>
#include <core/Pulay.h>

//! @addtogroup Solvation
//! @{
//! @file NonlinearPCM.h NonlinearPCM and helper classes

typedef ScalarFieldMultiplet<ScalarFieldData,5> ScalarFieldMuEps; //!< ion chemical potentials and effective local electric field

//! Nonlinear solvation models: shared electrostatic part implementation
class NonlinearPCM : public PCM, public Minimizable<ScalarFieldMuEps>, public Pulay<ScalarFieldTilde>, public NonlinearCommon
{
public:
	ScalarFieldMuEps state; //!< State of the solver = ion chemical potentials and effective local electric field

	//! See createFluidSolver()
	NonlinearPCM(const Everything& e, const FluidSolverParams& params);
    virtual ~NonlinearPCM();
	
	bool prefersGummel() const { return true; }

	void loadState(const char* filename); //!< Load state from file
	void saveState(const char* filename) const; //!< Save state to file
	void dumpDensities(const char* filenamePattern) const;
	void minimizeFluid(); //!< Converge using nonlinear conjugate gradients

	//! Compute gradient and free energy (with optional outputs)
	double operator()(const ScalarFieldMuEps& state, ScalarFieldMuEps& Adiel_state,
		ScalarFieldTilde* Adiel_rhoExplicitTilde=0, ScalarFieldTilde* Adiel_nCavityTilde=0, IonicGradient* forces=0, matrix3<>* Adiel_RRT=0) const;

	// Interface for Minimizable:
	void step(const ScalarFieldMuEps& dir, double alpha);
	double compute(ScalarFieldMuEps* grad, ScalarFieldMuEps* Kgrad);

protected:
	void set_internal(const ScalarFieldTilde& rhoExplicitTilde, const ScalarFieldTilde& nCavityTilde);
	double get_Adiel_and_grad_internal(ScalarFieldTilde& Adiel_rhoExplicitTilde, ScalarFieldTilde& Adiel_nCavityTilde, IonicGradient* extraForces, matrix3<>* Adiel_RRT) const;

private:
	RadialFunctionG preconditioner; //!< preconditioner for minimizer version
	std::shared_ptr<RealKernel> metric; //!< Pulay metric for SCF version
	std::shared_ptr<class LinearPCM> linearPCM;

protected:
	//Interface for Pulay<ScalarFieldTilde>
	double cycle(double dEprev, std::vector<double>& extraValues);
	void axpy(double alpha, const ScalarFieldTilde& X, ScalarFieldTilde& Y) const { ::axpy(alpha, X, Y); }
	double dot(const ScalarFieldTilde& X, const ScalarFieldTilde& Y) const { return ::dot(X, Y); }
	size_t variableSize() const { return gInfo.nG * sizeof(complex); }
	void readVariable(ScalarFieldTilde& X, FILE* fp) const;
	void writeVariable(const ScalarFieldTilde& X, FILE* fp) const;
	ScalarFieldTilde getVariable() const;
	void setVariable(const ScalarFieldTilde&);
	ScalarFieldTilde precondition(const ScalarFieldTilde&) const;
	ScalarFieldTilde applyMetric(const ScalarFieldTilde&) const;
private:
	void phiToState(bool setState); //!< update state if setState=true and epsilon/kappaSq in linearPCM if setState=false from the current phi
};

//! @}
#endif // JDFTX_ELECTRONIC_NONLINEARPCM_H
