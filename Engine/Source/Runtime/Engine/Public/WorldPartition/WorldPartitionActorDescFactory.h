// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "AssetData.h"
#include "UObject/ObjectMacros.h"
#include "WorldPartition/WorldPartitionActorDesc.h"

#if WITH_EDITOR
enum class EActorGridPlacement : uint8;

// Struct used to create actor descriptor
struct FWorldPartitionActorDescInitData
{
	UClass* NativeClass;
	FName PackageName;
	FName ActorPath;
	FAssetData AssetData;
	FTransform Transform;
};
#endif

class ENGINE_API FWorldPartitionActorDescFactory
{
#if WITH_EDITOR
private:
	void PostCreate(FWorldPartitionActorDesc* ActorDesc);

protected:
	bool ReadMetaData(const FWorldPartitionActorDescInitData& ActorDescInitData, FWorldPartitionActorDescData& OutData);

	virtual FWorldPartitionActorDesc* CreateInstance(const FWorldPartitionActorDescInitData& ActorDescInitData);
	virtual FWorldPartitionActorDesc* CreateInstance(AActor* InActor);

public:
	virtual ~FWorldPartitionActorDescFactory() = default;

	FWorldPartitionActorDesc* Create(const FWorldPartitionActorDescInitData& ActorDescInitData);
	FWorldPartitionActorDesc* Create(AActor* InActor);
#endif
};