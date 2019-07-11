// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ComponentSourceInterfaces.h"

#include "Components/PrimitiveComponent.h"
#include "Containers/Array.h"

namespace
{
	TArray<FMeshDescriptionBridge::FBuilder> Builders;
}

void
AddMeshDescriptionBridgeBuilder( FMeshDescriptionBridge::FBuilder Builder )
{
	Builders.Push(MoveTemp(Builder));
}

FComponentTarget
MakeComponentTarget(UPrimitiveComponent* Component)
{
	for ( const auto& Builder : Builders )
	{
		auto Bridge = Builder( Component );
		if ( Bridge.HasSource() )
		{
			return {Bridge, Component};
		}
	}
	return {};
}

AActor*
FComponentTarget::GetOwnerActor() const
{
	return Component->GetOwner();
}

UPrimitiveComponent*
FComponentTarget::GetOwnerComponent() const
{
	return Component;
}


void
FComponentTarget::SetOwnerVisibility(bool bVisible) const
{
	Component->SetVisibility(bVisible);
}

UMaterialInterface*
FComponentTarget::GetMaterial(int32 MaterialIndex) const
{
	return Component->GetMaterial(MaterialIndex);
}

FTransform
FComponentTarget::GetWorldTransform() const
{
	//return Component->GetOwner()->GetActorTransform();
	return Component->GetComponentTransform();
}

bool
FComponentTarget::HitTest(const FRay& WorldRay, FHitResult& OutHit) const
{
	FVector End = WorldRay.PointAt(HALF_WORLD_MAX);
	if (Component->LineTraceComponent(OutHit, WorldRay.Origin, End, FCollisionQueryParams(SCENE_QUERY_STAT(HitTest), true)))
	{
		return true;
	}
	return false;
}
