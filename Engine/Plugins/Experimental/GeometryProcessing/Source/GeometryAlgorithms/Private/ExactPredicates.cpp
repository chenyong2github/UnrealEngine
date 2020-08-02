// Copyright Epic Games, Inc. All Rights Reserved.

// Code to call exact predicates on vectors

#include "ExactPredicates.h"
#include "ExactPredicates/ThirdParty/ShewchukPredicatesInterface.h"


namespace ExactPredicates
{
	void GlobalInit()
	{
		ShewchukExactPredicates::exactinit();
	}
	
	double Orient2DInexact(double *pa, double *pb, double *pc)
	{
		return ShewchukExactPredicates::orient2dfast(pa, pb, pc);
	}

	double Orient2D(double *pa, double *pb, double *pc)
	{
		checkSlow(ShewchukExactPredicates::IsExactPredicateDataInitialized());
		return ShewchukExactPredicates::orient2d(pa, pb, pc);
	}

	double Orient3DInexact(double* PA, double* PB, double* PC, double* PD)
	{
		return ShewchukExactPredicates::orient3dfast(PA, PB, PC, PD);
	}

	double Orient3D(double* PA, double* PB, double* PC, double* PD)
	{
		checkSlow(ShewchukExactPredicates::IsExactPredicateDataInitialized());
		return ShewchukExactPredicates::orient3d(PA, PB, PC, PD);
	}
}
