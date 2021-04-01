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

class UActorDescContainer;

enum class EContainerClusterMode : uint8
{
	Partitioned, // Per Actor Partitioning
	Embedded // Per Container Partitioning: Every actor of the container are going to be clustered together
};
#endif

#if WITH_DEV_AUTOMATION_TESTS
namespace WorldPartitionTests
{
	class FWorldPartitionSoftRefTest;
}
#endif

/**
 * Represents a potentially unloaded actor (editor-only)
 */
class ENGINE_API FWorldPartitionActorDesc 
{
#if WITH_EDITOR
	friend class AActor;
	friend class UWorldPartition;
	friend class UActorDescContainer;
	friend struct FWorldPartitionHandleImpl;
	friend struct FWorldPartitionReferenceImpl;
	friend struct FWorldPartitionHandleUtils;

#if WITH_DEV_AUTOMATION_TESTS
	friend class WorldPartitionTests::FWorldPartitionSoftRefTest;
#endif

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
	inline bool GetActorIsHLODRelevant() const { return bActorIsHLODRelevant; }
	class UHLODLayer* GetHLODLayer() const;
	inline const TArray<FName>& GetDataLayers() const { return DataLayers; }
	inline FName GetActorPackage() const { return ActorPackage; }
	inline FName GetActorPath() const { return ActorPath; }
	inline FName GetActorLabel() const { return ActorLabel; }
	inline FName GetFolderPath() const { return FolderPath; }
	FBox GetBounds() const;

	FName GetActorName() const;

	virtual bool GetContainerInstance(const UActorDescContainer*& OutLevelContainer, FTransform& OutLevelTransform, EContainerClusterMode& OutClusterMode) const { return false; }

	bool operator==(const FWorldPartitionActorDesc& Other) const
	{
		return Guid == Other.Guid;
	}

	friend uint32 GetTypeHash(const FWorldPartitionActorDesc& Key)
	{
		return GetTypeHash(Key.Guid);
	}

protected:
	inline uint32 IncSoftRefCount() const
	{
		return ++SoftRefCount;
	}

	inline uint32 DecSoftRefCount() const
	{
		check(SoftRefCount > 0);
		return --SoftRefCount;
	}

	inline uint32 GetSoftRefCount() const
	{
		return SoftRefCount;
	}

	inline uint32 IncHardRefCount() const
	{
		return ++HardRefCount;
	}

	inline uint32 DecHardRefCount() const
	{
		check(HardRefCount > 0);
		return --HardRefCount;
	}

	inline uint32 GetHardRefCount() const
	{
		return HardRefCount;
	}

public:
	const TArray<FGuid>& GetReferences() const
	{
		return References;
	}

	FString ToString() const;

	bool IsLoaded() const;
	AActor* GetActor() const;
	AActor* Load() const;
	virtual void Unload();

	void RegisterActor();
	void UnregisterActor();

	virtual void Init(const AActor* InActor);
	virtual void Init(UActorDescContainer* InContainer, const FWorldPartitionActorDescInitData& DescData);

	void SerializeTo(TArray<uint8>& OutData);

protected:
	FWorldPartitionActorDesc();

	void TransformInstance(const FString& From, const FString& To, const FTransform& Transform);

	inline void TransferRefCounts(const FWorldPartitionActorDesc* From) const
	{
		SoftRefCount = From->SoftRefCount;
		HardRefCount = From->HardRefCount;
	}

	virtual void Serialize(FArchive& Ar);

	virtual void OnRegister() {}
	virtual void OnUnregister() {}

	// Persistent
	FGuid							Guid;
	FName							Class;
	FName							ActorPackage;
	FName							ActorPath;
	FName							ActorLabel;
	FVector							BoundsLocation;
	FVector							BoundsExtent;
	EActorGridPlacement				GridPlacement;
	FName							RuntimeGrid;
	bool							bActorIsEditorOnly;
	bool							bLevelBoundsRelevant;
	bool							bActorIsHLODRelevant;
	FName							HLODLayer;
	TArray<FName>					DataLayers;
	TArray<FGuid>					References;
	FName							FolderPath;
	
	// Transient
	mutable uint32					SoftRefCount;
	mutable uint32					HardRefCount;
	UClass*							ActorClass;
	mutable TWeakObjectPtr<AActor>	ActorPtr;
	UActorDescContainer*			Container;

public:
	// Tagging
	mutable uint32					Tag;
	static uint32					GlobalTag;
#endif
};
