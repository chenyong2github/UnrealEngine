// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "MassEntityTypes.h"
#include "MassProcessingTypes.h"
#include "InstancedStruct.h"
#include "MassEntityQuery.h"
#include "MassEntitySubsystem.generated.h"


class UMassEntitySubsystem;
struct FMassEntityQuery;
struct FMassExecutionContext;
struct FMassArchetypeData;
struct FMassCommandBuffer;
struct FArchetypeChunkCollection;
struct FMassArchetypeChunk;
class FOutputDevice;
enum class EMassFragmentAccess : uint8;

//@TODO: Comment this guy
UCLASS()
class MASSENTITY_API UMassEntitySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

	friend struct FMassEntityQuery;
private:
	// Index 0 is reserved so we can treat that index as an invalid entity handle
	constexpr static int32 NumReservedEntities = 1;

	struct FEntityData
	{
		TSharedPtr<FMassArchetypeData> CurrentArchetype;
		int32 SerialNumber = 0;

		void Reset()
		{
			CurrentArchetype.Reset();
			SerialNumber = 0;
		}

		bool IsValid() const
		{
			return SerialNumber != 0 && CurrentArchetype.IsValid();
		}
	};
	
public:
	struct FScopedProcessing
	{
		explicit FScopedProcessing(std::atomic<int32>& InProcessingScopeCount) : ScopedProcessingCount(InProcessingScopeCount)
		{
			++ScopedProcessingCount;
		}
		~FScopedProcessing()
		{
			--ScopedProcessingCount;
		}
	private:
		std::atomic<int32>& ScopedProcessingCount;
	};

	const static FMassEntityHandle InvalidEntity;

	UMassEntitySubsystem();

	//~UObject interface
	virtual void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) override;
	//~End of UObject interface

	//~USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~End of USubsystem interface
	
	/** 
	 * A special, relaxed but slower version of CreateArchetype functions that allows ComponentAngTagsList to contain 
	 * both components and tags. 
	 */
	FArchetypeHandle CreateArchetype(TConstArrayView<const UScriptStruct*> ComponentsAngTagsList);

	FArchetypeHandle CreateArchetype(TConstArrayView<const UScriptStruct*> ComponentList, const FMassTagBitSet& Tags, const FMassChunkFragmentBitSet& ChunkComponents);

	/** 
	 *  Creates an archetype like SourceArchetype + NewComponentList. 
	 *  @param SourceArchetype the archetype used to initially populate the list of components of the archetype being created. 
	 *  @param NewComponentList list of unique components to add to components fetched from SourceArchetype. Note that 
	 *   adding an empty list is not supported and doing so will result in failing a `check`
	 *  @return a handle of a new archetype
	 *  @note it's caller's responsibility to ensure that NewComponentList is not empty and contains only component
	 *   types that SourceArchetype doesn't already have. If the caller cannot guarantee it use of AddComponent functions
	 *   family is recommended.
	 */
	FArchetypeHandle CreateArchetype(const TSharedPtr<FMassArchetypeData>& SourceArchetype, const FMassFragmentBitSet& NewComponentList);

	FArchetypeHandle CreateArchetype(const FMassCompositionDescriptor& Descriptor);


	FArchetypeHandle GetArchetypeForEntity(FMassEntityHandle Entity) const;
	/** Method to iterate on all the component types of an archetype */
	static void ForEachArchetypeComponentType(const FArchetypeHandle Archetype, TFunction< void(const UScriptStruct* /*ComponentType*/)> Function);

	void SetDefaultChunkComponentValue(const FArchetypeHandle Archetype, FConstStructView InstancedStruct);

	/**
	 * Go through all archetypes and compact entities
	 * @param TimeAllowed to do entity compaction, once it reach that time it will stop and return
	 */
	void DoEntityCompaction(const double TimeAllowed);

	/**
	 * Creates fully built entity ready to be used by the subsystem
	 * @param Archetype you want this entity to be
	 * @return FLWEntity id of the newly created entity */
	FMassEntityHandle CreateEntity(const FArchetypeHandle Archetype);

	/**
	 * Creates fully built entity ready to be used by the subsystem
	 * @param ComponentInstanceList is the components to create the entity from and initialize values
	 * @return FLWEntity id of the newly created entity */
	FMassEntityHandle CreateEntity(TConstArrayView<FInstancedStruct> ComponentInstanceList);

	/** A version of CreateEntity that's creating a number of entities (Count) at one go
	 *  @param Archetype you want this entity to be
	 *  @param Count number of entities to create
	 *  @param OutEntities the newly created entities are appended to given array, i.e. the pre-existing content of OutEntities won't be affected by the call
	 *  @return number of entities created */
	int32 BatchCreateEntities(const FArchetypeHandle Archetype, const int32 Count, TArray<FMassEntityHandle>& OutEntities);

	/**
	 * Destroys a fully built entity, use ReleaseReservedEntity if entity was not yet built.
	 * @param Entity to destroy */
	void DestroyEntity(FMassEntityHandle Entity);

	/**
	 * Reserves an entity in the subsystem, the entity is still not ready to be used by the subsystem, need to call BuildEntity()
	 * @return FLWEntity id of the reserved entity */
	FMassEntityHandle ReserveEntity();

	/**
	 * Builds an entity for it to be ready to be used by the subsystem
	 * @param Entity to build which was retrieved with ReserveEntity() method
	 * @param Archetype you want this entity to be*/
	void BuildEntity(FMassEntityHandle Entity, const FArchetypeHandle Archetype);

	/**
	 * Builds an entity for it to be ready to be used by the subsystem
	 * @param Entity to build which was retrieved with ReserveEntity() method
	 * @param ComponentInstanceList is the components to create the entity from and initialize values*/
	void BuildEntity(FMassEntityHandle Entity, TConstArrayView<FInstancedStruct> ComponentInstanceList);

	/*
	 * Releases a previously reserved entity that was not yet built, otherwise call DestroyEntity
	 * @param Entity to release */
	void ReleaseReservedEntity(FMassEntityHandle Entity);

	/**
	 * Destroys all the entity in the provided array of entities
	 * @param InEntities to destroy
	 */
	void BatchDestroyEntities(TConstArrayView<FMassEntityHandle> InEntities);

	void BatchDestroyEntityChunks(const FArchetypeChunkCollection& Chunks);

	void AddComponentToEntity(FMassEntityHandle Entity, const UScriptStruct* ComponentType);

	/** 
	 *  Ensures that only unique components are added. 
	 *  @note It's caller's responsibility to ensure Entity's and ComponentList's validity. 
	 */
	void AddComponentListToEntity(FMassEntityHandle Entity, TConstArrayView<const UScriptStruct*> ComponentList);

	void AddComponentInstanceListToEntity(FMassEntityHandle Entity, TConstArrayView<FInstancedStruct> ComponentInstanceList);
	void RemoveComponentFromEntity(FMassEntityHandle Entity, const UScriptStruct* ComponentType);
	void RemoveComponentListFromEntity(FMassEntityHandle Entity, TConstArrayView<const UScriptStruct*> ComponentList);

	void AddTagToEntity(FMassEntityHandle Entity, const UScriptStruct* TagType);
	void RemoveTagFromEntity(FMassEntityHandle Entity, const UScriptStruct* TagType);
	void SwapTagsForEntity(FMassEntityHandle Entity, const UScriptStruct* FromComponentType, const UScriptStruct* ToComponentType);


	/**
	 * Adds fragments and tags indicated by InOutDescriptor to the Entity. The function also figures out which elements
	 * in InOutDescriptor are missing from the current composition of the given entity and then returns the resulting 
	 * delta via InOutDescriptor.
	 */
	void AddCompositionToEntity_GetDelta(FMassEntityHandle Entity, FMassCompositionDescriptor& InOutDescriptor);
	void RemoveCompositionFromEntity(FMassEntityHandle Entity, const FMassCompositionDescriptor& InDescriptor);

	/** 
	 * Moves an entity over to a new archetype by copying over components common to both archetypes
	 * @param Entity is the entity to move 
	 * @param NewArchetypeHandle the handle to the new archetype
	 */
	void MoveEntityToAnotherArchetype(FMassEntityHandle Entity, FArchetypeHandle NewArchetypeHandle);

	/** Copies values from ComponentInstanceList over to Entity's component. Caller is responsible for ensuring that 
	 *  the given entity does have given components. Failing this assumption will cause a check-fail.*/
	void SetEntityComponentsValues(FMassEntityHandle Entity, TArrayView<const FInstancedStruct> ComponentInstanceList);

	/** Copies values from ComponentInstanceList over to components of given entities collection. The caller is responsible 
	 *  for ensuring that the given entity archetype (FArchetypeChunkCollection.Archetype) does have given components. 
	 *  Failing this assumption will cause a check-fail. */
	static void BatchSetEntityComponentsValues(const FArchetypeChunkCollection& SparseEntities, TArrayView<const FInstancedStruct> ComponentInstanceList);

	// Return true if it is an valid built entity
	bool IsEntityActive(FMassEntityHandle Entity) const 
	{
		return IsEntityValid(Entity) && IsEntityBuilt(Entity);
	}

	// Returns true if Entity is valid
	bool IsEntityValid(FMassEntityHandle Entity) const;

	// Returns true if Entity is has been fully built (expecting a valid Entity)
	bool IsEntityBuilt(FMassEntityHandle Entity) const;

	// Asserts that IsEntityValid
	void CheckIfEntityIsValid(FMassEntityHandle Entity) const;

	// Asserts that IsEntityBuilt
	void CheckIfEntityIsActive(FMassEntityHandle Entity) const;

	template <typename ComponentType>
	ComponentType& GetComponentDataChecked(FMassEntityHandle Entity) const
	{
		return *((ComponentType*)InternalGetComponentDataChecked(Entity, ComponentType::StaticStruct()));
	}

	template <typename ComponentType>
	ComponentType* GetComponentDataPtr(FMassEntityHandle Entity) const
	{
		return (ComponentType*)InternalGetComponentDataPtr(Entity, ComponentType::StaticStruct());
	}

	FStructView GetComponentDataStruct(FMassEntityHandle Entity, const UScriptStruct* ComponentType) const
	{
		return FStructView(ComponentType, static_cast<uint8*>(InternalGetComponentDataPtr(Entity, ComponentType)));
	}

	uint32 GetArchetypeDataVersion() const { return ArchetypeDataVersion; }

	/**
	 * Creates and initializes a FMassExecutionContext instance.
	 */
	FMassExecutionContext CreateExecutionContext(const float DeltaSeconds) const;

	FScopedProcessing NewProcessingScope() { return FScopedProcessing(ProcessingScopeCount); }
	bool IsProcessing() const { return ProcessingScopeCount > 0; }
	FMassCommandBuffer& Defer() { return *DeferredCommandBuffer.Get(); }

#if WITH_MASSENTITY_DEBUG
	void DebugPrintEntity(int32 Index, FOutputDevice& Ar, const TCHAR* InPrefix = TEXT("")) const;
	void DebugPrintEntity(FMassEntityHandle Entity, FOutputDevice& Ar, const TCHAR* InPrefix = TEXT("")) const;
	void DebugPrintArchetypes(FOutputDevice& Ar) const;
	static void DebugGetStringDesc(const FArchetypeHandle& Archetype, FOutputDevice& Ar);
	void DebugGetArchetypesStringDetails(FOutputDevice& Ar, const bool bIncludeEmpty = true);
	void DebugGetArchetypeComponentTypes(const FArchetypeHandle& Archetype, TArray<const UScriptStruct*>& InOutComponentList) const;
	int32 DebugGetArchetypeEntitiesCount(const FArchetypeHandle& Archetype) const;	
	int32 DebugGetEntityCount() const { return Entities.Num() - NumReservedEntities - EntityFreeIndexList.Num(); }
	int32 DebugGetArchetypesCount() const { return ComponentHashToArchetypeMap.Num(); }
	void DebugRemoveAllEntities();
	void DebugForceArchetypeDataVersionBump() { ++ArchetypeDataVersion; }
	void DebugGetArchetypeStrings(const FArchetypeHandle& Archetype, TArray<FName>& OutComponentNames, TArray<FName>& OutTagNames);
#endif // WITH_MASSENTITY_DEBUG

protected:
	void GetValidArchetypes(const FMassEntityQuery& Query, TArray<FArchetypeHandle>& OutValidArchetypes);
	
	FArchetypeHandle InternalCreateSiblingArchetype(const TSharedPtr<FMassArchetypeData>& SourceArchetype, const FMassTagBitSet& OverrideTags);

private:
	void InternalBuildEntity(FMassEntityHandle Entity, const FArchetypeHandle Archetype);
	void InternalReleaseEntity(FMassEntityHandle Entity);

	/** 
	 *  Adds components in ComponentList to Entity. Only the unique components will be added.
	 */
	void InternalAddComponentListToEntityChecked(FMassEntityHandle Entity, const FMassFragmentBitSet& InComponents);

	/** 
	 *  Similar to InternalAddComponentListToEntity but expects NewComponentList not overlapping with current entity's
	 *  component list. It's callers responsibility to ensure that's true. Failing this will cause a `check` fail.
	 */
	void InternalAddComponentListToEntity(FMassEntityHandle Entity, const FMassFragmentBitSet& NewComponents);
	void* InternalGetComponentDataChecked(FMassEntityHandle Entity, const UScriptStruct* ComponentType) const;
	void* InternalGetComponentDataPtr(FMassEntityHandle Entity, const UScriptStruct* ComponentType) const;

private:
	TChunkedArray<FEntityData> Entities;
	TArray<int32> EntityFreeIndexList;

	TSharedPtr<FMassCommandBuffer> DeferredCommandBuffer;
	std::atomic<int32> SerialNumberGenerator;
	std::atomic<int32> ProcessingScopeCount;

	// the "version" number increased every time an archetype gets added
	uint32 ArchetypeDataVersion = 0;

	// Map of hash of sorted component list to archetypes with that hash
	TMap<uint32, TArray<TSharedPtr<FMassArchetypeData>>> ComponentHashToArchetypeMap;

	// Map to list of archetypes that contain the specified component type
	TMap<const UScriptStruct*, TArray<TSharedPtr<FMassArchetypeData>>> ComponentTypeToArchetypeMap;
};


//////////////////////////////////////////////////////////////////////
//

struct MASSENTITY_API FMassExecutionContext
{
private:
	struct FFragmentView 
	{
		FMassFragmentRequirement Requirement;
		TArrayView<FMassFragment> ComponentView;

		FFragmentView() {}
		explicit FFragmentView(const FMassFragmentRequirement& InRequirement) : Requirement(InRequirement) {}

		bool operator==(const UScriptStruct* ComponentType) const { return Requirement.StructType == ComponentType; }
	};
	TArray<FFragmentView, TInlineAllocator<8>> ComponentViews;

	struct FChunkFragmentView
	{
		FMassFragmentRequirement Requirement;
		FStructView ChunkComponentView;

		FChunkFragmentView() {}
		explicit FChunkFragmentView(const FMassFragmentRequirement& InRequirement) : Requirement(InRequirement)	{}

		bool operator==(const UScriptStruct* ComponentType) const { return Requirement.StructType == ComponentType; }
	};
	TArray<FChunkFragmentView, TInlineAllocator<4>> ChunkComponents;

	// mz@todo make this shared ptr thread-safe and never auto-flush in MT environment. 
	TSharedPtr<FMassCommandBuffer> DeferredCommandBuffer;
	TArrayView<FMassEntityHandle> EntityListView;
	
	/** If set this indicates the exact archetype and its chunks to be processed. 
	 *  @todo this data should live somewhere else, preferably be just a parameter to Query.ForEachEntityChunk function */
	FArchetypeChunkCollection ChunkCollection;
	
	FInstancedStruct AuxData;
	float DeltaTimeSeconds = 0.0f;
	int32 ChunkSerialModificationNumber = -1;
	FMassTagBitSet CurrentArchetypesTagBitSet;

#if WITH_MASSENTITY_DEBUG
	FString DebugExecutionDescription;
#endif
	
	/** @todo update comment, and look if we If true the EntitySystem will flush the deferred commands stored in DeferredCommandBuffer just after executing 
	 *  the given system. If False then the party calling UEntitySubsystem::ExecuteSystem is responsible for manually
	 *  calling FLWCCommandBuffer.ReplayBufferAgainstSystem() */
	bool bFlushDeferredCommands = true;

	TArrayView<FFragmentView> GetMutableRequirements() { return ComponentViews; }
	TArrayView<FChunkFragmentView> GetMutableChunkRequirements() { return ChunkComponents; }
	
	friend FMassArchetypeData;
	friend FMassEntityQuery;

public:
	FMassExecutionContext() = default;
	explicit FMassExecutionContext(const float InDeltaTimeSeconds, const bool bInFlushDeferredCommands = true)
		: DeltaTimeSeconds(InDeltaTimeSeconds)
		, bFlushDeferredCommands(bInFlushDeferredCommands)
	{}

#if WITH_MASSENTITY_DEBUG
	const FString& DebugGetExecutionDesc() const { return DebugExecutionDescription; }
	void DebugSetExecutionDesc(const FString& Description) { DebugExecutionDescription = Description; }
#endif

	/** Sets bFlushDeferredCommands. Note that setting to True while the system is being executed doesn't result in
	 *  immediate commands flushing */
	void SetFlushDeferredCommands(const bool bNewFlushDeferredCommands) { bFlushDeferredCommands = bNewFlushDeferredCommands; } 
	void SetDeferredCommandBuffer(const TSharedPtr<FMassCommandBuffer>& InDeferredCommandBuffer) { DeferredCommandBuffer = InDeferredCommandBuffer; }
	void SetChunkCollection(const FArchetypeChunkCollection& InChunkCollection);
	void SetChunkCollection(FArchetypeChunkCollection&& InChunkCollection);
	void ClearChunkCollection() { ChunkCollection.Reset(); }
	void SetAuxData(const FInstancedStruct& InAuxData) { AuxData = InAuxData; }

	float GetDeltaTimeSeconds() const
	{
		return DeltaTimeSeconds;
	}

	TSharedPtr<FMassCommandBuffer> GetSharedDeferredCommandBuffer() const { return DeferredCommandBuffer; }
	FMassCommandBuffer& Defer() { checkSlow(DeferredCommandBuffer.IsValid()); return *DeferredCommandBuffer.Get(); }

	TConstArrayView<FMassEntityHandle> GetEntities() const { return EntityListView; }
	int32 GetEntitiesNum() const { return EntityListView.Num(); }

	FMassEntityHandle GetEntity(const int32 Index) const
	{
		return EntityListView[Index];
	}

	template<typename T>
	bool DoesArchetypeHaveTag() const
	{
		static_assert(TIsDerivedFrom<T, FMassTag>::IsDerived, "Given struct is not of a valid fragment type.");
		return CurrentArchetypesTagBitSet.Contains<T>();
	}

	/** Chunk related operation */
	void SetCurrentChunkSerialModificationNumber(const int32 SerialModificationNumber) { ChunkSerialModificationNumber = SerialModificationNumber; }
	int32 GetChunkSerialModificationNumber() const { return ChunkSerialModificationNumber; }

	template<typename T>
	T& GetMutableChunkComponent()
	{
		static_assert(TIsDerivedFrom<T, FMassChunkFragment>::IsDerived, "Given struct doesn't represent a valid chunk fragment type. Make sure to inherit from FMassChunkFragment or one of its child-types.");

		const UScriptStruct* Type = T::StaticStruct();
		const FChunkFragmentView* FoundChunkComponentData = ChunkComponents.FindByPredicate([Type](const FChunkFragmentView& Element) { return Element.Requirement.StructType == Type; });
		checkf(FoundChunkComponentData, TEXT("Chunk Component requirement not found: %s"), *T::StaticStruct()->GetName());
		return FoundChunkComponentData->ChunkComponentView.template GetMutable<T>();
	}

	template<typename T>
	const T& GetChunkComponent() const
	{
		static_assert(TIsDerivedFrom<T, FMassChunkFragment>::IsDerived, "Given struct doesn't represent a valid chunk fragment type. Make sure to inherit from FMassChunkFragment or one of its child-types.");

		const UScriptStruct* Type = T::StaticStruct();
		const FChunkFragmentView* FoundChunkComponentData = ChunkComponents.FindByPredicate([Type](const FChunkFragmentView& Element) { return Element.Requirement.StructType == Type; } );
		checkf(FoundChunkComponentData, TEXT("Chunk Component requirement not found: %s"), *T::StaticStruct()->GetName());
		return FoundChunkComponentData->ChunkComponentView.template Get<T>();
	}

	template<typename TFragment>
	TArrayView<TFragment> GetMutableComponentView()
	{
		const UScriptStruct* ComponentType = TFragment::StaticStruct();
		const FFragmentView* View = ComponentViews.FindByPredicate([ComponentType](const FFragmentView& Element) { return Element.Requirement.StructType == ComponentType; });
		//checkfSlow(View != nullptr, TEXT("Requested component type not bound"));
		//checkfSlow(View->Requirement.AccessMode == EMassFragmentAccess::ReadWrite, TEXT("Requested component has not been bound for writing"));
		return MakeArrayView<TFragment>((TFragment*)View->ComponentView.GetData(), View->ComponentView.Num());
	}

	template<typename TFragment>
	TConstArrayView<TFragment> GetComponentView() const
	{
		const UScriptStruct* ComponentType = TFragment::StaticStruct();
		const FFragmentView* View = ComponentViews.FindByPredicate([ComponentType](const FFragmentView& Element) { return Element.Requirement.StructType == ComponentType; });
		//checkfSlow(View != nullptr, TEXT("Requested component type not bound"));
		return TConstArrayView<TFragment>((const TFragment*)View->ComponentView.GetData(), View->ComponentView.Num());
	}

	TConstArrayView<FMassFragment> GetComponentComponentView(const UScriptStruct* ComponentType) const
	{
		const FFragmentView* View = ComponentViews.FindByPredicate([ComponentType](const FFragmentView& Element) { return Element.Requirement.StructType == ComponentType; });
		checkSlow(View);
		return TConstArrayView<FMassFragment>((const FMassFragment*)View->ComponentView.GetData(), View->ComponentView.Num());;
	}

	TArrayView<FMassFragment> GetMutableComponentView(const UScriptStruct* ComponentType) 
	{
		const FFragmentView* View = ComponentViews.FindByPredicate([ComponentType](const FFragmentView& Element) { return Element.Requirement.StructType == ComponentType; });
		checkSlow(View);
		return View->ComponentView;
	}

	/** Sparse chunk related operation */
	const FArchetypeChunkCollection& GetChunkCollection() const { return ChunkCollection; }

	const FInstancedStruct& GetAuxData() const { return AuxData; }
	FInstancedStruct& GetMutableAuxData() { return AuxData; }

	void FlushDeferred(UMassEntitySubsystem& EntitySystem) const;

	void ClearExecutionData();
	void SetCurrentArchetypeData(FMassArchetypeData& ArchetypeData);

protected:
	void SetRequirements(TConstArrayView<FMassFragmentRequirement> InRequirements, TConstArrayView<FMassFragmentRequirement> InChunkRequirements);
	void ClearComponentViews()
	{
		for (FFragmentView& View : ComponentViews)
		{
			View.ComponentView = TArrayView<FMassFragment>();
		}
		for (FChunkFragmentView& View : ChunkComponents)
		{
			View.ChunkComponentView.Reset();
		}
	}
};
