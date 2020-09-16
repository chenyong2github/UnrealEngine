// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "AssetData.h"
#include "UObject/NameTypes.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/Actor.h"
#include "Containers/Set.h"
#include "Misc/Guid.h"
#include "Misc/HashBuilder.h"
#include "WorldPartition/WorldPartitionActorMetaDataSerializer.h"
#include "WorldPartition/WorldPartitionActorDescFactory.h"

#if WITH_EDITOR
// Struct used to create actor descriptor
struct FWorldPartitionActorDescInitData
{
	UClass* NativeClass;
	FName PackageName;
	FName ActorPath;
	FAssetData AssetData;
	FTransform Transform;
	FActorMetaDataSerializer* Serializer;
};
#endif

/**
 * Represents a potentially unloaded actor (editor-only)
 */
class ENGINE_API FWorldPartitionActorDesc 
{
#if WITH_EDITOR
	friend class AActor;
	friend class UWorldPartition;
	friend class FWorldPartitionActorDescFactory;

public:
	virtual ~FWorldPartitionActorDesc() {}

	inline const FGuid& GetGuid() const { return Guid; }
	
	inline FName GetClass() const { return Class; }
	inline UClass* GetActorClass() const { return ActorClass; }
	inline FVector GetOrigin() const { return GetBounds().GetCenter(); }
	inline EActorGridPlacement GetGridPlacement() const { return GridPlacement; }
	inline FName GetRuntimeGrid() const { return RuntimeGrid; }
	inline bool GetActorIsEditorOnly() const { return bActorIsEditorOnly; }
	inline bool GetLevelBoundsRelevant() const { return bLevelBoundsRelevant; }
	inline const TArray<FName>& GetLayers() const { return Layers; }
	inline FName GetActorPackage() const { return ActorPackage; }
	inline FName GetActorPath() const { return ActorPath; }
	FBox GetBounds() const;

	inline uint32 AddLoadedRefCount() const
	{
		return ++LoadedRefCount;
	}

	inline uint32 RemoveLoadedRefCount() const
	{
		check(LoadedRefCount > 0);
		return --LoadedRefCount;
	}

	inline uint32 GetLoadedRefCount() const
	{
		return LoadedRefCount;
	}

	inline uint32 GetHash() const
	{
		return Hash;
	}

	const TArray<FGuid>& GetReferences() const
	{
		return References;
	}

	FString ToString() const;

	AActor* GetActor() const;
	AActor* Load(const FLinkerInstancingContext* InstancingContext = nullptr);
	void Unload();

protected:
	FWorldPartitionActorDesc();
	virtual bool Init(const AActor* InActor);
	bool Init(const FWorldPartitionActorDescInitData& DescData);
		
	void UpdateHash();
	virtual void BuildHash(FHashBuilder& HashBuilder);

	virtual void SerializeMetaData(FActorMetaDataSerializer* Serializer);

	FGuid						Guid;
	FName						Class;
	FName						ActorPackage;
	FName						ActorPath;
	FVector						BoundsLocation;
	FVector						BoundsExtent;
	EActorGridPlacement			GridPlacement;
	FName						RuntimeGrid;
	bool						bActorIsEditorOnly;
	bool						bLevelBoundsRelevant;
	TArray<FName>				Layers;
	TArray<FGuid>				References;
	
	mutable uint32				LoadedRefCount;
	mutable uint32				Hash;

	// Cached values
	UClass*						ActorClass;

public:
	// Tagging
	mutable uint32				Tag;
	static uint32				GlobalTag;
#endif
};
