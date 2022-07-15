// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Class.h"
#include "Containers/ArrayView.h"
#include "Containers/UnrealString.h"
#include "MassEntityTypes.h"
#include "MassArchetypeTypes.h"
#include "MassExternalSubsystemTraits.h"
#include "MassEntityQuery.generated.h"


class UMassEntitySubsystem;
class UMassProcessor;
struct FMassArchetypeData;
struct FMassExecutionContext;
struct FMassFragment;
struct FMassArchetypeHandle;
struct FMassExecutionRequirements;
struct FMassRequirementAccessDetector;

UENUM()
enum class EMassFragmentAccess : uint8
{
	// no binding required
	None, 

	// We want to read the data for the fragment
	ReadOnly,

	// We want to read and write the data for the fragment
	ReadWrite,

	MAX
};

UENUM()
enum class EMassFragmentPresence : uint8
{
	// All of the required fragments must be present
	All,

	// One of the required fragments must be present
	Any,

	// None of the required fragments can be present
	None,

	// If fragment is present we'll use it, but it missing stop processing of a given archetype
	Optional,

	MAX
};


struct MASSENTITY_API FMassFragmentRequirement
{
	const UScriptStruct* StructType = nullptr;
	EMassFragmentAccess AccessMode = EMassFragmentAccess::None;
	EMassFragmentPresence Presence = EMassFragmentPresence::Optional;

public:
	FMassFragmentRequirement(){}
	FMassFragmentRequirement(const UScriptStruct* InStruct, const EMassFragmentAccess InAccessMode, const EMassFragmentPresence InPresence)
		: StructType(InStruct)
		, AccessMode(InAccessMode)
		, Presence(InPresence)
	{
		check(InStruct);
		checkf((Presence != EMassFragmentPresence::Any && Presence != EMassFragmentPresence::Optional)
			|| AccessMode == EMassFragmentAccess::ReadOnly || AccessMode == EMassFragmentAccess::ReadWrite, TEXT("Only ReadOnly and ReadWrite modes are suppored for optional requirements"));
	}

	bool RequiresBinding() const { return (AccessMode != EMassFragmentAccess::None); }
	bool IsOptional() const { return (Presence == EMassFragmentPresence::Optional || Presence == EMassFragmentPresence::Any); }

	FString DebugGetDescription() const;

	// these functions are used for sorting. See FScriptStructSortOperator
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
 *  FMassEntityQuery is a structure that serves two main purposes:
 *  1. Describe properties required of an archetype that's a subject of calculations
 *  2. Trigger calculations on cached set of valid archetypes as described by Requirements
 * 
 *  A query to be considered valid needs declared at least one EMassFragmentPresence::All, EMassFragmentPresence::Any 
 *  EMassFragmentPresence::Optional fragment requirement.
 */
USTRUCT()
struct MASSENTITY_API FMassEntityQuery
{
	GENERATED_BODY()

	friend struct FMassDebugger;

public:
	FMassEntityQuery();
	FMassEntityQuery(std::initializer_list<UScriptStruct*> InitList);
	FMassEntityQuery(TConstArrayView<const UScriptStruct*> InitList);
	FMassEntityQuery(UMassProcessor& Owner);

	void RegisterWithProcessor(UMassProcessor& Owner);

	/** Runs ExecuteFunction on all entities matching Requirements */
	void ForEachEntityChunk(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& ExecutionContext, const FMassExecuteFunction& ExecuteFunction);
	
	/** Will first verify that the archetype given with Collection matches the query's requirements, and if so will run the other, more generic ForEachEntityChunk implementation */
	void ForEachEntityChunk(const FMassArchetypeEntityCollection& Collection, UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& ExecutionContext, const FMassExecuteFunction& ExecuteFunction);

	/**
	 * Attempts to process every chunk of every affected archetype in parallel.
	 */
	void ParallelForEachEntityChunk(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& ExecutionContext, const FMassExecuteFunction& ExecuteFunction);

	/** Configures ExecutionContext with access/presence requirements declared by this query */
	void ApplyQueryRequirementsToContext(FMassExecutionContext& ExecutionContext);

	/** Will gather all archetypes from InEntitySubsystem matching this->Requirements.
	 *  Note that no work will be done if the cached data is up to date (as tracked by EntitySubsystemHash and 
	 *	ArchetypeDataVersion properties). */
	void CacheArchetypes(const UMassEntitySubsystem& InEntitySubsystem);

	FMassEntityQuery& AddRequirement(const UScriptStruct* FragmentType, const EMassFragmentAccess AccessMode, const EMassFragmentPresence Presence = EMassFragmentPresence::All)
	{
		checkf(Requirements.FindByPredicate([FragmentType](const FMassFragmentRequirement& Item){ return Item.StructType == FragmentType; }) == nullptr
			, TEXT("Duplicated requirements are not supported. %s already present"), *GetNameSafe(FragmentType));
		
		if (Presence != EMassFragmentPresence::None)
		{
			Requirements.Emplace(FragmentType, AccessMode, Presence);
		}

		switch (Presence)
		{
		case EMassFragmentPresence::All:
			RequiredAllFragments.Add(*FragmentType);
			break;
		case EMassFragmentPresence::Any:
			RequiredAnyFragments.Add(*FragmentType);
			break;
		case EMassFragmentPresence::Optional:
			RequiredOptionalFragments.Add(*FragmentType);
			break;
		case EMassFragmentPresence::None:
			RequiredNoneFragments.Add(*FragmentType);
			break;
		}
		// force recaching the next time this query is used or the following CacheArchetypes call.
		DirtyCachedData();
		return *this;
	}

	/** FMassEntityQuery ref returned for chaining */
	template<typename T>
	FMassEntityQuery& AddRequirement(const EMassFragmentAccess AccessMode, const EMassFragmentPresence Presence = EMassFragmentPresence::All)
	{
		checkf(Requirements.FindByPredicate([](const FMassFragmentRequirement& Item) { return Item.StructType == T::StaticStruct(); }) == nullptr
			, TEXT("Duplicated requirements are not supported. %s already present"), *T::StaticStruct()->GetName());

		static_assert(TIsDerivedFrom<T, FMassFragment>::IsDerived, "Given struct doesn't represent a valid fragment type. Make sure to inherit from FMassFragment or one of its child-types.");
		
		if (Presence != EMassFragmentPresence::None)
		{
			Requirements.Emplace(T::StaticStruct(), AccessMode, Presence);
		}
		
		switch (Presence)
		{
		case EMassFragmentPresence::All:
			RequiredAllFragments.Add<T>();
			break;
		case EMassFragmentPresence::Any:
			RequiredAnyFragments.Add<T>();
			break;
		case EMassFragmentPresence::Optional:
			RequiredOptionalFragments.Add<T>();
			break;
		case EMassFragmentPresence::None:
			RequiredNoneFragments.Add<T>();
			break;
		}
		// force recaching the next time this query is used or the following CacheArchetypes call.
		DirtyCachedData();
		return *this;
	}

	void AddTagRequirement(const UScriptStruct& TagType, const EMassFragmentPresence Presence)
	{
		checkfSlow(int(Presence) < int(EMassFragmentPresence::Optional), TEXT("Optional and MAX presence are not valid calues for AddTagRequirement"));
		switch (Presence)
		{
		case EMassFragmentPresence::All:
			RequiredAllTags.Add(TagType);
			break;
		case EMassFragmentPresence::Any:
			RequiredAnyTags.Add(TagType);
			break;
		case EMassFragmentPresence::None:
			RequiredNoneTags.Add(TagType);
			break;
		}
		DirtyCachedData();
	}

	template<typename T>
	FMassEntityQuery& AddTagRequirement(const EMassFragmentPresence Presence)
	{
		checkfSlow(int(Presence) < int(EMassFragmentPresence::Optional), TEXT("Optional and MAX presence are not valid calues for AddTagRequirement"));
		static_assert(TIsDerivedFrom<T, FMassTag>::IsDerived, "Given struct doesn't represent a valid tag type. Make sure to inherit from FMassFragment or one of its child-types.");
		switch (Presence)
		{
			case EMassFragmentPresence::All:
				RequiredAllTags.Add<T>();
				break;
			case EMassFragmentPresence::Any:
				RequiredAnyTags.Add<T>();
				break;
			case EMassFragmentPresence::None:
				RequiredNoneTags.Add<T>();
				break;
		}
		DirtyCachedData();
		return *this;
	}

	// actual implementation in specializations
	template<EMassFragmentPresence Presence> 
	FMassEntityQuery& AddTagRequirements(const FMassTagBitSet& TagBitSet)
	{
		static_assert(Presence == EMassFragmentPresence::None || Presence == EMassFragmentPresence::All || Presence == EMassFragmentPresence::Any
			, "The only valid values for AddTagRequirements are All, Any and None");
		return *this;
	}

	template<typename T>
	FMassEntityQuery& AddChunkRequirement(const EMassFragmentAccess AccessMode, const EMassFragmentPresence Presence = EMassFragmentPresence::All)
	{
		static_assert(TIsDerivedFrom<T, FMassChunkFragment>::IsDerived, "Given struct doesn't represent a valid chunk fragment type. Make sure to inherit from FMassChunkFragment or one of its child-types.");
		checkf(ChunkRequirements.FindByPredicate([](const FMassFragmentRequirement& Item) { return Item.StructType == T::StaticStruct(); }) == nullptr
			, TEXT("Duplicated requirements are not supported. %s already present"), *T::StaticStruct()->GetName());
		checkfSlow(Presence != EMassFragmentPresence::Any, TEXT("\'Any\' is not a valid Presence value for AddChunkRequirement."));

		switch (Presence)
		{
			case EMassFragmentPresence::All:
				RequiredAllChunkFragments.Add<T>();
				ChunkRequirements.Emplace(T::StaticStruct(), AccessMode, Presence);
				break;
			case EMassFragmentPresence::Optional:
				RequiredOptionalChunkFragments.Add<T>();
				ChunkRequirements.Emplace(T::StaticStruct(), AccessMode, Presence);
				break;
			case EMassFragmentPresence::None:
				RequiredNoneChunkFragments.Add<T>();
				break;
		}
		DirtyCachedData();
		return *this;
	}

	template<typename T>
	FMassEntityQuery& AddConstSharedRequirement(const EMassFragmentPresence Presence = EMassFragmentPresence::All)
	{
		static_assert(TIsDerivedFrom<T, FMassSharedFragment>::IsDerived, "Given struct doesn't represent a valid shared fragment type. Make sure to inherit from FMassSharedFragment or one of its child-types.");
		checkf(ConstSharedRequirements.FindByPredicate([](const FMassFragmentRequirement& Item) { return Item.StructType == T::StaticStruct(); }) == nullptr
			, TEXT("Duplicated requirements are not supported. %s already present"), *T::StaticStruct()->GetName());
		checkfSlow(Presence != EMassFragmentPresence::Any, TEXT("\'Any\' is not a valid Presence value for AddSharedRequirement."));

		switch (Presence)
		{
		case EMassFragmentPresence::All:
			RequiredAllSharedFragments.Add<T>();
			ConstSharedRequirements.Emplace(T::StaticStruct(), EMassFragmentAccess::ReadOnly, Presence);
			break;
		case EMassFragmentPresence::Optional:
			RequiredOptionalSharedFragments.Add<T>();
			ConstSharedRequirements.Emplace(T::StaticStruct(), EMassFragmentAccess::ReadOnly, Presence);
			break;
		case EMassFragmentPresence::None:
			RequiredNoneSharedFragments.Add<T>();
			break;
		}
		DirtyCachedData();
		return *this;
	}

	template<typename T>
	FMassEntityQuery& AddSharedRequirement(const EMassFragmentAccess AccessMode, const EMassFragmentPresence Presence = EMassFragmentPresence::All)
	{
		static_assert(TIsDerivedFrom<T, FMassSharedFragment>::IsDerived, "Given struct doesn't represent a valid shared fragment type. Make sure to inherit from FMassSharedFragment or one of its child-types.");
		checkf(SharedRequirements.FindByPredicate([](const FMassFragmentRequirement& Item) { return Item.StructType == T::StaticStruct(); }) == nullptr
			, TEXT("Duplicated requirements are not supported. %s already present"), *T::StaticStruct()->GetName());
		checkfSlow(Presence != EMassFragmentPresence::Any, TEXT("\'Any\' is not a valid Presence value for AddSharedRequirement."));

		switch (Presence)
		{
		case EMassFragmentPresence::All:
			RequiredAllSharedFragments.Add<T>();
			SharedRequirements.Emplace(T::StaticStruct(), AccessMode, Presence);
			if (AccessMode == EMassFragmentAccess::ReadWrite)
			{
				bRequiresGameThreadExecution |= TMassSharedFragmentTraits<T>::GameThreadOnly;
			}
			break;
		case EMassFragmentPresence::Optional:
			RequiredOptionalSharedFragments.Add<T>();
			SharedRequirements.Emplace(T::StaticStruct(), AccessMode, Presence);
			if (AccessMode == EMassFragmentAccess::ReadWrite)
			{
				bRequiresGameThreadExecution |= TMassSharedFragmentTraits<T>::GameThreadOnly;
			}
			break;
		case EMassFragmentPresence::None:
			RequiredNoneSharedFragments.Add<T>();
			break;
		}
		DirtyCachedData();
		return *this;
	}

	template<typename T>
	FMassEntityQuery& AddSubsystemRequirement(const EMassFragmentAccess AccessMode)
	{
		check(AccessMode != EMassFragmentAccess::None && AccessMode != EMassFragmentAccess::MAX);

		switch (AccessMode)
		{
		case EMassFragmentAccess::ReadOnly:
			RequiredConstSubsystems.Add<T>();
			bRequiresGameThreadExecution |= TMassExternalSubsystemTraits<T>::GameThreadOnly;
			break;
		case EMassFragmentAccess::ReadWrite:
			RequiredMutableSubsystems.Add<T>();
			bRequiresGameThreadExecution |= TMassExternalSubsystemTraits<T>::GameThreadOnly;
			break;
		default:
			check(false);
		}
		
		return *this;
	}

	FMassEntityQuery& AddSubsystemRequirement(const TSubclassOf<USubsystem> SubsystemClass, const EMassFragmentAccess AccessMode, const bool bGameThreadOnly = true)
	{
		check(AccessMode != EMassFragmentAccess::None && AccessMode != EMassFragmentAccess::MAX);

		switch (AccessMode)
		{
		case EMassFragmentAccess::ReadOnly:
			RequiredConstSubsystems.Add(**SubsystemClass);
			bRequiresGameThreadExecution |= bGameThreadOnly;
			break;
		case EMassFragmentAccess::ReadWrite:
			RequiredMutableSubsystems.Add(**SubsystemClass);
			bRequiresGameThreadExecution |= bGameThreadOnly;
			break;
		default:
			check(false);
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
	
	bool DoesArchetypeMatchRequirements(const FMassArchetypeHandle& ArchetypeHandle) const;

	bool DoesRequireGameThreadExecution() const { return bRequiresGameThreadExecution || bRequiresMutatingWorldAccess; }
	void RequireMutatingWorldAccess() { bRequiresMutatingWorldAccess = true; }

	/** The function validates requirements we make for queries. See the FMassEntityQuery struct description for details.
	 *  Note that this function is non-trivial and end users are not expected to need to use it. 
	 *  @return whether this query's requirements follow the rules. */
	bool CheckValidity() const;

	FString DebugGetDescription() const;
	FString DebugGetArchetypeCompatibilityDescription(const FMassArchetypeHandle& ArchetypeHandle) const;

	TConstArrayView<FMassFragmentRequirement> GetRequirements() const { return Requirements; }
	const FMassFragmentBitSet& GetRequiredAllFragments() const { return RequiredAllFragments; }
	const FMassFragmentBitSet& GetRequiredAnyFragments() const { return RequiredAnyFragments; }
	const FMassFragmentBitSet& GetRequiredOptionalFragments() const { return RequiredOptionalFragments; }
	const FMassFragmentBitSet& GetRequiredNoneFragments() const { return RequiredNoneFragments; }
	const FMassTagBitSet& GetRequiredAllTags() const { return RequiredAllTags; }
	const FMassTagBitSet& GetRequiredAnyTags() const { return RequiredAnyTags; }
	const FMassTagBitSet& GetRequiredNoneTags() const { return RequiredNoneTags; }
	const FMassChunkFragmentBitSet& GetRequiredAllChunkFragments() const { return RequiredAllChunkFragments; }
	const FMassChunkFragmentBitSet& GetRequiredOptionalChunkFragments() const { return RequiredOptionalChunkFragments; }
	const FMassChunkFragmentBitSet& GetRequiredNoneChunkFragments() const { return RequiredNoneChunkFragments; }
	const FMassSharedFragmentBitSet& GetRequiredAllSharedFragments() const { return RequiredAllSharedFragments; }
	const FMassSharedFragmentBitSet& GetRequiredOptionalSharedFragments() const { return RequiredOptionalSharedFragments; }
	const FMassSharedFragmentBitSet& GetRequiredNoneSharedFragments() const { return RequiredNoneSharedFragments; }

	const TArray<FMassArchetypeHandle>& GetArchetypes() const
	{ 
		return ValidArchetypes; 
	}

	/** 
	 * Goes through ValidArchetypes and sums up the number of entities contained in them.
	 * Note that the function is not const because calling it can result in re-caching of ValidArchetypes 
	 * @return the number of entities this given query would process if called "now"
	 */
	int32 GetNumMatchingEntities(UMassEntitySubsystem& InEntitySubsystem);

	/**
	 * Checks if any of ValidArchetypes has any entities.
	 * Note that the function is not const because calling it can result in re-caching of ValidArchetypes
	 * @return "true" if any of the ValidArchetypes has any entities, "false" otherwise
	 */
	bool HasMatchingEntities(UMassEntitySubsystem& InEntitySubsystem);

	/** 
	 * Sets a chunk filter condition that will applied to each chunk of all valid archetypes. Note 
	 * that this condition won't be applied when a specific entity colleciton is used (via FMassArchetypeEntityCollection )
	 * The value returned by InFunction controls whether to allow execution (true) or block it (false).
	 */
	void SetChunkFilter(const FMassChunkConditionFunction& InFunction) { ChunkCondition = InFunction; }

	void ClearChunkFilter() { ChunkCondition.Reset(); }

	bool HasChunkFilter() const { return bool(ChunkCondition); }

	/**
	 * Sets a archetype filter condition that will applied to each valid archetypes.
	 * The value returned by InFunction controls whether to allow execution (true) or block it (false).
	 */
	void SetArchetypeFilter(const FMassArchetypeConditionFunction& InFunction) { ArchetypeCondition = InFunction; }

	void ClearArchetypeFilter() { ArchetypeCondition.Reset(); }

	bool HasArchetypeFilter() const { return bool(ArchetypeCondition); }

	/** 
	 * If ArchetypeHandle is among ValidArchetypes then the function retrieves requirements mapping cached for it,
	 * otherwise an empty mapping will be returned (and the requirements binding will be done the slow way).
	 */
	const FMassQueryRequirementIndicesMapping& GetFragmentMappingForArchetype(const FMassArchetypeHandle ArchetypeHandle) const;

	void ExportRequirements(FMassExecutionRequirements& OutRequirements) const;

protected:
	void SortRequirements();
	void ReadCommandlineParams();

protected:
	friend struct FMassRequirementAccessDetector;

	TArray<FMassFragmentRequirement> Requirements;
	TArray<FMassFragmentRequirement> ChunkRequirements;
	TArray<FMassFragmentRequirement> ConstSharedRequirements;
	TArray<FMassFragmentRequirement> SharedRequirements;
	FMassTagBitSet RequiredAllTags;
	FMassTagBitSet RequiredAnyTags;
	FMassTagBitSet RequiredNoneTags;
	FMassFragmentBitSet RequiredAllFragments;
	FMassFragmentBitSet RequiredAnyFragments;
	FMassFragmentBitSet RequiredOptionalFragments;
	FMassFragmentBitSet RequiredNoneFragments;
	FMassChunkFragmentBitSet RequiredAllChunkFragments;
	FMassChunkFragmentBitSet RequiredOptionalChunkFragments;
	FMassChunkFragmentBitSet RequiredNoneChunkFragments;
	FMassSharedFragmentBitSet RequiredAllSharedFragments;
	FMassSharedFragmentBitSet RequiredOptionalSharedFragments;
	FMassSharedFragmentBitSet RequiredNoneSharedFragments;

	FMassExternalSubystemBitSet RequiredConstSubsystems;
	FMassExternalSubystemBitSet RequiredMutableSubsystems;

private:
	/** 
	 * This function represents a condition that will be called for every chunk to be processed before the actual 
	 * execution function is called. The chunk fragment requirements are already bound and ready to be used by the time 
	 * ChunkCondition is executed.
	 */
	FMassChunkConditionFunction ChunkCondition;

	/**
	 * This function represents a condition that will be called for every archetype to be processed before the actual
	 * execution function is called. The shared fragment requirements are already bound and ready to be used by the time
	 * ArchetypeCondition is executed.
	 */
	FMassArchetypeConditionFunction ArchetypeCondition;

	uint32 EntitySubsystemHash = 0;
	uint32 ArchetypeDataVersion = 0;

	TArray<FMassArchetypeHandle> ValidArchetypes;
	TArray<FMassQueryRequirementIndicesMapping> ArchetypeFragmentMapping;

	uint8 bAllowParallelExecution : 1;
	uint8 bRequiresGameThreadExecution : 1;
	uint8 bRequiresMutatingWorldAccess : 1;

	EMassExecutionContextType ExpectedContextType = EMassExecutionContextType::Local;

#if WITH_MASSENTITY_DEBUG
	uint8 bRegistered : 1;
#endif // WITH_MASSENTITY_DEBUG
};

template<>
FORCEINLINE FMassEntityQuery& FMassEntityQuery::AddTagRequirements<EMassFragmentPresence::All>(const FMassTagBitSet& TagBitSet)
{
	RequiredAllTags += TagBitSet;
	// force recaching the next time this query is used or the following CacheArchetypes call.
	DirtyCachedData();
	return *this;
}

template<>
FORCEINLINE FMassEntityQuery& FMassEntityQuery::AddTagRequirements<EMassFragmentPresence::Any>(const FMassTagBitSet& TagBitSet)
{
	RequiredAnyTags += TagBitSet;
	// force recaching the next time this query is used or the following CacheArchetypes call.
	DirtyCachedData();
	return *this;
}

template<>
FORCEINLINE FMassEntityQuery& FMassEntityQuery::AddTagRequirements<EMassFragmentPresence::None>(const FMassTagBitSet& TagBitSet)
{
	RequiredNoneTags += TagBitSet;
	// force recaching the next time this query is used or the following CacheArchetypes call.
	DirtyCachedData();
	return *this;
}
