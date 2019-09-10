// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "BaseGizmos/GizmoInterfaces.h"
#include "BaseGizmos/GizmoMath.h"



void IGizmoAxisSource::GetAxisFrame(
	FVector& PlaneNormalOut, FVector& PlaneAxis1Out, FVector& PlaneAxis2Out) const
{
	PlaneNormalOut = GetDirection();
	if (HasTangentVectors())
	{
		GetTangentVectors(PlaneAxis1Out, PlaneAxis2Out);
	}
	else
	{
		GizmoMath::MakeNormalPlaneBasis(PlaneNormalOut, PlaneAxis1Out, PlaneAxis2Out);
	}
}
