// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_EDITOR
#include "CoreMinimal.h"
#include "PropertyPairsMap.h"
#include "UObject/NameTypes.h"
#include "UObject/ObjectMacros.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/TopLevelAssetPath.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "UObject/WeakObjectPtr.h"
#include "Templates/SubclassOf.h"
#include "Misc/Guid.h"
#include "WorldPartition/WorldPartitionActorContainerID.h"

// Struct used to create actor descriptor
struct FWorldPartitionActorDescInitData
{
	UClass* NativeClass;
	FName PackageName;
	FSoftObjectPath ActorPath;
	TArray<uint8> SerializedData;

	FWorldPartitionActorDescInitData& SetNativeClass(UClass* InNativeClass) { NativeClass = InNativeClass; return *this; }
	FWorldPartitionActorDescInitData& SetPackageName(FName InPackageName) { PackageName = InPackageName; return *this; }
	FWorldPartitionActorDescInitData& SetActorPath(const FSoftObjectPath& InActorPath) { ActorPath = InActorPath; return *this; }
};

class AActor;
class UActorDescContainer;
class IStreamingGenerationErrorHandler;
struct FWorldPartitionActorFilter;

enum class EContainerClusterMode : uint8
{
	Partitioned, // Per Actor Partitioning
};

template <typename T, class F>
inline bool CompareUnsortedArrays(const TArray<T>& Array1, const TArray<T>& Array2, F Func)
{
	if (Array1.Num() == Array2.Num())
	{
		TArray<T> SortedArray1(Array1);
		TArray<T> SortedArray2(Array2);
		SortedArray1.Sort(Func);
		SortedArray2.Sort(Func);
		return SortedArray1 == SortedArray2;
	}
	return false;
}

template <typename T>
inline bool CompareUnsortedArrays(const TArray<T>& Array1, const TArray<T>& Array2)
{
	return CompareUnsortedArrays(Array1, Array2, [](const T& A, const T& B) { return A < B; });
}

template <>
inline bool CompareUnsortedArrays(const TArray<FName>& Array1, const TArray<FName>& Array2)
{
	return CompareUnsortedArrays(Array1, Array2, [](const FName& A, const FName& B) { return A.LexicalLess(B); });
}

/**
 * Represents a potentially unloaded actor (editor-only)
 */
class ENGINE_API FWorldPartitionActorDesc
{
	friend class AActor;
	friend class UWorldPartition;
	friend class FActorDescContainerCollection;
	friend struct FWorldPartitionHandleImpl;
	friend struct FWorldPartitionReferenceImpl;
	friend struct FWorldPartitionActorDescUtils;
	friend struct FWorldPartitionActorDescUnitTestAcccessor;
	friend class FAssetRootPackagePatcher;
	friend class FActorDescArchive;

public:
	struct FContainerInstance
	{
		const UActorDescContainer* Container = nullptr;
		FTransform Transform = FTransform::Identity;
		EContainerClusterMode ClusterMode;
		TMap<FActorContainerID, TSet<FGuid>> FilteredActors;
	};

	struct FGetContainerInstanceParams
	{
		FActorContainerID ContainerID;
		bool bBuildFilter = false;

		FGetContainerInstanceParams& SetContainerID(FActorContainerID InContainerID) { ContainerID = InContainerID; return *this; }
		FGetContainerInstanceParams& SetBuildFilter(bool bInBuildFilter) { bBuildFilter = bInBuildFilter; return *this; }
	};

	virtual ~FWorldPartitionActorDesc() {}

	inline const FGuid& GetGuid() const { return Guid; }
	
	inline FTopLevelAssetPath GetBaseClass() const { return BaseClass; }
	inline FTopLevelAssetPath GetNativeClass() const { return NativeClass; }
	inline UClass* GetActorNativeClass() const { return ActorNativeClass; }

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	UE_DEPRECATED(5.2, "GetOrigin is deprecated.")
	inline FVector GetOrigin() const { return GetBounds().GetCenter(); }
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	inline FName GetRuntimeGrid() const { return RuntimeGrid; }
	inline bool GetIsSpatiallyLoaded() const { return bIsForcedNonSpatiallyLoaded ? false : bIsSpatiallyLoaded; }
	inline bool GetIsSpatiallyLoadedRaw() const { return bIsSpatiallyLoaded; }
	inline bool GetActorIsEditorOnly() const { return bActorIsEditorOnly; }
	inline bool GetActorIsRuntimeOnly() const { return bActorIsRuntimeOnly; }

	UE_DEPRECATED(5.1, "SetIsSpatiallyLoadedRaw is deprecated and should not be used.")
	inline void SetIsSpatiallyLoadedRaw(bool bNewIsSpatiallyLoaded) { bIsSpatiallyLoaded = bNewIsSpatiallyLoaded; }

	inline bool GetActorIsHLODRelevant() const { return bActorIsHLODRelevant; }
	inline FName GetHLODLayer() const { return HLODLayer; }
	inline const TArray<FName>& GetDataLayers() const { return DataLayers; }
	inline bool HasResolvedDataLayerInstanceNames() const { return ResolvedDataLayerInstanceNames.IsSet(); }
	const TArray<FName>& GetDataLayerInstanceNames() const;
	inline const TArray<FName>& GetTags() const { return Tags; }
	inline void SetDataLayerInstanceNames(const TArray<FName>& InDataLayerInstanceNames) { ResolvedDataLayerInstanceNames = InDataLayerInstanceNames; }
	inline FName GetActorPackage() const { return ActorPackage; }
	inline FSoftObjectPath GetActorSoftPath() const { return ActorPath; }
	inline FName GetActorLabel() const { return ActorLabel; }
	inline FName GetFolderPath() const { return FolderPath; }
	inline const FGuid& GetFolderGuid() const { return FolderGuid; }

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	UE_DEPRECATED(5.2, "GetBounds is deprecated, GetEditorBounds or GetRuntimeBounds should be used instead.")
	FBox GetBounds() const { return GetEditorBounds(); }
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	virtual FBox GetEditorBounds() const;
	FBox GetRuntimeBounds() const;

	inline const FGuid& GetParentActor() const { return ParentActor; }
	inline bool IsUsingDataLayerAsset() const { return bIsUsingDataLayerAsset; }
	inline void AddProperty(FName PropertyName, FName PropertyValue = NAME_None) { Properties.AddProperty(PropertyName, PropertyValue); }
	inline bool GetProperty(FName PropertyName, FName* PropertyValue) const { return Properties.GetProperty(PropertyName, PropertyValue); }
	inline bool HasProperty(FName PropertyName) const { return Properties.HasProperty(PropertyName); }

	FName GetActorName() const;
	FName GetActorLabelOrName() const;
	FName GetDisplayClassName() const;

	virtual bool IsContainerInstance() const { return false; }
	virtual FName GetLevelPackage() const { return NAME_None; }
	virtual const FWorldPartitionActorFilter* GetContainerFilter() const { return nullptr; }
	virtual bool GetContainerInstance(const FGetContainerInstanceParams& InParams, FContainerInstance& OutContainerInstance) const { return false; }

	FGuid GetContentBundleGuid() const;

	virtual const FGuid& GetSceneOutlinerParent() const { return GetParentActor(); }
	virtual bool IsResaveNeeded() const { return false; }
	virtual bool IsRuntimeRelevant(const FActorContainerID& InContainerID) const;
	virtual bool IsEditorRelevant() const;
	virtual void CheckForErrors(IStreamingGenerationErrorHandler* ErrorHandler) const;

	UE_DEPRECATED(5.2, "ShouldValidateRuntimeGrid is deprecated and should not be used.")
	virtual bool ShouldValidateRuntimeGrid() const { return true; }

	bool operator==(const FWorldPartitionActorDesc& Other) const
	{
		return Guid == Other.Guid;
	}

	friend uint32 GetTypeHash(const FWorldPartitionActorDesc& Key)
	{
		return GetTypeHash(Key.Guid);
	}

	const FText& GetUnloadedReason() const;

	void SetUnloadedReason(FText* InUnloadedReason)
	{
		UnloadedReason = InUnloadedReason;
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

	inline uint32 IncHardRefCount() const
	{
		return ++HardRefCount;
	}

	inline uint32 DecHardRefCount() const
	{
		check(HardRefCount > 0);
		return --HardRefCount;
	}

	inline uint32 GetSoftRefCount() const
	{
		return SoftRefCount;
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

	UActorDescContainer* GetContainer() const
	{
		return Container;
	}

	virtual void SetContainer(UActorDescContainer* InContainer, UWorld* InWorldContext)
	{
		check(!Container || !InContainer);
		Container = InContainer;
	}

	enum class EToStringMode : uint8
	{
		Guid,
		Compact,
		Full
	};

	FString ToString(EToStringMode Mode = EToStringMode::Compact) const;

	bool IsLoaded(bool bEvenIfPendingKill=false) const;
	AActor* GetActor(bool bEvenIfPendingKill=true, bool bEvenIfUnreachable=false) const;
	AActor* Load() const;
	virtual void Unload();

	virtual void Init(const AActor* InActor);
	virtual void Init(const FWorldPartitionActorDescInitData& DescData);

	virtual bool Equals(const FWorldPartitionActorDesc* Other) const;

	void SerializeTo(TArray<uint8>& OutData);

	void TransformInstance(const FString& From, const FString& To);

	using FActorDescDeprecator = TFunction<void(FArchive&, FWorldPartitionActorDesc*)>;
	static void RegisterActorDescDeprecator(TSubclassOf<AActor> ActorClass, const FActorDescDeprecator& Deprecator);

protected:
	FWorldPartitionActorDesc();

	virtual void TransferFrom(const FWorldPartitionActorDesc* From);

	virtual void TransferWorldData(const FWorldPartitionActorDesc* From)
	{
		BoundsLocation = From->BoundsLocation;
		BoundsExtent = From->BoundsExtent;
	}

	virtual void Serialize(FArchive& Ar);

	// Persistent
	FGuid							Guid;
	FTopLevelAssetPath				BaseClass;
	FTopLevelAssetPath				NativeClass;
	FName							ActorPackage;
	FSoftObjectPath					ActorPath;
	FName							ActorLabel;
	FVector							BoundsLocation;
	FVector							BoundsExtent;
	FName							RuntimeGrid;
	bool							bIsSpatiallyLoaded;
	bool							bActorIsEditorOnly;
	bool							bActorIsRuntimeOnly;
	bool							bActorIsHLODRelevant;
	bool							bIsUsingDataLayerAsset; // Used to know if DataLayers array represents DataLayers Asset paths or the FNames of the deprecated version of Data Layers
	FName							HLODLayer;
	TArray<FName>					DataLayers;
	TArray<FGuid>					References;
	TArray<FName>					Tags;
	FPropertyPairsMap				Properties;
	FName							FolderPath;
	FGuid							FolderGuid;
	FGuid							ParentActor; // Used to validate settings against parent (to warn on layer/placement compatibility issues)
	FGuid							ContentBundleGuid;
	
	// Transient
	mutable uint32					SoftRefCount;
	mutable uint32					HardRefCount;
	UClass*							ActorNativeClass;
	mutable TWeakObjectPtr<AActor>	ActorPtr;
	UActorDescContainer*			Container;
	TOptional<TArray<FName>>		ResolvedDataLayerInstanceNames; // Can only resolve in ActorDesc if Container is not used as a template
	bool							bIsForcedNonSpatiallyLoaded;
	bool							bIsDefaultActorDesc;
	mutable FText*					UnloadedReason;

	static TMap<TSubclassOf<AActor>, FActorDescDeprecator> Deprecators;

#if DO_CHECK
public:
	struct FRegisteringUnregisteringGuard
	{
		FRegisteringUnregisteringGuard(FWorldPartitionActorDesc* InActorDesc)
			:ActorDesc(InActorDesc)
		{
			check(!ActorDesc->bIsRegisteringOrUnregistering);
			ActorDesc->bIsRegisteringOrUnregistering = true;
		}

		~FRegisteringUnregisteringGuard()
		{
			check(ActorDesc->bIsRegisteringOrUnregistering);
			ActorDesc->bIsRegisteringOrUnregistering = false;
		}

		FRegisteringUnregisteringGuard(const FRegisteringUnregisteringGuard&) = delete;
		FRegisteringUnregisteringGuard& operator=(const FRegisteringUnregisteringGuard&) = delete;

	private:
		FWorldPartitionActorDesc* ActorDesc;
	};
private:
	bool bIsRegisteringOrUnregistering = false;
#endif
};
#endif
