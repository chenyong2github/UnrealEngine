// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ComponentSourceInterfaces.h"

#include "Components/PrimitiveComponent.h"
#include "Containers/Array.h"

namespace
{
	TArray<TUniquePtr<FComponentTargetFactory>> Factories;
}

void
AddComponentTargetFactory( TUniquePtr<FComponentTargetFactory> Factory )
{
	Factories.Push( MoveTemp(Factory) );
}

bool
CanMakeComponentTarget(UActorComponent* Component)
{
	for ( const auto& Factory : Factories )
	{
		if ( Factory->CanBuild(Component) )
		{
			return true;
		}
	}
	return false;
}

TUniquePtr<FPrimitiveComponentTarget>
MakeComponentTarget(UPrimitiveComponent* Component)
{
	for ( const auto& Factory : Factories )
	{
		if ( Factory->CanBuild( Component ) )
		{
			return Factory->Build( Component );
		}
	}
	return {};
}

AActor*
FPrimitiveComponentTarget::GetOwnerActor() const
{
	return Component->GetOwner();
}

UPrimitiveComponent*
FPrimitiveComponentTarget::GetOwnerComponent() const
{
	return Component;
}


void
FPrimitiveComponentTarget::SetOwnerVisibility(bool bVisible) const
{
	Component->SetVisibility(bVisible);
}


int32 FPrimitiveComponentTarget::GetNumMaterials() const
{
	return Component->GetNumMaterials();
}

UMaterialInterface*
FPrimitiveComponentTarget::GetMaterial(int32 MaterialIndex) const
{
	return Component->GetMaterial(MaterialIndex);
}

FTransform
FPrimitiveComponentTarget::GetWorldTransform() const
{
	//return Component->GetOwner()->GetActorTransform();
	return Component->GetComponentTransform();
}

bool
FPrimitiveComponentTarget::HitTest(const FRay& WorldRay, FHitResult& OutHit) const
{
	FVector End = WorldRay.PointAt(HALF_WORLD_MAX);
	if (Component->LineTraceComponent(OutHit, WorldRay.Origin, End, FCollisionQueryParams(SCENE_QUERY_STAT(HitTest), true)))
	{
		return true;
	}
	return false;
}
