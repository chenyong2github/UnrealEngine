// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseGizmos/AxisPositionGizmo.h"
#include "InteractiveGizmoManager.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "BaseGizmos/GizmoMath.h"



UInteractiveGizmo* UAxisPositionGizmoBuilder::BuildGizmo(const FToolBuilderState& SceneState) const
{
	UAxisPositionGizmo* NewGizmo = NewObject<UAxisPositionGizmo>(SceneState.GizmoManager);
	return NewGizmo;
}




void UAxisPositionGizmo::Setup()
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
	ParameterSource = NewObject<UGizmoLocalFloatParameterSource>(this);
	HitTarget = NewObject<UGizmoComponentHitTarget>(this);
	StateTarget = NewObject<UGizmoNilStateTarget>(this);

	bInInteraction = false;
}


FInputRayHit UAxisPositionGizmo::CanBeginClickDragSequence(const FInputDeviceRay& PressPos)
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

void UAxisPositionGizmo::OnClickPress(const FInputDeviceRay& PressPos)
{
	InteractionOrigin = LastHitPosition;
	InteractionAxis = AxisSource->GetDirection();

	FVector RayNearestPt; float RayNearestParam;
	GizmoMath::NearestPointOnLineToRay(InteractionOrigin, InteractionAxis,
		PressPos.WorldRay.Origin, PressPos.WorldRay.Direction,
		InteractionStartPoint, InteractionStartParameter,
		RayNearestPt, RayNearestParam);

	float DirectionSign = FVector::DotProduct(InteractionStartPoint - AxisSource->GetOrigin(), InteractionAxis);
	ParameterSign = (bEnableSignedAxis && DirectionSign < 0) ? -1.0f : 1.0f;

	InteractionCurPoint = InteractionStartPoint;
	InteractionStartParameter *= ParameterSign;
	InteractionCurParameter = InteractionStartParameter;

	InitialTargetParameter = ParameterSource->GetParameter();
	ParameterSource->BeginModify();

	bInInteraction = true;

	if (StateTarget)
	{
		StateTarget->BeginUpdate();
	}
}

void UAxisPositionGizmo::OnClickDrag(const FInputDeviceRay& DragPos)
{
	FVector AxisNearestPt; float AxisNearestParam;
	FVector RayNearestPt; float RayNearestParam;
	GizmoMath::NearestPointOnLineToRay(InteractionOrigin, InteractionAxis,
		DragPos.WorldRay.Origin, DragPos.WorldRay.Direction,
		AxisNearestPt, AxisNearestParam,
		RayNearestPt, RayNearestParam);

	InteractionCurPoint = AxisNearestPt;
	InteractionCurParameter = ParameterSign * AxisNearestParam;

	float DeltaParam = InteractionCurParameter - InteractionStartParameter;
	float NewValue = InitialTargetParameter + DeltaParam;

	ParameterSource->SetParameter(NewValue);
}

void UAxisPositionGizmo::OnClickRelease(const FInputDeviceRay& ReleasePos)
{
	check(bInInteraction);

	ParameterSource->EndModify();
	if (StateTarget)
	{
		StateTarget->EndUpdate();
	}
	bInInteraction = false;
}


void UAxisPositionGizmo::OnTerminateDragSequence()
{
	check(bInInteraction);

	ParameterSource->EndModify();
	if (StateTarget)
	{
		StateTarget->EndUpdate();
	}
	bInInteraction = false;
}



FInputRayHit UAxisPositionGizmo::BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos)
{
	FInputRayHit GizmoHit;
	if (HitTarget)
	{
		GizmoHit = HitTarget->IsHit(PressPos);
	}
	return GizmoHit;
}

void UAxisPositionGizmo::OnBeginHover(const FInputDeviceRay& DevicePos)
{
	HitTarget->UpdateHoverState(true);
}

bool UAxisPositionGizmo::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	// not necessary...
	HitTarget->UpdateHoverState(true);
	return true;
}

void UAxisPositionGizmo::OnEndHover()
{
	HitTarget->UpdateHoverState(false);
}