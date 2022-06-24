// Copyright Epic Games, Inc. All Rights Reserved.
 
#include "BaseGizmos/GizmoElementTorus.h"
#include "BaseGizmos/GizmoRenderingUtil.h"
#include "BaseGizmos/GizmoMath.h"
#include "InputState.h"
#include "Materials/MaterialInterface.h"
#include "SceneManagement.h"

void UGizmoElementTorus::Render(IToolsContextRenderAPI* RenderAPI, const FRenderTraversalState& RenderState)
{
	if (!IsVisible())
	{
		return;
	}

	check(RenderAPI);


	FRenderTraversalState CurrentRenderState(RenderState);
	bool bVisibleViewDependent = UpdateRenderState(RenderAPI, Center, CurrentRenderState);

	if (bVisibleViewDependent)
	{
		if (const UMaterialInterface* UseMaterial = CurrentRenderState.GetCurrentMaterial())
		{
			FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();

			FVector TorusSideAxis = Normal ^ BeginAxis;
			TorusSideAxis.Normalize();

			DrawTorus(PDI, CurrentRenderState.LocalToWorldTransform.ToMatrixWithScale(), BeginAxis, TorusSideAxis, OuterRadius, InnerRadius, OuterSegments, InnerSlices,  UseMaterial->GetRenderProxy(), SDPG_Foreground, bPartial, Angle, bEndCaps);
		}
	}
}



FInputRayHit UGizmoElementTorus::LineTrace(const UGizmoViewContext* ViewContext, const FLineTraceTraversalState& LineTraceState, const FVector& RayOrigin, const FVector& RayDirection)
{
	// Line trace is not supported for torus.
	return FInputRayHit();
}

FBoxSphereBounds UGizmoElementTorus::CalcBounds(const FTransform& LocalToWorld) const
{
	// Box sphere bounds is not supported for torus.
	return FBoxSphereBounds();
}

void UGizmoElementTorus::SetCenter(const FVector& InCenter)
{
	Center = InCenter;
}

FVector UGizmoElementTorus::GetCenter() const
{
	return Center;
}

void UGizmoElementTorus::SetNormal(const FVector& InNormal)
{
	Normal = InNormal;
	Normal.Normalize();
}

FVector UGizmoElementTorus::GetNormal() const
{
	return Normal;
}

void UGizmoElementTorus::SetBeginAxis(const FVector& InBeginAxis)
{
	BeginAxis = InBeginAxis;
	BeginAxis.Normalize();
}

FVector UGizmoElementTorus::GetBeginAxis() const
{
	return BeginAxis;
}

void UGizmoElementTorus::SetOuterRadius(float InOuterRadius)
{
	OuterRadius = InOuterRadius;
}

float UGizmoElementTorus::GetOuterRadius() const
{
	return OuterRadius;
}

void UGizmoElementTorus::SetInnerRadius(float InInnerRadius)
{
	InnerRadius = InInnerRadius;
}

float UGizmoElementTorus::GetInnerRadius() const
{
	return InnerRadius;
}

void UGizmoElementTorus::SetOuterSegments(int32 InOuterSegments)
{
	OuterSegments = InOuterSegments;
}

int32 UGizmoElementTorus::GetOuterSegments() const
{
	return OuterSegments;
}

void UGizmoElementTorus::SetInnerSlices(int32 InInnerSlices)
{
	InnerSlices = InInnerSlices;
}

int32 UGizmoElementTorus::GetInnerSlices() const
{
	return InnerSlices;
}

void UGizmoElementTorus::SetPartial(bool InPartial)
{
	bPartial = InPartial;
}

bool UGizmoElementTorus::GetPartial() const
{
	return bPartial;
}

void UGizmoElementTorus::SetScreenAlignPartial(bool InScreenAlignPartial)
{
	bScreenAlignPartial = InScreenAlignPartial;
}

bool UGizmoElementTorus::GetScreenAlignPartial() const
{
	return bPartial;
}

void UGizmoElementTorus::SetAngle(float InAngle)
{
	Angle = InAngle;
}

float UGizmoElementTorus::GetAngle() const
{
	return Angle;
}

void UGizmoElementTorus::SetEndCaps(bool InEndCaps)
{
	bEndCaps = InEndCaps;
}

bool UGizmoElementTorus::GetEndCaps() const
{
	return bEndCaps;
}
