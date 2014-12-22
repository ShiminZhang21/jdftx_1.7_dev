/*-------------------------------------------------------------------
Copyright 2014 Ravishankar Sundararaman

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

#ifndef JDFTX_PHONON_PHONON_H
#define JDFTX_PHONON_PHONON_H

#include <electronic/Everything.h>
#include <electronic/ColumnBundle.h>
#include <core/LatticeUtils.h>

class Phonon
{
public:
	std::vector<std::pair<string,string> > input; //input file contents
	
	vector3<int> sup; //phonon supercell 
	double dr; //perturbation amplitude in Cartesian coordinates
	double T; //temperature for free energy estimation
	double Fcut; //fillings cutoff for optimizing number of bands
	
	Phonon();
	void setup(bool printDefaults); //setup unit cell and basis modes for perturbations
	void dump(); //main calculations (sequence of supercell calculations) as well as output
	
private:
	Everything e; //data for original unit cell
	Everything eSupTemplate; //uninitialized version of eSup, with various flags later used to create eSup for each mode
	std::shared_ptr<Everything> eSup; //supercell data for current perturbation

	int nSpins, nSpinor; //number of explicit spins and spinor length
	int nBandsOpt; //optimized number of bands, accounting for Fcut
	int prodSup; //number of unit cells in supercell
	
	struct Perturbation
	{	int sp, at; //!< species and atom number (within first unit cell)
		vector3<> dir; //!< Cartesian unit vector along perturbation
		double weight; //!< weight of perturbation (adds up to 3 nAtoms / nSymmetries)
	};
	std::vector<Perturbation> perturbations;
	
	//Run supercell calculation for specified perturbation
	void processPerturbation(const Perturbation& pert);
	
	//set unperturbed state of supercell from unit cell
	//optionally get the subspace Hamiltonian at supercell Gamma point (for all bands, not just those in nBandsOpt)
	void setSupState(std::vector<matrix>* Hsub=0); 
	
	struct StateMapEntry : public Supercell::KmeshTransform //contain source k-point rotation here
	{	int qSup; //state index for supercell
		vector3<int> iG; //reciprocal lattice offset
		int nqPrev; //number of previous unit cell k-points that point to this supercell
		
		//Wavefunction map
		int nIndices; int* indexPref;
		void setIndex(const std::vector<int>& index);
		
		StateMapEntry();
		~StateMapEntry();
	private:
		int *index;
		#ifdef GPU_ENABLED
		int *indexGpu;
		#endif
	};
	std::vector<std::shared_ptr<StateMapEntry> > stateMap; //map from unit cell k-points to supercell k-points
};

#endif //JDFTX_PHONON_PHONON_H