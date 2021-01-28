// Copyright Epic Games, Inc. All Rights Reserved.

#include "ToolTargets/PrimitiveComponentToolTarget.h"

#include "Components/PrimitiveComponent.h"

bool UPrimitiveComponentToolTarget::IsValid() const
{
	return Component && !Component->IsPendingKillOrUnreachable() && Component->IsValidLowLevel();
}

UPrimitiveComponent* UPrimitiveComponentToolTarget::GetOwnerComponent() const
{
	return IsValid() ? Component : nullptr;
}

AActor* UPrimitiveComponentToolTarget::GetOwnerActor() const
{
	return IsValid() ? Component->GetOwner() : nullptr;
}

void UPrimitiveComponentToolTarget::SetOwnerVisibility(bool bVisible) const
{
	if (IsValid())
	{
		Component->SetVisibility(bVisible);
	}
}

FTransform UPrimitiveComponentToolTarget::GetWorldTransform() const
{
	return IsValid() ? Component->GetComponentTransform() : FTransform::Identity;
}

bool UPrimitiveComponentToolTarget::HitTestComponent(const FRay& WorldRay, FHitResult& OutHit) const
{
	FVector End = WorldRay.PointAt(HALF_WORLD_MAX);
	if (IsValid() && Component->LineTraceComponent(OutHit, WorldRay.Origin, End, FCollisionQueryParams(SCENE_QUERY_STAT(HitTest), true)))
	{
		return true;
	}
	return false;
}
