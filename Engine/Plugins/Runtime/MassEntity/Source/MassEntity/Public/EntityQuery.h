// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Class.h"
#include "Containers/ArrayView.h"
#include "Containers/UnrealString.h"
#include "LWComponentTypes.h"
#include "ArchetypeTypes.h"
#include "EntityQuery.generated.h"


class UMassEntitySubsystem;
struct FArchetypeData;
struct FLWComponentSystemExecutionContext;
struct FLWComponentData;
struct FArchetypeHandle;

enum class ELWComponentAccess : uint8
{
	// no binding required
	None, 

	// We want to read the data for the component
	ReadOnly,

	// We want to read and write the data for the component
	ReadWrite,

	MAX
};

enum class ELWComponentPresence : uint8
{
	// All of the required components must be present
	All,

	// One of the required components must be present
	Any,

	// None of the required components can be present
	None,

	// If component is present we'll use it, but it missing stop processing of a given archetype
	Optional,

	MAX
};


struct MASSENTITY_API FLWComponentRequirement
{
	const UScriptStruct* StructType = nullptr;
	ELWComponentAccess AccessMode = ELWComponentAccess::None;
	ELWComponentPresence Presence = ELWComponentPresence::Optional;

public:
	FLWComponentRequirement(){}
	FLWComponentRequirement(const UScriptStruct* InStruct, const ELWComponentAccess InAccessMode, const ELWComponentPresence InPresence)
		: StructType(InStruct)
		, AccessMode(InAccessMode)
		, Presence(InPresence)
	{
		check(InStruct);
		checkf((Presence != ELWComponentPresence::Any && Presence != ELWComponentPresence::Optional)
			|| AccessMode == ELWComponentAccess::ReadOnly || AccessMode == ELWComponentAccess::ReadWrite, TEXT("Only ReadOnly and ReadWrite modes are suppored for optional requirements"));
	}

	bool RequiresBinding() const { return (AccessMode != ELWComponentAccess::None); }
	bool IsOptional() const { return (Presence == ELWComponentPresence::Optional || Presence == ELWComponentPresence::Any); }

	FString DebugGetDescription() const;

	// these functions are used for sorting. See FLWComponentSorterOperator
	int32 GetStructureSize() const
	{
		return StructType->GetStructureSize();
	}

	FName GetFName() const
	{
		return StructType->GetFName();
	}
};


/** 
 *  FLWComponentQuery is a structure that serves two main purposes:
 *  1. Describe properties required of an archetype that's a subject of calculations
 *  2. Trigger calculations on cached set of valid archetypes as described by Requirements
 * 
 *  A query to be considered valid needs declared at least one ELWComponentPresence::All, ELWComponentPresence::Any 
 *  ELWComponentPresence::Optional component requirement.
 */
USTRUCT()
struct MASSENTITY_API FLWComponentQuery
{
	GENERATED_BODY()

public:
	FLWComponentQuery();
	FLWComponentQuery(std::initializer_list<UScriptStruct*> InitList);
	FLWComponentQuery(TConstArrayView<const UScriptStruct*> InitList);

	/** Runs ExecuteFunction on all entities matching Requirements */
	void ForEachEntityChunk(UMassEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& ExecutionContext, const FLWComponentSystemExecuteFunction& ExecuteFunction);
	
	/** Will first verify that the archetype given with Chunks matches the query's requirements, and if so will run the other, more generic ForEachEntityChunk implementation */
	void ForEachEntityChunk(const FArchetypeChunkCollection& Chunks, UMassEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& ExecutionContext, const FLWComponentSystemExecuteFunction& ExecuteFunction);

	/**
	 * Attempts to process every chunk of every affected archetype in parallel.
	 */
	void ParallelForEachEntityChunk(UMassEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& ExecutionContext, const FLWComponentSystemExecuteFunction& ExecuteFunction);

	/** Will gather all archetypes from InEntitySubsystem matching this->Requirements.
	 *  Note that no work will be done if the cached data is up to date (as tracked by EntitySubsystemHash and 
	 *	ArchetypeDataVersion properties). */
	void CacheArchetypes(UMassEntitySubsystem& InEntitySubsystem);

	FLWComponentQuery& AddRequirement(const UScriptStruct* ComponentType, const ELWComponentAccess AccessMode, const ELWComponentPresence Presence = ELWComponentPresence::All)
	{
		checkf(Requirements.FindByPredicate([ComponentType](const FLWComponentRequirement& Item){ return Item.StructType == ComponentType; }) == nullptr
			, TEXT("Duplicated requirements are not supported. %s already present"), *GetNameSafe(ComponentType));
		
		if (Presence != ELWComponentPresence::None)
		{
			Requirements.Emplace(ComponentType, AccessMode, Presence);
		}

		switch (Presence)
		{
		case ELWComponentPresence::All:
			RequiredAllComponents.Add(*ComponentType);
			break;
		case ELWComponentPresence::Any:
			RequiredAnyComponents.Add(*ComponentType);
			break;
		case ELWComponentPresence::Optional:
			RequiredOptionalComponents.Add(*ComponentType);
			break;
		case ELWComponentPresence::None:
			RequiredNoneComponents.Add(*ComponentType);
			break;
		}
		// force recaching the next time this query is used or the following CacheArchetypes call.
		DirtyCachedData();
		return *this;
	}

	/** FLWComponentQuery ref returned for chaining */
	template<typename T>
	FLWComponentQuery& AddRequirement(const ELWComponentAccess AccessMode, const ELWComponentPresence Presence = ELWComponentPresence::All)
	{
		checkf(Requirements.FindByPredicate([](const FLWComponentRequirement& Item) { return Item.StructType == T::StaticStruct(); }) == nullptr
			, TEXT("Duplicated requirements are not supported. %s already present"), *T::StaticStruct()->GetName());

		static_assert(TIsDerivedFrom<T, FLWComponentData>::IsDerived, "Given struct doesn't represent a valid fragment type. Make sure to inherit from FLWComponentData or one of its child-types.");
		
		if (Presence != ELWComponentPresence::None)
		{
			Requirements.Emplace(T::StaticStruct(), AccessMode, Presence);
		}
		
		switch (Presence)
		{
		case ELWComponentPresence::All:
			RequiredAllComponents.Add<T>();
			break;
		case ELWComponentPresence::Any:
			RequiredAnyComponents.Add<T>();
			break;
		case ELWComponentPresence::Optional:
			RequiredOptionalComponents.Add<T>();
			break;
		case ELWComponentPresence::None:
			RequiredNoneComponents.Add<T>();
			break;
		}
		// force recaching the next time this query is used or the following CacheArchetypes call.
		DirtyCachedData();
		return *this;
	}

	void AddTagRequirement(const UScriptStruct& ComponentType, const ELWComponentPresence Presence)
	{
		checkfSlow(int(Presence) < int(ELWComponentPresence::Optional), TEXT("Optional and MAX presence are not valid calues for AddTagRequirement"));
		switch (Presence)
		{
		case ELWComponentPresence::All:
			RequiredAllTags.Add(ComponentType);
			break;
		case ELWComponentPresence::Any:
			RequiredAnyTags.Add(ComponentType);
			break;
		case ELWComponentPresence::None:
			RequiredNoneTags.Add(ComponentType);
			break;
		}
	}

	template<typename T>
	FLWComponentQuery& AddTagRequirement(const ELWComponentPresence Presence)
	{
		checkfSlow(int(Presence) < int(ELWComponentPresence::Optional), TEXT("Optional and MAX presence are not valid calues for AddTagRequirement"));
		static_assert(TIsDerivedFrom<T, FComponentTag>::IsDerived, "Given struct doesn't represent a valid tag type. Make sure to inherit from FLWComponentData or one of its child-types.");
		switch (Presence)
		{
			case ELWComponentPresence::All:
				RequiredAllTags.Add<T>();
				break;
			case ELWComponentPresence::Any:
				RequiredAnyTags.Add<T>();
				break;
			case ELWComponentPresence::None:
				RequiredNoneTags.Add<T>();
				break;
		}

		return *this;
	}

	// actual implementation in specializations
	template<ELWComponentPresence Presence> 
	FLWComponentQuery& AddTagRequirements(const FLWTagBitSet& TagBitSet)
	{
		static_assert(Presence == ELWComponentPresence::None || Presence == ELWComponentPresence::All || Presence == ELWComponentPresence::Any
			, "The only valid values for AddTagRequirements are All, Any and None");
		return *this;
	}

	template<typename T>
	FLWComponentQuery& AddChunkRequirement(const ELWComponentAccess AccessMode, const ELWComponentPresence Presence = ELWComponentPresence::All)
	{
		static_assert(TIsDerivedFrom<T, FLWChunkComponent>::IsDerived, "Given struct doesn't represent a valid chunk fragment type. Make sure to inherit from FLWChunkComponent or one of its child-types.");
		checkf(ChunkRequirements.FindByPredicate([](const FLWComponentRequirement& Item) { return Item.StructType == T::StaticStruct(); }) == nullptr
			, TEXT("Duplicated requirements are not supported. %s already present"), *T::StaticStruct()->GetName());
		checkfSlow(Presence == ELWComponentPresence::None || Presence == ELWComponentPresence::All
			, TEXT("The only valid Presence values for AddChunkRequirement are All and None"));
		
		if (Presence == ELWComponentPresence::All)
		{
			RequiredAllChunkComponents.Add<T>();
			ChunkRequirements.Emplace(T::StaticStruct(), AccessMode, Presence);
		}
		else
		{
			RequiredNoneChunkComponents.Add<T>();
		}

		return *this;
	}

	void Clear()
	{
		Requirements.Empty();
		DirtyCachedData();
	}

	FORCEINLINE void DirtyCachedData()
	{
		EntitySubsystemHash = 0;
		ArchetypeDataVersion = 0;
	}
	
	bool DoesArchetypeMatchRequirements(const FArchetypeHandle& ArchetypeHandle) const;

	/** The function validates requirements we make for queries. See the FLWComponentQuery struct description for details.
	 *  Note that this function is non-trivial and end users are not expected to need to use it. 
	 *  @return whether this query's requirements follow the rules. */
	bool CheckValidity() const;

	FString DebugGetDescription() const;

	TConstArrayView<FLWComponentRequirement> GetRequirements() const { return Requirements; }
	const FLWComponentBitSet& GetRequiredAllComponents() const { return RequiredAllComponents; }
	const FLWComponentBitSet& GetRequiredAnyComponents() const { return RequiredAnyComponents; }
	const FLWComponentBitSet& GetRequiredOptionalComponents() const { return RequiredOptionalComponents; }
	const FLWComponentBitSet& GetRequiredNoneComponents() const { return RequiredNoneComponents; }
	const FLWTagBitSet& GetRequiredAllTags() const { return RequiredAllTags; }
	const FLWTagBitSet& GetRequiredAnyTags() const { return RequiredAnyTags; }
	const FLWTagBitSet& GetRequiredNoneTags() const { return RequiredNoneTags; }
	const FLWChunkComponentBitSet& GetRequiredAllChunkComponents() const { return RequiredAllChunkComponents; }
	const FLWChunkComponentBitSet& GetRequiredNoneChunkComponents() const { return RequiredNoneChunkComponents; }

	const TArray<FArchetypeHandle>& GetArchetypes() const
	{ 
		return ValidArchetypes; 
	}

	/** 
	 * Goes through ValidArchetypes and sums up the number of entities contained in them.
	 * Note that the function is not const because calling it can result in re-caching of ValidArchetypes 
	 * @return the number of entities this given query would process if called "now"
	 */
	int32 GetMatchingEntitiesNum(UMassEntitySubsystem& InEntitySubsystem);

	/**
	 * Checks if any of ValidArchetypes has any entities.
	 * Note that the function is not const because calling it can result in re-caching of ValidArchetypes
	 * @return "true" if any of the ValidArchetypes has any entities, "false" otherwise
	 */
	bool HasMatchingEntities(UMassEntitySubsystem& InEntitySubsystem);

	/** 
	 * Sets a chunk filter condition that will applied to each chunk of all valid archetypes. Note 
	 * that this condition won't be applied when a specific collection of chunks is used (via FArchetypeChunkCollection)
	 * The value returned by InFunction controls whether to allow execution (true) or block it (false).
	 */
	void SetChunkFilter(const FLWComponentSystemChunkConditionFunction& InFunction) { ChunkCondition = InFunction; }

	void ClearChunkFilter() { ChunkCondition.Reset(); }

	bool HasChunkFilter() const { return bool(ChunkCondition); }

protected:
	void SortRequirements();
	void ReadCommandlineParams();

protected:
	TArray<FLWComponentRequirement> Requirements;
	TArray<FLWComponentRequirement> ChunkRequirements;
	FLWTagBitSet RequiredAllTags;
	FLWTagBitSet RequiredAnyTags;
	FLWTagBitSet RequiredNoneTags;
	FLWComponentBitSet RequiredAllComponents;
	FLWComponentBitSet RequiredAnyComponents;
	FLWComponentBitSet RequiredOptionalComponents;
	FLWComponentBitSet RequiredNoneComponents;
	FLWChunkComponentBitSet RequiredAllChunkComponents;
	FLWChunkComponentBitSet RequiredNoneChunkComponents;

private:
	/** 
	 * This function represents a condition that will be called for every chunk to be processed before the actual 
	 * execution function is called. The chunk component requirements are already bound and ready to be used by the time 
	 * ChunkCondition is executed.
	 */
	FLWComponentSystemChunkConditionFunction ChunkCondition;

	uint32 EntitySubsystemHash = 0;
	uint32 ArchetypeDataVersion = 0;

	TArray<FArchetypeHandle> ValidArchetypes;
	TArray<FLWRequirementIndicesMapping> ArchetypeComponentMapping;

	bool bAllowParallelExecution = false;
};

template<>
FORCEINLINE FLWComponentQuery& FLWComponentQuery::AddTagRequirements<ELWComponentPresence::All>(const FLWTagBitSet& TagBitSet)
{
	RequiredAllTags += TagBitSet;
	// force recaching the next time this query is used or the following CacheArchetypes call.
	DirtyCachedData();
	return *this;
}

template<>
FORCEINLINE FLWComponentQuery& FLWComponentQuery::AddTagRequirements<ELWComponentPresence::Any>(const FLWTagBitSet& TagBitSet)
{
	RequiredAnyTags += TagBitSet;
	// force recaching the next time this query is used or the following CacheArchetypes call.
	DirtyCachedData();
	return *this;
}

template<>
FORCEINLINE FLWComponentQuery& FLWComponentQuery::AddTagRequirements<ELWComponentPresence::None>(const FLWTagBitSet& TagBitSet)
{
	RequiredNoneTags += TagBitSet;
	// force recaching the next time this query is used or the following CacheArchetypes call.
	DirtyCachedData();
	return *this;
}
