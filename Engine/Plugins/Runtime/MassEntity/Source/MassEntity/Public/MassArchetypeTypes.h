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
struct FArchetypeChunkCollection;
struct FMassEntityView;

typedef TFunction< void(FMassExecutionContext& /*ExecutionContext*/) > FMassExecuteFunction;
typedef TFunction< bool(const FMassExecutionContext& /*ExecutionContext*/) > FMassChunkConditionFunction;
typedef TFunction< bool(const FMassExecutionContext& /*ExecutionContext*/) > FMassArchetypeConditionFunction;


//////////////////////////////////////////////////////////////////////
//

// An opaque handle to an archetype
struct FArchetypeHandle final
{
	FArchetypeHandle() = default;
	bool IsValid() const { return DataPtr.IsValid(); }

	MASSENTITY_API bool operator==(const FMassArchetypeData* Other) const;
	bool operator==(const FArchetypeHandle& Other) const { return DataPtr == Other.DataPtr; }
	bool operator!=(const FArchetypeHandle& Other) const { return DataPtr != Other.DataPtr; }

	MASSENTITY_API friend uint32 GetTypeHash(const FArchetypeHandle& Instance);
private:
	FArchetypeHandle(const TSharedPtr<FMassArchetypeData>& InDataPtr)
	: DataPtr(InDataPtr)
	{}
	TSharedPtr<FMassArchetypeData> DataPtr;

	friend UMassEntitySubsystem;
	friend FArchetypeChunkCollection;
	friend FMassEntityQuery;
	friend FMassEntityView;
};


//////////////////////////////////////////////////////////////////////
//

/** A struct that converts an arbitrary array of entities of given Archetype into a sequence of continuous
 *  entity chunks. The goal is to have the user create an instance of this struct once and run through a bunch of
 *  systems. The runtime code usually uses FMassArchetypeChunkIterator to iterate on the chunk collection.
 */
struct MASSENTITY_API FArchetypeChunkCollection
{
public:
	struct FChunkInfo
	{
		int32 ChunkIndex = INDEX_NONE;
		int32 SubchunkStart = 0;
		/** negative or 0-length means "all available entities within chunk" */
		int32 Length = 0;

		FChunkInfo() = default;
		explicit FChunkInfo(const int32 InChunkIndex, const int32 InSubchunkStart = 0, const int32 InLength = 0) : ChunkIndex(InChunkIndex), SubchunkStart(InSubchunkStart), Length(InLength) {}
		/** Note that we consider invalid-length chunks valid as long as ChunkIndex and SubchunkStart are valid */
		bool IsSet() const { return ChunkIndex != INDEX_NONE && SubchunkStart >= 0; }

		bool operator==(const FChunkInfo& Other) const
		{
			return ChunkIndex == Other.ChunkIndex && SubchunkStart == Other.SubchunkStart && Length == Other.Length;
		}
		bool operator!=(const FChunkInfo& Other) const { return !(*this == Other); }
	};

	enum EDuplicatesHandling
	{
		NoDuplicates,	// indicates that the caller guarantees there are no duplicates in the input Entities collection
						// note that in no-shipping builds a `check` will fail if duplicates are present.
		FoldDuplicates,	// indicates that it's possible that Entities contains duplicates. The input Entities collection 
						// will be processed and duplicates will be removed.
	};

private:
	TArray<FChunkInfo> Chunks;
	/** entity indices indicated by SubChunks are only valid with given Archetype */
	FArchetypeHandle Archetype;

public:
	FArchetypeChunkCollection() = default;
	FArchetypeChunkCollection(const FArchetypeHandle& InArchetype, TConstArrayView<FMassEntityHandle> InEntities, EDuplicatesHandling DuplicatesHandling);
	explicit FArchetypeChunkCollection(FArchetypeHandle& InArchetypeHandle);
	explicit FArchetypeChunkCollection(TSharedPtr<FMassArchetypeData>& InArchetype);

	TArrayView<const FChunkInfo> GetChunks() const { return Chunks; }
	const FArchetypeHandle& GetArchetype() const { return Archetype; }
	bool IsEmpty() const { return Chunks.Num() == 0 && Archetype.IsValid() == false; }
	bool IsSet() const { return Archetype.IsValid(); }
	void Reset() 
	{ 
		Archetype = FArchetypeHandle();
		Chunks.Reset();
	}

	/** The comparison function that checks if Other is identical to this. Intended for diagnostics/debugging. */
	bool IsSame(const FArchetypeChunkCollection& Other) const;

private:
	void GatherChunksFromArchetype(TSharedPtr<FMassArchetypeData>& InArchetype);
};

//////////////////////////////////////////////////////////////////////
//

/**
 *  The type used to iterate over given archetype's chunks, be it full, continuous chunks or sparse subchunks. It hides
 *  this details from the rest of the system.
 */
struct MASSENTITY_API FMassArchetypeChunkIterator
{
private:
	const FArchetypeChunkCollection& ChunkData;
	int32 CurrentChunkIndex = 0;

public:
	explicit FMassArchetypeChunkIterator(const FArchetypeChunkCollection& InChunkData) : ChunkData(InChunkData), CurrentChunkIndex(0) {}

	operator bool() const { return ChunkData.GetChunks().IsValidIndex(CurrentChunkIndex) && ChunkData.GetChunks()[CurrentChunkIndex].IsSet(); }
	FMassArchetypeChunkIterator& operator++() { ++CurrentChunkIndex; return *this; }

	const FArchetypeChunkCollection::FChunkInfo* operator->() const { check(bool(*this)); return &ChunkData.GetChunks()[CurrentChunkIndex]; }
	const FArchetypeChunkCollection::FChunkInfo& operator*() const { check(bool(*this)); return ChunkData.GetChunks()[CurrentChunkIndex]; }
};

//////////////////////////////////////////////////////////////////////
//
struct FInternalEntityHandle 
{
	FInternalEntityHandle() = default;
	FInternalEntityHandle(uint8* InChunkRawMemory, const int32 InIndexWithinChunk)
        : ChunkRawMemory(InChunkRawMemory), IndexWithinChunk(InIndexWithinChunk)
	{}
	bool IsValid() const { return ChunkRawMemory != nullptr && IndexWithinChunk != INDEX_NONE; }
	bool operator==(const FInternalEntityHandle & Other) const { return ChunkRawMemory == Other.ChunkRawMemory && IndexWithinChunk == Other.IndexWithinChunk; }

	uint8* ChunkRawMemory = nullptr;
	int32 IndexWithinChunk = INDEX_NONE;
};

typedef TArray<int32, TInlineAllocator<16>> FMassFragmentIndicesMapping;
typedef TConstArrayView<int32> FMassFragmentIndicesMappingView;
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
