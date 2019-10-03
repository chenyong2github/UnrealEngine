// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "BaseGizmos/HitTargets.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/EngineTypes.h"    // FHitResult



FInputRayHit UGizmoComponentHitTarget::IsHit(const FInputDeviceRay& ClickPos) const
{
	FInputRayHit Hit;
	if (Component)
	{
		FVector End = ClickPos.WorldRay.PointAt(HALF_WORLD_MAX);
		FHitResult OutHit;
		if (Component->LineTraceComponent(OutHit, ClickPos.WorldRay.Origin, End, FCollisionQueryParams(SCENE_QUERY_STAT(HitTest), true)))
		{
			return FInputRayHit(OutHit.Distance);
		}
	}
	return Hit;
}

