// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseGizmos/HitTargets.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/EngineTypes.h"    // FHitResult



FInputRayHit UGizmoComponentHitTarget::IsHit(const FInputDeviceRay& ClickPos) const
{
	FInputRayHit Hit;
	if (Component)
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

