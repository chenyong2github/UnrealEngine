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
		check(ShewchukExactPredicates::IsExactPredicateDataInitialized());
		return ShewchukExactPredicates::orient2dfast(pa, pb, pc);
	}

	double Orient2D(double *pa, double *pb, double *pc)
	{
		check(ShewchukExactPredicates::IsExactPredicateDataInitialized());
		return ShewchukExactPredicates::orient2d(pa, pb, pc);
	}
}
