// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "AssetData.h"
#include "UObject/NameTypes.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/Actor.h"
#include "Containers/Set.h"
#include "Misc/Guid.h"

#if WITH_EDITOR
// Struct used to create actor descriptor
struct FWorldPartitionActorDescInitData
{
	UClass* NativeClass;
	FName PackageName;
	FName ActorPath;
	TArray<uint8> SerializedData;
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
	inline const TArray<FName>& GetDataLayers() const { return DataLayers; }
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

	const TArray<FGuid>& GetReferences() const
	{
		return References;
	}

	FString ToString() const;

	AActor* GetActor() const;
	AActor* Load(const FLinkerInstancingContext* InstancingContext = nullptr) const;
	void Unload();

	virtual void Init(const AActor* InActor);
	void Init(const FWorldPartitionActorDescInitData& DescData);

	void SerializeTo(TArray<uint8>& OutData);

	void TransformInstance(const FString& From, const FString& To, const FTransform& Transform);

protected:
	FWorldPartitionActorDesc();

	inline void SetLoadedRefCount(uint32 InLoadedRefCount) const
	{
		LoadedRefCount = InLoadedRefCount;
	}

	virtual void Serialize(FArchive& Ar);

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
	TArray<FName>				DataLayers;
	TArray<FGuid>				References;
	
	mutable uint32				LoadedRefCount;

	// Cached values
	UClass*						ActorClass;

public:
	// Tagging
	mutable uint32				Tag;
	static uint32				GlobalTag;
#endif
};
