// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassArchetypeData.h"
#include "MassEntityTypes.h"
#include "Misc/StringBuilder.h"


//////////////////////////////////////////////////////////////////////
// FMassArchetypeData
namespace UE::Mass::Core 
{
	constexpr static bool bBitwiseRelocateFragments = true;
}// UE::Mass::Core

void FMassArchetypeData::ForEachFragmentType(TFunction< void(const UScriptStruct* /*Fragment*/)> Function) const
{
	for (const FMassArchetypeFragmentConfig& FragmentData : FragmentConfigs)
	{
		Function(FragmentData.FragmentType);
	}
}

bool FMassArchetypeData::HasFragmentType(const UScriptStruct* FragmentType) const
{
	return (FragmentType && CompositionDescriptor.Fragments.Contains(*FragmentType));
}

void FMassArchetypeData::Initialize(const FMassArchetypeCompositionDescriptor& InCompositionDescriptor, const FMassArchetypeSharedFragmentValues& InSharedFragmentValues)
{
	TArray<const UScriptStruct*, TInlineAllocator<16>> SortedFragmentList;
	InCompositionDescriptor.Fragments.ExportTypes(SortedFragmentList);

	SortedFragmentList.Sort(FScriptStructSortOperator());

	// Figure out how many bytes all of the individual fragments (and metadata) will cost per entity
	int32 FragmentSizeTallyBytes = 0;

	// Alignment padding computation is currently very conservative and over-estimated.
	int32 AlignmentPadding = 0;
	{
		// Save room for the 'metadata' (entity array)
		FragmentSizeTallyBytes += sizeof(FMassEntityHandle);

		// Tally up the fragment sizes and place them in the index map
		FragmentConfigs.AddDefaulted(SortedFragmentList.Num());
		FragmentIndexMap.Reserve(SortedFragmentList.Num());

		for (int32 FragmentIndex = 0; FragmentIndex < SortedFragmentList.Num(); ++FragmentIndex)
		{
			const UScriptStruct* FragmentType = SortedFragmentList[FragmentIndex];
			checkSlow(FragmentType);
			FragmentConfigs[FragmentIndex].FragmentType = FragmentType;
			
			AlignmentPadding += FragmentType->GetMinAlignment();
			FragmentSizeTallyBytes += FragmentType->GetStructureSize();

			FragmentIndexMap.Add(FragmentType, FragmentIndex);
			CompositionDescriptor.Fragments.Add(*FragmentType);
		}
	}

	// Tags
	CompositionDescriptor.Tags = InCompositionDescriptor.Tags;

	// Chunk fragments
	CompositionDescriptor.ChunkFragments = InCompositionDescriptor.ChunkFragments;
	TArray<const UScriptStruct*, TInlineAllocator<16>> ChunkFragmentList;
	CompositionDescriptor.ChunkFragments.ExportTypes(ChunkFragmentList);
	ChunkFragmentList.Sort(FScriptStructSortOperator());
	for (const UScriptStruct* ChunkFragmentType : ChunkFragmentList)
	{
		check(ChunkFragmentType);
		ChunkFragmentsTemplate.Emplace(ChunkFragmentType);
	}

	// Share fragments
	CompositionDescriptor.SharedFragments = InCompositionDescriptor.SharedFragments;
	SharedFragmentValues = InSharedFragmentValues;

	TotalBytesPerEntity = FragmentSizeTallyBytes;
	int32 ChunkAvailableSize = GetChunkAllocSize() - AlignmentPadding;
	check(TotalBytesPerEntity <= ChunkAvailableSize);

	NumEntitiesPerChunk = ChunkAvailableSize / TotalBytesPerEntity;

	// Set up the offsets for each fragment into the chunk data
	EntityListOffsetWithinChunk = 0;
	int32 CurrentOffset = NumEntitiesPerChunk * sizeof(FMassEntityHandle);
	for (FMassArchetypeFragmentConfig& FragmentData : FragmentConfigs)
	{
		CurrentOffset = Align(CurrentOffset, FragmentData.FragmentType->GetMinAlignment());
		FragmentData.ArrayOffsetWithinChunk = CurrentOffset;
		const int32 SizeOfThisFragmentArray = NumEntitiesPerChunk * FragmentData.FragmentType->GetStructureSize();
		CurrentOffset += SizeOfThisFragmentArray;
	}
}

void FMassArchetypeData::InitializeWithSibling(const FMassArchetypeData& SiblingArchetype, const FMassTagBitSet& OverrideTags)
{
	checkf(IsInitialized() == false, TEXT("Trying to %s but this archetype has already been initialized"));

	FragmentConfigs = SiblingArchetype.FragmentConfigs;
	FragmentIndexMap = SiblingArchetype.FragmentIndexMap;
	CompositionDescriptor.Fragments = SiblingArchetype.CompositionDescriptor.Fragments;
	CompositionDescriptor.Tags = OverrideTags;
	CompositionDescriptor.ChunkFragments = SiblingArchetype.CompositionDescriptor.ChunkFragments;
	CompositionDescriptor.SharedFragments = SiblingArchetype.CompositionDescriptor.SharedFragments;
	ChunkFragmentsTemplate = SiblingArchetype.ChunkFragmentsTemplate;
	SharedFragmentValues = SiblingArchetype.GetSharedFragmentValues();

	TotalBytesPerEntity = SiblingArchetype.TotalBytesPerEntity;
	NumEntitiesPerChunk = SiblingArchetype.NumEntitiesPerChunk;

	// Set up the offsets for each fragment into the chunk data
	EntityListOffsetWithinChunk = 0;
}

void FMassArchetypeData::AddEntity(FMassEntityHandle Entity)
{
	AddEntityInternal(Entity, true/*bInitializeFragments*/);
}

int32 FMassArchetypeData::AddEntityInternal(FMassEntityHandle Entity, const bool bInitializeFragments)
{
	int32 IndexWithinChunk = 0;
	int32 AbsoluteIndex = 0;
	int32 ChunkIndex = 0;
	int32 EmptyChunkIndex = INDEX_NONE;
	int32 EmptyAbsoluteIndex = INDEX_NONE;

	FMassArchetypeChunk* DestinationChunk = nullptr;
	// Check chunks for a free spot (trying to reuse the earlier ones first so later ones might get freed up) 
	//@TODO: This could be accelerated to include a cached index to the first chunk with free spots or similar
	for (FMassArchetypeChunk& Chunk : Chunks)
	{
		if (Chunk.GetNumInstances() == 0)
		{
			// Remember first empty chunk but continue looking for a chunk that has space and same group tag
			if (EmptyChunkIndex == INDEX_NONE)
			{
				EmptyChunkIndex = ChunkIndex;
				EmptyAbsoluteIndex = AbsoluteIndex;
			}
		}
		else if (Chunk.GetNumInstances() < NumEntitiesPerChunk)
		{
			IndexWithinChunk = Chunk.GetNumInstances();
			AbsoluteIndex += IndexWithinChunk;

			Chunk.AddInstance();

			DestinationChunk = &Chunk;
			break;
		}
		AbsoluteIndex += NumEntitiesPerChunk;
		++ChunkIndex;
	}

	if (DestinationChunk == nullptr)
	{
		// Check if it is a recycled chunk
		if (EmptyChunkIndex != INDEX_NONE)
		{
			DestinationChunk = &Chunks[EmptyChunkIndex];
			DestinationChunk->Recycle(ChunkFragmentsTemplate);
			AbsoluteIndex = EmptyAbsoluteIndex;
		}
		else
		{
			DestinationChunk = &Chunks.Emplace_GetRef(GetChunkAllocSize(), ChunkFragmentsTemplate);
		}

		check(DestinationChunk);
		DestinationChunk->AddInstance();
	}

	// Initialize the fragment memory
	if (bInitializeFragments)
	{
		for (const FMassArchetypeFragmentConfig& FragmentConfig : FragmentConfigs)
		{
			void* FragmentPtr = FragmentConfig.GetFragmentData(DestinationChunk->GetRawMemory(), IndexWithinChunk);
			FragmentConfig.FragmentType->InitializeStruct(FragmentPtr);
		}
	}

	// Add to the table and map
	EntityMap.Add(Entity.Index, AbsoluteIndex);
	DestinationChunk->GetEntityArrayElementRef(EntityListOffsetWithinChunk, IndexWithinChunk) = Entity;

	return AbsoluteIndex;
}

void FMassArchetypeData::RemoveEntity(FMassEntityHandle Entity)
{
	const int32 AbsoluteIndex = EntityMap.FindAndRemoveChecked(Entity.Index);
	RemoveEntityInternal(AbsoluteIndex, true/*bDestroyFragments*/);
}

void FMassArchetypeData::RemoveEntityInternal(const int32 AbsoluteIndex, const bool bDestroyFragments)
{
	const int32 ChunkIndex = AbsoluteIndex / NumEntitiesPerChunk;
	const int32 IndexWithinChunk = AbsoluteIndex % NumEntitiesPerChunk;

	FMassArchetypeChunk& Chunk = Chunks[ChunkIndex];

	const int32 IndexToSwapFrom = Chunk.GetNumInstances() - 1;

	checkf(bDestroyFragments || UE::Mass::Core::bBitwiseRelocateFragments, TEXT("We allow not to destroy fragments only in bit wise relocation mode."));

	// Remove and swap the last entry in the chunk to the location of the removed item (if it's not the same as the dying entry)
	if (IndexToSwapFrom != IndexWithinChunk)
	{
		for (const FMassArchetypeFragmentConfig& FragmentConfig : FragmentConfigs)
		{
			void* DyingFragmentPtr = FragmentConfig.GetFragmentData(Chunk.GetRawMemory(), IndexWithinChunk);
			void* MovingFragmentPtr = FragmentConfig.GetFragmentData(Chunk.GetRawMemory(), IndexToSwapFrom);

			if (UE::Mass::Core::bBitwiseRelocateFragments)
			{
				// Destroy fragment data
				if (bDestroyFragments)
				{
					FragmentConfig.FragmentType->DestroyStruct(DyingFragmentPtr);
				}

				// Move last entry
				FMemory::Memcpy(DyingFragmentPtr, MovingFragmentPtr, FragmentConfig.FragmentType->GetStructureSize());
			}
			else
			{
				// Destroy & initialize the fragment data
				FragmentConfig.FragmentType->ClearScriptStruct(DyingFragmentPtr);

				// Copy last entry
				FragmentConfig.FragmentType->CopyScriptStruct(DyingFragmentPtr, MovingFragmentPtr);

				// Destroy last entry
				FragmentConfig.FragmentType->DestroyStruct(MovingFragmentPtr);
			}
		}

		// Update the entity table and map
		const FMassEntityHandle EntityBeingSwapped = Chunk.GetEntityArrayElementRef(EntityListOffsetWithinChunk, IndexToSwapFrom);
		Chunk.GetEntityArrayElementRef(EntityListOffsetWithinChunk, IndexWithinChunk) = EntityBeingSwapped;
		EntityMap.FindChecked(EntityBeingSwapped.Index) = AbsoluteIndex;
	}
	else if (!UE::Mass::Core::bBitwiseRelocateFragments || bDestroyFragments)
	{
		for (const FMassArchetypeFragmentConfig& FragmentConfig : FragmentConfigs)
		{
			// Destroy the fragment data
			void* DyingFragmentPtr = FragmentConfig.GetFragmentData(Chunk.GetRawMemory(), IndexWithinChunk);
			FragmentConfig.FragmentType->DestroyStruct(DyingFragmentPtr);
		}
	}
	
	Chunk.RemoveInstance();

	// If the chunk itself is empty now, see if we can remove it entirely
	// Note: This is only possible for trailing chunks, to avoid messing up the absolute indices in the entities map
	while ((Chunks.Num() > 0) && (Chunks.Last().GetNumInstances() == 0))
	{
		Chunks.RemoveAt(Chunks.Num() - 1, 1, /*bAllowShrinking=*/ false);
	}
}

void FMassArchetypeData::BatchDestroyEntityChunks(FMassArchetypeSubChunks::FConstSubChunkArrayView SubChunkContainer, TArray<FMassEntityHandle>& OutEntitiesRemoved)
{
	const int32 InitialOutEntitiesCount = OutEntitiesRemoved.Num();

	// Sorting the subchunks info so that subchunks of a given chunk are processed "from the back". Otherwise removing 
	// a subchunk from the front of the chunk would inevitably invalidate following subchunks' information.
	FMassArchetypeSubChunks::FSubChunkArray Subchunks(SubChunkContainer);
	Subchunks.Sort([](const FMassArchetypeSubChunks::FSubChunkInfo& A, const FMassArchetypeSubChunks::FSubChunkInfo& B) 
		{ 
			return A.ChunkIndex < B.ChunkIndex || (A.ChunkIndex == B.ChunkIndex && A.SubchunkStart > B.SubchunkStart);
		});

	for (const FMassArchetypeSubChunks::FSubChunkInfo SubchunkInfo : Subchunks)
	{ 
		FMassArchetypeChunk& Chunk = Chunks[SubchunkInfo.ChunkIndex];

		// gather entities we're about to remove
		FMassEntityHandle* DyingEntityPtr = &Chunk.GetEntityArrayElementRef(EntityListOffsetWithinChunk, SubchunkInfo.SubchunkStart);
		OutEntitiesRemoved.Append(DyingEntityPtr, SubchunkInfo.Length);

		const int32 NumberToMove = FMath::Min(Chunk.GetNumInstances() - (SubchunkInfo.SubchunkStart + SubchunkInfo.Length), SubchunkInfo.Length);
		checkf(NumberToMove >= 0, TEXT("Trying to move a negative number of elements indicates a problem with SubchunkInfo, it's possibly out of date."));
		const int32 NumberToCut = FMath::Max(SubchunkInfo.Length - NumberToMove, 0);
		
		if (NumberToMove > 0)
		{
			const int32 SwapStartIndex = Chunk.GetNumInstances() - NumberToMove;
			checkf((SubchunkInfo.SubchunkStart + NumberToMove - 1) < SwapStartIndex, TEXT("Remove and Move ranges overlap"));

			for (const FMassArchetypeFragmentConfig& FragmentConfig : FragmentConfigs)
			{
				void* DyingFragmentPtr = FragmentConfig.GetFragmentData(Chunk.GetRawMemory(), SubchunkInfo.SubchunkStart);
				void* MovingFragmentPtr = FragmentConfig.GetFragmentData(Chunk.GetRawMemory(), SwapStartIndex);

				if (UE::Mass::Core::bBitwiseRelocateFragments)
				{
					// Destroy the fragments we'll replace by the following copy
					FragmentConfig.FragmentType->DestroyStruct(DyingFragmentPtr, NumberToMove);

					// Swap fragments to the empty space just created.
					FMemory::Memcpy(DyingFragmentPtr, MovingFragmentPtr, FragmentConfig.FragmentType->GetStructureSize() * NumberToMove);
				}
				else
				{
					// Clear fragments that we will copy over. Clear destroys and initializes the fragments, which is needed for CopyScriptStruct().
					FragmentConfig.FragmentType->ClearScriptStruct(DyingFragmentPtr, NumberToMove);

					// Swap fragments into the empty space just created.
					FragmentConfig.FragmentType->CopyScriptStruct(DyingFragmentPtr, MovingFragmentPtr, NumberToMove);

					// Destroy the fragments that were moved.
					FragmentConfig.FragmentType->DestroyStruct(MovingFragmentPtr, NumberToMove);
				}
			}

			// Update the entity table and map
			const FMassEntityHandle* MovingEntityPtr = &Chunk.GetEntityArrayElementRef(EntityListOffsetWithinChunk, SwapStartIndex);
			int32 AbsoluteIndex = SubchunkInfo.ChunkIndex * NumEntitiesPerChunk + SubchunkInfo.SubchunkStart;

			for (int i = 0; i < NumberToMove; ++i)
			{
				DyingEntityPtr[i] = MovingEntityPtr[i];
				EntityMap.FindChecked(MovingEntityPtr[i].Index) = AbsoluteIndex++;
			}
		}

		if (NumberToCut > 0)
		{
			// just clean up the rest. Note that we explicitly do not clean the spots vacated by entities moved from 
			// the back of the chunk - if we did the risk calling DestroyStruct on them multiple times
			const int32 CutStartIndex = SubchunkInfo.SubchunkStart + NumberToMove;
			for (const FMassArchetypeFragmentConfig& FragmentConfig : FragmentConfigs)
			{
				// Destroy the fragment data
				void* DyingFragmentPtr = FragmentConfig.GetFragmentData(Chunk.GetRawMemory(), CutStartIndex);
				FragmentConfig.FragmentType->DestroyStruct(DyingFragmentPtr, NumberToCut);
			}
		}

		Chunk.RemoveMultipleInstances(SubchunkInfo.Length);
	}

	for (int i = InitialOutEntitiesCount; i < OutEntitiesRemoved.Num(); ++i)
	{
		EntityMap.FindAndRemoveChecked(OutEntitiesRemoved[i].Index);
	}

	// If the chunk itself is empty now, see if we can remove it entirely
	// Note: This is only possible for trailing chunks, to avoid messing up the absolute indices in the entities map
	while ((Chunks.Num() > 0) && (Chunks.Last().GetNumInstances() == 0))
	{
		Chunks.RemoveAt(Chunks.Num() - 1, 1, /*bAllowShrinking=*/ false);
	}
}

bool FMassArchetypeData::HasFragmentDataForEntity(const UScriptStruct* FragmentType, int32 EntityIndex) const
{
	return (FragmentType && CompositionDescriptor.Fragments.Contains(*FragmentType));
}

void* FMassArchetypeData::GetFragmentDataForEntityChecked(const UScriptStruct* FragmentType, int32 EntityIndex) const
{
	const FMassRawEntityInChunkData InternalIndex = MakeEntityHandle(EntityIndex);
	
	// failing the below Find means given entity's archetype is missing given FragmentType
	const int32 FragmentIndex = FragmentIndexMap.FindChecked(FragmentType);
	return GetFragmentData(FragmentIndex, InternalIndex);
}

void* FMassArchetypeData::GetFragmentDataForEntity(const UScriptStruct* FragmentType, int32 EntityIndex) const
{
	if (const int32* FragmentIndex = FragmentIndexMap.Find(FragmentType))
	{
		FMassRawEntityInChunkData InternalIndex = MakeEntityHandle(EntityIndex);
		// failing the below Find means given entity's archetype is missing given FragmentType
		return GetFragmentData(*FragmentIndex, InternalIndex);
	}
	return nullptr;
}

void FMassArchetypeData::SetFragmentsData(const FMassEntityHandle Entity, TArrayView<const FInstancedStruct> FragmentInstances)
{
	FMassRawEntityInChunkData InternalIndex = MakeEntityHandle(Entity);

	for (const FInstancedStruct& Instance : FragmentInstances)
	{
		const UScriptStruct* FragmentType = Instance.GetScriptStruct();
		check(FragmentType);
		const int32 FragmentIndex = FragmentIndexMap.FindChecked(FragmentType);
		void* FragmentMemory = GetFragmentData(FragmentIndex, InternalIndex);
		// No UE::Mass::Core::bBitwiseRelocateFragments, this isn't a move fragment
		FragmentType->CopyScriptStruct(FragmentMemory, Instance.GetMemory());
	}
}

void FMassArchetypeData::SetFragmentData(FMassArchetypeSubChunks::FConstSubChunkArrayView SubChunkContainer, const FInstancedStruct& FragmentSource)
{
	check(FragmentSource.IsValid());
	const UScriptStruct* FragmentType = FragmentSource.GetScriptStruct();
	check(FragmentType);
	const int32 FragmentIndex = FragmentIndexMap.FindChecked(FragmentType);
	const int32 FragmentTypeSize = FragmentType->GetStructureSize();
	const uint8* FragmentSourceMemory = FragmentSource.GetMemory();
	check(FragmentSourceMemory);
	
	for (FMassArchetypeChunkIterator ChunkIterator(SubChunkContainer); ChunkIterator; ++ChunkIterator)
	{
		uint8* FragmentMemory = (uint8*)FragmentConfigs[FragmentIndex].GetFragmentData(Chunks[ChunkIterator->ChunkIndex].GetRawMemory(), ChunkIterator->SubchunkStart);
		for (int i = ChunkIterator->Length; i; --i, FragmentMemory += FragmentTypeSize)
		{
			// No UE::Mass::Core::bBitwiseRelocateFragments, this isn't a move of a fragment
			FragmentType->CopyScriptStruct(FragmentMemory, FragmentSourceMemory);
		}
	}
}

void FMassArchetypeData::MoveEntityToAnotherArchetype(const FMassEntityHandle Entity, FMassArchetypeData& NewArchetype)
{
	check(&NewArchetype != this);

	const int32 AbsoluteIndex = EntityMap.FindAndRemoveChecked(Entity.Index);
	const int32 ChunkIndex = AbsoluteIndex / NumEntitiesPerChunk;
	const int32 IndexWithinChunk = AbsoluteIndex % NumEntitiesPerChunk;
	FMassArchetypeChunk& Chunk = Chunks[ChunkIndex];

	constexpr bool bInitializeFragmentsDuringCreation = !UE::Mass::Core::bBitwiseRelocateFragments;
	const int32 NewAbsoluteIndex = NewArchetype.AddEntityInternal(Entity, bInitializeFragmentsDuringCreation);
	const int32 NewChunkIndex = NewAbsoluteIndex / NewArchetype.NumEntitiesPerChunk;
	const int32 NewIndexWithinChunk = NewAbsoluteIndex % NewArchetype.NumEntitiesPerChunk;
	FMassArchetypeChunk& NewChunk = NewArchetype.Chunks[NewChunkIndex];

	// for every NewArchetype's fragment see if it was in the old fragment as well and if so copy it's value. 
	// If not then initialize the fragment (unless it has already been initialized based on bInitializeFragmentsDuringCreation
	// value
	for (const FMassArchetypeFragmentConfig& NewFragmentConfig : NewArchetype.FragmentConfigs)
	{
		const int32* OldFragmentIndex = FragmentIndexMap.Find(NewFragmentConfig.FragmentType);
		void* Dst = NewFragmentConfig.GetFragmentData(NewChunk.GetRawMemory(), NewIndexWithinChunk);

		// Only copy if the fragment type exists in both archetypes
		if (OldFragmentIndex)
		{
			const void* Src = FragmentConfigs[*OldFragmentIndex].GetFragmentData(Chunk.GetRawMemory(), IndexWithinChunk);
			if (UE::Mass::Core::bBitwiseRelocateFragments)
			{
				FMemory::Memcpy(Dst, Src, NewFragmentConfig.FragmentType->GetStructureSize());
			}
			else
			{
				NewFragmentConfig.FragmentType->CopyScriptStruct(Dst, Src);
			}
		}
		else if (bInitializeFragmentsDuringCreation == false)
		{
			// the fragment's unique to the NewArchetype need to be initialized
			// @todo we're doing it for tags here as well. A tiny bit of perf lost. Probably not worth adding a check
			// but something to keep in mind. Will go away once tags are more of an archetype fragment than entity's
			NewFragmentConfig.FragmentType->InitializeStruct(Dst);
		}
	}

	constexpr bool bDestroyFragments = !UE::Mass::Core::bBitwiseRelocateFragments;
	RemoveEntityInternal(AbsoluteIndex, bDestroyFragments);
}

void FMassArchetypeData::ExecuteFunction(FMassExecutionContext& RunContext, const FMassExecuteFunction& Function, const FMassQueryRequirementIndicesMapping& RequirementMapping, FMassArchetypeSubChunks::FConstSubChunkArrayView SubChunkContainer)
{
	// mz@todo to be removed
	RunContext.SetCurrentArchetypesTagBitSet(GetTagBitSet());

	BindConstSharedFragmentRequirements(RunContext, RequirementMapping.ChunkFragments);
	BindSharedFragmentRequirements(RunContext, RequirementMapping.ChunkFragments);

	for (FMassArchetypeChunkIterator ChunkIterator(SubChunkContainer); ChunkIterator; ++ChunkIterator)
	{
		FMassArchetypeChunk& Chunk = Chunks[ChunkIterator->ChunkIndex];
		
		const int32 ChunkLength = ChunkIterator->Length > 0 ? ChunkIterator->Length : (Chunk.GetNumInstances() - ChunkIterator->SubchunkStart);
		if (ChunkLength)
		{
			checkf((ChunkIterator->SubchunkStart + ChunkLength) <= Chunk.GetNumInstances() && ChunkLength > 0, TEXT("Invalid subchunk, it is going over the number of instances in the chunk or it is empty."));

			RunContext.SetCurrentChunkSerialModificationNumber(Chunk.GetSerialModificationNumber());
			BindChunkFragmentRequirements(RunContext, RequirementMapping.ChunkFragments, Chunk);
			BindEntityRequirements(RunContext, RequirementMapping.EntityFragments, Chunk, ChunkIterator->SubchunkStart, ChunkLength);

			Function(RunContext);
		}
	}
}

void FMassArchetypeData::ExecuteFunction(FMassExecutionContext& RunContext, const FMassExecuteFunction& Function, const FMassQueryRequirementIndicesMapping& RequirementMapping, const FMassArchetypeConditionFunction& ArchetypeCondition, const FMassChunkConditionFunction& ChunkCondition)
{
	// mz@todo to be removed
	RunContext.SetCurrentArchetypesTagBitSet(GetTagBitSet());

	BindConstSharedFragmentRequirements(RunContext, RequirementMapping.ConstSharedFragments);
	BindSharedFragmentRequirements(RunContext, RequirementMapping.SharedFragments);

	if(!ArchetypeCondition || ArchetypeCondition(RunContext))
	{
		for (FMassArchetypeChunk& Chunk : Chunks)
		{
			if (Chunk.GetNumInstances())
			{
				RunContext.SetCurrentChunkSerialModificationNumber(Chunk.GetSerialModificationNumber());
				BindChunkFragmentRequirements(RunContext, RequirementMapping.ChunkFragments, Chunk);

				if (!ChunkCondition || ChunkCondition(RunContext))
				{
					BindEntityRequirements(RunContext, RequirementMapping.EntityFragments, Chunk, 0, Chunk.GetNumInstances());
					Function(RunContext);
				}
			}
		}
	}
}

void FMassArchetypeData::ExecutionFunctionForChunk(FMassExecutionContext RunContext, const FMassExecuteFunction& Function, const FMassQueryRequirementIndicesMapping& RequirementMapping, const FMassArchetypeSubChunks::FSubChunkInfo& ChunkInfo, const FMassChunkConditionFunction& ChunkCondition)
{
	FMassArchetypeChunk& Chunk = Chunks[ChunkInfo.ChunkIndex];
	const int32 ChunkLength = ChunkInfo.Length > 0 ? ChunkInfo.Length : (Chunk.GetNumInstances() - ChunkInfo.SubchunkStart);

	if (ChunkLength)
	{
		BindConstSharedFragmentRequirements(RunContext, RequirementMapping.ChunkFragments);
		BindSharedFragmentRequirements(RunContext, RequirementMapping.ChunkFragments);

		RunContext.SetCurrentArchetypesTagBitSet(GetTagBitSet());
		RunContext.SetCurrentChunkSerialModificationNumber(Chunk.GetSerialModificationNumber());
		BindChunkFragmentRequirements(RunContext, RequirementMapping.ChunkFragments, Chunk);

		if (!ChunkCondition || ChunkCondition(RunContext))
		{
			BindEntityRequirements(RunContext, RequirementMapping.EntityFragments, Chunk, ChunkInfo.SubchunkStart, ChunkLength);
			Function(RunContext);
		}
	}
}

void FMassArchetypeData::CompactEntities(const double TimeAllowed)
{
	const double TimeAllowedEnd = FPlatformTime::Seconds() + TimeAllowed;

	TArray<FMassArchetypeChunk*> SortedChunks;
	for (FMassArchetypeChunk& Chunk : Chunks)
	{
		// Skip already full chunks
		const int32 NumInstances = Chunk.GetNumInstances();
		if(NumInstances > 0 && NumInstances < NumEntitiesPerChunk)
		{
			SortedChunks.Add(&Chunk);
		}
	}

	// Check if there is anything to compact at all
	if (SortedChunks.Num() <= 1)
	{
		return;
	}

	SortedChunks.Sort([](const FMassArchetypeChunk& LHS, const FMassArchetypeChunk& RHS)
	{
		return LHS.GetNumInstances() < RHS.GetNumInstances();
	});

	int32 ChunkToFillSortedIdx = 0;
	int32 ChunkToEmptySortedIdx = SortedChunks.Num() -1;
	while (ChunkToFillSortedIdx < ChunkToEmptySortedIdx && FPlatformTime::Seconds() < TimeAllowedEnd)
	{
		while (ChunkToFillSortedIdx < SortedChunks.Num() && SortedChunks[ChunkToFillSortedIdx]->GetNumInstances() == NumEntitiesPerChunk)
		{
			ChunkToFillSortedIdx++;
		}
		while (ChunkToEmptySortedIdx >= 0 && SortedChunks[ChunkToEmptySortedIdx]->GetNumInstances() == 0)
		{
			ChunkToEmptySortedIdx--;
		}
		if (ChunkToFillSortedIdx >= ChunkToEmptySortedIdx)
		{
			break;
		}

		FMassArchetypeChunk* ChunkToFill = SortedChunks[ChunkToFillSortedIdx];
		FMassArchetypeChunk* ChunkToEmpty = SortedChunks[ChunkToEmptySortedIdx];
		const int32 NumberOfEntitiesToMove =  FMath::Min(NumEntitiesPerChunk-ChunkToFill->GetNumInstances(), ChunkToEmpty->GetNumInstances());
		const int32 FromIndex = ChunkToEmpty->GetNumInstances() - NumberOfEntitiesToMove;
		const int32 ToIndex = ChunkToFill->GetNumInstances();
		check(NumberOfEntitiesToMove > 0);

		for (const FMassArchetypeFragmentConfig& FragmentConfig : FragmentConfigs)
		{
			void* FromFragmentPtr = FragmentConfig.GetFragmentData(ChunkToEmpty->GetRawMemory(), FromIndex);
			void* ToFragmentPtr = FragmentConfig.GetFragmentData(ChunkToFill->GetRawMemory(), ToIndex);

			if (UE::Mass::Core::bBitwiseRelocateFragments)
			{
				// Move all entries
				FMemory::Memcpy(ToFragmentPtr, FromFragmentPtr, FragmentConfig.FragmentType->GetStructureSize() * NumberOfEntitiesToMove);
			}
			else
			{
				// Destroy & initialize the fragment data
				FragmentConfig.FragmentType->ClearScriptStruct(ToFragmentPtr, NumberOfEntitiesToMove);

				// Copy all entries
				FragmentConfig.FragmentType->CopyScriptStruct(ToFragmentPtr, FromFragmentPtr, NumberOfEntitiesToMove);

				// Destroy all entries
				FragmentConfig.FragmentType->DestroyStruct(FromFragmentPtr, NumberOfEntitiesToMove);
			}
		}

		FMassEntityHandle* FromEntity = &ChunkToEmpty->GetEntityArrayElementRef(EntityListOffsetWithinChunk, FromIndex);
		FMassEntityHandle* ToEntity = &ChunkToFill->GetEntityArrayElementRef(EntityListOffsetWithinChunk, ToIndex);
		FMemory::Memcpy(ToEntity, FromEntity, NumberOfEntitiesToMove * sizeof(FMassEntityHandle));
		ChunkToFill->AddMultipleInstances(NumberOfEntitiesToMove);
		ChunkToEmpty->RemoveMultipleInstances(NumberOfEntitiesToMove);

		const int32 ChunkToFillIdx = UE_PTRDIFF_TO_INT32(ChunkToFill - &Chunks[0]);
		check(ChunkToFillIdx >=0 && ChunkToFillIdx < Chunks.Num());
		const int32 AbsoluteIndex = ChunkToFillIdx * NumEntitiesPerChunk + ToIndex;

		for (int32 i =0; i < NumberOfEntitiesToMove; i++, ++ToEntity)
		{
			EntityMap.FindChecked(ToEntity->Index) = AbsoluteIndex+i;
		}
	}
}

void FMassArchetypeData::GetRequirementsFragmentMapping(TConstArrayView<FMassFragmentRequirement> Requirements, FMassFragmentIndicesMapping& OutFragmentIndices)
{
	OutFragmentIndices.Reset(Requirements.Num());
	for (const FMassFragmentRequirement& Requirement : Requirements)
	{
		if (Requirement.RequiresBinding())
		{
			const int32* FragmentIndex = FragmentIndexMap.Find(Requirement.StructType);
			check(FragmentIndex != nullptr || Requirement.IsOptional());
			OutFragmentIndices.Add(FragmentIndex ? *FragmentIndex : INDEX_NONE);
		}
	}
}

void FMassArchetypeData::GetRequirementsChunkFragmentMapping(TConstArrayView<FMassFragmentRequirement> ChunkRequirements, FMassFragmentIndicesMapping& OutFragmentIndices)
{
	int32 LastFoundFragmentIndex = -1;
	OutFragmentIndices.Reset(ChunkRequirements.Num());
	for (const FMassFragmentRequirement& Requirement : ChunkRequirements)
	{
		if (Requirement.RequiresBinding())
		{
			int32 FragmentIndex = INDEX_NONE;
			// mz@todo Add comment here as this code seems to be assuming a certain order for chunk fragments, please explain
			for (int32 i = LastFoundFragmentIndex + 1; i < ChunkFragmentsTemplate.Num(); ++i)
			{
				if (ChunkFragmentsTemplate[i].GetScriptStruct()->IsChildOf(Requirement.StructType))
				{
					FragmentIndex = i;
					break;
				}
			}

			check(FragmentIndex != INDEX_NONE || Requirement.IsOptional());
			OutFragmentIndices.Add(FragmentIndex);
			LastFoundFragmentIndex = FragmentIndex;
		}
	}
}

void FMassArchetypeData::GetRequirementsConstSharedFragmentMapping(TConstArrayView<FMassFragmentRequirement> Requirements, FMassFragmentIndicesMapping& OutFragmentIndices)
{
	OutFragmentIndices.Reset(Requirements.Num());
	for (const FMassFragmentRequirement& Requirement : Requirements)
	{
		if (Requirement.RequiresBinding())
		{
			const int32 FragmentIndex = SharedFragmentValues.GetConstSharedFragments().IndexOfByPredicate(FStructTypeEqualOperator(Requirement.StructType));
			check(FragmentIndex != INDEX_NONE || Requirement.IsOptional());
			OutFragmentIndices.Add(FragmentIndex);
		}
	}
}

void FMassArchetypeData::GetRequirementsSharedFragmentMapping(TConstArrayView<FMassFragmentRequirement> Requirements, FMassFragmentIndicesMapping& OutFragmentIndices)
{
	OutFragmentIndices.Reset(Requirements.Num());
	for (const FMassFragmentRequirement& Requirement : Requirements)
	{
		if (Requirement.RequiresBinding())
		{
			const int32 FragmentIndex = SharedFragmentValues.GetSharedFragments().IndexOfByPredicate(FStructTypeEqualOperator(Requirement.StructType));
			check(FragmentIndex != INDEX_NONE || Requirement.IsOptional());
			OutFragmentIndices.Add(FragmentIndex);
		}
	}
}

void FMassArchetypeData::BindEntityRequirements(FMassExecutionContext& RunContext, const FMassFragmentIndicesMapping& EntityFragmentsMapping, FMassArchetypeChunk& Chunk, const int32 SubchunkStart, const int32 SubchunkLength)
{
	// auto-correcting number of entities to process in case SubchunkStart +  SubchunkLength > Chunk.GetNumInstances()
	const int32 NumEntities = SubchunkLength >= 0 ? FMath::Min(SubchunkLength, Chunk.GetNumInstances() - SubchunkStart) : Chunk.GetNumInstances();
	check(SubchunkStart >= 0 && SubchunkStart < Chunk.GetNumInstances());

	if (EntityFragmentsMapping.Num() > 0)
	{
		check(RunContext.GetMutableRequirements().Num() == EntityFragmentsMapping.Num());

		for (int i = 0; i < EntityFragmentsMapping.Num(); ++i)
		{
			FMassExecutionContext::FFragmentView& Requirement = RunContext.FragmentViews[i];
			const int32 FragmentIndex = EntityFragmentsMapping[i];

			check(FragmentIndex != INDEX_NONE || Requirement.Requirement.IsOptional());
			if (FragmentIndex != INDEX_NONE)
			{
				Requirement.FragmentView = TArrayView<FMassFragment>((FMassFragment*)GetFragmentData(FragmentIndex, Chunk.GetRawMemory(), SubchunkStart), NumEntities);
			}
			else
			{
				// @todo this might not be needed
				Requirement.FragmentView = TArrayView<FMassFragment>();
			}
		}
	}
	else
	{
		// Map in the required data arrays from the current chunk to the array views
		for (FMassExecutionContext::FFragmentView& Requirement : RunContext.GetMutableRequirements())
		{
			const int32* FragmentIndex = FragmentIndexMap.Find(Requirement.Requirement.StructType);
			check(FragmentIndex != nullptr || Requirement.Requirement.IsOptional());
			if (FragmentIndex)
			{
				Requirement.FragmentView = TArrayView<FMassFragment>((FMassFragment*)GetFragmentData(*FragmentIndex, Chunk.GetRawMemory(), SubchunkStart), NumEntities);
			}
			else
			{
				Requirement.FragmentView = TArrayView<FMassFragment>();
			}
		}
	}

	RunContext.EntityListView = TArrayView<FMassEntityHandle>(&Chunk.GetEntityArrayElementRef(EntityListOffsetWithinChunk, SubchunkStart), NumEntities);
}

void FMassArchetypeData::BindChunkFragmentRequirements(FMassExecutionContext& RunContext, const FMassFragmentIndicesMapping& ChunkFragmentsMapping, FMassArchetypeChunk& Chunk)
{
	if (ChunkFragmentsMapping.Num() > 0)
	{
		check(RunContext.GetMutableChunkRequirements().Num() == ChunkFragmentsMapping.Num());

		for (int i = 0; i < ChunkFragmentsMapping.Num(); ++i)
		{
			FMassExecutionContext::FChunkFragmentView& ChunkRequirement = RunContext.ChunkFragmentViews[i];
			const int32 ChunkFragmentIndex = ChunkFragmentsMapping[i];

			check(ChunkFragmentIndex != INDEX_NONE || ChunkRequirement.Requirement.IsOptional());
			ChunkRequirement.FragmentView = ChunkFragmentIndex != INDEX_NONE ? Chunk.GetMutableChunkFragmentViewChecked(ChunkFragmentIndex) : FStructView();
		}
	}
	else
	{
		for (FMassExecutionContext::FChunkFragmentView& ChunkRequirement : RunContext.GetMutableChunkRequirements())
		{
			FInstancedStruct* ChunkFragmentInstance = Chunk.FindMutableChunkFragment(ChunkRequirement.Requirement.StructType);
			check(ChunkFragmentInstance != nullptr || ChunkRequirement.Requirement.IsOptional());
			ChunkRequirement.FragmentView = ChunkFragmentInstance ? FStructView(*ChunkFragmentInstance) : FStructView();
		}
	}
}

void FMassArchetypeData::BindConstSharedFragmentRequirements(FMassExecutionContext& RunContext, const FMassFragmentIndicesMapping& FragmentsMapping)
{
	if (FragmentsMapping.Num() > 0)
	{
		check(RunContext.GetMutableConstSharedRequirements().Num() == FragmentsMapping.Num());

		for (int i = 0; i < FragmentsMapping.Num(); ++i)
		{
			FMassExecutionContext::FConstSharedFragmentView& Requirement = RunContext.ConstSharedFragmentViews[i];
			const int32 FragmentIndex = FragmentsMapping[i];

			check(FragmentIndex != INDEX_NONE || Requirement.Requirement.IsOptional());
			Requirement.FragmentView = FragmentIndex != INDEX_NONE ? SharedFragmentValues.GetConstSharedFragments()[FragmentIndex] : FConstSharedStruct();
		}
	}
	else
	{
		for (FMassExecutionContext::FConstSharedFragmentView& Requirement : RunContext.GetMutableConstSharedRequirements())
		{
			const FConstSharedStruct* SharedFragment = SharedFragmentValues.GetConstSharedFragments().FindByPredicate(FStructTypeEqualOperator(Requirement.Requirement.StructType) );
			check(SharedFragment != nullptr || Requirement.Requirement.IsOptional());
			Requirement.FragmentView = SharedFragment ? *SharedFragment : FConstSharedStruct();
		}
	}
}

void FMassArchetypeData::BindSharedFragmentRequirements(FMassExecutionContext& RunContext, const FMassFragmentIndicesMapping& FragmentsMapping)
{
	if (FragmentsMapping.Num() > 0)
	{
		check(RunContext.GetMutableSharedRequirements().Num() == FragmentsMapping.Num());

		for (int i = 0; i < FragmentsMapping.Num(); ++i)
		{
			FMassExecutionContext::FSharedFragmentView& Requirement = RunContext.SharedFragmentViews[i];
			const int32 FragmentIndex = FragmentsMapping[i];

			check(FragmentIndex != INDEX_NONE || Requirement.Requirement.IsOptional());
			Requirement.FragmentView = FragmentIndex != INDEX_NONE ? SharedFragmentValues.GetSharedFragments()[FragmentIndex] : FSharedStruct();
		}
	}
	else
	{
		for (FMassExecutionContext::FSharedFragmentView& Requirement : RunContext.GetMutableSharedRequirements())
		{
			const FSharedStruct* SharedFragment = SharedFragmentValues.GetSharedFragments().FindByPredicate(FStructTypeEqualOperator(Requirement.Requirement.StructType));
			check(SharedFragment != nullptr || Requirement.Requirement.IsOptional());
			Requirement.FragmentView = SharedFragment ? *SharedFragment : FSharedStruct();
		}
	}
}

SIZE_T FMassArchetypeData::GetAllocatedSize() const
{
	int32 NumAllocatedChunkBuffers = 0;
	for (const FMassArchetypeChunk& Chunk : Chunks)
	{
		if (Chunk.GetRawMemory() != nullptr)
		{
			++NumAllocatedChunkBuffers;
		}
	}

	return sizeof(FMassArchetypeData) +
		SharedFragmentValues.GetAllocatedSize() +
		ChunkFragmentsTemplate.GetAllocatedSize() +
		FragmentConfigs.GetAllocatedSize() +
		Chunks.GetAllocatedSize() +
		(NumAllocatedChunkBuffers * GetChunkAllocSize()) +
		EntityMap.GetAllocatedSize() +
		FragmentIndexMap.GetAllocatedSize();
}

FString FMassArchetypeData::DebugGetDescription() const
{
#if WITH_MASSENTITY_DEBUG
	FStringOutputDevice OutDescription;

	OutDescription += TEXT("Chunk fragments: ");
	CompositionDescriptor.ChunkFragments.DebugGetStringDesc(OutDescription);
	OutDescription += TEXT("\nTags: ");
	CompositionDescriptor.Tags.DebugGetStringDesc(OutDescription);
	OutDescription += TEXT("\nFragments: ");
	CompositionDescriptor.Fragments.DebugGetStringDesc(OutDescription);
	
	return static_cast<FString>(OutDescription);
#else
	return {};
#endif
}

#if WITH_MASSENTITY_DEBUG
void FMassArchetypeData::DebugPrintArchetype(FOutputDevice& Ar)
{
	FStringOutputDevice TagsDecription;
	CompositionDescriptor.Tags.DebugGetStringDesc(TagsDecription);
	Ar.Logf(ELogVerbosity::Log, TEXT("Tags: %s"), *TagsDecription);
	Ar.Logf(ELogVerbosity::Log, TEXT("Fragments: %s"), *DebugGetDescription());
	Ar.Logf(ELogVerbosity::Log, TEXT("\tChunks: %d x %d KB = %d KB total"), Chunks.Num(), GetChunkAllocSize() / 1024, (GetChunkAllocSize()*Chunks.Num()) / 1024);
	
	int ChunkWithFragmentsCount = 0;
	for (FMassArchetypeChunk& Chunk : Chunks)
	{
		ChunkWithFragmentsCount += Chunk.DebugGetChunkFragmentCount() > 0 ? 1 : 0;
	}
	if (ChunkWithFragmentsCount)
	{
		Ar.Logf(ELogVerbosity::Log, TEXT("\tChunks with fragments: %d"), ChunkWithFragmentsCount);
	}

	const int32 CurrentEntityCapacity = Chunks.Num() * NumEntitiesPerChunk;
	Ar.Logf(ELogVerbosity::Log, TEXT("\tEntity Count    : %d"), EntityMap.Num());
	Ar.Logf(ELogVerbosity::Log, TEXT("\tEntity Capacity : %d"), CurrentEntityCapacity);
	if (Chunks.Num() > 1)
	{
		const float Scaler = 100.0f / (float)CurrentEntityCapacity;
		// count non-last chunks to see how occupied they are
		int EntitiesPerChunkMin = CurrentEntityCapacity;
		int EntitiesPerChunkMax = 0;
		for (int ChunkIndex = 0; ChunkIndex < Chunks.Num() - 1; ++ChunkIndex)
		{
			const int Population = Chunks[ChunkIndex].GetNumInstances();
			EntitiesPerChunkMin = FMath::Min(Population, EntitiesPerChunkMin);
			EntitiesPerChunkMax = FMath::Max(Population, EntitiesPerChunkMax);
		}
		Ar.Logf(ELogVerbosity::Log, TEXT("\tEntity Occupancy: %.1f%% (min: %.1f%%, max: %.1f%%)"), Scaler * EntityMap.Num(), Scaler * EntitiesPerChunkMin, Scaler * EntitiesPerChunkMax);
	}
	else 
	{
		Ar.Logf(ELogVerbosity::Log, TEXT("\tEntity Occupancy: %.1f%%"), CurrentEntityCapacity > 0 ? ((EntityMap.Num() * 100.0f) / (float)CurrentEntityCapacity) : 0.f);
	}
	Ar.Logf(ELogVerbosity::Log, TEXT("\tBytes / Entity  : %d"), TotalBytesPerEntity);
	Ar.Logf(ELogVerbosity::Log, TEXT("\tEntities / Chunk: %d"), NumEntitiesPerChunk);

	Ar.Logf(ELogVerbosity::Log, TEXT("\tOffset 0x%04X: Entity[] (%d bytes each)"), EntityListOffsetWithinChunk, sizeof(FMassEntityHandle));
	int32 TotalBytesOfValidData = sizeof(FMassEntityHandle) * NumEntitiesPerChunk;
	for (const FMassArchetypeFragmentConfig& FragmentConfig : FragmentConfigs)
	{
		TotalBytesOfValidData += FragmentConfig.FragmentType->GetStructureSize() * NumEntitiesPerChunk;
		Ar.Logf(ELogVerbosity::Log, TEXT("\tOffset 0x%04X: %s[] (%d bytes each)"), FragmentConfig.ArrayOffsetWithinChunk, *FragmentConfig.FragmentType->GetName(), FragmentConfig.FragmentType->GetStructureSize());
	}

	//@TODO: Print out padding in between things?

	const int32 UnusuablePaddingOffset = TotalBytesPerEntity * NumEntitiesPerChunk;
	const int32 UnusuablePaddingAmount = GetChunkAllocSize() - UnusuablePaddingOffset;
	if (UnusuablePaddingAmount > 0)
	{
		Ar.Logf(ELogVerbosity::Log, TEXT("\tOffset 0x%04X: WastePadding[] (%d bytes total)"), UnusuablePaddingOffset, UnusuablePaddingAmount);
	}

	if (GetChunkAllocSize() != TotalBytesOfValidData + UnusuablePaddingAmount)
	{
		Ar.Logf(ELogVerbosity::Log, TEXT("\t@TODO: EXTRA PADDING HERE:  TotalBytesOfValidData: %d (%d missing)"), TotalBytesOfValidData, GetChunkAllocSize() - TotalBytesOfValidData);
	}
}

void FMassArchetypeData::DebugPrintEntity(FMassEntityHandle Entity, FOutputDevice& Ar, const TCHAR* InPrefix) const
{
	for (const FMassArchetypeFragmentConfig& FragmentConfig : FragmentConfigs)
	{
		void* Data = GetFragmentDataForEntityChecked(FragmentConfig.FragmentType, Entity.Index);
		
		FString FragmentName = FragmentConfig.FragmentType->GetName();
		FragmentName.RemoveFromStart(InPrefix);

		FString ValueStr;
		FragmentConfig.FragmentType->ExportText(ValueStr, Data, /*Default*/nullptr, /*OwnerObject*/nullptr, EPropertyPortFlags::PPF_IncludeTransient, /*ExportRootScope*/nullptr);

		Ar.Logf(TEXT("%s: %s"), *FragmentName, *ValueStr);
	}
}

#endif // WITH_MASSENTITY_DEBUG

void FMassArchetypeData::REMOVEME_GetArrayViewForFragmentInChunk(int32 ChunkIndex, const UScriptStruct* FragmentType, void*& OutChunkBase, int32& OutNumEntities)
{
	const FMassArchetypeChunk& Chunk = Chunks[ChunkIndex];
	const int32 FragmentIndex = FragmentIndexMap.FindChecked(FragmentType);

	OutChunkBase = FragmentConfigs[FragmentIndex].GetFragmentData(Chunk.GetRawMemory(), 0);
	OutNumEntities = Chunk.GetNumInstances();
}

