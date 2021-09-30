// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntitySubsystem.h"
#include "ArchetypeTypes.h"

struct FMassEntityQuery;
struct FMassExecutionContext;
class FOutputDevice;
struct FArchetypeChunkCollection;

// This is one chunk within an archetype
struct FMassArchetypeChunk
{
private:
	uint8* RawMemory = nullptr;
	int32 NumInstances = 0;
	int32 SerialModificationNumber = 0;
	TArray<FInstancedStruct> ChunkComponentData;

public:
	explicit FMassArchetypeChunk(int32 AllocSize, TConstArrayView<FInstancedStruct> InChunkComponentTemplates)
		: ChunkComponentData(InChunkComponentTemplates)
	{
		RawMemory = (uint8*)FMemory::Malloc(AllocSize);
	}

	~FMassArchetypeChunk()
	{
		FMemory::Free(RawMemory);
	}

	// Returns the Entity array element at the specified index
	FMassEntityHandle& GetEntityArrayElementRef(int32 ChunkBase, int32 IndexWithinChunk)
	{
		return ((FMassEntityHandle*)(RawMemory + ChunkBase))[IndexWithinChunk];
	}

	uint8* GetRawMemory() const
	{
		return RawMemory;
	}

	int32 GetNumInstances() const
	{
		return NumInstances;
	}

	void AddMultipleInstances(uint32 Count)
	{
		NumInstances += Count;
		SerialModificationNumber++;
	}

	void RemoveMultipleInstances(uint32 Count)
	{
		NumInstances -= Count;
		check(NumInstances >= 0);
		SerialModificationNumber++;
	}

	void AddInstance()
	{
		AddMultipleInstances(1);
	}

	void RemoveInstance()
	{
		RemoveMultipleInstances(1);
	}

	int32 GetSerialModificationNumber() const
	{
		return SerialModificationNumber;
	}

	FStructView GetMutableChunkComponentViewChecked(const int32 Index) { return FStructView(ChunkComponentData[Index]); }

	FInstancedStruct* FindMutableChunkComponent(const UScriptStruct* Type)
	{
		return ChunkComponentData.FindByPredicate([Type](const FInstancedStruct& Element)
			{
				return Element.GetScriptStruct()->IsChildOf(Type);
			});
	}

	void Recycle(TConstArrayView<FInstancedStruct> InChunkComponentsTemplate)
	{
		checkf(NumInstances == 0, TEXT("Recycling a chunk that is not empty."));
		SerialModificationNumber++;
		ChunkComponentData = InChunkComponentsTemplate;
	}

#if WITH_MASSENTITY_DEBUG
	int32 DebugGetChunkComponentCount() const { return ChunkComponentData.Num(); }
#endif // WITH_MASSENTITY_DEBUG
};

// Information for a single component type in an archetype
struct FMassArchetypeFragmentConfig
{
	const UScriptStruct* ComponentType = nullptr;
	int32 ArrayOffsetWithinChunk = 0;

	void* GetComponentData(uint8* ChunkBase, int32 IndexWithinChunk) const
	{
		return ChunkBase + ArrayOffsetWithinChunk + (IndexWithinChunk * ComponentType->GetStructureSize());
	}
};

// An archetype is defined by a collection of unique component types (no duplicates).
// Order doesn't matter, there will only ever be one FMassArchetypeData per unique set of component types per entity manager subsystem
struct FMassArchetypeData
{
private:
	// One-stop-shop variable describing the archetype's component and tag composition 
	FMassCompositionDescriptor CompositionDescriptor;

	TArray<FInstancedStruct> ChunkComponentTemplates;

	TArray<FMassArchetypeFragmentConfig, TInlineAllocator<16>> ComponentConfigs;
	
	TArray<FMassArchetypeChunk> Chunks;

	// Entity ID to index within archetype
	//@TODO: Could be folded into FEntityData in the entity manager at the expense of a bit
	// of loss of encapsulation and extra complexity during archetype changes
	TMap<int32, int32> EntityMap;
	
	TMap<const UScriptStruct*, int32> ComponentIndexMap;

	int32 NumEntitiesPerChunk;
	int32 TotalBytesPerEntity;
	int32 EntityListOffsetWithinChunk;

	friend FMassEntityQuery;
	friend FArchetypeChunkCollection;

public:
	TConstArrayView<FMassArchetypeFragmentConfig> GetComponentConfigs() const { return ComponentConfigs; }
	const FMassFragmentBitSet& GetComponentBitSet() const { return CompositionDescriptor.Components; }
	const FMassTagBitSet& GetTagBitSet() const { return CompositionDescriptor.Tags; }
	const FMassChunkFragmentBitSet& GetChunkComponentBitSet() const { return CompositionDescriptor.ChunkComponents; }
	const FMassCompositionDescriptor& GetCompositionDescriptor() const { return CompositionDescriptor; }

	/** Method to iterate on all the component types */
	void ForEachComponentType(TFunction< void(const UScriptStruct* /*ComponentType*/)> Function) const;
	bool HasComponentType(const UScriptStruct* ComponentType) const;
	bool HasTagType(const UScriptStruct* ComponentType) const { check(ComponentType); return CompositionDescriptor.Tags.Contains(*ComponentType); }
	bool IsEquivalent(TConstArrayView<const UScriptStruct*> OtherList) const;
	bool IsEquivalent(const FMassFragmentBitSet& InComponentBitSet, const FMassTagBitSet& InTagBitSet, const FMassChunkFragmentBitSet& InChunkComponentsBitSet) const
	{ 
		return CompositionDescriptor.IsEquivalent(InComponentBitSet, InTagBitSet, InChunkComponentsBitSet);
	}
	bool IsEquivalent(const FMassCompositionDescriptor& OtherCompositionDescriptor) const
	{
		return CompositionDescriptor.IsEquivalent(OtherCompositionDescriptor);
	}

	void Initialize(const FMassFragmentBitSet& Components, const FMassTagBitSet& Tags, const FMassChunkFragmentBitSet& ChunkComponents);

	/** 
	 * A special way of initializing an archetype resulting in a copy of SiblingArchetype's setup with OverrideTags
	 * replacing original tags of SiblingArchetype
	 */
	void InitializeWithSibling(const FMassArchetypeData& SiblingArchetype, const FMassTagBitSet& OverrideTags);

	void AddEntity(FMassEntityHandle Entity);
	void RemoveEntity(FMassEntityHandle Entity);
	void BatchDestroyEntityChunks(const FArchetypeChunkCollection& ChunkCollection, TArray<FMassEntityHandle>& OutEntitiesRemoved);

	bool HasComponentDataForEntity(const UScriptStruct* ComponentType, int32 EntityIndex) const;
	void* GetComponentDataForEntityChecked(const UScriptStruct* ComponentType, int32 EntityIndex) const;
	void* GetComponentDataForEntity(const UScriptStruct* ComponentType, int32 EntityIndex) const;

	FORCEINLINE int32 GetInternalIndexForEntity(const int32 EntityIndex) const { return EntityMap.FindChecked(EntityIndex); }
	int32 GetNumEntitiesPerChunk() const { return NumEntitiesPerChunk; }

	int32 GetNumEntities() const { return EntityMap.Num(); }

	int32 GetChunkAllocSize() const { return 64*1024; }

	int32 GetChunkCount() const { return Chunks.Num(); }

	void ExecuteFunction(FMassExecutionContext& RunContext, const FMassExecuteFunction& Function, const FMassQueryRequirementIndicesMapping& RequirementMapping, const FArchetypeChunkCollection& ChunkCollection);
	void ExecuteFunction(FMassExecutionContext& RunContext, const FMassExecuteFunction& Function, const FMassQueryRequirementIndicesMapping& RequirementMapping, const FMassChunkConditionFunction& ChunkCondition = FMassChunkConditionFunction());

	void ExecutionFunctionForChunk(FMassExecutionContext RunContext, const FMassExecuteFunction& Function, const FMassQueryRequirementIndicesMapping& RequirementMapping, const FArchetypeChunkCollection::FChunkInfo& ChunkInfo, const FMassChunkConditionFunction& ChunkCondition = FMassChunkConditionFunction());

	/**
	 * Compacts entities to fill up chunks as much as possible
	 */
	void CompactEntities(const double TimeAllowed);

	/**
	 * Moves the entity from this archetype to another, will only copy all matching component types
	 * @param Entity is the entity to move
	 * @param NewArchetype the archetype to move to
	 */
	void MoveEntityToAnotherArchetype(const FMassEntityHandle Entity, FMassArchetypeData& NewArchetype);

	/**
	 * Set all component sources data on specified entity, will check if there are component sources type that does not exist in the archetype
	 * @param Entity is the entity to set the data of all components
	 * @param ComponentSources are the components to copy the data from
	 */
	void SetComponentsData(const FMassEntityHandle Entity, TArrayView<const FInstancedStruct> ComponentSources);

	/** For all entities indicated by ChunkCollection the function sets the value of component of type
	 *  ComponentSource.GetScriptStruct to the value represented by ComponentSource.GetMemory */
	void SetComponentData(const FArchetypeChunkCollection& ChunkCollection, const FInstancedStruct& ComponentSource);

	void SetDefaultChunkComponentValue(FConstStructView InstancedStruct);

	/** Returns conversion from given Requirements to archetype's component indices */
	void GetRequirementsComponentMapping(TConstArrayView<FMassFragmentRequirement> Requirements, FMassFragmentIndicesMapping& OutComponentIndices);

	/** Returns conversion from given ChunkRequirements to archetype's chunk component indices */
	void GetRequirementsChunkComponentMapping(TConstArrayView<FMassFragmentRequirement> ChunkRequirements, FMassFragmentIndicesMapping& OutComponentIndices);

	SIZE_T GetAllocatedSize() const;

	// Converts the list of components into a user-readable debug string
	FString DebugGetDescription() const;

#if WITH_MASSENTITY_DEBUG
	/**
	 * Prints out debug information about the archetype
	 */
	void DebugPrintArchetype(FOutputDevice& Ar);

	/**
	 * Prints out component's values for the specified entity. 
	 * @param Entity The entity for which we want to print component values
	 * @param Ar The output device
	 * @param InPrefix Optional prefix to remove from component names
	 */
	void DebugPrintEntity(FMassEntityHandle Entity, FOutputDevice& Ar, const TCHAR* InPrefix = TEXT("")) const;
#endif // WITH_MASSENTITY_DEBUG

	void REMOVEME_GetArrayViewForComponentInChunk(int32 ChunkIndex, const UScriptStruct* ComponentType, void*& OutChunkBase, int32& OutNumEntities);

	//////////////////////////////////////////////////////////////////////
	// low level api
	FORCEINLINE const int32* GetComponentIndex(const UScriptStruct* ComponentType) const { return ComponentIndexMap.Find(ComponentType); }
	FORCEINLINE int32 GetComponentIndexChecked(const UScriptStruct* ComponentType) const { return ComponentIndexMap.FindChecked(ComponentType); }

	FORCEINLINE void* GetComponentData(const int32 ComponentIndex, const FInternalEntityHandle EntityIndex) const
	{
		return ComponentConfigs[ComponentIndex].GetComponentData(EntityIndex.ChunkRawMemory, EntityIndex.IndexWithinChunk);
	}

	FORCEINLINE FInternalEntityHandle MakeEntityHandle(int32 EntityIndex) const
	{
		const int32 AbsoluteIndex = EntityMap.FindChecked(EntityIndex);
		const int32 ChunkIndex = AbsoluteIndex / NumEntitiesPerChunk;
	
		return FInternalEntityHandle(Chunks[ChunkIndex].GetRawMemory(), AbsoluteIndex % NumEntitiesPerChunk); 
	}

	FORCEINLINE FInternalEntityHandle MakeEntityHandle(FMassEntityHandle Entity) const
	{
		return MakeEntityHandle(Entity.Index); 
	}

	bool IsInitialized() const { return TotalBytesPerEntity > 0 && ComponentConfigs.IsEmpty() == false; }

protected:
	FORCEINLINE void* GetComponentData(const int32 ComponentIndex, uint8* ChunkRawMemory, const int32 IndexWithinChunk) const
	{
		return ComponentConfigs[ComponentIndex].GetComponentData(ChunkRawMemory, IndexWithinChunk);
	}

	void BindEntityRequirements(FMassExecutionContext& RunContext, const FMassFragmentIndicesMapping& EntityComponentsMapping, FMassArchetypeChunk& Chunk, const int32 SubchunkStart, const int32 SubchunkLength);
	void BindChunkComponentRequirements(FMassExecutionContext& RunContext, const FMassFragmentIndicesMapping& ChunkComponentsMapping, FMassArchetypeChunk& Chunk);

private:
	int32 AddEntityInternal(FMassEntityHandle Entity, const bool bInitializeComponents);
	void RemoveEntityInternal(const int32 AbsoluteIndex, const bool bDestroyComponents);
};
