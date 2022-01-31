// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassEntitySubsystem.h"
#include "MassEntityTestTypes.h"

#define LOCTEXT_NAMESPACE "MassTest"

PRAGMA_DISABLE_OPTIMIZATION

//----------------------------------------------------------------------//
// tests 
//----------------------------------------------------------------------//
namespace FMassChunkCollectionTest
{

void Shuffle(FRandomStream& Rand, TArray<FMassEntityHandle>& Entities)
{
	for (int i = 0; i < Entities.Num(); ++i)
	{
		const int32 NewIndex = Rand.RandRange(0, Entities.Num() - 1);
		Entities.Swap(i, NewIndex);
	}
}

struct FChunkCollectionTestBase : FEntityTestBase
{
	TArray<FMassEntityHandle> Entities;

	virtual bool SetUp() override
	{
		FEntityTestBase::SetUp();

		const int32 Count = 100;
		EntitySubsystem->BatchCreateEntities(FloatsArchetype, Count, Entities);
		return true;
	}

	virtual void TearDown() override
	{
		Entities.Reset();
		FEntityTestBase::TearDown();
	}
};

struct FChunkCollection_CreateBasic : FChunkCollectionTestBase
{
	virtual bool InstantTest() override
	{
		TArray<FMassEntityHandle> EntitiesSubSet;
		
		// Should end up as last chunk
		EntitiesSubSet.Add(Entities[99]);
		EntitiesSubSet.Add(Entities[97]);
		EntitiesSubSet.Add(Entities[98]);

		// Should end up as third chunk
		EntitiesSubSet.Add(Entities[20]);
		EntitiesSubSet.Add(Entities[22]);
		EntitiesSubSet.Add(Entities[21]);

		// Should end up as second chunk
		EntitiesSubSet.Add(Entities[18]);

		// Should end up as first chunk
		EntitiesSubSet.Add(Entities[10]);
		EntitiesSubSet.Add(Entities[13]);
		EntitiesSubSet.Add(Entities[11]);
		EntitiesSubSet.Add(Entities[12]);

		FMassArchetypeSubChunks ChunkCollection(FloatsArchetype, EntitiesSubSet, FMassArchetypeSubChunks::NoDuplicates);
		FMassArchetypeSubChunks::FConstSubChunkArrayView Chunks = ChunkCollection.GetChunks();
		AITEST_EQUAL("The predicted sub-chunk count should match", Chunks.Num(), 4);
		AITEST_EQUAL("The [10-13] chunk should be first and start at 10", Chunks[0].SubchunkStart, 10);
		AITEST_EQUAL("The [10-13] chunk should be first and have a length of 4", Chunks[0].Length, 4);
		AITEST_EQUAL("The [18] chunk should be second and start at 18", Chunks[1].SubchunkStart, 18); 
		AITEST_EQUAL("The [18] chunk should be second and have a length of 1", Chunks[1].Length, 1);
		AITEST_EQUAL("The [20-22] chunk should be third and start at 20", Chunks[2].SubchunkStart, 20);
		AITEST_EQUAL("The [20-22] chunk should be third and have a length of 3", Chunks[2].Length, 3);
		AITEST_EQUAL("The [97-99] chunk should be third and start at 97", Chunks[3].SubchunkStart, 97);
		AITEST_EQUAL("The [97-99] chunk should be third and have a length of 3", Chunks[3].Length, 3);

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FChunkCollection_CreateBasic, "System.Mass.ChunkCollection.Create.Basic");

struct FChunkCollection_CreateOrderInvariant : FChunkCollectionTestBase
{
	virtual bool InstantTest() override
	{
		TArray<FMassEntityHandle> EntitiesSubSet(&Entities[10], 30);
		EntitiesSubSet.RemoveAt(10, 1, false);

		FMassArchetypeSubChunks CollectionFromOrdered(FloatsArchetype, EntitiesSubSet, FMassArchetypeSubChunks::NoDuplicates);

		FRandomStream Rand(0);
		Shuffle(Rand, EntitiesSubSet);

		FMassArchetypeSubChunks CollectionFromRandom(FloatsArchetype, EntitiesSubSet, FMassArchetypeSubChunks::NoDuplicates);

		AITEST_TRUE("The resulting chunk collection should be the same regardless of the order of input entities", CollectionFromOrdered.IsSame(CollectionFromRandom));
		
		// just to roughly make sure the result is what we expect
		FMassArchetypeSubChunks::FConstSubChunkArrayView Chunks = CollectionFromOrdered.GetChunks();
		AITEST_EQUAL("The result should contain two chunks", Chunks.Num(), 2);

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FChunkCollection_CreateOrderInvariant, "System.Mass.ChunkCollection.Create.OrderInvariant");

struct FChunkCollection_CreateCrossChunk : FEntityTestBase
{
	virtual bool InstantTest() override
	{
#if WITH_MASSENTITY_DEBUG
		TArray<FMassEntityHandle> Entities;
		const int32 EntitiesPerChunk = EntitySubsystem->DebugGetArchetypeEntitiesCountPerChunk(FloatsArchetype);

		const int32 SpillOver = 10;
		const int32 Count = EntitiesPerChunk + SpillOver;
		EntitySubsystem->BatchCreateEntities(FloatsArchetype, Count, Entities);
		
		TArray<FMassEntityHandle> EntitiesSubCollection;
		EntitiesSubCollection.Add(Entities[EntitiesPerChunk]);
		for (int i = 1; i < SpillOver; ++i)
		{
			EntitiesSubCollection.Add(Entities[EntitiesPerChunk + i]);
			EntitiesSubCollection.Add(Entities[EntitiesPerChunk - i]);
		}
		
		FMassArchetypeSubChunks ChunkCollection(FloatsArchetype, EntitiesSubCollection, FMassArchetypeSubChunks::NoDuplicates);
		FMassArchetypeSubChunks::FConstSubChunkArrayView Chunks = ChunkCollection.GetChunks();
		AITEST_EQUAL("The given continuous range should get split in two", Chunks.Num(), 2);
		AITEST_EQUAL("The part in first archetype\'s chunk should contain 9 elements", Chunks[0].Length, 9);
		AITEST_EQUAL("The part in second archetype\'s chunk should contain 10 elements", Chunks[1].Length, 10);
#endif // WITH_MASSENTITY_DEBUG
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FChunkCollection_CreateCrossChunk, "System.Mass.ChunkCollection.Create.CrossChunk");

struct FChunkCollection_CreateWithDuplicates : FChunkCollectionTestBase
{
	virtual bool InstantTest() override
	{
		TArray<FMassEntityHandle> EntitiesWithDuplicates;
		EntitiesWithDuplicates.Add(Entities[0]);
		EntitiesWithDuplicates.Add(Entities[0]);
		EntitiesWithDuplicates.Add(Entities[0]);
		EntitiesWithDuplicates.Add(Entities[1]);
		EntitiesWithDuplicates.Add(Entities[2]);
		EntitiesWithDuplicates.Add(Entities[2]);

		FMassArchetypeSubChunks ChunkCollection(FloatsArchetype, EntitiesWithDuplicates, FMassArchetypeSubChunks::FoldDuplicates);
		FMassArchetypeSubChunks::FConstSubChunkArrayView Chunks = ChunkCollection.GetChunks();
		AITEST_EQUAL("The result should have a single subchunk", Chunks.Num(), 1);
		AITEST_EQUAL("The resulting subchunk should be of length 3", Chunks[0].Length, 3);

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FChunkCollection_CreateWithDuplicates, "System.Mass.ChunkCollection.Create.Duplicates");

} // FMassChunkCollectionTest

PRAGMA_ENABLE_OPTIMIZATION

#undef LOCTEXT_NAMESPACE
