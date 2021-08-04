// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseGizmos/HitTargets.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/EngineTypes.h"    // FHitResult

FInputRayHit UGizmoLambdaHitTarget::IsHit(const FInputDeviceRay& ClickPos) const
{
	if (IsHitFunction)
	{
		return IsHit(ClickPos);
	}
	return FInputRayHit();
}

void UGizmoLambdaHitTarget::UpdateHoverState(bool bHovering)
{
	if (UpdateHoverFunction)
	{
		UpdateHoverFunction(bHovering);
	}
}

void UGizmoLambdaHitTarget::UpdateInteractingState(bool bHovering)
{
	if (UpdateInteractingFunction)
	{
		UpdateInteractingFunction(bHovering);
	}
}

FInputRayHit UGizmoComponentHitTarget::IsHit(const FInputDeviceRay& ClickPos) const
{
	FInputRayHit Hit;
	if (Component && (!Condition || Condition(ClickPos)))
	{
		// if a gizmo is not visible it cannot be hit
		bool bVisible = Component->IsVisible() && ( Component->GetOwner() && Component->GetOwner()->IsHidden() == false );
#if WITH_EDITOR		
		bVisible = bVisible && Component->IsVisibleInEditor() && (Component->GetOwner() && Component->GetOwner()->IsHiddenEd() == false);
#endif

		if (bVisible)
		{
			FVector End = ClickPos.WorldRay.PointAt(HALF_WORLD_MAX);
			FHitResult OutHit;
			if (Component->LineTraceComponent(OutHit, ClickPos.WorldRay.Origin, End, FCollisionQueryParams(SCENE_QUERY_STAT(HitTest), true)))
			{
				return FInputRayHit(OutHit.Distance);
			}
		}
	}
	return Hit;
}

void UGizmoComponentHitTarget::UpdateHoverState(bool bHovering)
{
	if (UpdateHoverFunction)
	{
		UpdateHoverFunction(bHovering);
	}
}

void UGizmoComponentHitTarget::UpdateInteractingState(bool bHovering)
{
	if (UpdateInteractingFunction)
	{
		UpdateInteractingFunction(bHovering);
	}
}

UGizmoComponentHitTarget* UGizmoComponentHitTarget::Construct(UPrimitiveComponent* Component, UObject* Outer)
{
	UGizmoComponentHitTarget* NewHitTarget = NewObject<UGizmoComponentHitTarget>(Outer);
	NewHitTarget->Component = Component;
	return NewHitTarget;
}

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



