// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_SphericalPoseReader.h"
#include "Kismet/KismetMathLibrary.h"
#include "Units/RigUnitContext.h"

FRigUnit_SphericalPoseReader_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	URigHierarchy* Hierarchy = Context.Hierarchy;
	if (Hierarchy == nullptr)
	{
		return;
	}

	if (Context.State == EControlRigState::Init)
	{
		return;
	}

	if (!DriverItem.IsValid())
	{
		return;
	}

	// remap/clamp inputs
	RemapAndConvertInputs(
		InnerRegion, OuterRegion,
		ActiveRegionSize, PositiveWidth, NegativeWidth, PositiveHeight, NegativeHeight,
		FalloffSize, PositiveWidthFalloff, NegativeWidthFalloff, PositiveHeightFalloff, NegativeHeightFalloff);
	
	// get the world offset
	const FTransform LocalDriverTransformInit = Hierarchy->GetInitialLocalTransform(DriverItem);
	const FTransform GlobalDriverParentTransform = Hierarchy->GetParentTransform(DriverItem);
	FTransform WorldOffset = LocalDriverTransformInit * GlobalDriverParentTransform;
	// rotate by optional static offset
	FQuat RotationOffsetQuat = FQuat::MakeFromEuler(RotationOffset);
	WorldOffset.SetRotation(WorldOffset.GetRotation()*RotationOffsetQuat);

	// get driver axis
	const FTransform GlobalDriverTransform = Hierarchy->GetGlobalTransform(DriverItem);
	const FVector CurrentGlobalDriverAxis = GlobalDriverTransform.GetRotation().RotateVector(DriverAxis);
	DriverNormal = WorldOffset.InverseTransformVectorNoScale(CurrentGlobalDriverAxis).GetSafeNormal();

	// evaluate the interpolation of the output param
	const float AngleFromForward = FMath::Acos(FVector::DotProduct(DriverNormal, FVector::ZAxisVector));
	const float Mag = RemapRange(AngleFromForward, 0.0f, PI, 0.0f, 1.0f);
	Driver2D = DriverNormal;
	if (Mag > SMALL_NUMBER)
	{
		Driver2D.Z = 0.0f;
		Driver2D.Normalize();
		Driver2D *= Mag;
	}
	Driver2D.Z = -1.0f;

	if (FMath::IsNearlyZero(Mag))
	{
		// avoid NaNs from DistanceToEllipse, and guaranteed to be inside inner ellipse
		OutputParam = 1.0f;
		Debug.DrawDebug(WorldOffset, Context.DrawInterface, InnerRegion, OuterRegion, DriverNormal, Driver2D, OutputParam);
		return;
	}

	const float PointX = Driver2D.X;
	const float PointY = Driver2D.Y;

	float EllipseWidth;
	float EllipseHeight;

	// query inner ellipse
	InnerRegion.GetEllipseWidthAndHeight(PointX, PointY, EllipseWidth, EllipseHeight);
	FEllipseQuery InnerEllipseResults;
	DistanceToEllipse(PointX, PointY, EllipseWidth, EllipseHeight, InnerEllipseResults);

	// query outer ellipse
	OuterRegion.GetEllipseWidthAndHeight(PointX, PointY, EllipseWidth, EllipseHeight);
	FEllipseQuery OuterEllipseResults;
	DistanceToEllipse(PointX, PointY, EllipseWidth, EllipseHeight, OuterEllipseResults);

	// calc output param
	OutputParam = CalcOutputParam(InnerEllipseResults, OuterEllipseResults);
	
	// do all debug drawing
	Debug.DrawDebug(WorldOffset, Context.DrawInterface, InnerRegion, OuterRegion, DriverNormal, Driver2D, OutputParam);
}

void FRigUnit_SphericalPoseReader::RemapAndConvertInputs(
	FSphericalRegion& InnerRegion,
	FSphericalRegion& OuterRegion,
	const float ActiveRegionSize,
	const float PositiveWidth,
	const float NegativeWidth,
	const float PositiveHeight,
	const float NegativeHeight,
	const float FalloffSize,
	const float PositiveWidthFalloff,
	const float NegativeWidthFalloff,
	const float PositiveHeightFalloff,
	const float NegativeHeightFalloff)
{
	// remap normalized inputs to angles
	float RegionAngle = ActiveRegionSize * 180.0f;
	RegionAngle = FMath::Min(FMath::Max(0.5f, RegionAngle), 178.0f);

	InnerRegion.RegionAngleRadians = FMath::DegreesToRadians(RegionAngle);
	InnerRegion.PosWidth = PositiveWidth;
	InnerRegion.NegWidth = NegativeWidth;
	InnerRegion.PosHeight = PositiveHeight;
	InnerRegion.NegHeight = NegativeHeight;

	// clamp outer falloff angle to always be greater than inner
	float FalloffAngle = RegionAngle + (FalloffSize * 180.0f);
	FalloffAngle = FMath::Min(FMath::Max(1.0f, FalloffAngle), 179.0f);
	OuterRegion.RegionAngleRadians = FMath::DegreesToRadians(FalloffAngle);

	// clamp falloff to always be outside inner angle
	const float InvOuterAngleRadians = 1.0f / OuterRegion.RegionAngleRadians;
	//
	const float PosWidthMin = (InnerRegion.RegionAngleRadians * PositiveWidth) * InvOuterAngleRadians;
	OuterRegion.PosWidth = FMath::Lerp(PosWidthMin, 1.0f, PositiveWidthFalloff);
	//
	const float NegWidthMin = (InnerRegion.RegionAngleRadians * NegativeWidth) * InvOuterAngleRadians;
	OuterRegion.NegWidth = FMath::Lerp(NegWidthMin, 1.0f, NegativeWidthFalloff);
	//
	const float PosHeightMin = (InnerRegion.RegionAngleRadians * PositiveHeight) * InvOuterAngleRadians;
	OuterRegion.PosHeight = FMath::Lerp(PosHeightMin, 1.0f, PositiveHeightFalloff);
	//
	const float NegHeightMin = (InnerRegion.RegionAngleRadians * NegativeHeight) * InvOuterAngleRadians;
	OuterRegion.NegHeight = FMath::Lerp(NegHeightMin, 1.0f, NegativeHeightFalloff);
}

float FRigUnit_SphericalPoseReader::CalcOutputParam(
	const FEllipseQuery& InnerEllipseResults,
	const FEllipseQuery& OuterEllipseResults)
{
    if (InnerEllipseResults.IsInside)
    {
        return 1.0f; // inside inner ellipse
    }

    if (!OuterEllipseResults.IsInside)
    {
        return 0.0f; // outside outer ellipse
    }

    // we're between outer and inner ellipse, calculate falloff
    const float DistanceToOuter = FMath::Sqrt(OuterEllipseResults.DistSq);
    const float DistanceToInner = FMath::Sqrt(InnerEllipseResults.DistSq);
    const float TotalDistance = DistanceToInner + DistanceToOuter;
    if (TotalDistance < 0.0001f)
    {
        return 0.0f; // don't lerp when outer is VERY close to inner (avoid div by zero)
    }

    return 1.0f - (DistanceToInner / TotalDistance);
}

void FRigUnit_SphericalPoseReader::DistanceToEllipse(
	const float InX,
	const float InY,
	const float SizeX,
	const float SizeY,
	FEllipseQuery& OutEllipseQuery)
{
	if (SizeX <= KINDA_SMALL_NUMBER || SizeY <= KINDA_SMALL_NUMBER)
	{
		return; // degenerate ellipse
	}
	
    const float PX = FMath::Abs(InX);
    const float PY = FMath::Abs(InY);

	const float SizeXSq = SizeX * SizeX;
	const float SizeYSq = SizeY * SizeY;

	const float InvSizeX = 1.0f / SizeX;
	const float InvSizeY = 1.0f / SizeY;
	
    float TX = 0.70710678118f;
    float TY = 0.70710678118f;

    const int Iterations = 2; // this could be higher for greatly quality
    for (int i=0; i<Iterations; ++i)
    {
        const float ScaledX = SizeX * TX;
        const float ScaledY = SizeY * TY;

        const float EX = (SizeXSq - SizeYSq) * (TX * TX * TX) * InvSizeX;
        const float EY = (SizeYSq - SizeXSq) * (TY * TY * TY) * InvSizeY;

        const float RX = ScaledX - EX;
        const float RY = ScaledY - EY;

        const float QX = PX - EX;
        const float QY = PY - EY;

        const float R = FMath::Sqrt(RX * RX + RY * RY);
        const float Q = FMath::Sqrt(QY * QY + QX * QX);

        TX = FMath::Min(1.f, FMath::Max(0.f, (QX * R / Q + EX) * InvSizeX));
        TY = FMath::Min(1.f, FMath::Max(0.f, (QY * R / Q + EY) * InvSizeY));

        const float InvT = 1.0f / FMath::Sqrt(TX * TX + TY * TY);

        TX *= InvT;
        TY *= InvT;
    }

    OutEllipseQuery.ClosestX = SizeX * (InX < 0 ? -TX : TX);
    OutEllipseQuery.ClosestY = SizeY * (InY < 0 ? -TY : TY);
    const float ToClosestX = OutEllipseQuery.ClosestX - InX;
    const float ToClosestY = OutEllipseQuery.ClosestY - InY;
    OutEllipseQuery.DistSq = FMath::Pow(ToClosestX, 2.f) + FMath::Pow(ToClosestY, 2.f);
    const float CenterToClosestDistSq = FMath::Pow(OutEllipseQuery.ClosestX, 2.f) + FMath::Pow(OutEllipseQuery.ClosestY, 2.f);
    const float CenterToInputDistSq = FMath::Pow(InX, 2.f) + FMath::Pow(InY, 2.f);
    OutEllipseQuery.IsInside = CenterToClosestDistSq > CenterToInputDistSq;
}

float FRigUnit_SphericalPoseReader::RemapRange(
	const float T,
	const float AStart,
	const float AEnd,
	const float BStart,
	const float BEnd)
{
	check(FMath::Abs(AEnd - AStart) > 0.0f);
	return BStart + (T - AStart) * (BEnd - BStart) / (AEnd - AStart);
}
