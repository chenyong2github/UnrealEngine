// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_EDITOR
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "WorldPartition/WorldPartitionActorDesc.h"

class AActor;
struct FAssetData;

struct ENGINE_API FWorldPartitionActorDescUtils
{
	static bool IsValidActorDescriptorFromAssetData(const FAssetData& InAssetData);
	static TUniquePtr<FWorldPartitionActorDesc> GetActorDescriptorFromAssetData(const FAssetData& InAssetData);
	static void AppendAssetDataTagsFromActor(const AActor* Actor, TArray<UObject::FAssetRegistryTag>& OutTags);
	static void UpdateActorDescriptorFomActor(const AActor* Actor, TUniquePtr<FWorldPartitionActorDesc>& ActorDesc);
	static void ReplaceActorDescriptorPointerFromActor(const AActor* OldActor, AActor* NewActor, FWorldPartitionActorDesc* ActorDesc);
};
#endif