// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/Types.h"

namespace UE::CADKernel
{
namespace MeshConst
{
constexpr double MinCurvature = 0.001;
constexpr double GeometricToMeshingToleranceFactor = 10.;
constexpr double ElementRatioVsThickness = 20.;

}

class FMeshingTolerances
{
public:
	const double GeometricTolerance;
	const double SquareGeometricTolerance;
	const double SquareGeometricTolerance2;
	const double MeshingTolerance;
	const double SquareMeshingTolerance;

	FMeshingTolerances(double InGeometricTolerance3D)
		: GeometricTolerance(InGeometricTolerance3D)
		, SquareGeometricTolerance(FMath::Square(GeometricTolerance))
		, SquareGeometricTolerance2(2. * SquareGeometricTolerance)
		, MeshingTolerance(GeometricTolerance * MeshConst::GeometricToMeshingToleranceFactor)
		, SquareMeshingTolerance(FMath::Square(MeshingTolerance))
	{
	}
};

} // namespace UE::CADKernel

