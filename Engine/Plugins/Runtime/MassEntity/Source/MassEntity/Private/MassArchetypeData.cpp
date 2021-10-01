// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassArchetypeData.h"
#include "MassEntityTypes.h"
#include "Misc/StringBuilder.h"


//////////////////////////////////////////////////////////////////////
// FMassArchetypeData
namespace UE::Mass::Core 
{
	constexpr static bool bBitwiseRelocateComponents = true;
}// UE::Mass::Core

void FMassArchetypeData::ForEachComponentType(TFunction< void(const UScriptStruct* /*Component*/)> Function) const
{
	for (const FMassArchetypeFragmentConfig& ComponentData : ComponentConfigs)
	{
		Function(ComponentData.ComponentType);
	}
}

bool FMassArchetypeData::HasComponentType(const UScriptStruct* ComponentType) const
{
	return (ComponentType && CompositionDescriptor.Components.Contains(*ComponentType));
}

bool FMassArchetypeData::IsEquivalent(TConstArrayView<const UScriptStruct*> OtherList) const
{
	if (OtherList.Num() != ComponentConfigs.Num())
	{
		return false;
	}

	for (int32 Index = 0; Index < ComponentConfigs.Num(); ++Index)
	{
		if (OtherList[Index] != ComponentConfigs[Index].ComponentType)
		{
			return false;
		}
	}

	return true;
}

void FMassArchetypeData::Initialize(const FMassFragmentBitSet& Components, const FMassTagBitSet& Tags, const FMassChunkFragmentBitSet& ChunkComponents)
{
	TArray<const UScriptStruct*, TInlineAllocator<16>> SortedComponentList;
	Components.ExportTypes(SortedComponentList);

	SortedComponentList.Sort(FMassSorterOperator<UScriptStruct>());

	// Figure out how many bytes all of the individual components (and metadata) will cost per entity
	int32 ComponentSizeTallyBytes = 0;

	// Alignment padding computation is currently very conservative and over-estimated.
	int32 AlignmentPadding = 0;
	{
		// Save room for the 'metadata' (entity array)
		ComponentSizeTallyBytes += sizeof(FMassEntityHandle);

		// Tally up the component sizes and place them in the index map
		ComponentConfigs.AddDefaulted(SortedComponentList.Num());
		ComponentIndexMap.Reserve(SortedComponentList.Num());

		for (int32 ComponentIndex = 0; ComponentIndex < SortedComponentList.Num(); ++ComponentIndex)
		{
			const UScriptStruct* ComponentType = SortedComponentList[ComponentIndex];
			checkSlow(ComponentType);
			ComponentConfigs[ComponentIndex].ComponentType = ComponentType;
			
			AlignmentPadding += ComponentType->GetMinAlignment();
			ComponentSizeTallyBytes += ComponentType->GetStructureSize();

			ComponentIndexMap.Add(ComponentType, ComponentIndex);
			CompositionDescriptor.Components.Add(*ComponentType);
		}
		checkSlow(Components.IsEquivalent(CompositionDescriptor.Components));
	}

	CompositionDescriptor.Tags = Tags;
	CompositionDescriptor.ChunkComponents = ChunkComponents;
	// create the chunk component instances
	TArray<const UScriptStruct*, TInlineAllocator<16>> ChunkComponentList;
	ChunkComponents.ExportTypes(ChunkComponentList);
	ChunkComponentList.Sort(FMassSorterOperator<UScriptStruct>());
	for (const UScriptStruct* ChunkComponentType : ChunkComponentList)
	{
		check(ChunkComponentType);
		ChunkComponentTemplates.Add(FInstancedStruct(ChunkComponentType));
	}

	TotalBytesPerEntity = ComponentSizeTallyBytes;
	int32 ChunkAvailableSize = GetChunkAllocSize() - AlignmentPadding;
	check(TotalBytesPerEntity <= ChunkAvailableSize);

	NumEntitiesPerChunk = ChunkAvailableSize / TotalBytesPerEntity;

	// Set up the offsets for each component into the chunk data
	EntityListOffsetWithinChunk = 0;
	int32 CurrentOffset = NumEntitiesPerChunk * sizeof(FMassEntityHandle);
	for (FMassArchetypeFragmentConfig& ComponentData : ComponentConfigs)
	{
		CurrentOffset = Align(CurrentOffset, ComponentData.ComponentType->GetMinAlignment());
		ComponentData.ArrayOffsetWithinChunk = CurrentOffset;
		const int32 SizeOfThisComponentArray = NumEntitiesPerChunk * ComponentData.ComponentType->GetStructureSize();
		CurrentOffset += SizeOfThisComponentArray;
	}
}

void FMassArchetypeData::InitializeWithSibling(const FMassArchetypeData& SiblingArchetype, const FMassTagBitSet& OverrideTags)
{
	checkf(IsInitialized() == false, TEXT("Trying to %s but this archetype has already been initialized"));

	ComponentConfigs = SiblingArchetype.ComponentConfigs;
	ComponentIndexMap = SiblingArchetype.ComponentIndexMap;
	CompositionDescriptor.Components = SiblingArchetype.CompositionDescriptor.Components;
	CompositionDescriptor.Tags = OverrideTags;
	CompositionDescriptor.ChunkComponents = SiblingArchetype.CompositionDescriptor.ChunkComponents;
	ChunkComponentTemplates = SiblingArchetype.ChunkComponentTemplates;

	TotalBytesPerEntity = SiblingArchetype.TotalBytesPerEntity;
	NumEntitiesPerChunk = SiblingArchetype.NumEntitiesPerChunk;

	// Set up the offsets for each component into the chunk data
	EntityListOffsetWithinChunk = 0;
}

void FMassArchetypeData::AddEntity(FMassEntityHandle Entity)
{
	AddEntityInternal(Entity, true/*bInitializeComponents*/);
}

int32 FMassArchetypeData::AddEntityInternal(FMassEntityHandle Entity, const bool bInitializeComponents)
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
			DestinationChunk->Recycle(ChunkComponentTemplates);
			AbsoluteIndex = EmptyAbsoluteIndex;
		}
		else
		{
			DestinationChunk = &Chunks.Emplace_GetRef(GetChunkAllocSize(), ChunkComponentTemplates);
		}

		check(DestinationChunk);
		DestinationChunk->AddInstance();
	}

	// Initialize the component memory
	if (bInitializeComponents)
	{
		for (const FMassArchetypeFragmentConfig& ComponentConfig : ComponentConfigs)
		{
			void* ComponentPtr = ComponentConfig.GetComponentData(DestinationChunk->GetRawMemory(), IndexWithinChunk);
			ComponentConfig.ComponentType->InitializeStruct(ComponentPtr);
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
	RemoveEntityInternal(AbsoluteIndex, true/*bDestroyComponents*/);
}

void FMassArchetypeData::RemoveEntityInternal(const int32 AbsoluteIndex, const bool bDestroyComponents)
{
	const int32 ChunkIndex = AbsoluteIndex / NumEntitiesPerChunk;
	const int32 IndexWithinChunk = AbsoluteIndex % NumEntitiesPerChunk;

	FMassArchetypeChunk& Chunk = Chunks[ChunkIndex];

	Chunk.RemoveInstance();
	const int32 IndexToSwapFrom = Chunk.GetNumInstances();

	checkf(bDestroyComponents || UE::Mass::Core::bBitwiseRelocateComponents, TEXT("We allow not to destroy components only in bit wise relocation mode."));

	// Remove and swap the last entry in the chunk to the location of the removed item (if it's not the same as the dying entry)
	if (IndexToSwapFrom != IndexWithinChunk)
	{
		for (const FMassArchetypeFragmentConfig& ComponentConfig : ComponentConfigs)
		{
			void* DyingComponentPtr = ComponentConfig.GetComponentData(Chunk.GetRawMemory(), IndexWithinChunk);
			void* MovingComponentPtr = ComponentConfig.GetComponentData(Chunk.GetRawMemory(), IndexToSwapFrom);

			if (UE::Mass::Core::bBitwiseRelocateComponents)
			{
				// Destroy component data
				if (bDestroyComponents)
				{
					ComponentConfig.ComponentType->DestroyStruct(DyingComponentPtr);
				}

				// Move last entry
				FMemory::Memcpy(DyingComponentPtr, MovingComponentPtr, ComponentConfig.ComponentType->GetStructureSize());
			}
			else
			{
				// Destroy & initialize the component data
				ComponentConfig.ComponentType->ClearScriptStruct(DyingComponentPtr);

				// Copy last entry
				ComponentConfig.ComponentType->CopyScriptStruct(DyingComponentPtr, MovingComponentPtr);

				// Destroy last entry
				ComponentConfig.ComponentType->DestroyStruct(MovingComponentPtr);
			}
		}

		// Update the entity table and map
		const FMassEntityHandle EntityBeingSwapped = Chunk.GetEntityArrayElementRef(EntityListOffsetWithinChunk, IndexToSwapFrom);
		Chunk.GetEntityArrayElementRef(EntityListOffsetWithinChunk, IndexWithinChunk) = EntityBeingSwapped;
		EntityMap.FindChecked(EntityBeingSwapped.Index) = AbsoluteIndex;
	}
	else if (!UE::Mass::Core::bBitwiseRelocateComponents || bDestroyComponents)
	{
		for (const FMassArchetypeFragmentConfig& ComponentConfig : ComponentConfigs)
		{
			// Destroy the component data
			void* DyingComponentPtr = ComponentConfig.GetComponentData(Chunk.GetRawMemory(), IndexWithinChunk);
			ComponentConfig.ComponentType->DestroyStruct(DyingComponentPtr);
		}
	}

	// If the chunk itself is empty now, see if we can remove it entirely
	// Note: This is only possible for trailing chunks, to avoid messing up the absolute indices in the entities map
	while ((Chunks.Num() > 0) && (Chunks.Last().GetNumInstances() == 0))
	{
		Chunks.RemoveAt(Chunks.Num() - 1, 1, /*bAllowShrinking=*/ false);
	}
}

void FMassArchetypeData::BatchDestroyEntityChunks(const FArchetypeChunkCollection& ChunkCollection, TArray<FMassEntityHandle>& OutEntitiesRemoved)
{
	const int32 InitialOutEntitiesCount = OutEntitiesRemoved.Num();

	// Sorting the subchunks info so that subchunks of a given chunk are processed "from the back". Otherwise removing 
	// a subchunk from the front of the chunk would inevitably invalidate following subchunks' information.
	TArray<FArchetypeChunkCollection::FChunkInfo> Subchunks(ChunkCollection.GetChunks());
	Subchunks.Sort([](const FArchetypeChunkCollection::FChunkInfo& A, const FArchetypeChunkCollection::FChunkInfo& B) 
		{ 
			return A.ChunkIndex < B.ChunkIndex || (A.ChunkIndex == B.ChunkIndex && A.SubchunkStart > B.SubchunkStart);
		});

	for (const FArchetypeChunkCollection::FChunkInfo SubchunkInfo : Subchunks)
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

			for (const FMassArchetypeFragmentConfig& ComponentConfig : ComponentConfigs)
			{
				void* DyingComponentPtr = ComponentConfig.GetComponentData(Chunk.GetRawMemory(), SubchunkInfo.SubchunkStart);
				void* MovingComponentPtr = ComponentConfig.GetComponentData(Chunk.GetRawMemory(), SwapStartIndex);

				if (UE::Mass::Core::bBitwiseRelocateComponents)
				{
					// Destroy the components we'll replace by the following copy
					ComponentConfig.ComponentType->DestroyStruct(DyingComponentPtr, NumberToMove);

					// Swap components to the empty space just created.
					FMemory::Memcpy(DyingComponentPtr, MovingComponentPtr, ComponentConfig.ComponentType->GetStructureSize() * NumberToMove);
				}
				else
				{
					// Clear components that we will copy over. Clear destroys and initializes the components, which is needed for CopyScriptStruct().
					ComponentConfig.ComponentType->ClearScriptStruct(DyingComponentPtr, NumberToMove);

					// Swap components into the empty space just created.
					ComponentConfig.ComponentType->CopyScriptStruct(DyingComponentPtr, MovingComponentPtr, NumberToMove);

					// Destroy the components that were moved.
					ComponentConfig.ComponentType->DestroyStruct(MovingComponentPtr, NumberToMove);
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
			for (const FMassArchetypeFragmentConfig& ComponentConfig : ComponentConfigs)
			{
				// Destroy the component data
				void* DyingComponentPtr = ComponentConfig.GetComponentData(Chunk.GetRawMemory(), CutStartIndex);
				ComponentConfig.ComponentType->DestroyStruct(DyingComponentPtr, NumberToCut);
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

bool FMassArchetypeData::HasComponentDataForEntity(const UScriptStruct* ComponentType, int32 EntityIndex) const
{
	return (ComponentType && CompositionDescriptor.Components.Contains(*ComponentType));
}

void* FMassArchetypeData::GetComponentDataForEntityChecked(const UScriptStruct* ComponentType, int32 EntityIndex) const
{
	const FInternalEntityHandle InternalIndex = MakeEntityHandle(EntityIndex);
	
	// failing the below Find means given entity's archetype is missing given ComponentType
	const int32 ComponentIndex = ComponentIndexMap.FindChecked(ComponentType);
	return GetComponentData(ComponentIndex, InternalIndex);
}

void* FMassArchetypeData::GetComponentDataForEntity(const UScriptStruct* ComponentType, int32 EntityIndex) const
{
	if (const int32* ComponentIndex = ComponentIndexMap.Find(ComponentType))
	{
		FInternalEntityHandle InternalIndex = MakeEntityHandle(EntityIndex);
		// failing the below Find means given entity's archetype is missing given ComponentType
		return GetComponentData(*ComponentIndex, InternalIndex);
	}
	return nullptr;
}

void FMassArchetypeData::SetComponentsData(const FMassEntityHandle Entity, TArrayView<const FInstancedStruct> ComponentInstances)
{
	FInternalEntityHandle InternalIndex = MakeEntityHandle(Entity);

	for (const FInstancedStruct& Instance : ComponentInstances)
	{
		const UScriptStruct* ComponentType = Instance.GetScriptStruct();
		check(ComponentType);
		const int32 ComponentIndex = ComponentIndexMap.FindChecked(ComponentType);
		void* ComponentMemory = GetComponentData(ComponentIndex, InternalIndex);
		// No UE::Mass::Core::bBitwiseRelocateComponents, this isn't a move component
		ComponentType->CopyScriptStruct(ComponentMemory, Instance.GetMemory());
	}
}

void FMassArchetypeData::SetComponentData(const FArchetypeChunkCollection& ChunkCollection, const FInstancedStruct& ComponentSource)
{
	check(ComponentSource.IsValid());
	const UScriptStruct* ComponentType = ComponentSource.GetScriptStruct();
	check(ComponentType);
	const int32 ComponentIndex = ComponentIndexMap.FindChecked(ComponentType);
	const int32 ComponentTypeSize = ComponentType->GetStructureSize();
	const uint8* ComponentSourceMemory = ComponentSource.GetMemory();
	check(ComponentSourceMemory);
	
	for (FMassArchetypeChunkIterator ChunkIterator(ChunkCollection); ChunkIterator; ++ChunkIterator)
	{
		uint8* ComponentMemory = (uint8*)ComponentConfigs[ComponentIndex].GetComponentData(Chunks[ChunkIterator->ChunkIndex].GetRawMemory(), ChunkIterator->SubchunkStart);
		for (int i = ChunkIterator->Length; i; --i, ComponentMemory += ComponentTypeSize)
		{
			// No UE::Mass::Core::bBitwiseRelocateComponents, this isn't a move of a component
			ComponentType->CopyScriptStruct(ComponentMemory, ComponentSourceMemory);
		}
	}
}

void FMassArchetypeData::SetDefaultChunkComponentValue(FConstStructView InstancedStruct)
{
	if (ensure(InstancedStruct.GetScriptStruct()) && CompositionDescriptor.ChunkComponents.Contains(*InstancedStruct.GetScriptStruct()))
	{
		if (FInstancedStruct* Element = ChunkComponentTemplates.FindByPredicate([&InstancedStruct](const FInstancedStruct& Element) { return Element.GetScriptStruct() == InstancedStruct.GetScriptStruct(); }))
		{
			*Element = InstancedStruct;
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

	constexpr bool bInitializeComponentsDuringCreation = !UE::Mass::Core::bBitwiseRelocateComponents;
	const int32 NewAbsoluteIndex = NewArchetype.AddEntityInternal(Entity, bInitializeComponentsDuringCreation);
	const int32 NewChunkIndex = NewAbsoluteIndex / NewArchetype.NumEntitiesPerChunk;
	const int32 NewIndexWithinChunk = NewAbsoluteIndex % NewArchetype.NumEntitiesPerChunk;
	FMassArchetypeChunk& NewChunk = NewArchetype.Chunks[NewChunkIndex];

	// for every NewArchetype's component see if it was in the old component as well and if so copy it's value. 
	// If not then initialize the component (unless it has already been initialized based on bInitializeComponentsDuringCreation
	// value
	for (const FMassArchetypeFragmentConfig& NewComponentConfig : NewArchetype.ComponentConfigs)
	{
		const int32* OldComponentIndex = ComponentIndexMap.Find(NewComponentConfig.ComponentType);
		void* Dst = NewComponentConfig.GetComponentData(NewChunk.GetRawMemory(), NewIndexWithinChunk);

		// Only copy if the component type exists in both archetypes
		if (OldComponentIndex)
		{
			const void* Src = ComponentConfigs[*OldComponentIndex].GetComponentData(Chunk.GetRawMemory(), IndexWithinChunk);
			if (UE::Mass::Core::bBitwiseRelocateComponents)
			{
				FMemory::Memcpy(Dst, Src, NewComponentConfig.ComponentType->GetStructureSize());
			}
			else
			{
				NewComponentConfig.ComponentType->CopyScriptStruct(Dst, Src);
			}
		}
		else if (bInitializeComponentsDuringCreation == false)
		{
			// the component's unique to the NewArchetype need to be initialized
			// @todo we're doing it for tags here as well. A tiny bit of perf lost. Probably not worth adding a check
			// but something to keep in mind. Will go away once tags are more of an archetype fragment than entity's
			NewComponentConfig.ComponentType->InitializeStruct(Dst);
		}
	}

	constexpr bool bDestroyComponents = !UE::Mass::Core::bBitwiseRelocateComponents;
	RemoveEntityInternal(AbsoluteIndex, bDestroyComponents);
}

void FMassArchetypeData::ExecuteFunction(FMassExecutionContext& RunContext, const FMassExecuteFunction& Function, const FMassQueryRequirementIndicesMapping& RequirementMapping, const FArchetypeChunkCollection& ChunkCollection)
{
	check(ChunkCollection.GetArchetype() == this);

	// mz@todo to be removed
	RunContext.SetCurrentArchetypeData(*this);
	for (FMassArchetypeChunkIterator ChunkIterator(ChunkCollection); ChunkIterator; ++ChunkIterator)
	{
		FMassArchetypeChunk& Chunk = Chunks[ChunkIterator->ChunkIndex];
		
		const int32 ChunkLength = ChunkIterator->Length > 0 ? ChunkIterator->Length : (Chunk.GetNumInstances() - ChunkIterator->SubchunkStart);
		if (ChunkLength)
		{
			checkf((ChunkIterator->SubchunkStart + ChunkLength) <= Chunk.GetNumInstances() && ChunkLength > 0, TEXT("Invalid subchunk, it is going over the number of instances in the chunk or it is empty."));

			RunContext.SetCurrentChunkSerialModificationNumber(Chunk.GetSerialModificationNumber());
			BindChunkComponentRequirements(RunContext, RequirementMapping.ChunkComponents, Chunk);
			BindEntityRequirements(RunContext, RequirementMapping.EntityComponents, Chunk, ChunkIterator->SubchunkStart, ChunkLength);

			Function(RunContext);
		}
	}
}

void FMassArchetypeData::ExecuteFunction(FMassExecutionContext& RunContext, const FMassExecuteFunction& Function, const FMassQueryRequirementIndicesMapping& RequirementMapping, const FMassChunkConditionFunction& ChunkCondition)
{
	// mz@todo to be removed
	RunContext.SetCurrentArchetypeData(*this);
	for (FMassArchetypeChunk& Chunk : Chunks)
	{
		if (Chunk.GetNumInstances())
		{
			RunContext.SetCurrentChunkSerialModificationNumber(Chunk.GetSerialModificationNumber());
			BindChunkComponentRequirements(RunContext, RequirementMapping.ChunkComponents, Chunk);

			if (!ChunkCondition || ChunkCondition(RunContext))
			{
				BindEntityRequirements(RunContext, RequirementMapping.EntityComponents, Chunk, 0, Chunk.GetNumInstances());
				Function(RunContext);
			}
		}
	}
}

void FMassArchetypeData::ExecutionFunctionForChunk(FMassExecutionContext RunContext, const FMassExecuteFunction& Function, const FMassQueryRequirementIndicesMapping& RequirementMapping, const FArchetypeChunkCollection::FChunkInfo& ChunkInfo, const FMassChunkConditionFunction& ChunkCondition)
{
	FMassArchetypeChunk& Chunk = Chunks[ChunkInfo.ChunkIndex];
	const int32 ChunkLength = ChunkInfo.Length > 0 ? ChunkInfo.Length : (Chunk.GetNumInstances() - ChunkInfo.SubchunkStart);

	if (ChunkLength)
	{
		RunContext.SetCurrentArchetypeData(*this);
		RunContext.SetCurrentChunkSerialModificationNumber(Chunk.GetSerialModificationNumber());
		BindChunkComponentRequirements(RunContext, RequirementMapping.ChunkComponents, Chunk);

		if (!ChunkCondition || ChunkCondition(RunContext))
		{
			BindEntityRequirements(RunContext, RequirementMapping.EntityComponents, Chunk, ChunkInfo.SubchunkStart, ChunkLength);
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

		for (const FMassArchetypeFragmentConfig& ComponentConfig : ComponentConfigs)
		{
			void* FromComponentPtr = ComponentConfig.GetComponentData(ChunkToEmpty->GetRawMemory(), FromIndex);
			void* ToComponentPtr = ComponentConfig.GetComponentData(ChunkToFill->GetRawMemory(), ToIndex);

			if (UE::Mass::Core::bBitwiseRelocateComponents)
			{
				// Move all entries
				FMemory::Memcpy(ToComponentPtr, FromComponentPtr, ComponentConfig.ComponentType->GetStructureSize() * NumberOfEntitiesToMove);
			}
			else
			{
				// Destroy & initialize the component data
				ComponentConfig.ComponentType->ClearScriptStruct(ToComponentPtr, NumberOfEntitiesToMove);

				// Copy all entries
				ComponentConfig.ComponentType->CopyScriptStruct(ToComponentPtr, FromComponentPtr, NumberOfEntitiesToMove);

				// Destroy all entries
				ComponentConfig.ComponentType->DestroyStruct(FromComponentPtr, NumberOfEntitiesToMove);
			}
		}

		FMassEntityHandle* FromEntity = &ChunkToEmpty->GetEntityArrayElementRef(EntityListOffsetWithinChunk, FromIndex);
		FMassEntityHandle* ToEntity = &ChunkToFill->GetEntityArrayElementRef(EntityListOffsetWithinChunk, ToIndex);
		FMemory::Memcpy(ToEntity, FromEntity, NumberOfEntitiesToMove * sizeof(FMassEntityHandle));
		ChunkToFill->AddMultipleInstances(NumberOfEntitiesToMove);
		ChunkToEmpty->RemoveMultipleInstances(NumberOfEntitiesToMove);

		const int32 ChunkToFillIdx = ChunkToFill - &Chunks[0];
		check(ChunkToFillIdx >=0 && ChunkToFillIdx < Chunks.Num());
		const int32 AbsoluteIndex = ChunkToFillIdx * NumEntitiesPerChunk + ToIndex;

		for (int32 i =0; i < NumberOfEntitiesToMove; i++, ++ToEntity)
		{
			EntityMap.FindChecked(ToEntity->Index) = AbsoluteIndex+i;
		}
	}
}

void FMassArchetypeData::GetRequirementsComponentMapping(TConstArrayView<FMassFragmentRequirement> Requirements, FMassFragmentIndicesMapping& OutComponentIndices)
{
	OutComponentIndices.Reset(Requirements.Num());
	for (const FMassFragmentRequirement& Requirement : Requirements)
	{
		if (Requirement.RequiresBinding())
		{
			const int32* ComponentIndex = ComponentIndexMap.Find(Requirement.StructType);
			check(ComponentIndex != nullptr || Requirement.IsOptional());
			OutComponentIndices.Add(ComponentIndex ? *ComponentIndex : INDEX_NONE);
		}
	}
}

void FMassArchetypeData::GetRequirementsChunkComponentMapping(TConstArrayView<FMassFragmentRequirement> ChunkRequirements, FMassFragmentIndicesMapping& OutComponentIndices)
{
	int32 LastFoundComponentIndex = -1;
	OutComponentIndices.Reset(ChunkRequirements.Num());
	for (const FMassFragmentRequirement& Requirement : ChunkRequirements)
	{
		if (Requirement.RequiresBinding())
		{
			int32 ComponentIndex = INDEX_NONE;
			for (int32 i = LastFoundComponentIndex + 1; i < ChunkComponentTemplates.Num(); ++i)
			{
				if (ChunkComponentTemplates[i].GetScriptStruct()->IsChildOf(Requirement.StructType))
				{
					ComponentIndex = i;
					break;
				}
			}

			check(ComponentIndex != INDEX_NONE);
			OutComponentIndices.Add(ComponentIndex);
			LastFoundComponentIndex = ComponentIndex;
		}
	}
}

void FMassArchetypeData::BindEntityRequirements(FMassExecutionContext& RunContext, const FMassFragmentIndicesMapping& EntityComponentsMapping, FMassArchetypeChunk& Chunk, const int32 SubchunkStart, const int32 SubchunkLength)
{
	// auto-correcting number of entities to process in case SubchunkStart +  SubchunkLength > Chunk.GetNumInstances()
	const int32 NumEntities = SubchunkLength >= 0 ? FMath::Min(SubchunkLength, Chunk.GetNumInstances() - SubchunkStart) : Chunk.GetNumInstances();
	check(SubchunkStart >= 0 && SubchunkStart < Chunk.GetNumInstances());

	if (EntityComponentsMapping.Num() > 0)
	{
		check(RunContext.GetMutableRequirements().Num() == EntityComponentsMapping.Num());

		for (int i = 0; i < EntityComponentsMapping.Num(); ++i)
		{
			FMassExecutionContext::FFragmentView& Requirement = RunContext.ComponentViews[i];
			const int32 ComponentIndex = EntityComponentsMapping[i];

			check(ComponentIndex != INDEX_NONE || Requirement.Requirement.IsOptional());
			if (ComponentIndex != INDEX_NONE)
			{
				Requirement.ComponentView = TArrayView<FMassFragment>((FMassFragment*)GetComponentData(ComponentIndex, Chunk.GetRawMemory(), SubchunkStart), NumEntities);
			}
			else
			{
				// @todo this might not be needed
				Requirement.ComponentView = TArrayView<FMassFragment>();
			}
		}
	}
	else
	{
		// Map in the required data arrays from the current chunk to the array views
		for (FMassExecutionContext::FFragmentView& Requirement : RunContext.GetMutableRequirements())
		{
			const int32* ComponentIndex = ComponentIndexMap.Find(Requirement.Requirement.StructType);
			check(ComponentIndex != nullptr || Requirement.Requirement.IsOptional());
			if (ComponentIndex)
			{
				Requirement.ComponentView = TArrayView<FMassFragment>((FMassFragment*)GetComponentData(*ComponentIndex, Chunk.GetRawMemory(), SubchunkStart), NumEntities);
			}
			else
			{
				Requirement.ComponentView = TArrayView<FMassFragment>();
			}
		}
	}

	RunContext.EntityListView = TArrayView<FMassEntityHandle>(&Chunk.GetEntityArrayElementRef(EntityListOffsetWithinChunk, SubchunkStart), NumEntities);
}

void FMassArchetypeData::BindChunkComponentRequirements(FMassExecutionContext& RunContext, const FMassFragmentIndicesMapping& ChunkComponentsMapping, FMassArchetypeChunk& Chunk)
{
	if (ChunkComponentsMapping.Num() > 0)
	{
		check(RunContext.GetMutableChunkRequirements().Num() == ChunkComponentsMapping.Num());

		for (int i = 0; i < ChunkComponentsMapping.Num(); ++i)
		{
			FMassExecutionContext::FChunkFragmentView& ChunkRequirement = RunContext.ChunkComponents[i];
			const int32 ChunkComponentIndex = ChunkComponentsMapping[i];

			check(ChunkComponentIndex != INDEX_NONE);
			ChunkRequirement.ChunkComponentView = Chunk.GetMutableChunkComponentViewChecked(ChunkComponentIndex);
		}
	}
	else
	{
		for (FMassExecutionContext::FChunkFragmentView& ChunkRequirement : RunContext.GetMutableChunkRequirements())
		{
			FInstancedStruct* ChunkComponentInstance = Chunk.FindMutableChunkComponent(ChunkRequirement.Requirement.StructType);
			check(ChunkComponentInstance != nullptr);
			ChunkRequirement.ChunkComponentView = FStructView(*ChunkComponentInstance);
		}
	}
}

SIZE_T FMassArchetypeData::GetAllocatedSize() const
{
	return sizeof(FMassArchetypeData) +
		ComponentConfigs.GetAllocatedSize() +
		Chunks.GetAllocatedSize() +
		(Chunks.Num() * GetChunkAllocSize()) +
		EntityMap.GetAllocatedSize() +
		ComponentIndexMap.GetAllocatedSize();
}

FString FMassArchetypeData::DebugGetDescription() const
{
#if WITH_MASSENTITY_DEBUG
	TStringBuilder<256> ArchetypeDebugName;
	
	ArchetypeDebugName.Append(TEXT("<"));

	bool bNeedsComma = false;
	for (const FMassArchetypeFragmentConfig& ComponentConfig : ComponentConfigs)
	{
		if (bNeedsComma)
		{
			ArchetypeDebugName.Append(TEXT(","));
		}
		ArchetypeDebugName.Append(ComponentConfig.ComponentType->GetName());
		bNeedsComma = true;
	}

	ArchetypeDebugName.Append(TEXT(">"));
	return ArchetypeDebugName.ToString();
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
	Ar.Logf(ELogVerbosity::Log, TEXT("Components: %s"), *DebugGetDescription());
	Ar.Logf(ELogVerbosity::Log, TEXT("\tChunks: %d x %d KB = %d KB total"), Chunks.Num(), GetChunkAllocSize() / 1024, (GetChunkAllocSize()*Chunks.Num()) / 1024);
	
	int ChunkWithComponentsCount = 0;
	for (FMassArchetypeChunk& Chunk : Chunks)
	{
		ChunkWithComponentsCount += Chunk.DebugGetChunkComponentCount() > 0 ? 1 : 0;
	}
	if (ChunkWithComponentsCount)
	{
		Ar.Logf(ELogVerbosity::Log, TEXT("\tChunks with components: %d"), ChunkWithComponentsCount);
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
	for (const FMassArchetypeFragmentConfig& ComponentConfig : ComponentConfigs)
	{
		TotalBytesOfValidData += ComponentConfig.ComponentType->GetStructureSize() * NumEntitiesPerChunk;
		Ar.Logf(ELogVerbosity::Log, TEXT("\tOffset 0x%04X: %s[] (%d bytes each)"), ComponentConfig.ArrayOffsetWithinChunk, *ComponentConfig.ComponentType->GetName(), ComponentConfig.ComponentType->GetStructureSize());
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
	for (const FMassArchetypeFragmentConfig& ComponentConfig : ComponentConfigs)
	{
		void* Data = GetComponentDataForEntityChecked(ComponentConfig.ComponentType, Entity.Index);
		
		FString ComponentName = ComponentConfig.ComponentType->GetName();
		ComponentName.RemoveFromStart(InPrefix);

		FString ValueStr;
		ComponentConfig.ComponentType->ExportText(ValueStr, Data, /*Default*/nullptr, /*OwnerObject*/nullptr, EPropertyPortFlags::PPF_IncludeTransient, /*ExportRootScope*/nullptr);

		Ar.Logf(TEXT("%s: %s"), *ComponentName, *ValueStr);
	}
}

#endif // WITH_MASSENTITY_DEBUG

void FMassArchetypeData::REMOVEME_GetArrayViewForComponentInChunk(int32 ChunkIndex, const UScriptStruct* ComponentType, void*& OutChunkBase, int32& OutNumEntities)
{
	const FMassArchetypeChunk& Chunk = Chunks[ChunkIndex];
	const int32 ComponentIndex = ComponentIndexMap.FindChecked(ComponentType);

	OutChunkBase = ComponentConfigs[ComponentIndex].GetComponentData(Chunk.GetRawMemory(), 0);
	OutNumEntities = Chunk.GetNumInstances();
}

