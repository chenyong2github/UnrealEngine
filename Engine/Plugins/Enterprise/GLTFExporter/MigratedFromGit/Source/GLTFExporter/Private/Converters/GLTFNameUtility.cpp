// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFNameUtility.h"
#include "Actors/GLTFInteractionHotspotActor.h"
#include "Animation/SkeletalMeshActor.h"
#include "Engine.h"

namespace
{
	FString GetNameWithLODSuffix(const UObject* Object, int32 LODIndex)
	{
		FString Name;
		Object->GetName(Name);

		if (LODIndex != 0)
		{
			Name += TEXT("_LOD") + FString::FromInt(LODIndex);
		}

		return Name;
	}

	template <typename ActorType>
    FString GetActorNameIfOwnerOfType(const UActorComponent* Component)
	{
		if (const ActorType* Owner = Cast<ActorType>(Component->GetOwner()))
		{
			return Owner->GetName();
		}

		return Component->GetName();
	}
}

FString FGLTFNameUtility::GetName(const UStaticMesh* StaticMesh, int32 LODIndex)
{
	return GetNameWithLODSuffix(StaticMesh, LODIndex);
}

FString FGLTFNameUtility::GetName(const USkeletalMesh* SkeletalMesh, int32 LODIndex)
{
	return GetNameWithLODSuffix(SkeletalMesh, LODIndex);
}

FString FGLTFNameUtility::GetName(const USceneComponent* Component)
{
	if (const AActor* Owner = Component->GetOwner())
	{
		return Owner->GetName() + TEXT("_") + Component->GetName();
	}

	return Component->GetName();
}

FString FGLTFNameUtility::GetName(const UStaticMeshComponent* Component)
{
	return GetActorNameIfOwnerOfType<AStaticMeshActor>(Component);
}

FString FGLTFNameUtility::GetName(const USkeletalMeshComponent* Component)
{
	return GetActorNameIfOwnerOfType<ASkeletalMeshActor>(Component);
}

FString FGLTFNameUtility::GetName(const ULightComponent* Component)
{
	return GetActorNameIfOwnerOfType<ALight>(Component);
}

FString FGLTFNameUtility::GetName(const UCameraComponent* Component)
{
	return GetActorNameIfOwnerOfType<ACameraActor>(Component);
}

FString FGLTFNameUtility::GetName(const UGLTFInteractionHotspotComponent* Component)
{
	return GetActorNameIfOwnerOfType<AGLTFInteractionHotspotActor>(Component);
}
