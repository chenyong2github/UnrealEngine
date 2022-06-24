// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseGizmos/GizmoElementHitTargets.h"
#include "BaseGizmos/GizmoElementBase.h"
#include "BaseGizmos/GizmoViewContext.h"
#include "BaseGizmos/TransformProxy.h"
#include "Engine/EngineTypes.h"    // FHitResult


FInputRayHit UGizmoElementHitTarget::IsHit(const FInputDeviceRay& ClickPos) const
{
	if (GizmoElement && GizmoViewContext && GizmoTransformProxy && (!Condition || Condition(ClickPos)))
	{
		UGizmoElementBase::FLineTraceTraversalState LineTraceState;
		LineTraceState.Initialize(GizmoViewContext, GizmoTransformProxy->GetTransform());
		return GizmoElement->LineTrace(GizmoViewContext, LineTraceState, ClickPos.WorldRay.Origin, ClickPos.WorldRay.Direction);
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

UGizmoElementHitTarget* UGizmoElementHitTarget::Construct(UGizmoElementBase* InGizmoElement, UGizmoViewContext* InGizmoViewContext, UObject* Outer)
{
	UGizmoElementHitTarget* NewHitTarget = NewObject<UGizmoElementHitTarget>(Outer);
	NewHitTarget->GizmoElement = InGizmoElement;
	NewHitTarget->GizmoViewContext = InGizmoViewContext;
	return NewHitTarget;
}

FInputRayHit UGizmoElementHitMultiTarget::IsHit(const FInputDeviceRay& ClickPos) const
{
	if (GizmoElement && GizmoViewContext && GizmoTransformProxy && (!Condition || Condition(ClickPos)))
	{
		UGizmoElementBase::FLineTraceTraversalState LineTraceState;
		LineTraceState.Initialize(GizmoViewContext, GizmoTransformProxy->GetTransform());
		return GizmoElement->LineTrace(GizmoViewContext, LineTraceState, ClickPos.WorldRay.Origin, ClickPos.WorldRay.Direction);
	}
	return FInputRayHit();
}

void UGizmoElementHitMultiTarget::UpdateHoverState(bool bInHovering, uint32 PartIdentifier)
{
	if (!GizmoElement)
	{
		return;
	}

	if (bInHovering && !bHovering)
	{
		GizmoElement->UpdatePartInteractionState(EGizmoElementInteractionState::Hovering, PartIdentifier);
		bHovering = true;
		bInteracting = false;
	}
	else if (!bInHovering && bHovering)
	{
		GizmoElement->UpdatePartInteractionState(EGizmoElementInteractionState::None, PartIdentifier);
		bHovering = false;
	}
}

void UGizmoElementHitMultiTarget::UpdateInteractingState(bool bInInteracting, uint32 PartIdentifier)
{
	if (!GizmoElement)
	{
		return;
	}

	if (bInInteracting && !bInteracting)
	{
		GizmoElement->UpdatePartInteractionState(EGizmoElementInteractionState::Interacting, PartIdentifier);
		bInteracting = true;
		bHovering = false;
	}
	else if (!bInInteracting && bInteracting)
	{
		GizmoElement->UpdatePartInteractionState(EGizmoElementInteractionState::None, PartIdentifier);
		bInteracting = false;
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

UGizmoElementHitMultiTarget* UGizmoElementHitMultiTarget::Construct(UGizmoElementBase* InGizmoElement, UGizmoViewContext* InGizmoViewContext, UObject* Outer)
{
	UGizmoElementHitMultiTarget* NewHitTarget = NewObject<UGizmoElementHitMultiTarget>(Outer);
	NewHitTarget->GizmoElement = InGizmoElement;
	NewHitTarget->GizmoViewContext = InGizmoViewContext;
	return NewHitTarget;
}



