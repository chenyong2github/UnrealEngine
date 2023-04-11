// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/SceneComponent.h"
#include "Containers/Map.h"
#include "InstancedStaticMeshDelegates.h"
#include "Materials/MaterialInterface.h"

#include "GeometryCollectionISMPoolComponent.generated.h"

class AActor;
class UGeometryComponent;
class UInstancedStaticMeshComponent;
class UGeometryCollectionISMPoolComponent;

struct FGeometryCollectionISMInstance
{
	int32 StartIndex;
	int32 Count;
};

struct FInstanceGroups
{
public:
	using FInstanceGroupId = int32;

	struct FInstanceGroupRange
	{
		FInstanceGroupRange(int32 InStart, int32 InCount)
		{
			InstanceIdToIndex.Reserve(InCount);
			for (int Index = 0; Index < InCount; ++Index)
			{
				InstanceIdToIndex.Add(InStart + Index);
			}
		}

		bool TryIndexReallocate(int32 OldIndex, int32 NewIndex)
		{
			for (int32 i = 0; i < InstanceIdToIndex.Num(); ++i)
			{
				if (InstanceIdToIndex[i] == OldIndex)
				{
					InstanceIdToIndex[i] = NewIndex;
					return true;
				}
			}
			return false;
		}

		int32 Count() const { return InstanceIdToIndex.Num(); }
		TArray<int32> InstanceIdToIndex;
	};

	bool IsEmpty() const
	{
		return InstancesCount == 0;
	}

	FInstanceGroupId AddGroup(int32 Count)
	{
		const int32 StartIndex = InstancesCount;
		InstancesCount += Count;
		GroupRanges.Emplace(NextGroupId, FInstanceGroupRange(StartIndex, Count));
		return NextGroupId++;
	}

	void RemoveGroup(FInstanceGroupId GroupId)
	{
		check(GroupRanges.Contains(GroupId));
		const FInstanceGroupRange& GroupRangeToRemove = GroupRanges[GroupId];
		InstancesCount -= GroupRangeToRemove.InstanceIdToIndex.Num();
		GroupRanges.Remove(GroupId);
	}

	void IndexRemoved(int32 IndexToRemove)
	{
		bool bFound = false;
		for (TPair<FInstanceGroupId, FInstanceGroupRange>& GroupRange : GroupRanges)
		{
			if (GroupRange.Value.TryIndexReallocate(IndexToRemove, INDEX_NONE))
			{
				bFound = true;
				break;
			}
		}
		check(bFound);
	}

	void IndexReallocated(int32 OldIndex, int32 NewIndex)
	{
		bool bFound = false;
		for (TPair<FInstanceGroupId, FInstanceGroupRange>& GroupRange : GroupRanges)
		{
			if (GroupRange.Value.TryIndexReallocate(OldIndex, NewIndex))
			{
				bFound = true;
				break;
			}
		}
		check(bFound);
	}
	const FInstanceGroupRange& GetGroup(int32 GroupIndex) const { return GroupRanges[GroupIndex]; };

private:
	int32 InstancesCount = 0;
	int32 NextGroupId = 0;
	TMap<FInstanceGroupId, FInstanceGroupRange> GroupRanges;
};

/** */
struct FISMComponentDescription
{
	bool bUseHISM = false;
	bool bReverseCulling = false;
	bool bIsStaticMobility = false;
	bool bAffectShadow = true;
	bool bAffectDistanceFieldLighting = false;
	bool bAffectDynamicIndirectLighting = false;
	int32 NumCustomDataFloats = 0;
	int32 StartCullDistance = 0;
	int32 EndCullDistance = 0;
	int32 MinLod = 0;
	float LodScale = 1.f;

	bool operator==(const FISMComponentDescription& Other) const
	{
		return bUseHISM == Other.bUseHISM &&
			bReverseCulling == Other.bReverseCulling &&
			bIsStaticMobility == Other.bIsStaticMobility &&
			bAffectShadow == Other.bAffectShadow &&
			bAffectDistanceFieldLighting == Other.bAffectDistanceFieldLighting &&
			bAffectDynamicIndirectLighting == Other.bAffectDistanceFieldLighting &&
			NumCustomDataFloats == Other.NumCustomDataFloats &&
			StartCullDistance == Other.StartCullDistance && 
			EndCullDistance == Other.EndCullDistance &&
			MinLod == Other.MinLod &&
			LodScale == Other.LodScale;
	}
};

FORCEINLINE uint32 GetTypeHash(const FISMComponentDescription& Desc)
{
	const uint32 PackedBools = (Desc.bUseHISM ? 1 : 0) | (Desc.bReverseCulling ? 2 : 0) | (Desc.bIsStaticMobility ? 4 : 0) | (Desc.bAffectShadow ? 8 : 0) | (Desc.bAffectDistanceFieldLighting ? 16 : 0) | (Desc.bAffectDynamicIndirectLighting ? 32 : 0);
	uint32 Hash = HashCombine(GetTypeHash(PackedBools), GetTypeHash(Desc.NumCustomDataFloats));
	Hash = HashCombine(Hash, GetTypeHash(Desc.StartCullDistance));
	Hash = HashCombine(Hash, GetTypeHash(Desc.EndCullDistance));
	Hash = HashCombine(Hash, GetTypeHash(Desc.MinLod));
	return HashCombine(Hash, GetTypeHash(Desc.LodScale));
}

/**
* This represent a unique mesh with potentially overriden materials
* if the array is empty , there's no overrides
*/
struct FGeometryCollectionStaticMeshInstance
{
	UStaticMesh* StaticMesh = nullptr;
	TArray<UMaterialInterface*> MaterialsOverrides;
	FISMComponentDescription Desc;

	bool operator==(const FGeometryCollectionStaticMeshInstance& Other) const 
	{
		if (StaticMesh == Other.StaticMesh && Desc == Other.Desc)
		{
			if (MaterialsOverrides.Num() == Other.MaterialsOverrides.Num())
			{
				for (int32 MatIndex = 0; MatIndex < MaterialsOverrides.Num(); MatIndex++)
				{
					const FName MatName = MaterialsOverrides[MatIndex] ? MaterialsOverrides[MatIndex]->GetFName() : NAME_None;
					const FName OtherName = Other.MaterialsOverrides[MatIndex] ? Other.MaterialsOverrides[MatIndex]->GetFName() : NAME_None;
					if (MatName != OtherName)
					{
						return false;
					}
				}
				return true;
			}
		}
		return false;
	}
};

FORCEINLINE uint32 GetTypeHash(const FGeometryCollectionStaticMeshInstance& MeshInstance)
{
	uint32 CombinedHash = GetTypeHash(MeshInstance.StaticMesh);
	CombinedHash = HashCombine(CombinedHash, GetTypeHash(MeshInstance.MaterialsOverrides.Num()));
	for (const UMaterialInterface* Material: MeshInstance.MaterialsOverrides)
	{
		CombinedHash = HashCombine(CombinedHash, GetTypeHash(Material));
	}
	CombinedHash = HashCombine(CombinedHash, GetTypeHash(MeshInstance.Desc));
	return CombinedHash;
}

struct FGeometryCollectionMeshInfo
{
	int32 ISMIndex;
	int32 InstanceGroupIndex;
};

struct FGeometryCollectionISMPool;

/**
* FGeometryCollectionMeshGroup
* a mesh groupo contains various mesh with their instances
*/
struct FGeometryCollectionMeshGroup
{
	using FMeshId = int32;

	FMeshId AddMesh(const FGeometryCollectionStaticMeshInstance& MeshInstance, int32 InstanceCount, const FGeometryCollectionMeshInfo& ISMInstanceInfo);
	bool BatchUpdateInstancesTransforms(FGeometryCollectionISMPool& ISMPool, FMeshId MeshId, int32 StartInstanceIndex, const TArray<FTransform>& NewInstancesTransforms, bool bWorldSpace, bool bMarkRenderStateDirty, bool bTeleport);
	void RemoveAllMeshes(FGeometryCollectionISMPool& ISMPool);

	TMap<FGeometryCollectionStaticMeshInstance, FMeshId> Meshes;
	TArray<FGeometryCollectionMeshInfo> MeshInfos;
};

struct FGeometryCollectionISM
{
	FGeometryCollectionISM(AActor* OwmingActor, const FGeometryCollectionStaticMeshInstance& InMeshInstance);

	int32 AddInstanceGroup(int32 InstanceCount, TArrayView<const float> CustomDataFloats);

	TObjectPtr<UInstancedStaticMeshComponent> ISMComponent;
	FGeometryCollectionStaticMeshInstance MeshInstance;
	FInstanceGroups InstanceGroups;
};


struct FGeometryCollectionISMPool
{
	using FISMIndex = int32;

	FGeometryCollectionMeshInfo AddISM(UGeometryCollectionISMPoolComponent* OwningComponent, const FGeometryCollectionStaticMeshInstance& MeshInstance, int32 InstanceCount, TArrayView<const float> CustomDataFloats);
	bool BatchUpdateInstancesTransforms(FGeometryCollectionMeshInfo& MeshInfo, int32 StartInstanceIndex, const TArray<FTransform>& NewInstancesTransforms, bool bWorldSpace, bool bMarkRenderStateDirty, bool bTeleport);
	void RemoveISM(const FGeometryCollectionMeshInfo& MeshInfo);
	
	void OnISMInstanceIndexUpdated(UInstancedStaticMeshComponent* InComponent, TArrayView<const FInstancedStaticMeshDelegates::FInstanceIndexUpdateData> InIndexUpdates);

	/** Clear all ISM components and associated data */
	void Clear();

	TMap<FGeometryCollectionStaticMeshInstance, FISMIndex> MeshToISMIndex;
	TMap<UInstancedStaticMeshComponent*, FISMIndex> ISMComponentToISMIndex;
	TArray<FGeometryCollectionISM> ISMs;
	TArray<int32> FreeList;
};


/**
* UGeometryCollectionISMPoolComponent
*   Component that managed a pool of ISM in order to optimize render of geometry collections when not using fracture
*/
UCLASS(meta = (BlueprintSpawnableComponent))
class GEOMETRYCOLLECTIONENGINE_API UGeometryCollectionISMPoolComponent: public USceneComponent
{
	GENERATED_UCLASS_BODY()

public:
	using FMeshGroupId = int32;
	using FMeshId = int32;

	//~ Begin UActorComponent Interface
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	//~ End UActorComponent Interface

	/** 
	* Create an Mesh group which represent an arbitrary set of mesh with their instance 
	* no resources are created until the meshes are added for this group 
	* return a mesh group Id used to add and update instances
	*/
	FMeshGroupId CreateMeshGroup();

	/** destroy  a mesh group and its associated resources */
	void DestroyMeshGroup(FMeshGroupId MeshGroupId);

	/** Add a static mesh for a nmesh group */
	FMeshId AddMeshToGroup(FMeshGroupId MeshGroupId, const FGeometryCollectionStaticMeshInstance& MeshInstance, int32 InstanceCount, TArrayView<const float> CustomDataFloats);

	/** Add a static mesh for a nmesh group */
	bool BatchUpdateInstancesTransforms(FMeshGroupId MeshGroupId, FMeshId MeshId, int32 StartInstanceIndex, const TArray<FTransform>& NewInstancesTransforms, bool bWorldSpace = false, bool bMarkRenderStateDirty = false, bool bTeleport = false);

	/** Instance Index updated on the InstancedStaticMeshComponent which might need to be handled by the pool instance groups */
	void OnISMInstanceIndexUpdated(UInstancedStaticMeshComponent* InComponent, TArrayView<const FInstancedStaticMeshDelegates::FInstanceIndexUpdateData> InIndexUpdates)
	{
		// forward to the Pool
		Pool.OnISMInstanceIndexUpdated(InComponent, InIndexUpdates);
	}

private:
	uint32 NextMeshGroupId = 0;
	TMap<FMeshGroupId, FGeometryCollectionMeshGroup> MeshGroups;
	FGeometryCollectionISMPool Pool;

	FDelegateHandle OnISMInstanceIndexUpdatedHandle;

	// Expose internals for debug draw support.
	friend class UGeometryCollectionISMPoolDebugDrawComponent;
};
