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
	friend class UWorldPartitionRuntimeHash;
	friend class FWorldPartitionStreamingGenerator;
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
	inline FName GetRuntimeGrid() const { return RuntimeGrid; }
	inline bool GetIsSpatiallyLoaded() const { return bIsSpatiallyLoaded; }
	inline bool GetActorIsEditorOnly() const { return bActorIsEditorOnly; }
	inline bool GetLevelBoundsRelevant() const { return bLevelBoundsRelevant; }
	inline bool GetActorIsHLODRelevant() const { return bActorIsHLODRelevant; }
	inline FName GetHLODLayer() const { return HLODLayer; }
	inline const TArray<FName>& GetDataLayers() const { return DataLayers; }
	inline FName GetActorPackage() const { return ActorPackage; }
	inline FName GetActorPath() const { return ActorPath; }
	inline FName GetActorLabel() const { return ActorLabel; }
	inline FName GetFolderPath() const { return FolderPath; }
	inline const FGuid& GetFolderGuid() const { return FolderGuid; }
	FBox GetBounds() const;
	inline const FGuid& GetParentActor() const { return ParentActor; }

	FName GetActorName() const;

	FName GetActorLabelOrName() const;

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

	void SetContainer(UActorDescContainer* InContainer)
	{
		check(!Container || !InContainer);
		Container = InContainer;
	}

public:
	const TArray<FGuid>& GetReferences() const
	{
		return References;
	}

	UActorDescContainer* GetContainer() const
	{
		return Container;
	}

	FString ToString() const;

	bool IsLoaded(bool bEvenIfPendingKill=false) const;
	AActor* GetActor(bool bEvenIfPendingKill=true, bool bEvenIfUnreachable=false) const;
	AActor* Load() const;
	virtual void Unload();
	virtual bool ShouldBeLoadedByEditorCells() const { return true; }

	void RegisterActor();
	void UnregisterActor();

	virtual void Init(const AActor* InActor);
	virtual void Init(const FWorldPartitionActorDescInitData& DescData);

	virtual bool Equals(const FWorldPartitionActorDesc* Other) const;

	void SerializeTo(TArray<uint8>& OutData);

	UWorld* GetWorld() const;

protected:
	FWorldPartitionActorDesc();

	void TransformInstance(const FString& From, const FString& To);

	virtual void TransferFrom(const FWorldPartitionActorDesc* From)
	{
		Container = From->Container;
		SoftRefCount = From->SoftRefCount;
		HardRefCount = From->HardRefCount;
	}

	virtual void Serialize(FArchive& Ar);

	virtual void OnRegister(UWorld* InWorld) {}
	virtual void OnUnregister() {}

	// Persistent
	FGuid							Guid;
	FName							Class;
	FName							ActorPackage;
	FName							ActorPath;
	FName							ActorLabel;
	FVector							BoundsLocation;
	FVector							BoundsExtent;
	FName							RuntimeGrid;
	bool							bIsSpatiallyLoaded;
	bool							bActorIsEditorOnly;
	bool							bLevelBoundsRelevant;
	bool							bActorIsHLODRelevant;
	FName							HLODLayer;
	TArray<FName>					DataLayers;
	TArray<FGuid>					References;
	FName							FolderPath;
	FGuid							FolderGuid;

	FGuid							ParentActor; // Used to validate settings against parent (to warn on layer/placement compatibility issues)
	
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
