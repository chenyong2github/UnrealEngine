// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArchetypeTypes.h"
#include "MassEntitySubsystem.h"
#include "ArchetypeData.h"
#include "LWCCommandBuffer.h"


//////////////////////////////////////////////////////////////////////
// FArchetypeHandle
bool FArchetypeHandle::operator==(const FMassArchetypeData* Other) const
{
	return DataPtr.Get() == Other;
}

uint32 GetTypeHash(const FArchetypeHandle& Instance)
{
	return GetTypeHash(Instance.DataPtr.Get());
}

//////////////////////////////////////////////////////////////////////
// FArchetypeChunkCollection

FArchetypeChunkCollection::FArchetypeChunkCollection(const FArchetypeHandle& InArchetype, TConstArrayView<FMassEntityHandle> InEntities)
	: Archetype(InArchetype)
{
	check(InArchetype.IsValid());
	const FMassArchetypeData& ArchetypeData = *InArchetype.DataPtr.Get();
	const int32 NumEntitiesPerChunk = ArchetypeData.GetNumEntitiesPerChunk();

	// InEntities has a real chance of not being sorted by AbsoluteIndex. We gotta fix that to optimize how we process the data 
	TArray<int32> TrueIndices;
	TrueIndices.AddUninitialized(InEntities.Num());
	int32 i = 0;
	for (const FMassEntityHandle& Entity : InEntities)
	{
		TrueIndices[i] = ArchetypeData.GetInternalIndexForEntity(Entity.Index);
		++i;
	}
	TrueIndices.Sort();

	// the following block of code is splitting up sorted AbsoluteIndices into 
	// continuous chunks
	int32 ChunkEnd = INDEX_NONE;
	FChunkInfo DummyChunk;
	FChunkInfo* SubChunkPtr = &DummyChunk;
	int32 SubchunkLen = 0;
	int32 PrevAbsoluteIndex = INDEX_NONE;
	for (const int32 Index : TrueIndices)
	{
		// if run across a chunk border or run into an index discontinuity 
		if (Index >= ChunkEnd || Index != (PrevAbsoluteIndex + 1))
		{
			SubChunkPtr->Length = SubchunkLen;
			// note that both ChunkIndex and ChunkEnd will change only if AbsoluteIndex >= ChunkEnd
			const int32 ChunkIndex = Index / NumEntitiesPerChunk;
			ChunkEnd = (ChunkIndex + 1) * NumEntitiesPerChunk;
			SubchunkLen = 0;
			// new subchunk
			const int32 SubchunkStart = Index % NumEntitiesPerChunk;
			SubChunkPtr = &Chunks.Add_GetRef(FChunkInfo(ChunkIndex, SubchunkStart));
		}
		++SubchunkLen;
		PrevAbsoluteIndex = Index;
	}

	SubChunkPtr->Length = SubchunkLen;
}

FArchetypeChunkCollection::FArchetypeChunkCollection(FArchetypeHandle& InArchetypeHandle)
{
	check(InArchetypeHandle.DataPtr.IsValid());
	GatherChunksFromArchetype(InArchetypeHandle.DataPtr);
}

FArchetypeChunkCollection::FArchetypeChunkCollection(TSharedPtr<FMassArchetypeData>& InArchetype)
{
	GatherChunksFromArchetype(InArchetype);
}

void FArchetypeChunkCollection::GatherChunksFromArchetype(TSharedPtr<FMassArchetypeData>& InArchetype)
{
	check(InArchetype.IsValid());
	Archetype.DataPtr = InArchetype;

	const int32 ChunkCount = InArchetype->GetChunkCount();
	Chunks.Reset(ChunkCount);
	for (int32 i = 0; i < ChunkCount; ++i)
	{
		Chunks.Add(FChunkInfo(i));
	}
}
