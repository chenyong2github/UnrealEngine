// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/GCObject.h"
#include "UObject/ObjectPtr.h"
#include "Containers/Map.h"
#include "WorldPartition/WorldPartitionActorDesc.h"

class ULevelInstanceSubsystem;
class UActorDescContainer;
enum class ELevelInstanceRuntimeBehavior : uint8;

/**
 * ActorDesc for Actors that are part of a LevelInstanceActor Level.
 */
class ENGINE_API FLevelInstanceActorDesc : public FWorldPartitionActorDesc, public FGCObject
{
#if WITH_EDITOR
	friend class ALevelInstance;

public:
	inline FName GetLevelPackage() const { return LevelPackage; }

	virtual bool GetContainerInstance(const UActorDescContainer*& OutLevelContainer, FTransform& OutLevelTransform, EContainerClusterMode& OutClusterMode) const override;

protected:
	FLevelInstanceActorDesc();
	virtual void Init(const AActor* InActor) override;
	virtual void Init(UActorDescContainer* InContainer, const FWorldPartitionActorDescInitData& DescData) override;
	virtual void Serialize(FArchive& Ar) override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector);
	virtual FString GetReferencerName() const override
	{
		return TEXT("FLevelInstanceActorDesc");
	}
	virtual void OnRegister(UWorld* InWorld) override;
	virtual void OnUnregister() override;

	FName LevelPackage;
	FTransform LevelInstanceTransform;
	ELevelInstanceRuntimeBehavior DesiredRuntimeBehavior;

	TObjectPtr<UActorDescContainer> LevelInstanceContainer;

private:
	static UActorDescContainer* RegisterActorDescContainer(FName PackageName, UWorld* InWorld);
	static bool UnregisterActorDescContainer(UActorDescContainer* Container);
	static TMap<FName, TWeakObjectPtr<UActorDescContainer>> ActorDescContainers;
#endif
};
