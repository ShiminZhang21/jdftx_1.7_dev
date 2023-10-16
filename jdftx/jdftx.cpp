/*-------------------------------------------------------------------
Copyright 2011 Ravishankar Sundararaman, Kendra Letchworth Weaver

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

#include <electronic/Everything.h>
#include <electronic/ColumnBundle.h>
#include <core/matrix.h>
#include <electronic/Dump.h>
#include <electronic/ElecMinimizer.h>
#include <electronic/LatticeMinimizer.h>
#include <electronic/Vibrations.h>
#include <electronic/IonicDynamics.h>
#include <electronic/PerturbationSolver.h>
#include <electronic/TestPerturbation.h>
#include <fluid/FluidSolver.h>
#include <core/Util.h>
#include <commands/parser.h>

//Program entry point
int main(int argc, char** argv)
{	//Parse command line, initialize system and logs:
	Everything e; //the parent data structure for, well, everything
	InitParams ip("Performs Joint Density Functional Theory calculations.", &e);
	initSystemCmdline(argc, argv, ip);
	
	//Parse input file and setup
	ElecVars& eVars = e.eVars;
	parse(readInputFile(ip.inputFilename), e, ip.printDefaults);
	if(ip.dryRun) eVars.skipWfnsInit = true;
	e.setup();
	e.dump(DumpFreq_Init, 0);
	Citations::print();
	if(ip.dryRun)
	{	logPrintf("Dry run successful: commands are valid and initialization succeeded.\n");
		finalizeSystem();
		return 0;
	}
	else logPrintf("Initialization completed successfully at t[s]: %9.2lf\n\n", clock_sec());
	logFlush();
	
	if(e.cntrl.dumpOnly)
	{	//Single energy calculation so that all dependent quantities have been initialized:
		if(eVars.isRandom)
			die("Dump-only mode requires wfns to be read in using initial-state or wavefunction.\n\n");
		if(e.eInfo.fillingsUpdate==ElecInfo::FillingsHsub and (not eVars.HauxInitialized))
			die("Dump-only mode with smearing requires eigenvals to be read in using initial-state.\n\n");
		logPrintf("\n----------- Energy evaluation at fixed state -------------\n"); logFlush();
		eVars.elecEnergyAndGrad(e.ener, 0, 0, true); //calculate Hsub so that eigenvalues are available (used by many dumps)
		logPrintf("# Energy components:\n"); e.ener.print(); logPrintf("\n");
	}
	else if(e.cntrl.fixed_H)
	{	//Band structure calculation - ion and fluid minimization need to be handled differently
		if(eVars.nFilenamePattern.length())
		{	//If starting from density, compute potential:
			eVars.EdensityAndVscloc(e.ener);
			if(eVars.fluidSolver && eVars.fluidSolver->useGummel())
			{	//Relies on the gummel loop, so EdensityAndVscloc would not have invoked minimize
				eVars.fluidSolver->minimizeFluid();
				eVars.EdensityAndVscloc(e.ener); //update Vscloc
			}
		}
		if(e.exCorr.exxFactor() and e.eVars.isRandom)
			die("Fixed Hamiltonian calculations with EXX require occupied wavefunctions to be read in (use initial-state or wavefunction commands).\n");
		e.iInfo.augmentDensityGridGrad(eVars.Vscloc); //update Vscloc atom projections for ultrasoft psp's 
		logPrintf("\n----------- Band structure minimization -------------\n"); logFlush();
		bandMinimize(e); // Do the band-structure minimization
		//Update fillings if necessary:
		if(e.eInfo.fillingsUpdate == ElecInfo::FillingsHsub)
		{	//Calculate mu from nElectrons:
			double Bz, mu = e.eInfo.findMu(eVars.Hsub_eigs, e.eInfo.nElectrons, Bz);
			//Update fillings:
			for(int q=e.eInfo.qStart; q<e.eInfo.qStop; q++)
				eVars.F[q] = e.eInfo.smear(e.eInfo.muEff(mu,Bz,q), eVars.Hsub_eigs[q]);
			//Update TS and muN:
			e.eInfo.updateFillingsEnergies(eVars.Hsub_eigs, e.ener);
			e.eInfo.smearReport();
		}
	}
	else if(e.vibrations) //Bypasses ionic/lattice minimization, calls electron/fluid minimization loops at various ionic configurations
	{	e.vibrations->calculate();
	}
	else if(e.ionicDynParams.nSteps)
	{	//Born-Oppenheimer molecular dynamics
		IonicDynamics idyn(e);
		idyn.run();
	}
	else if(e.latticeMinParams.nIterations)
	{	//Lattice minimization loop (which invokes the ionic minimization loop)
		LatticeMinimizer lmin(e);
		lmin.minimize(e.latticeMinParams);
	}
	else if(e.vptParams.nIterations)
	{
		if (e.spring) {
			e.spring->computeSubMatrix();
		} else {
			//Variational perturbation solver
			PerturbationSolver ps(e);
			if (e.vptInfo.testing) {
				TestPerturbation testps(e, ps);
				testps.testVPT();
			} else {
				ps.solvePerturbation();
			}
		}
	}
	else
	{	//Ionic minimization loop (which calls electron/fluid minimization loops)
		IonicMinimizer imin(e);
		imin.minimize(e.ionicMinParams);
	}

	//Final dump:
	e.dump(DumpFreq_End, 0);
	
	finalizeSystem();
	return 0;
}
