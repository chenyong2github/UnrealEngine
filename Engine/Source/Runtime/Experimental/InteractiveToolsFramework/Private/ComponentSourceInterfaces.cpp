// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComponentSourceInterfaces.h"

#include "Components/PrimitiveComponent.h"
#include "Containers/Array.h"

namespace
{
	TArray<TUniquePtr<FComponentTargetFactory>> Factories;
}


void AddComponentTargetFactory( TUniquePtr<FComponentTargetFactory> Factory )
{
	Factories.Push( MoveTemp(Factory) );
}

bool CanMakeComponentTarget(UActorComponent* Component)
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

TUniquePtr<FPrimitiveComponentTarget> MakeComponentTarget(UPrimitiveComponent* Component)
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




bool FComponentMaterialSet::operator!=(const FComponentMaterialSet& Other) const
{
	int32 Num = Materials.Num();
	if (Other.Materials.Num() != Num)
	{
		return true;
	}
	for (int32 j = 0; j < Num; ++j)
	{
		if (Other.Materials[j] != Materials[j])
		{
			return true;
		}
	}
	return false;
}


AActor* FPrimitiveComponentTarget::GetOwnerActor() const
{
	return Component->GetOwner();
}

UPrimitiveComponent* FPrimitiveComponentTarget::GetOwnerComponent() const
{
	return Component;
}


void FPrimitiveComponentTarget::SetOwnerVisibility(bool bVisible) const
{
	Component->SetVisibility(bVisible);
}


int32 FPrimitiveComponentTarget::GetNumMaterials() const
{
	return Component->GetNumMaterials();
}

UMaterialInterface* FPrimitiveComponentTarget::GetMaterial(int32 MaterialIndex) const
{
	return Component->GetMaterial(MaterialIndex);
}

void FPrimitiveComponentTarget::GetMaterialSet(FComponentMaterialSet& MaterialSetOut, bool bAssetMaterials) const
{
	int32 NumMaterials = Component->GetNumMaterials(); 
	MaterialSetOut.Materials.SetNum(NumMaterials);
	for (int32 k = 0; k < NumMaterials; ++k)
	{
		MaterialSetOut.Materials[k] = Component->GetMaterial(k);
	}
}


void FPrimitiveComponentTarget::CommitMaterialSetUpdate(const FComponentMaterialSet& MaterialSet, bool bApplyToAsset)
{
	check(false);		// not implemented
}




FTransform FPrimitiveComponentTarget::GetWorldTransform() const
{
	//return Component->GetOwner()->GetActorTransform();
	return Component->GetComponentTransform();
}

bool FPrimitiveComponentTarget::HitTest(const FRay& WorldRay, FHitResult& OutHit) const
{
	FVector End = WorldRay.PointAt(HALF_WORLD_MAX);
	if (Component->LineTraceComponent(OutHit, WorldRay.Origin, End, FCollisionQueryParams(SCENE_QUERY_STAT(HitTest), true)))
	{
		return true;
	}
	return false;
}
