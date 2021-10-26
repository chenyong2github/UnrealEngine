// Copyright Epic Games, Inc. All Rights Reserved.
#include "CADKernel/Geo/Curves/EllipseCurve.h"


TSharedPtr<CADKernel::FEntityGeom> CADKernel::FEllipseCurve::ApplyMatrix(const FMatrixH& InMatrix) const
{
	FMatrixH NewMatrix = InMatrix * Matrix;
	return FEntity::MakeShared<FEllipseCurve>(NewMatrix, RadiusU, RadiusV, Boundary);
}

#ifdef CADKERNEL_DEV
CADKernel::FInfoEntity& CADKernel::FEllipseCurve::GetInfo(FInfoEntity& Info) const
{
	return FCurve::GetInfo(Info).Add(TEXT("Matrix"), Matrix)
		.Add(TEXT("radius U"), RadiusU)
		.Add(TEXT("radius V"), RadiusV);
}
#endif

