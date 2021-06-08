// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADKernel/Geo/Surfaces/NURBSSurface.h"

#include "CADKernel/Geo/GeoPoint.h"

using namespace CADKernel;

TSharedPtr<FEntityGeom> FNURBSSurface::ApplyMatrix(const FMatrixH& InMatrix) const
{
	TArray<FPoint> TransformedPoles;
	TransformedPoles.Reserve(Poles.Num());

	for (const FPoint& Pole : Poles)
	{
		TransformedPoles.Emplace(InMatrix.Multiply(Pole));
	}

	return FEntity::MakeShared<FNURBSSurface>(Tolerance3D, PoleUNum, PoleVNum, UDegree, VDegree, UNodalVector, VNodalVector, TransformedPoles, Weights);
}

#ifdef CADKERNEL_DEV
FInfoEntity& FNURBSSurface::GetInfo(FInfoEntity& Info) const
{
	return FSurface::GetInfo(Info)
		.Add(TEXT("Degre"), UDegree, VDegree)
		.Add(TEXT("Is Rational"), bIsRational)
		.Add(TEXT("Poles Num"), PoleUNum, PoleVNum)
		.Add(TEXT("Nodal Vector U"), UNodalVector)
		.Add(TEXT("Nodal Vector V"), VNodalVector)
		.Add(TEXT("Poles"), Poles)
		.Add(TEXT("Weights"), Weights);
}
#endif

void FNURBSSurface::Finalize()
{
	if (bIsRational)
	{
		const double FirstWeigth = Weights[0];

		bool bIsReallyRational = false;
		for (const double& Weight : Weights)
		{
			if (!FMath::IsNearlyEqual(Weight, FirstWeigth))
			{
				bIsReallyRational = true;
				break;
			}
		}

		if (!bIsReallyRational)
		{
			if (!FMath::IsNearlyEqual(1., FirstWeigth))
			{
				for (FPoint& Pole : Poles)
				{
					Pole /= FirstWeigth;
				}
			}
			bIsRational = false;
		}
	}

	if (bIsRational)
	{
		HomogeneousPoles.SetNum(Poles.Num() * 4);
		for (int32 Index = 0, Jndex = 0; Index < Poles.Num(); Index++)
		{
			HomogeneousPoles[Jndex++] = Poles[Index].X * Weights[Index];
			HomogeneousPoles[Jndex++] = Poles[Index].Y * Weights[Index];
			HomogeneousPoles[Jndex++] = Poles[Index].Z * Weights[Index];
			HomogeneousPoles[Jndex++] = Weights[Index];
		}
	}
	else
	{
		HomogeneousPoles.SetNum(Poles.Num() * 3);
		memcpy(HomogeneousPoles.GetData(), Poles.GetData(), sizeof(FPoint) * Poles.Num());
	}

	double UMin = UNodalVector[UDegree];
	double UMax = UNodalVector[UNodalVector.Num() - 1 - UDegree];
	double VMin = VNodalVector[VDegree];
	double VMax = VNodalVector[VNodalVector.Num() - 1 - VDegree];

	Boundary.Set(UMin, UMax, VMin, VMax);
	SetMinToleranceIso();
}


