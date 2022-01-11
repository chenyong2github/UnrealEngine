// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassEntitySubsystem.h"
#include "MassArchetypeData.h"
#include "MassCommandBuffer.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "UObject/UObjectIterator.h"

const FMassEntityHandle UMassEntitySubsystem::InvalidEntity;

//@TODO: Everything still alive leaks at shutdown
//@TODO: No re-entrance safety while running a system (e.g., preventing someone from adding/removing entities or altering archetypes, etc...)
//@TODO: Do we allow GCable types?  If so, need to implement AddReferencedObjects
//@TODO: Do a UObject stat scope around the system Run call

//////////////////////////////////////////////////////////////////////
// UMassEntitySubsystem

UMassEntitySubsystem::UMassEntitySubsystem()
	: ObserverManager(*this)
{
}

void UMassEntitySubsystem::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	Super::GetResourceSizeEx(CumulativeResourceSize);

	const SIZE_T MyExtraSize = Entities.GetAllocatedSize() + 
		EntityFreeIndexList.GetAllocatedSize() +
		DeferredCommandBuffer->GetAllocatedSize() +
		FragmentHashToArchetypeMap.GetAllocatedSize() +
		FragmentTypeToArchetypeMap.GetAllocatedSize();
	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(MyExtraSize);

	for (const auto& KVP : FragmentHashToArchetypeMap)
	{
		for (const TSharedPtr<FMassArchetypeData>& ArchetypePtr : KVP.Value)
		{
			CumulativeResourceSize.AddDedicatedSystemMemoryBytes(ArchetypePtr->GetAllocatedSize());
		}
	}
}

void UMassEntitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// Index 0 is reserved so we can treat that index as an invalid entity handle
	Entities.Add();
	SerialNumberGenerator.fetch_add(FMath::Max(1,NumReservedEntities));

	DeferredCommandBuffer = MakeShareable(new FMassCommandBuffer());

	// initialize the bitsets

	FMassFragmentBitSet Fragments;
	FMassTagBitSet Tags;
	FMassChunkFragmentBitSet ChunkFragments;

	for (TObjectIterator<UStruct> StructIt; StructIt; ++StructIt)
	{
		if (StructIt->IsChildOf(FMassFragment::StaticStruct()))
		{
			Fragments.Add(*CastChecked<UScriptStruct>(*StructIt));
		}
		else if (StructIt->IsChildOf(FMassTag::StaticStruct()))
		{
			Tags.Add(*CastChecked<UScriptStruct>(*StructIt));
		}
		else if (StructIt->IsChildOf(FMassChunkFragment::StaticStruct()))
		{
			ChunkFragments.Add(*CastChecked<UScriptStruct>(*StructIt));
		}
	}
}

void UMassEntitySubsystem::PostInitialize()
{
	// this needs to be done after all the subsystems have been initialized since some processors might want to access
	// them during processors' initialization
	ObserverManager.Initialize();
}

void UMassEntitySubsystem::Deinitialize()
{
	//@TODO: Should actually do this...
}

FArchetypeHandle UMassEntitySubsystem::CreateArchetype(TConstArrayView<const UScriptStruct*> FragmentsAngTagsList)
{
	FMassChunkFragmentBitSet ChunkFragments;
	FMassTagBitSet Tags;
	TArray<const UScriptStruct*, TInlineAllocator<16>> FragmentList;
	FragmentList.Reserve(FragmentsAngTagsList.Num());

	for (const UScriptStruct* Type : FragmentsAngTagsList)
	{
		if (Type->IsChildOf(FMassFragment::StaticStruct()))
		{
			FragmentList.Add(Type);
		}
		else if (Type->IsChildOf(FMassTag::StaticStruct()))
		{
			Tags.Add(*Type);
		}
		else if (Type->IsChildOf(FMassChunkFragment::StaticStruct()))
		{
			ChunkFragments.Add(*Type);
		}
		else
		{
			UE_LOG(LogMass, Warning, TEXT("%s: %s is not a valid fragment nor tag type. Ignoring.")
				, ANSI_TO_TCHAR(__FUNCTION__), *GetNameSafe(Type));
		}
	}

	const FMassArchetypeCompositionDescriptor Composition(FMassFragmentBitSet(FragmentList), Tags, ChunkFragments, FMassSharedFragmentBitSet());
	return CreateArchetype(Composition, FMassArchetypeSharedFragmentValues());
}

FArchetypeHandle UMassEntitySubsystem::CreateArchetype(const TSharedPtr<FMassArchetypeData>& SourceArchetype, const FMassFragmentBitSet& NewFragments)
{
	check(SourceArchetype.IsValid());
	checkf(NewFragments.IsEmpty() == false, TEXT("%s Adding an empty fragment list to an archetype is not supported."), ANSI_TO_TCHAR(__FUNCTION__));

	const FMassArchetypeCompositionDescriptor Composition(NewFragments + SourceArchetype->GetFragmentBitSet(), SourceArchetype->GetTagBitSet(), SourceArchetype->GetChunkFragmentBitSet(), SourceArchetype->GetSharedFragmentBitSet());
	return CreateArchetype(Composition, SourceArchetype->GetSharedFragmentValues());
}

FArchetypeHandle UMassEntitySubsystem::CreateArchetype(const FMassArchetypeCompositionDescriptor& Composition, const FMassArchetypeSharedFragmentValues& SharedFragmentValues)
{
	const uint32 TypeHash = Composition.CalculateHash();

	TArray<TSharedPtr<FMassArchetypeData>>& HashRow = FragmentHashToArchetypeMap.FindOrAdd(TypeHash);

	FArchetypeHandle Result;
	for (TSharedPtr<FMassArchetypeData>& Ptr : HashRow)
	{
		if (Ptr->IsEquivalent(Composition, SharedFragmentValues))
		{
			Result.DataPtr = Ptr;
			break;
		}
	}

	if (!Result.DataPtr.IsValid())
	{
		// Create a new archetype
		TSharedPtr<FMassArchetypeData> NewArchetype = MakeShareable(new FMassArchetypeData);
		NewArchetype->Initialize(Composition, SharedFragmentValues);
		HashRow.Add(NewArchetype);

		for (const FMassArchetypeFragmentConfig& FragmentConfig : NewArchetype->GetFragmentConfigs())
		{
			checkSlow(FragmentConfig.FragmentType)
			FragmentTypeToArchetypeMap.FindOrAdd(FragmentConfig.FragmentType).Add(NewArchetype);
		}

		Result.DataPtr = NewArchetype;

		++ArchetypeDataVersion;
	}

	return MoveTemp(Result);
}

FArchetypeHandle UMassEntitySubsystem::InternalCreateSiblingArchetype(const TSharedPtr<FMassArchetypeData>& SourceArchetype, const FMassTagBitSet& OverrideTags)
{
	checkSlow(SourceArchetype.IsValid());
	const FMassArchetypeData& SourceArchetypeRef = *SourceArchetype.Get();
	FMassArchetypeCompositionDescriptor NewComposition(SourceArchetypeRef.GetFragmentBitSet(), OverrideTags, SourceArchetypeRef.GetChunkFragmentBitSet(), SourceArchetypeRef.GetSharedFragmentBitSet());
	const uint32 TypeHash = NewComposition.CalculateHash();

	TArray<TSharedPtr<FMassArchetypeData>>& HashRow = FragmentHashToArchetypeMap.FindOrAdd(TypeHash);

	FArchetypeHandle Result;
	for (TSharedPtr<FMassArchetypeData>& Ptr : HashRow)
	{
		if (Ptr->IsEquivalent(NewComposition, SourceArchetypeRef.GetSharedFragmentValues()))
		{
			Result.DataPtr = Ptr;
			break;
		}
	}

	if (!Result.DataPtr.IsValid())
	{
		// Create a new archetype
		TSharedPtr<FMassArchetypeData> NewArchetype = MakeShareable(new FMassArchetypeData);
		NewArchetype->InitializeWithSibling(SourceArchetypeRef, OverrideTags);
		HashRow.Add(NewArchetype);

		for (const FMassArchetypeFragmentConfig& FragmentConfig : NewArchetype->GetFragmentConfigs())
		{
			checkSlow(FragmentConfig.FragmentType)
			FragmentTypeToArchetypeMap.FindOrAdd(FragmentConfig.FragmentType).Add(NewArchetype);
		}

		Result.DataPtr = NewArchetype;

		++ArchetypeDataVersion;
	}

	return MoveTemp(Result);
}

FArchetypeHandle UMassEntitySubsystem::GetArchetypeForEntity(FMassEntityHandle Entity) const
{
	FArchetypeHandle Result;
	if (IsEntityValid(Entity))
	{
		Result.DataPtr = Entities[Entity.Index].CurrentArchetype;
	}
	return Result;
}

void UMassEntitySubsystem::ForEachArchetypeFragmentType(const FArchetypeHandle Archetype, TFunction< void(const UScriptStruct* /*FragmentType*/)> Function)
{
	check(Archetype.DataPtr.IsValid());
	const FMassArchetypeData& ArchetypeData = *Archetype.DataPtr.Get();
	ArchetypeData.ForEachFragmentType(Function);
}

void UMassEntitySubsystem::DoEntityCompaction(const double TimeAllowed)
{
	const double TimeAllowedEnd = FPlatformTime::Seconds() + TimeAllowed;

	bool bReachedTimeLimit = false;
	for (const auto& KVP : FragmentHashToArchetypeMap)
	{
		for (const TSharedPtr<FMassArchetypeData>& ArchetypePtr : KVP.Value)
		{
			const double TimeAllowedLeft = TimeAllowedEnd - FPlatformTime::Seconds();
			bReachedTimeLimit = TimeAllowedLeft <= 0.0;
			if (bReachedTimeLimit)
			{
				break;
			}
			ArchetypePtr->CompactEntities(TimeAllowedLeft);
		}
		if (bReachedTimeLimit)
		{
			break;
		}
	}
}

FMassEntityHandle UMassEntitySubsystem::CreateEntity(const FArchetypeHandle Archetype)
{
	check(Archetype.DataPtr.IsValid());

	FMassEntityHandle Entity = ReserveEntity();
	InternalBuildEntity(Entity, Archetype);
	return Entity;
}

FMassEntityHandle UMassEntitySubsystem::CreateEntity(TConstArrayView<FInstancedStruct> FragmentInstanceList)
{
	check(FragmentInstanceList.Num() > 0);

	FArchetypeHandle Archetype = CreateArchetype(FMassArchetypeCompositionDescriptor(FragmentInstanceList, FMassTagBitSet(), FMassChunkFragmentBitSet(), FMassSharedFragmentBitSet()), FMassArchetypeSharedFragmentValues());
	check(Archetype.DataPtr.IsValid());

	FMassEntityHandle Entity = ReserveEntity();
	InternalBuildEntity(Entity, Archetype);

	FEntityData& EntityData = Entities[Entity.Index];
	EntityData.CurrentArchetype->SetFragmentsData(Entity, FragmentInstanceList);

	return Entity;
}

FMassEntityHandle UMassEntitySubsystem::ReserveEntity()
{
	// @todo: Need to add thread safety to the reservation of an entity
	FMassEntityHandle Result;
	Result.Index = (EntityFreeIndexList.Num() > 0) ? EntityFreeIndexList.Pop(/*bAllowShrinking=*/ false) : Entities.Add();
	Result.SerialNumber = SerialNumberGenerator.fetch_add(1);
	Entities[Result.Index].SerialNumber = Result.SerialNumber;

	return Result;
}

void UMassEntitySubsystem::ReleaseReservedEntity(FMassEntityHandle Entity)
{
	checkf(!IsEntityBuilt(Entity), TEXT("Entity is already built, use DestroyEntity() instead"));

	InternalReleaseEntity(Entity);
}

void UMassEntitySubsystem::BuildEntity(FMassEntityHandle Entity, FArchetypeHandle Archetype)
{
	checkf(!IsEntityBuilt(Entity), TEXT("Expecting an entity that is not already built"));
	check(Archetype.DataPtr.IsValid());

	InternalBuildEntity(Entity, Archetype);
}

void UMassEntitySubsystem::BuildEntity(FMassEntityHandle Entity, TConstArrayView<FInstancedStruct> FragmentInstanceList)
{
	check(FragmentInstanceList.Num() > 0);
	checkf(!IsEntityBuilt(Entity), TEXT("Expecting an entity that is not already built"));

	const FMassArchetypeCompositionDescriptor Composition(FragmentInstanceList, FMassTagBitSet(), FMassChunkFragmentBitSet(), FMassSharedFragmentBitSet());
	FArchetypeHandle Archetype = CreateArchetype(Composition, FMassArchetypeSharedFragmentValues());
	check(Archetype.DataPtr.IsValid());

	InternalBuildEntity(Entity, Archetype);

	FEntityData& EntityData = Entities[Entity.Index];
	EntityData.CurrentArchetype->SetFragmentsData(Entity, FragmentInstanceList);
}

TSharedRef<UMassEntitySubsystem::FEntityCreationContext> UMassEntitySubsystem::BatchCreateEntities(const FArchetypeHandle Archetype, const int32 Count, TArray<FMassEntityHandle>& OutEntities)
{
	FMassArchetypeData* ArchetypePtr = Archetype.DataPtr.Get();
	check(ArchetypePtr);
	check(Count > 0);
	
	int32 Index = OutEntities.Num();
	OutEntities.AddDefaulted(Count);

	// @todo optimize
	for (; Index < OutEntities.Num(); ++Index)
	{
		FMassEntityHandle& Result = OutEntities[Index];
		Result.Index = (EntityFreeIndexList.Num() > 0) ? EntityFreeIndexList.Pop(/*bAllowShrinking=*/ false) : Entities.Add();
		Result.SerialNumber = SerialNumberGenerator++;

		FEntityData& EntityData = Entities[Result.Index];
		EntityData.CurrentArchetype = Archetype.DataPtr;
		EntityData.SerialNumber = Result.SerialNumber;
		
		ArchetypePtr->AddEntity(Result);
	}
		
	FEntityCreationContext* CreationContext = new FEntityCreationContext(Count);
	// @todo this could probably be optimized since one would assume we're adding elements to OutEntities in order.
	// Then again, if that's the case, the sorting will be almost instant
	new (&CreationContext->ChunkCollection)FArchetypeChunkCollection(Archetype, MakeArrayView(&OutEntities[OutEntities.Num() - Count], Count), FArchetypeChunkCollection::NoDuplicates);
	if (ObserverManager.HasOnAddedObserversForFragments(ArchetypePtr->GetCompositionDescriptor().Fragments))
	{
		CreationContext->OnSpawningFinished = [this](FEntityCreationContext& Context){
			ObserverManager.OnPostEntitiesCreated(Context.ChunkCollection);
		};
	}

	return MakeShareable(CreationContext);
}

void UMassEntitySubsystem::DestroyEntity(FMassEntityHandle Entity)
{
	checkf(IsProcessing() == false, TEXT("Synchronous API function %s called during mass processing. Use asynchronous API instead."), ANSI_TO_TCHAR(__FUNCTION__));
	
	CheckIfEntityIsActive(Entity);

	FEntityData& EntityData = Entities[Entity.Index];
	FMassArchetypeData* Archetype = EntityData.CurrentArchetype.Get();
	check(Archetype);
	Archetype->RemoveEntity(Entity);

	InternalReleaseEntity(Entity);

	if (Archetype->GetNumEntities() == 0)
	{
		//@TODO: Drop our reference to the archetype?
	}
}

void UMassEntitySubsystem::BatchDestroyEntities(TConstArrayView<FMassEntityHandle> InEntities)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("BatchDestroyEntities");

	checkf(IsProcessing() == false, TEXT("Synchronous API function %s called during mass processing. Use asynchronous API instead."), ANSI_TO_TCHAR(__FUNCTION__));
	
	EntityFreeIndexList.Reserve(EntityFreeIndexList.Num() + InEntities.Num());

	// @todo optimize, we can make savings by implementing Archetype->RemoveEntities()
	for (const FMassEntityHandle Entity : InEntities)
	{
		if (Entities.IsValidIndex(Entity.Index) == false)
		{
			continue;
		}

		FEntityData& EntityData = Entities[Entity.Index];
		if (EntityData.SerialNumber != Entity.SerialNumber)
		{
			continue;
		}

		FMassArchetypeData* Archetype = EntityData.CurrentArchetype.Get();
		check(Archetype);
		Archetype->RemoveEntity(Entity);

		EntityData.Reset();
		EntityFreeIndexList.Add(Entity.Index);
	}
}

void UMassEntitySubsystem::BatchDestroyEntityChunks(const FArchetypeChunkCollection& Chunks)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("BatchDestroyEntityChunks");

	checkf(IsProcessing() == false, TEXT("Synchronous API function %s called during mass processing. Use asynchronous API instead."), ANSI_TO_TCHAR(__FUNCTION__));

	FMassProcessingContext ProcessingContext(*this, /*TimeDelta=*/0.0f);
	ProcessingContext.bFlushCommandBuffer = false;
	ProcessingContext.CommandBuffer = MakeShareable(new FMassCommandBuffer());
	ObserverManager.OnPreEntitiesDestroyed(ProcessingContext, Chunks);

	check(Chunks.GetArchetype().IsValid());
	TArray<FMassEntityHandle> EntitiesRemoved;
	Chunks.GetArchetype().DataPtr->BatchDestroyEntityChunks(Chunks, EntitiesRemoved);
	
	EntityFreeIndexList.Reserve(EntityFreeIndexList.Num() + EntitiesRemoved.Num());

	for (const FMassEntityHandle& Entity : EntitiesRemoved)
	{
		check(Entities.IsValidIndex(Entity.Index));

		FEntityData& EntityData = Entities[Entity.Index];
		if (EntityData.SerialNumber == Entity.SerialNumber)
		{
			EntityData.Reset();
			EntityFreeIndexList.Add(Entity.Index);
		}
	}

	ensure(ProcessingContext.CommandBuffer->HasPendingCommands() == false);
}

void UMassEntitySubsystem::AddFragmentToEntity(FMassEntityHandle Entity, const UScriptStruct* FragmentType)
{
	checkf(FragmentType, TEXT("Null fragment type passed in to %s"), ANSI_TO_TCHAR(__FUNCTION__));
	checkf(IsProcessing() == false, TEXT("Synchronous API function %s called during mass processing. Use asynchronous API instead."), ANSI_TO_TCHAR(__FUNCTION__));

	CheckIfEntityIsActive(Entity);

	InternalAddFragmentListToEntityChecked(Entity, FMassFragmentBitSet(*FragmentType));
}

void UMassEntitySubsystem::AddFragmentListToEntity(FMassEntityHandle Entity, TConstArrayView<const UScriptStruct*> FragmentList)
{
	CheckIfEntityIsActive(Entity);

	InternalAddFragmentListToEntityChecked(Entity, FMassFragmentBitSet(FragmentList));
}

void UMassEntitySubsystem::AddCompositionToEntity_GetDelta(FMassEntityHandle Entity, FMassArchetypeCompositionDescriptor& InDescriptor)
{
	CheckIfEntityIsActive(Entity);

	FEntityData& EntityData = Entities[Entity.Index];
	FMassArchetypeData* OldArchetype = EntityData.CurrentArchetype.Get();
	check(OldArchetype);

	InDescriptor.Fragments -= OldArchetype->GetCompositionDescriptor().Fragments;
	InDescriptor.Tags -= OldArchetype->GetCompositionDescriptor().Tags;

	ensureMsgf(InDescriptor.ChunkFragments.IsEmpty(), TEXT("Adding new chunk fragments is not supported"));

	if (InDescriptor.IsEmpty() == false)
	{
		FMassArchetypeCompositionDescriptor NewDescriptor = OldArchetype->GetCompositionDescriptor();
		NewDescriptor.Fragments += InDescriptor.Fragments;
		NewDescriptor.Tags+= InDescriptor.Tags;

		const FArchetypeHandle NewArchetypeHandle = CreateArchetype(NewDescriptor, OldArchetype->GetSharedFragmentValues());

		if (ensure(NewArchetypeHandle.DataPtr != EntityData.CurrentArchetype))
		{
			// Move the entity over
			FMassArchetypeData* NewArchetype = NewArchetypeHandle.DataPtr.Get();
			check(NewArchetype);
			EntityData.CurrentArchetype->MoveEntityToAnotherArchetype(Entity, *NewArchetype);
			EntityData.CurrentArchetype = NewArchetypeHandle.DataPtr;

			ObserverManager.OnPostCompositionAdded(Entity, InDescriptor);
		}
	}
}

void UMassEntitySubsystem::RemoveCompositionFromEntity(FMassEntityHandle Entity, const FMassArchetypeCompositionDescriptor& InDescriptor)
{
	CheckIfEntityIsActive(Entity);

	if(InDescriptor.IsEmpty() == false)
	{
		FEntityData& EntityData = Entities[Entity.Index];
		FMassArchetypeData* OldArchetype = EntityData.CurrentArchetype.Get();
		check(OldArchetype);

		FMassArchetypeCompositionDescriptor NewDescriptor = OldArchetype->GetCompositionDescriptor();
		NewDescriptor.Fragments -= InDescriptor.Fragments;
		NewDescriptor.Tags -= InDescriptor.Tags;

		ensureMsgf(InDescriptor.ChunkFragments.IsEmpty(), TEXT("Removing chunk fragments is not supported"));
		ensureMsgf(InDescriptor.SharedFragments.IsEmpty(), TEXT("Removing shared fragments is not supported"));

		if (NewDescriptor.IsEquivalent(OldArchetype->GetCompositionDescriptor()) == false)
		{
			ensureMsgf(OldArchetype->GetCompositionDescriptor().HasAll(InDescriptor), TEXT("Some of the elements being removed are already missing from entity\'s composition."));
			ObserverManager.OnPreCompositionRemoved(Entity, InDescriptor);

			const FArchetypeHandle NewArchetypeHandle = CreateArchetype(NewDescriptor, OldArchetype->GetSharedFragmentValues());

			if (ensure(NewArchetypeHandle.DataPtr != EntityData.CurrentArchetype))
			{
				// Move the entity over
				FMassArchetypeData* NewArchetype = NewArchetypeHandle.DataPtr.Get();
				check(NewArchetype);
				EntityData.CurrentArchetype->MoveEntityToAnotherArchetype(Entity, *NewArchetype);
				EntityData.CurrentArchetype = NewArchetypeHandle.DataPtr;
			}
		}
	}
}

const FMassArchetypeCompositionDescriptor& UMassEntitySubsystem::GetArchetypeComposition(const FArchetypeHandle& ArchetypeHandle) const
{
	return ArchetypeHandle.DataPtr->GetCompositionDescriptor();
}

void UMassEntitySubsystem::InternalBuildEntity(FMassEntityHandle Entity, const FArchetypeHandle Archetype)
{
	FEntityData& EntityData = Entities[Entity.Index];
	EntityData.CurrentArchetype = Archetype.DataPtr;
	EntityData.CurrentArchetype->AddEntity(Entity);
}

void UMassEntitySubsystem::InternalReleaseEntity(FMassEntityHandle Entity)
{
	FEntityData& EntityData = Entities[Entity.Index];
	EntityData.Reset();
	EntityFreeIndexList.Add(Entity.Index);
}

void UMassEntitySubsystem::InternalAddFragmentListToEntityChecked(FMassEntityHandle Entity, const FMassFragmentBitSet& InFragments)
{
	FEntityData& EntityData = Entities[Entity.Index];
	FMassArchetypeData* OldArchetype = EntityData.CurrentArchetype.Get();
	check(OldArchetype);

	UE_CLOG(OldArchetype->GetFragmentBitSet().HasAny(InFragments), LogMass, Log
		, TEXT("Trying to add a new fragment type to an entity, but it already has some of them. (%s)")
		, *InFragments.GetOverlap(OldArchetype->GetFragmentBitSet()).DebugGetStringDesc());

	const FMassFragmentBitSet NewFragments = InFragments - OldArchetype->GetFragmentBitSet();
	if (NewFragments.IsEmpty() == false)
	{
		InternalAddFragmentListToEntity(Entity, NewFragments);
	}
}

void UMassEntitySubsystem::InternalAddFragmentListToEntity(FMassEntityHandle Entity, const FMassFragmentBitSet& NewFragments)
{
	checkf(NewFragments.IsEmpty() == false, TEXT("%s is intended for internal calls with non empty NewFragments parameter"), ANSI_TO_TCHAR(__FUNCTION__));
	check(Entities.IsValidIndex(Entity.Index));
	FEntityData& EntityData = Entities[Entity.Index];
	check(EntityData.CurrentArchetype.IsValid());

	// fetch or create the new archetype
	const FArchetypeHandle NewArchetypeHandle = CreateArchetype(EntityData.CurrentArchetype, NewFragments);

	if (NewArchetypeHandle.DataPtr != EntityData.CurrentArchetype)
	{
		// Move the entity over
		FMassArchetypeData* NewArchetype = NewArchetypeHandle.DataPtr.Get();
		check(NewArchetype);
		EntityData.CurrentArchetype->MoveEntityToAnotherArchetype(Entity, *NewArchetype);
		EntityData.CurrentArchetype = NewArchetypeHandle.DataPtr;
	}
}

void UMassEntitySubsystem::AddFragmentInstanceListToEntity(FMassEntityHandle Entity, TConstArrayView<FInstancedStruct> FragmentInstanceList)
{
	checkf(IsProcessing() == false, TEXT("Synchronous API function %s called during mass processing. Use asynchronous API instead."), ANSI_TO_TCHAR(__FUNCTION__));

	CheckIfEntityIsActive(Entity);
	checkf(FragmentInstanceList.Num() > 0, TEXT("Need to specify at least one fragment instances for this operation"));

	InternalAddFragmentListToEntityChecked(Entity, FMassFragmentBitSet(FragmentInstanceList));

	FEntityData& EntityData = Entities[Entity.Index];
	EntityData.CurrentArchetype->SetFragmentsData(Entity, FragmentInstanceList);
}

void UMassEntitySubsystem::RemoveFragmentFromEntity(FMassEntityHandle Entity, const UScriptStruct* FragmentType)
{
	RemoveFragmentListFromEntity(Entity, MakeArrayView(&FragmentType, 1));
}

void UMassEntitySubsystem::RemoveFragmentListFromEntity(FMassEntityHandle Entity, TConstArrayView<const UScriptStruct*> FragmentList)
{
	checkf(IsProcessing() == false, TEXT("Synchronous API function %s called during mass processing. Use asynchronous API instead."), ANSI_TO_TCHAR(__FUNCTION__));

	CheckIfEntityIsActive(Entity);
	
	FEntityData& EntityData = Entities[Entity.Index];
	FMassArchetypeData* OldArchetype = EntityData.CurrentArchetype.Get();
	check(OldArchetype);

	const FMassFragmentBitSet FragmentsToRemove(FragmentList);

	if (ensureMsgf(OldArchetype->GetFragmentBitSet().HasAny(FragmentsToRemove), TEXT("Trying to remove a list of fragments from an entity but none of the fragments given was found.")))
	{
		// If all the fragments got removed this will result in fetching of the empty archetype
		const FMassArchetypeCompositionDescriptor NewComposition(OldArchetype->GetFragmentBitSet() - FragmentsToRemove, OldArchetype->GetTagBitSet(), OldArchetype->GetChunkFragmentBitSet(), OldArchetype->GetSharedFragmentBitSet());
		const FArchetypeHandle NewArchetypeHandle = CreateArchetype(NewComposition, OldArchetype->GetSharedFragmentValues());

		// Move the entity over
		FMassArchetypeData* NewArchetype = NewArchetypeHandle.DataPtr.Get();
		check(NewArchetype);
		OldArchetype->MoveEntityToAnotherArchetype(Entity, *NewArchetype);
		EntityData.CurrentArchetype = NewArchetypeHandle.DataPtr;
	}
}

void UMassEntitySubsystem::SwapTagsForEntity(FMassEntityHandle Entity, const UScriptStruct* OldTagType, const UScriptStruct* NewTagType)
{
	checkf(IsProcessing() == false, TEXT("Synchronous API function %s called during mass processing. Use asynchronous API instead."), ANSI_TO_TCHAR(__FUNCTION__));

	CheckIfEntityIsActive(Entity);

	checkf((OldTagType != nullptr) && OldTagType->IsChildOf(FMassTag::StaticStruct()), TEXT("%s works only with tags while '%s' is not one."), ANSI_TO_TCHAR(__FUNCTION__), *GetPathNameSafe(OldTagType));
	checkf((NewTagType != nullptr) && NewTagType->IsChildOf(FMassTag::StaticStruct()), TEXT("%s works only with tags while '%s' is not one."), ANSI_TO_TCHAR(__FUNCTION__), *GetPathNameSafe(NewTagType));

	FEntityData& EntityData = Entities[Entity.Index];
	FMassArchetypeData* CurrentArchetype = EntityData.CurrentArchetype.Get();
	check(CurrentArchetype);

	FMassTagBitSet NewTagBitSet = CurrentArchetype->GetTagBitSet();
	NewTagBitSet.Remove(*OldTagType);
	NewTagBitSet.Add(*NewTagType);
	
	if (NewTagBitSet != CurrentArchetype->GetTagBitSet())
	{
		FArchetypeHandle NewArchetypeHandle = InternalCreateSiblingArchetype(EntityData.CurrentArchetype, NewTagBitSet);
		checkSlow(NewArchetypeHandle.IsValid());

		// Move the entity over
		EntityData.CurrentArchetype->MoveEntityToAnotherArchetype(Entity, *NewArchetypeHandle.DataPtr.Get());
		EntityData.CurrentArchetype = NewArchetypeHandle.DataPtr;
	}
}

void UMassEntitySubsystem::AddTagToEntity(FMassEntityHandle Entity, const UScriptStruct* TagType)
{
	checkf((TagType != nullptr) && TagType->IsChildOf(FMassTag::StaticStruct()), TEXT("%s works only with tags while '%s' is not one."), ANSI_TO_TCHAR(__FUNCTION__), *GetPathNameSafe(TagType));

	CheckIfEntityIsActive(Entity);

	FEntityData& EntityData = Entities[Entity.Index];
	FMassArchetypeData* CurrentArchetype = EntityData.CurrentArchetype.Get();
	check(CurrentArchetype);

	if (CurrentArchetype->HasTagType(TagType) == false)
	{
		//FMassTagBitSet NewTags = CurrentArchetype->GetTagBitSet() - *TagType;
		FMassTagBitSet NewTags = CurrentArchetype->GetTagBitSet();
		NewTags.Add(*TagType);
		FArchetypeHandle NewArchetypeHandle = InternalCreateSiblingArchetype(EntityData.CurrentArchetype, NewTags);
		checkSlow(NewArchetypeHandle.IsValid());

		// Move the entity over
		EntityData.CurrentArchetype->MoveEntityToAnotherArchetype(Entity, *NewArchetypeHandle.DataPtr.Get());
		EntityData.CurrentArchetype = NewArchetypeHandle.DataPtr;
	}
}
	
void UMassEntitySubsystem::RemoveTagFromEntity(FMassEntityHandle Entity, const UScriptStruct* TagType)
{
	checkf((TagType != nullptr) && TagType->IsChildOf(FMassTag::StaticStruct()), TEXT("%s works only with tags while '%s' is not one."), ANSI_TO_TCHAR(__FUNCTION__), *GetPathNameSafe(TagType));

	CheckIfEntityIsActive(Entity);

	FEntityData& EntityData = Entities[Entity.Index];
	FMassArchetypeData* CurrentArchetype = EntityData.CurrentArchetype.Get();
	check(CurrentArchetype);

	if (CurrentArchetype->HasTagType(TagType))
	{
		// CurrentArchetype->GetTagBitSet() -  *TagType
		FMassTagBitSet NewTags = CurrentArchetype->GetTagBitSet();
		NewTags.Remove(*TagType);
		FArchetypeHandle NewArchetypeHandle = InternalCreateSiblingArchetype(EntityData.CurrentArchetype, NewTags);
		checkSlow(NewArchetypeHandle.IsValid());

		// Move the entity over
		EntityData.CurrentArchetype->MoveEntityToAnotherArchetype(Entity, *NewArchetypeHandle.DataPtr.Get());
		EntityData.CurrentArchetype = NewArchetypeHandle.DataPtr;
	}
}


void UMassEntitySubsystem::MoveEntityToAnotherArchetype(FMassEntityHandle Entity, FArchetypeHandle NewArchetypeHandle)
{
	CheckIfEntityIsActive(Entity);

	FMassArchetypeData* NewArchetype = NewArchetypeHandle.DataPtr.Get();
	check(NewArchetype);

	// Move the entity over
	FEntityData& EntityData = Entities[Entity.Index];
	EntityData.CurrentArchetype->MoveEntityToAnotherArchetype(Entity, *NewArchetype);
	EntityData.CurrentArchetype = NewArchetypeHandle.DataPtr;
}

void UMassEntitySubsystem::SetEntityFragmentsValues(FMassEntityHandle Entity, TArrayView<const FInstancedStruct> FragmentInstanceList)
{
	CheckIfEntityIsActive(Entity);

	FEntityData& EntityData = Entities[Entity.Index];
	EntityData.CurrentArchetype->SetFragmentsData(Entity, FragmentInstanceList);
}

void UMassEntitySubsystem::BatchSetEntityFragmentsValues(const FArchetypeChunkCollection& SparseEntities, TArrayView<const FInstancedStruct> FragmentInstanceList)
{
	FMassArchetypeData* Archetype = SparseEntities.GetArchetype().DataPtr.Get();
	check(Archetype);

	for (const FInstancedStruct& FragmentTemplate : FragmentInstanceList)
	{
		Archetype->SetFragmentData(SparseEntities, FragmentTemplate);
	}
}

void* UMassEntitySubsystem::InternalGetFragmentDataChecked(FMassEntityHandle Entity, const UScriptStruct* FragmentType) const
{
	CheckIfEntityIsActive(Entity);

	checkf((FragmentType != nullptr) && FragmentType->IsChildOf(FMassFragment::StaticStruct()), TEXT("InternalGetFragmentDataChecked called with an invalid fragment type '%s'"), *GetPathNameSafe(FragmentType));
	const FEntityData& EntityData = Entities[Entity.Index];
	return EntityData.CurrentArchetype->GetFragmentDataForEntityChecked(FragmentType, Entity.Index);
}

void* UMassEntitySubsystem::InternalGetFragmentDataPtr(FMassEntityHandle Entity, const UScriptStruct* FragmentType) const
{
	CheckIfEntityIsActive(Entity);
	checkf((FragmentType != nullptr) && FragmentType->IsChildOf(FMassFragment::StaticStruct()), TEXT("InternalGetFragmentData called with an invalid fragment type '%s'"), *GetPathNameSafe(FragmentType));
	const FEntityData& EntityData = Entities[Entity.Index];
	return EntityData.CurrentArchetype->GetFragmentDataForEntity(FragmentType, Entity.Index);
}

bool UMassEntitySubsystem::IsEntityValid(FMassEntityHandle Entity) const
{
	return (Entity.Index > 0) && Entities.IsValidIndex(Entity.Index) && (Entities[Entity.Index].SerialNumber == Entity.SerialNumber);
}

bool UMassEntitySubsystem::IsEntityBuilt(FMassEntityHandle Entity) const
{
	CheckIfEntityIsValid(Entity);
	return Entities[Entity.Index].CurrentArchetype.IsValid();
}

void UMassEntitySubsystem::CheckIfEntityIsValid(FMassEntityHandle Entity) const
{
	checkf(IsEntityValid(Entity), TEXT("Invalid entity (ID: %d, SN:%d, %s)"), Entity.Index, Entity.SerialNumber,
		   (Entity.Index == 0) ? TEXT("was never initialized") : TEXT("already destroyed"));
}

void UMassEntitySubsystem::CheckIfEntityIsActive(FMassEntityHandle Entity) const
{
	checkf(IsEntityBuilt(Entity), TEXT("Entity not yet created(ID: %d, SN:%d)"));
}

void UMassEntitySubsystem::GetValidArchetypes(const FMassEntityQuery& Query, TArray<FArchetypeHandle>& OutValidArchetypes)
{
	//@TODO: Not optimized yet, but we call this rarely now, so not a big deal.

	// First get set of all archetypes that contain *any* fragment
	TSet<TSharedPtr<FMassArchetypeData>> AnyArchetypes;
	for (const FMassFragmentRequirement& Requirement : Query.GetRequirements())
	{
		check(Requirement.StructType);
		if (Requirement.Presence != EMassFragmentPresence::None)
		{
			if (TArray<TSharedPtr<FMassArchetypeData>>* pData = FragmentTypeToArchetypeMap.Find(Requirement.StructType))
			{
				AnyArchetypes.Append(*pData);
			}
		}
	}

	// Then verify that they contain *all* required fragments
	for (TSharedPtr<FMassArchetypeData>& ArchetypePtr : AnyArchetypes)
	{
		FMassArchetypeData& Archetype = *(ArchetypePtr.Get());

		if (Archetype.GetTagBitSet().HasAll(Query.GetRequiredAllTags()) == false)
		{
			// missing some required tags, skip.
#if WITH_MASSENTITY_DEBUG
			const FMassTagBitSet UnsatisfiedTags = Query.GetRequiredAllTags() - Archetype.GetTagBitSet();
			FStringOutputDevice Description;
			UnsatisfiedTags.DebugGetStringDesc(Description);
			UE_LOG(LogMass, VeryVerbose, TEXT("Archetype did not match due to missing tags: %s")
				, *Description);
#endif // WITH_MASSENTITY_DEBUG
			continue;
		}

		if (Archetype.GetTagBitSet().HasNone(Query.GetRequiredNoneTags()) == false)
		{
			// has some tags required to be absent
#if WITH_MASSENTITY_DEBUG
			const FMassTagBitSet UnwantedTags = Query.GetRequiredAllTags().GetOverlap(Archetype.GetTagBitSet());
			FStringOutputDevice Description;
			UnwantedTags.DebugGetStringDesc(Description);
			UE_LOG(LogMass, VeryVerbose, TEXT("Archetype has tags required absent: %s")
				, *Description);
#endif // WITH_MASSENTITY_DEBUG
			continue;
		}

		if (Query.GetRequiredAnyTags().IsEmpty() == false 
			&& Archetype.GetTagBitSet().HasAny(Query.GetRequiredAnyTags()) == false)
		{
#if WITH_MASSENTITY_DEBUG
			FStringOutputDevice Description;
			Query.GetRequiredAnyTags().DebugGetStringDesc(Description);
			UE_LOG(LogMass, VeryVerbose, TEXT("Archetype did not match due to missing \'any\' tags: %s")
				, *Description);
#endif // WITH_MASSENTITY_DEBUG
			continue;
		}
		
		if (Archetype.GetFragmentBitSet().HasAll(Query.GetRequiredAllFragments()) == false)
		{
			// missing some required fragments, skip.
#if WITH_MASSENTITY_DEBUG
			const FMassFragmentBitSet UnsatisfiedFragments = Query.GetRequiredAllFragments() - Archetype.GetFragmentBitSet();
			FStringOutputDevice Description;
			UnsatisfiedFragments.DebugGetStringDesc(Description);
			UE_LOG(LogMass, VeryVerbose, TEXT("Archetype did not match due to missing Fragments: %s")
				, *Description);
#endif // WITH_MASSENTITY_DEBUG
			continue;
		}

		if (Archetype.GetFragmentBitSet().HasNone(Query.GetRequiredNoneFragments()) == false)
		{
			// has some Fragments required to be absent
#if WITH_MASSENTITY_DEBUG
			const FMassFragmentBitSet UnwantedFragments = Query.GetRequiredAllFragments().GetOverlap(Archetype.GetFragmentBitSet());
			FStringOutputDevice Description;
			UnwantedFragments.DebugGetStringDesc(Description);
			UE_LOG(LogMass, VeryVerbose, TEXT("Archetype has Fragments required absent: %s")
				, *Description);
#endif // WITH_MASSENTITY_DEBUG
			continue;
		}

		if (Query.GetRequiredAnyFragments().IsEmpty() == false 
			&& Archetype.GetFragmentBitSet().HasAny(Query.GetRequiredAnyFragments()) == false)
		{
#if WITH_MASSENTITY_DEBUG
			FStringOutputDevice Description;
			Query.GetRequiredAnyFragments().DebugGetStringDesc(Description);
			UE_LOG(LogMass, VeryVerbose, TEXT("Archetype did not match due to missing \'any\' fragments: %s")
				, *Description);
#endif // WITH_MASSENTITY_DEBUG
			continue;
		}

		if (Archetype.GetChunkFragmentBitSet().HasAll(Query.GetRequiredAllChunkFragments()) == false)
		{
			// missing some required fragments, skip.
#if WITH_MASSENTITY_DEBUG
			const FMassChunkFragmentBitSet UnsatisfiedFragments = Query.GetRequiredAllChunkFragments() - Archetype.GetChunkFragmentBitSet();
			FStringOutputDevice Description;
			UnsatisfiedFragments.DebugGetStringDesc(Description);
			UE_LOG(LogMass, VeryVerbose, TEXT("Archetype did not match due to missing Chunk Fragments: %s")
				, *Description);
#endif // WITH_MASSENTITY_DEBUG
			continue;
		}

		if (Archetype.GetChunkFragmentBitSet().HasNone(Query.GetRequiredNoneChunkFragments()) == false)
		{
			// has some Fragments required to be absent
#if WITH_MASSENTITY_DEBUG
			const FMassChunkFragmentBitSet UnwantedFragments = Query.GetRequiredNoneChunkFragments().GetOverlap(Archetype.GetChunkFragmentBitSet());
			FStringOutputDevice Description;
			UnwantedFragments.DebugGetStringDesc(Description);
			UE_LOG(LogMass, VeryVerbose, TEXT("Archetype has Chunk Fragments required absent: %s")
				, *Description);
#endif // WITH_MASSENTITY_DEBUG
			continue;
		}

		if (Archetype.GetSharedFragmentBitSet().HasAll(Query.GetRequiredAllSharedFragments()) == false)
		{
			// missing some required fragments, skip.
#if WITH_MASSENTITY_DEBUG
			const FMassSharedFragmentBitSet UnsatisfiedFragments = Query.GetRequiredAllSharedFragments() - Archetype.GetSharedFragmentBitSet();
			FStringOutputDevice Description;
			UnsatisfiedFragments.DebugGetStringDesc(Description);
			UE_LOG(LogMass, VeryVerbose, TEXT("Archetype did not match due to missing Shared Fragments: %s")
				, *Description);
#endif // WITH_MASSENTITY_DEBUG
			continue;
		}

		if (Archetype.GetSharedFragmentBitSet().HasNone(Query.GetRequiredNoneSharedFragments()) == false)
		{
			// has some Fragments required to be absent
#if WITH_MASSENTITY_DEBUG
			const FMassSharedFragmentBitSet UnwantedFragments = Query.GetRequiredNoneSharedFragments().GetOverlap(Archetype.GetSharedFragmentBitSet());
			FStringOutputDevice Description;
			UnwantedFragments.DebugGetStringDesc(Description);
			UE_LOG(LogMass, VeryVerbose, TEXT("Archetype has Shared Fragments required absent: %s")
				, *Description);
#endif // WITH_MASSENTITY_DEBUG
			continue;
		}


		OutValidArchetypes.Add(ArchetypePtr);
	}
}

FMassExecutionContext UMassEntitySubsystem::CreateExecutionContext(const float DeltaSeconds) const
{
	FMassExecutionContext ExecutionContext(DeltaSeconds);
	ExecutionContext.SetDeferredCommandBuffer(DeferredCommandBuffer);
	return MoveTemp(ExecutionContext);
}

#if WITH_MASSENTITY_DEBUG
void UMassEntitySubsystem::DebugPrintArchetypes(FOutputDevice& Ar, const bool bIncludeEmpty) const
{
	Ar.Logf(ELogVerbosity::Log, TEXT("Listing archetypes contained in %s"), *GetPathNameSafe(this));

	int32 NumBuckets = 0;
	int32 NumArchetypes = 0;
	int32 LongestArchetypeBucket = 0;
	for (const auto& KVP : FragmentHashToArchetypeMap)
	{
		for (const TSharedPtr<FMassArchetypeData>& ArchetypePtr : KVP.Value)
		{
			if (ArchetypePtr.IsValid() && (bIncludeEmpty == true || ArchetypePtr->GetChunkCount() > 0))
			{
				ArchetypePtr->DebugPrintArchetype(Ar);
			}
		}

		const int32 NumArchetypesInBucket = KVP.Value.Num();
		LongestArchetypeBucket = FMath::Max(LongestArchetypeBucket, NumArchetypesInBucket);
		NumArchetypes += NumArchetypesInBucket;
		++NumBuckets;
	}

	Ar.Logf(ELogVerbosity::Log, TEXT("FragmentHashToArchetypeMap: %d archetypes across %d buckets, longest bucket is %d"),
		NumArchetypes, NumBuckets, LongestArchetypeBucket);
}

void UMassEntitySubsystem::DebugPrintEntity(int32 Index, FOutputDevice& Ar, const TCHAR* InPrefix) const
{
	if (Index >= Entities.Num())
	{
		Ar.Logf(ELogVerbosity::Log, TEXT("Unable to list fragments values for out of range index in %s"), *GetPathNameSafe(this));
		return;
	}

	const FEntityData& EntityData = Entities[Index];
	if (!EntityData.IsValid())
	{
		Ar.Logf(ELogVerbosity::Log, TEXT("Unable to list fragments values for invalid entity in %s"), *GetPathNameSafe(this));
	}

	FMassEntityHandle Entity;
	Entity.Index = Index;
	Entity.SerialNumber = EntityData.SerialNumber;
	DebugPrintEntity(Entity, Ar, InPrefix);
}

void UMassEntitySubsystem::DebugPrintEntity(FMassEntityHandle Entity, FOutputDevice& Ar, const TCHAR* InPrefix) const
{
	if (!IsEntityActive(Entity))
	{
		Ar.Logf(ELogVerbosity::Log, TEXT("Unable to list fragments values for invalid entity in %s"), *GetPathNameSafe(this));
	}

	Ar.Logf(ELogVerbosity::Log, TEXT("Listing fragments values for Entity[%s] in %s"), *Entity.DebugGetDescription(), *GetPathNameSafe(this));

	const FEntityData& EntityData = Entities[Entity.Index];
	FMassArchetypeData* Archetype = EntityData.CurrentArchetype.Get();
	if (Archetype == nullptr)
	{
		Ar.Logf(ELogVerbosity::Log, TEXT("Unable to list fragments values for invalid entity in %s"), *GetPathNameSafe(this));
	}
	else
	{
		Archetype->DebugPrintEntity(Entity, Ar, InPrefix);
	}
}

void UMassEntitySubsystem::DebugGetStringDesc(const FArchetypeHandle& Archetype, FOutputDevice& Ar)
{
	Ar.Logf(TEXT("%s"), Archetype.IsValid() ? *Archetype.DataPtr->DebugGetDescription() : TEXT("INVALID"));
}

void UMassEntitySubsystem::DebugGetArchetypesStringDetails(FOutputDevice& Ar, const bool bIncludeEmpty)
{
#if WITH_MASSENTITY_DEBUG
	Ar.SetAutoEmitLineTerminator(true);
	for (auto Pair : FragmentHashToArchetypeMap)
	{
		Ar.Logf(ELogVerbosity::Log, TEXT("\n-----------------------------------\nHash: %u"), Pair.Key);
		for (TSharedPtr<FMassArchetypeData> Archetype : Pair.Value)
		{
			if (Archetype.IsValid() && (bIncludeEmpty == true || Archetype->GetChunkCount() > 0))
			{
				Archetype->DebugPrintArchetype(Ar);
				Ar.Logf(ELogVerbosity::Log, TEXT("+++++++++++++++++++++++++\n"));
			}
		}
	}
#endif // WITH_MASSENTITY_DEBUG
}

void UMassEntitySubsystem::DebugGetArchetypeFragmentTypes(const FArchetypeHandle& Archetype, TArray<const UScriptStruct*>& InOutFragmentList) const
{
	if (Archetype.DataPtr.IsValid())
	{
		Archetype.DataPtr->GetCompositionDescriptor().Fragments.DebugGetStructTypes(InOutFragmentList);
	}
}

int32 UMassEntitySubsystem::DebugGetArchetypeEntitiesCount(const FArchetypeHandle& Archetype) const
{
	return Archetype.DataPtr.IsValid() ? Archetype.DataPtr->GetNumEntities() : 0;
}

int32 UMassEntitySubsystem::DebugGetArchetypeEntitiesCountPerChunk(const FArchetypeHandle& Archetype) const
{
	return Archetype.DataPtr.IsValid() ? Archetype.DataPtr->GetNumEntitiesPerChunk() : 0;
}

void UMassEntitySubsystem::DebugRemoveAllEntities()
{
	for (int EntityIndex = NumReservedEntities; EntityIndex < Entities.Num(); ++EntityIndex)
	{
		FEntityData& EntityData = Entities[EntityIndex];
		if (EntityData.IsValid() == false)
		{
			// already dead
			continue;
		}
		const TSharedPtr<FMassArchetypeData>& Archetype = EntityData.CurrentArchetype;
		FMassEntityHandle Entity;
		Entity.Index = EntityIndex;
		Entity.SerialNumber = EntityData.SerialNumber;
		Archetype->RemoveEntity(Entity);

		EntityData.Reset();
		EntityFreeIndexList.Add(EntityIndex);
	}
}

void UMassEntitySubsystem::DebugGetArchetypeStrings(const FArchetypeHandle& Archetype, TArray<FName>& OutFragmentNames, TArray<FName>& OutTagNames)
{
	if (Archetype.IsValid() == false)
	{
		return;
	}

	FMassArchetypeData& ArchetypeRef = *Archetype.DataPtr.Get();
	
	OutFragmentNames.Reserve(ArchetypeRef.GetFragmentConfigs().Num());
	for (const FMassArchetypeFragmentConfig& FragmentConfig : ArchetypeRef.GetFragmentConfigs())
	{
		checkSlow(FragmentConfig.FragmentType);
		OutFragmentNames.Add(FragmentConfig.FragmentType->GetFName());
	}

	ArchetypeRef.GetTagBitSet().DebugGetIndividualNames(OutTagNames);
}

//////////////////////////////////////////////////////////////////////
// Debug commands

FAutoConsoleCommandWithWorldArgsAndOutputDevice GPrintArchetypesCmd(
	TEXT("EntityManager.PrintArchetypes"),
	TEXT("Prints information about all archetypes in the current world"),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Params, UWorld* World, FOutputDevice& Ar)
{
	if (UMassEntitySubsystem* EntitySystem = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr)
	{
		EntitySystem->DebugPrintArchetypes(Ar);
	}
	else
	{
		Ar.Logf(ELogVerbosity::Error, TEXT("Failed to find Entity Subsystem for world %s"), *GetPathNameSafe(World));
	}
}));
#endif // WITH_MASSENTITY_DEBUG

//////////////////////////////////////////////////////////////////////
// FMassExecutionContext

void FMassExecutionContext::FlushDeferred(UMassEntitySubsystem& EntitySystem) const
{
	if (bFlushDeferredCommands && DeferredCommandBuffer)
	{
		DeferredCommandBuffer->ReplayBufferAgainstSystem(&EntitySystem);
	}
}

void FMassExecutionContext::ClearExecutionData()
{
	FragmentViews.Reset();
	CurrentArchetypesTagBitSet.Reset();
	ChunkSerialModificationNumber = -1;
}

void FMassExecutionContext::SetCurrentArchetypeData(FMassArchetypeData& ArchetypeData)
{
	CurrentArchetypesTagBitSet = ArchetypeData.GetTagBitSet();
}

void FMassExecutionContext::SetChunkCollection(const FArchetypeChunkCollection& InChunkCollection)
{
	check(ChunkCollection.IsEmpty());
	ChunkCollection = InChunkCollection;
}

void FMassExecutionContext::SetChunkCollection(FArchetypeChunkCollection&& InChunkCollection)
{
	check(ChunkCollection.IsEmpty());
	ChunkCollection = MoveTemp(InChunkCollection);
}

void FMassExecutionContext::SetRequirements(TConstArrayView<FMassFragmentRequirement> InRequirements, 
	TConstArrayView<FMassFragmentRequirement> InChunkRequirements, 
	TConstArrayView<FMassFragmentRequirement> InConstSharedRequirements, 
	TConstArrayView<FMassFragmentRequirement> InSharedRequirements)
{ 
	FragmentViews.Reset();
	for (const FMassFragmentRequirement& Requirement : InRequirements)
	{
		if (Requirement.RequiresBinding())
		{
			FragmentViews.Emplace(Requirement);
		}
	}

	ChunkFragmentViews.Reset();
	for (const FMassFragmentRequirement& Requirement : InChunkRequirements)
	{
		if (Requirement.RequiresBinding())
		{
			ChunkFragmentViews.Emplace(Requirement);
		}
	}

	ConstSharedFragmentViews.Reset();
	for (const FMassFragmentRequirement& Requirement : InConstSharedRequirements)
	{
		if (Requirement.RequiresBinding())
		{
			ConstSharedFragmentViews.Emplace(Requirement);
		}
	}

	SharedFragmentViews.Reset();
	for (const FMassFragmentRequirement& Requirement : InSharedRequirements)
	{
		if (Requirement.RequiresBinding())
		{
			SharedFragmentViews.Emplace(Requirement);
		}
	}
}

