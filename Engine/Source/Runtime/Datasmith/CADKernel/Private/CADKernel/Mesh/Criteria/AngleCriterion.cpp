// Copyright Epic Games, Inc. All Rights Reserved.
#include "CADKernel/Mesh/Criteria/AngleCriterion.h"

CADKernel::FAngleCriterion::FAngleCriterion(double DegreeAngle) 
	: FCriterion(ECriterion::Angle)
{
	AngleCriterionValue = FMath::DegreesToRadians(DegreeAngle);
	SinMaxAngle = sin(AngleCriterionValue*0.5);
}

CADKernel::FAngleCriterion::FAngleCriterion(FCADKernelArchive& Archive, ECriterion InCriterionType)
	: FCriterion(InCriterionType)
{
	Serialize(Archive);
}

void CADKernel::FAngleCriterion::Serialize(FCADKernelArchive& Ar)
{
	FCriterion::Serialize(Ar);
	Ar << AngleCriterionValue;
	Ar << SinMaxAngle;
}
