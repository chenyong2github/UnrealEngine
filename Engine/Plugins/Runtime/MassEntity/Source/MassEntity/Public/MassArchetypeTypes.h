// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Class.h"
#include "Containers/ArrayView.h"
#include "Containers/UnrealString.h"
#include "MassEntityTypes.h"

class UMassEntitySubsystem;
struct FMassArchetypeData;
struct FMassExecutionContext;
struct FMassFragment;
struct FMassArchetypeChunkIterator;
struct FMassEntityQuery;
struct FMassArchetypeSubChunks;
struct FMassEntityView;

typedef TFunction< void(FMassExecutionContext& /*ExecutionContext*/) > FMassExecuteFunction;
typedef TFunction< bool(const FMassExecutionContext& /*ExecutionContext*/) > FMassChunkConditionFunction;
typedef TFunction< bool(const FMassExecutionContext& /*ExecutionContext*/) > FMassArchetypeConditionFunction;


//////////////////////////////////////////////////////////////////////
// FMassArchetypeHandle

// An opaque handle to an archetype
struct FMassArchetypeHandle final
{
	FMassArchetypeHandle() = default;
	bool IsValid() const { return DataPtr.IsValid(); }

	bool operator==(const FMassArchetypeHandle& Other) const { return DataPtr == Other.DataPtr; }
	bool operator!=(const FMassArchetypeHandle& Other) const { return DataPtr != Other.DataPtr; }

	MASSENTITY_API friend uint32 GetTypeHash(const FMassArchetypeHandle& Instance);
private:
	FMassArchetypeHandle(const TSharedPtr<FMassArchetypeData>& InDataPtr)
	: DataPtr(InDataPtr)
	{}
	TSharedPtr<FMassArchetypeData> DataPtr;

	friend UMassEntitySubsystem;
	friend FMassArchetypeSubChunks;
	friend FMassEntityQuery;
	friend FMassEntityView;
};


//////////////////////////////////////////////////////////////////////
// FMassArchetypeSubChunks 

/** A struct that converts an arbitrary array of entities of given Archetype into a sequence of continuous
 *  entity chunks. The goal is to have the user create an instance of this struct once and run through a bunch of
 *  systems. The runtime code usually uses FMassArchetypeChunkIterator to iterate on the chunk collection.
 */
struct MASSENTITY_API FMassArchetypeSubChunks 
{
public:
	struct FSubChunkInfo
	{
		int32 ChunkIndex = INDEX_NONE;
		int32 SubchunkStart = 0;
		/** negative or 0-length means "all available entities within chunk" */
		int32 Length = 0;

		FSubChunkInfo() = default;
		explicit FSubChunkInfo(const int32 InChunkIndex, const int32 InSubchunkStart = 0, const int32 InLength = 0) : ChunkIndex(InChunkIndex), SubchunkStart(InSubchunkStart), Length(InLength) {}
		/** Note that we consider invalid-length chunks valid as long as ChunkIndex and SubchunkStart are valid */
		bool IsSet() const { return ChunkIndex != INDEX_NONE && SubchunkStart >= 0; }

		bool operator==(const FSubChunkInfo& Other) const
		{
			return ChunkIndex == Other.ChunkIndex && SubchunkStart == Other.SubchunkStart && Length == Other.Length;
		}
		bool operator!=(const FSubChunkInfo& Other) const { return !(*this == Other); }
	};

	enum EDuplicatesHandling
	{
		NoDuplicates,	// indicates that the caller guarantees there are no duplicates in the input Entities collection
						// note that in no-shipping builds a `check` will fail if duplicates are present.
		FoldDuplicates,	// indicates that it's possible that Entities contains duplicates. The input Entities collection 
						// will be processed and duplicates will be removed.
	};

	using FSubChunkArray = TArray<FSubChunkInfo>;
	using FConstSubChunkArrayView = TConstArrayView<FSubChunkInfo>;

private:
	FSubChunkArray SubChunks;
	/** entity indices indicated by SubChunks are only valid with given Archetype */
	FMassArchetypeHandle Archetype;

public:
	FMassArchetypeSubChunks() = default;
	FMassArchetypeSubChunks(const FMassArchetypeHandle& InArchetype, TConstArrayView<FMassEntityHandle> InEntities, EDuplicatesHandling DuplicatesHandling);
	explicit FMassArchetypeSubChunks(FMassArchetypeHandle& InArchetypeHandle);
	explicit FMassArchetypeSubChunks(TSharedPtr<FMassArchetypeData>& InArchetype);

	FConstSubChunkArrayView GetChunks() const { return SubChunks; }
	const FMassArchetypeHandle& GetArchetype() const { return Archetype; }
	bool IsEmpty() const { return SubChunks.Num() == 0 && Archetype.IsValid() == false; }
	bool IsSet() const { return Archetype.IsValid(); }
	void Reset() 
	{ 
		Archetype = FMassArchetypeHandle();
		SubChunks.Reset();
	}

	/** The comparison function that checks if Other is identical to this. Intended for diagnostics/debugging. */
	bool IsSame(const FMassArchetypeSubChunks& Other) const;

private:
	void GatherChunksFromArchetype(TSharedPtr<FMassArchetypeData>& InArchetype);
};

//////////////////////////////////////////////////////////////////////
// FMassArchetypeChunkIterator

/**
 *  The type used to iterate over given archetype's chunks, be it full, continuous chunks or sparse subchunks. It hides
 *  this details from the rest of the system.
 */
struct MASSENTITY_API FMassArchetypeChunkIterator
{
private:
	FMassArchetypeSubChunks::FConstSubChunkArrayView SubChunks;
	int32 CurrentChunkIndex = 0;

public:
	explicit FMassArchetypeChunkIterator(const FMassArchetypeSubChunks::FConstSubChunkArrayView& InSubChunks) : SubChunks(InSubChunks), CurrentChunkIndex(0) {}

	operator bool() const { return SubChunks.IsValidIndex(CurrentChunkIndex) && SubChunks[CurrentChunkIndex].IsSet(); }
	FMassArchetypeChunkIterator& operator++() { ++CurrentChunkIndex; return *this; }

	const FMassArchetypeSubChunks::FSubChunkInfo* operator->() const { check(bool(*this)); return &SubChunks[CurrentChunkIndex]; }
	const FMassArchetypeSubChunks::FSubChunkInfo& operator*() const { check(bool(*this)); return SubChunks[CurrentChunkIndex]; }
};

//////////////////////////////////////////////////////////////////////
// FMassRawEntityInChunkData

struct FMassRawEntityInChunkData 
{
	FMassRawEntityInChunkData() = default;
	FMassRawEntityInChunkData(uint8* InChunkRawMemory, const int32 InIndexWithinChunk)
        : ChunkRawMemory(InChunkRawMemory), IndexWithinChunk(InIndexWithinChunk)
	{}
	bool IsValid() const { return ChunkRawMemory != nullptr && IndexWithinChunk != INDEX_NONE; }
	bool operator==(const FMassRawEntityInChunkData & Other) const { return ChunkRawMemory == Other.ChunkRawMemory && IndexWithinChunk == Other.IndexWithinChunk; }

	uint8* ChunkRawMemory = nullptr;
	int32 IndexWithinChunk = INDEX_NONE;
};

//////////////////////////////////////////////////////////////////////
// FMassQueryRequirementIndicesMapping

using FMassFragmentIndicesMapping = TArray<int32, TInlineAllocator<16>>;

struct FMassQueryRequirementIndicesMapping
{
	FMassQueryRequirementIndicesMapping() = default;

	FMassFragmentIndicesMapping EntityFragments;
	FMassFragmentIndicesMapping ChunkFragments;
	FMassFragmentIndicesMapping ConstSharedFragments;
	FMassFragmentIndicesMapping SharedFragments;
	FORCEINLINE bool IsEmpty() const
	{
		return EntityFragments.Num() == 0 || ChunkFragments.Num() == 0;
	}
};
