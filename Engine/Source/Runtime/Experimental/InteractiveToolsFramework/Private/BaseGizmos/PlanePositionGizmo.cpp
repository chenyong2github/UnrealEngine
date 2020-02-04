// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseGizmos/PlanePositionGizmo.h"
#include "InteractiveGizmoManager.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "BaseGizmos/GizmoMath.h"



UInteractiveGizmo* UPlanePositionGizmoBuilder::BuildGizmo(const FToolBuilderState& SceneState) const
{
	UPlanePositionGizmo* NewGizmo = NewObject<UPlanePositionGizmo>(SceneState.GizmoManager);
	return NewGizmo;
}




void UPlanePositionGizmo::Setup()
{
	UInteractiveGizmo::Setup();

	// Add default mouse input behavior
	UClickDragInputBehavior* MouseBehavior = NewObject<UClickDragInputBehavior>();
	MouseBehavior->Initialize(this);
	MouseBehavior->SetDefaultPriority(FInputCapturePriority(FInputCapturePriority::DEFAULT_GIZMO_PRIORITY));
	AddInputBehavior(MouseBehavior);
	UMouseHoverBehavior* HoverBehavior = NewObject<UMouseHoverBehavior>();
	HoverBehavior->Initialize(this);
	HoverBehavior->SetDefaultPriority(FInputCapturePriority(FInputCapturePriority::DEFAULT_GIZMO_PRIORITY));
	AddInputBehavior(HoverBehavior);

	AxisSource = NewObject<UGizmoConstantAxisSource>(this);
	ParameterSource = NewObject<UGizmoLocalVec2ParameterSource>(this);
	HitTarget = NewObject<UGizmoComponentHitTarget>(this);
	StateTarget = NewObject<UGizmoNilStateTarget>(this);

	bInInteraction = false;
}

FInputRayHit UPlanePositionGizmo::CanBeginClickDragSequence(const FInputDeviceRay& PressPos)
{
	FInputRayHit GizmoHit;
	if (HitTarget && AxisSource && ParameterSource)
	{
		GizmoHit = HitTarget->IsHit(PressPos);
		if (GizmoHit.bHit)
		{
			LastHitPosition = PressPos.WorldRay.PointAt(GizmoHit.HitDepth);
		}
	}
	return GizmoHit;
}

void UPlanePositionGizmo::OnClickPress(const FInputDeviceRay& PressPos)
{
	InteractionOrigin = LastHitPosition;
	InteractionNormal = AxisSource->GetDirection();
	if (AxisSource->HasTangentVectors())
	{
		AxisSource->GetTangentVectors(InteractionAxisX, InteractionAxisY);
	}
	else
	{
		GizmoMath::MakeNormalPlaneBasis(InteractionNormal, InteractionAxisX, InteractionAxisY);
	}

	bool bIntersects; FVector IntersectionPoint;
	GizmoMath::RayPlaneIntersectionPoint(
		InteractionOrigin, InteractionNormal,
		PressPos.WorldRay.Origin, PressPos.WorldRay.Direction,
		bIntersects, IntersectionPoint);
	check(bIntersects);  // need to handle this case...

	InteractionStartPoint = InteractionCurPoint = IntersectionPoint;

	float DirectionSignX = FVector::DotProduct(InteractionStartPoint - AxisSource->GetOrigin(), InteractionAxisX);
	ParameterSigns.X = (bEnableSignedAxis && DirectionSignX < 0) ? -1.0f : 1.0f;
	ParameterSigns.X *= (bFlipX) ? -1.0f : 1.0f;
	float DirectionSignY = FVector::DotProduct(InteractionStartPoint - AxisSource->GetOrigin(), InteractionAxisY);
	ParameterSigns.Y = (bEnableSignedAxis && DirectionSignY < 0) ? -1.0f : 1.0f;
	ParameterSigns.Y *= (bFlipY) ? -1.0f : 1.0f;

	InteractionStartParameter = GizmoMath::ComputeCoordinatesInPlane(IntersectionPoint,
			InteractionOrigin, InteractionNormal, InteractionAxisX, InteractionAxisY);
	InteractionStartParameter.X *= ParameterSigns.X;
	InteractionStartParameter.Y *= ParameterSigns.Y;
	InteractionCurParameter = InteractionStartParameter;

	InitialTargetParameter = ParameterSource->GetParameter();
	ParameterSource->BeginModify();

	bInInteraction = true;

	if (StateTarget)
	{
		StateTarget->BeginUpdate();
	}
}

void UPlanePositionGizmo::OnClickDrag(const FInputDeviceRay& DragPos)
{
	bool bIntersects; FVector IntersectionPoint;
	GizmoMath::RayPlaneIntersectionPoint(
		InteractionOrigin, InteractionNormal,
		DragPos.WorldRay.Origin, DragPos.WorldRay.Direction,
		bIntersects, IntersectionPoint);

	if (bIntersects == false)
	{
		return;
	}

	InteractionCurPoint = IntersectionPoint;
	InteractionCurParameter = GizmoMath::ComputeCoordinatesInPlane(IntersectionPoint,
		InteractionOrigin, InteractionNormal, InteractionAxisX, InteractionAxisY);
	InteractionCurParameter.X *= ParameterSigns.X;
	InteractionCurParameter.Y *= ParameterSigns.Y;

	FVector2D DeltaParam = InteractionCurParameter - InteractionStartParameter;
	FVector2D NewValue = InitialTargetParameter + DeltaParam;

	ParameterSource->SetParameter(NewValue);
}

void UPlanePositionGizmo::OnClickRelease(const FInputDeviceRay& ReleasePos)
{
	check(bInInteraction);

	ParameterSource->EndModify();
	if (StateTarget)
	{
		StateTarget->EndUpdate();
	}
	bInInteraction = false;
}


void UPlanePositionGizmo::OnTerminateDragSequence()
{
	check(bInInteraction);

	ParameterSource->EndModify();
	if (StateTarget)
	{
		StateTarget->EndUpdate();
	}
	bInInteraction = false;
}





FInputRayHit UPlanePositionGizmo::BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos)
{
	FInputRayHit GizmoHit;
	if (HitTarget)
	{
		GizmoHit = HitTarget->IsHit(PressPos);
	}
	return GizmoHit;
}

void UPlanePositionGizmo::OnBeginHover(const FInputDeviceRay& DevicePos)
{
	HitTarget->UpdateHoverState(true);
}

bool UPlanePositionGizmo::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	// not necessary...
	HitTarget->UpdateHoverState(true);
	return true;
}

void UPlanePositionGizmo::OnEndHover()
{
	HitTarget->UpdateHoverState(false);
}
