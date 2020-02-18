// Copyright Epic Games, Inc. All Rights Reserved.

// Interface for exact predicates w/ Unreal Engine vector types

#pragma once

#include "CoreMinimal.h"
#include "VectorTypes.h"

namespace ExactPredicates
{
	/**
	 * Must be called once for exact predicates to work.
	 * Will be called by GeometryAlgorithmsModule startup, so you don't need to manually call this.
	 */
	void GlobalInit();

	// TODO: also provide a direct float-based version?
	double GEOMETRYALGORITHMS_API Orient2DInexact(double *PA, double *PB, double *PC);
	double GEOMETRYALGORITHMS_API Orient2D(double *PA, double *PB, double *PC);

	/**
	 * @return value indicating which side of line AB point C is on, or 0 if ABC are collinear 
	 */
	template<typename VectorType>
	double Orient2D(const VectorType& A, const VectorType& B, const VectorType& C)
	{
		double PA[2]{ A.X, A.Y };
		double PB[2]{ B.X, B.Y };
		double PC[2]{ C.X, C.Y };
		return Orient2D(PA, PB, PC);
	}

	// TODO: all remaining predicates
}; // namespace ExactPredicates
