// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "ActorPartition/PartitionActor.h"
#include "Misc/Guid.h"
#include "ISMPartition/ISMComponentDescriptor.h"
#include "ISMPartition/ISMComponentData.h"
#include "Templates/Tuple.h"
#include "Containers/SortedMap.h"
#include "ISMPartitionActor.generated.h"

struct FISMClientHandle;

DECLARE_LOG_CATEGORY_EXTERN(LogISMPartition, Log, All);

/** Actor base class for instance containers placed on a grid.
	See UActorPartitionSubsystem. */
UCLASS(Abstract)
class ENGINE_API AISMPartitionActor : public APartitionActor
{
	GENERATED_UCLASS_BODY()

public:
#if WITH_EDITOR	
	//~ Begin AActor Interface
	virtual void PreEditUndo() override;
	virtual void PostEditUndo() override;
	//~ End AActor Interface	

	FISMClientHandle RegisterClient(const FGuid& ClientGuid);
	void UnregisterClient(FISMClientHandle& ClientHandle);

	int32 RegisterISMComponentDescriptor(const FISMComponentDescriptor& Descriptor);
	const FISMComponentDescriptor& GetISMComponentDescriptor(int32 DescriptorIndex) const { return Descriptors[DescriptorIndex]; }

	void AddISMInstance(const FISMClientHandle& Handle, const FTransform& InstanceTransform, const TSortedMap<int32, TArray<FTransform>>& InstanceDefinition);
	void RemoveISMInstance(const FISMClientHandle& Handle, int32 InstanceIndex, bool* bOutIsEmpty = nullptr);
	void RemoveISMInstances(const FISMClientHandle& Handle);
	void SelectISMInstances(const FISMClientHandle& Handle, bool bSelect, const TSet<int32>& Indices);
	void SetISMInstanceTransform(const FISMClientHandle& Handle, int32 InstanceIndex, const FTransform& NewTransform, bool bTeleport, const TSortedMap<int32, TArray<FTransform>>& InstanceDefinition);
	int32 GetISMInstanceIndex(const FISMClientHandle& Handle, const UInstancedStaticMeshComponent* ISMComponent, int32 ComponentIndex) const;
	FBox GetISMInstanceBounds(const FISMClientHandle& Handle, const TSet<int32>& Indices) const;
	void ReserveISMInstances(const FISMClientHandle& Handle, int32 AddedInstanceCount, const TSortedMap<int32, TArray<FTransform>>& InstanceDefinition);

	bool IsISMComponent(const UPrimitiveComponent* Component) const;
	void BeginUpdate();
	void EndUpdate();
	void UpdateHISMTrees(bool bAsync, bool bForce);
	void GetClientComponents(const FISMClientHandle& Handle, TArray<UInstancedStaticMeshComponent*>& OutComponents);
	void OutputStats() const;
protected:

private:
	void RemoveISMInstancesInternal(FISMComponentData& ComponentData, FISMClientData& OwnerData, int32 InstanceIndex);

	void InvalidateComponentLightingCache(FISMComponentData& ComponentData);
	void AddInstanceToComponent(FISMComponentData& ComponentData, const FTransform& WorldTransform);
	void UpdateInstanceTransform(FISMComponentData& ComponentData, int32 ComponentInstanceIndex, const FTransform& WorldTransform, bool bTeleport);
	void RemoveInstanceFromComponent(FISMComponentData& ComponentData, int32 ComponentInstanceIndex);
	bool DestroyComponentIfEmpty(FISMComponentDescriptor& Descriptor, FISMComponentData& ComponentData);
	void ModifyComponent(FISMComponentData& ComponentData);
	void CreateComponent(const FISMComponentDescriptor& Descriptor, FISMComponentData& ComponentData);
	void ModifyActor();
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TArray<FGuid> Clients;
		
	UPROPERTY()
	TArray<FISMComponentDescriptor> Descriptors;

	UPROPERTY()
	TArray<FISMComponentData> DescriptorComponents;

	/** If greater than 0 means we are between a BeginUpdate/EndUpdate call and there are some things we can delay/optimize */
	int32 UpdateDepth;

	/** If Modified as already been called between a BeginUpdate/EndUpdate (avoid multiple Modify calls on the component) */
	bool bWasModifyCalled;
#endif
};

USTRUCT()
struct FISMClientHandle
{
	GENERATED_USTRUCT_BODY();

	FISMClientHandle()
		: Index(-1)
	{

	}

	bool IsValid() const { return Index >= 0 && Guid.IsValid(); }

	void Serialize(FArchive& Ar)
	{
		Ar << Index;
		Ar << Guid;
	}

private:
	friend class AISMPartitionActor;

	FISMClientHandle(int32 ClientIndex, FGuid ClientGuid)
		: Index(ClientIndex)
		, Guid(ClientGuid)
	{

	}

	UPROPERTY()
	int32 Index;

	UPROPERTY()
	FGuid Guid;
};