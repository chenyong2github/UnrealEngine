// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseGizmos/GizmoElementHitTargets.h"
#include "BaseGizmos/GizmoElementBase.h"
#include "Engine/EngineTypes.h"    // FHitResult


FInputRayHit UGizmoElementHitTarget::IsHit(const FInputDeviceRay& ClickPos) const
{
	if (GizmoElement && (!Condition || Condition(ClickPos)))
	{
		return GizmoElement->LineTrace(ClickPos.WorldRay.Origin, ClickPos.WorldRay.Direction);
	}
	return FInputRayHit();
}

void UGizmoElementHitTarget::UpdateHoverState(bool bHovering)
{
	if (!GizmoElement)
	{
		return;
	}

	if (bHovering)
	{
		GizmoElement->SetElementInteractionState(EGizmoElementInteractionState::Hovering);
	}
	else
	{
		GizmoElement->SetElementInteractionState(EGizmoElementInteractionState::None);
	}
}

void UGizmoElementHitTarget::UpdateInteractingState(bool bInteracting)
{
	if (!GizmoElement)
	{
		return;
	}

	if (bInteracting)
	{
		GizmoElement->SetElementInteractionState(EGizmoElementInteractionState::Interacting);
	}
	else
	{
		GizmoElement->SetElementInteractionState(EGizmoElementInteractionState::None);
	}
}

UGizmoElementHitTarget* UGizmoElementHitTarget::Construct(UGizmoElementBase* InGizmoElement, UObject* Outer)
{
	UGizmoElementHitTarget* NewHitTarget = NewObject<UGizmoElementHitTarget>(Outer);
	NewHitTarget->GizmoElement = InGizmoElement;
	return NewHitTarget;
}

FInputRayHit UGizmoElementHitMultiTarget::IsHit(const FInputDeviceRay& ClickPos) const
{
	if (GizmoElement && (!Condition || Condition(ClickPos)))
	{
		return GizmoElement->LineTrace(ClickPos.WorldRay.Origin, ClickPos.WorldRay.Direction);
	}
	return FInputRayHit();
}

void UGizmoElementHitMultiTarget::UpdateHoverState(bool bHovering, uint32 PartIdentifier)
{
	if (!GizmoElement)
	{
		return;
	}

	if (bHovering)
	{
		GizmoElement->UpdatePartInteractionState(EGizmoElementInteractionState::Hovering, PartIdentifier);
	}
	else
	{
		GizmoElement->UpdatePartInteractionState(EGizmoElementInteractionState::None, PartIdentifier);
	}
}

void UGizmoElementHitMultiTarget::UpdateInteractingState(bool bInteracting, uint32 PartIdentifier)
{
	if (!GizmoElement)
	{
		return;
	}

	if (bInteracting)
	{
		GizmoElement->UpdatePartInteractionState(EGizmoElementInteractionState::Interacting, PartIdentifier);
	}
	else
	{
		GizmoElement->UpdatePartInteractionState(EGizmoElementInteractionState::None, PartIdentifier);
	}
}

void UGizmoElementHitMultiTarget::UpdateHittableState(bool bHittable, uint32 PartIdentifier)
{
	if (!GizmoElement)
	{
		return;
	}

	GizmoElement->UpdatePartHittableState(bHittable, PartIdentifier);
}

UGizmoElementHitMultiTarget* UGizmoElementHitMultiTarget::Construct(UGizmoElementBase* InGizmoElement, UObject* Outer)
{
	UGizmoElementHitMultiTarget* NewHitTarget = NewObject<UGizmoElementHitMultiTarget>(Outer);
	NewHitTarget->GizmoElement = InGizmoElement;
	return NewHitTarget;
}



