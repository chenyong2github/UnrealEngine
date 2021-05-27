// Copyright Epic Games, Inc. All Rights Reserved.

#include "Restorability/StaticMeshCollisionPropertyComparer.h"

#include "PropertyComparisonParams.h"

#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/UnrealType.h"

void FStaticMeshCollisionPropertyComparer::Register(FLevelSnapshotsModule& Module)
{
	FStructProperty* BodyInstanceProperty = FindFProperty<FStructProperty>(UPrimitiveComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(UPrimitiveComponent, BodyInstance));
	if (!ensure(BodyInstanceProperty))
	{
		return;
	}
	FBoolProperty* UseDefaultCollisionProperty = FindFProperty<FBoolProperty>(UStaticMeshComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(UStaticMeshComponent, bUseDefaultCollision));
	if (!ensure(UseDefaultCollisionProperty))
	{
		return;
	}
	
	const TSharedRef<FStaticMeshCollisionPropertyComparer> CollisionPropertyFix = MakeShareable(new FStaticMeshCollisionPropertyComparer);
	CollisionPropertyFix->BodyInstanceProperty = BodyInstanceProperty;
	CollisionPropertyFix->UseDefaultCollisionProperty = UseDefaultCollisionProperty;
	Module.RegisterPropertyComparer(UStaticMeshComponent::StaticClass(), CollisionPropertyFix);
}

IPropertyComparer::EPropertyComparison FStaticMeshCollisionPropertyComparer::ShouldConsiderPropertyEqual(const FPropertyComparisonParams& Params) const
{
	if (Params.LeafProperty == UseDefaultCollisionProperty)
	{
		// Background: UStaticMeshComponent::PostEditChangeProperty sometimes changes bUseDefaultCollision to false even though the mesh's default collision settings equal the settings in BodyInstance.
		// This causes bUseDefaultCollision to show as changed, even though, effectively nothing has changed about the component's collision settings.
		// We can safely skip showing UStaticMeshComponent::bUseDefaultCollision: if snapshot and world collision values differ, then BodyInstance will already show as changed anyways.
		return EPropertyComparison::TreatEqual;
	}

	if (Params.LeafProperty == BodyInstanceProperty)
	{
		UStaticMeshComponent* SnapshotObject = Cast<UStaticMeshComponent>(Params.SnapshotObject);
		UStaticMeshComponent* WorldObject = Cast<UStaticMeshComponent>(Params.WorldObject);
#if UE_BUILD_DEBUG
		// Should never fail, double-check on debug builds
		check(SnapshotObject);
		check(WorldObject);
#endif
		
		const bool bDefaultCollisionConfigIsEqual = SnapshotObject->bUseDefaultCollision == WorldObject->bUseDefaultCollision;
		const bool bUseDefaultCollision = SnapshotObject->bUseDefaultCollision;
		return bDefaultCollisionConfigIsEqual
			?
			bUseDefaultCollision ? EPropertyComparison::TreatEqual : EPropertyComparison::CheckNormally
			:
			EPropertyComparison::CheckNormally;
	}

	return EPropertyComparison::CheckNormally;
}
