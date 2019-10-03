// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "BaseGizmos/AxisSources.h"
#include "Components/SceneComponent.h"

FVector UGizmoComponentAxisSource::GetOrigin() const
{
	const FTransform& WorldTransform = Component->GetComponentToWorld();
	return WorldTransform.GetLocation();
}

FVector UGizmoComponentAxisSource::GetDirection() const
{
	const FTransform& WorldTransform = Component->GetComponentToWorld();
	FVector Axis(0, 0, 0);
	Axis[FMath::Clamp(AxisIndex, 0, 2)] = 1.0;
	if (bLocalAxes)
	{
		return WorldTransform.GetRotation().RotateVector(Axis);
	} 
	else
	{
		return Axis;
	}
}

void UGizmoComponentAxisSource::GetTangentVectors(FVector& TangentXOut, FVector& TangentYOut) const
{
	const FTransform& WorldTransform = Component->GetComponentToWorld();
	TangentXOut = FVector(0, 1, 0);
	TangentYOut = FVector(0, 0, 1);
	int Index = FMath::Clamp(AxisIndex, 0, 2);
	if (Index == 1)
	{
		TangentXOut = FVector(-1, 0, 0);
		TangentYOut = FVector(0, 0, 1);
	}
	else if (Index == 2)
	{
		TangentXOut = FVector(1, 0, 0);
		TangentYOut = FVector(0, 1, 0);
	}
	if (bLocalAxes)
	{
		TangentXOut = WorldTransform.GetRotation().RotateVector(TangentXOut);
		TangentYOut = WorldTransform.GetRotation().RotateVector(TangentYOut);
	}
}