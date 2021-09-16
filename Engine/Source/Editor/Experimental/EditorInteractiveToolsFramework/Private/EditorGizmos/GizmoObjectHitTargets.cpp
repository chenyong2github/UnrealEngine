// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorGizmos/GizmoObjectHitTargets.h"
#include "EditorGizmos/GizmoBaseObject.h"
#include "Engine/EngineTypes.h"    // FHitResult


FInputRayHit UGizmoObjectHitTarget::IsHit(const FInputDeviceRay& ClickPos) const
{
	if (GizmoObject && (!Condition || Condition(ClickPos)))
	{
		return GizmoObject->LineTraceObject(ClickPos.WorldRay.Origin, ClickPos.WorldRay.Direction);
	}
	return FInputRayHit();
}

void UGizmoObjectHitTarget::UpdateHoverState(bool bHovering)
{
	if (GizmoObject)
	{
		GizmoObject->SetHoverState(bHovering);
	}
}

void UGizmoObjectHitTarget::UpdateInteractingState(bool bInteracting)
{
	if (GizmoObject)
	{
		GizmoObject->SetInteractingState(bInteracting);
	}
}

UGizmoObjectHitTarget* UGizmoObjectHitTarget::Construct(UGizmoBaseObject* InGizmoObject, UObject* Outer)
{
	UGizmoObjectHitTarget* NewHitTarget = NewObject<UGizmoObjectHitTarget>(Outer);
	NewHitTarget->GizmoObject = InGizmoObject;
	return NewHitTarget;
}



