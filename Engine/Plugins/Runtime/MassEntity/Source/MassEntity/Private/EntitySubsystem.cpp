// Copyright Epic Games, Inc. All Rights Reserved.

#include "EntitySubsystem.h"
#include "ArchetypeData.h"
#include "LWCCommandBuffer.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "UObject/UObjectIterator.h"

const FLWEntity UEntitySubsystem::InvalidEntity;

DEFINE_LOG_CATEGORY(LogAggregateTicking);

namespace UE { namespace AggregateTicking {

FString DebugGetComponentAccessString(ELWComponentAccess Access)
{
#if WITH_AGGREGATETICKING_DEBUG
	switch (Access)
	{
	case ELWComponentAccess::None:	return TEXT("--");
	case ELWComponentAccess::ReadOnly:	return TEXT("RO");
	case ELWComponentAccess::ReadWrite:	return TEXT("RW");
	default:
		ensureMsgf(false, TEXT("Missing string conversion for ELWComponentAccess=%d"), Access);
		break;
	}
#endif // WITH_AGGREGATETICKING_DEBUG
	return TEXT("Missing string conversion");
}

}} // UE::AggregateTicking

//@TODO: Everything still alive leaks at shutdown
//@TODO: No re-entrance safety while running a system (e.g., preventing someone from adding/removing entities or altering archetypes, etc...)
//@TODO: Do we allow GCable types?  If so, need to implement AddReferencedObjects
//@TODO: Do a UObject stat scope around the system Run call
//@TODO: Tag components end up taking up space per-entity when they shouldn't

//////////////////////////////////////////////////////////////////////
// UEntitySubsystem

UEntitySubsystem::UEntitySubsystem()
{
}

void UEntitySubsystem::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	Super::GetResourceSizeEx(CumulativeResourceSize);

	const SIZE_T MyExtraSize = Entities.GetAllocatedSize() + 
		EntityFreeIndexList.GetAllocatedSize() +
		DeferredCommandBuffer->GetAllocatedSize() +
		ComponentHashToArchetypeMap.GetAllocatedSize() +
		ComponentTypeToArchetypeMap.GetAllocatedSize();
	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(MyExtraSize);

	for (const auto& KVP : ComponentHashToArchetypeMap)
	{
		for (const TSharedPtr<FArchetypeData>& ArchetypePtr : KVP.Value)
		{
			CumulativeResourceSize.AddDedicatedSystemMemoryBytes(ArchetypePtr->GetAllocatedSize());
		}
	}
}

void UEntitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// Index 0 is reserved so we can treat that index as an invalid entity handle
	Entities.Add();
	SerialNumberGenerator.fetch_add(FMath::Max(1,NumReservedEntities));

	DeferredCommandBuffer = MakeShareable(new FLWCCommandBuffer());

	// initialize the bitsets

	FLWComponentBitSet Components;
	FLWTagBitSet Tags;
	FLWChunkComponentBitSet ChunkComponents;

	for (TObjectIterator<UStruct> StructIt; StructIt; ++StructIt)
	{
		if (StructIt->IsChildOf(FLWComponentData::StaticStruct()))
		{
			Components.Add(*CastChecked<UScriptStruct>(*StructIt));
		}
		else if (StructIt->IsChildOf(FComponentTag::StaticStruct()))
		{
			Tags.Add(*CastChecked<UScriptStruct>(*StructIt));
		}
		else if (StructIt->IsChildOf(FLWChunkComponent::StaticStruct()))
		{
			ChunkComponents.Add(*CastChecked<UScriptStruct>(*StructIt));
		}
	}
}

void UEntitySubsystem::Deinitialize()
{
	//@TODO: Should actually do this...
}

FArchetypeHandle UEntitySubsystem::CreateArchetype(TConstArrayView<const UScriptStruct*> ComponentsAngTagsList)
{
	FLWChunkComponentBitSet ChunkComponents;
	FLWTagBitSet Tags;
	TArray<const UScriptStruct*, TInlineAllocator<16>> ComponentList;
	ComponentList.Reserve(ComponentsAngTagsList.Num());

	for (const UScriptStruct* Type : ComponentsAngTagsList)
	{
		if (Type->IsChildOf(FLWComponentData::StaticStruct()))
		{
			ComponentList.Add(Type);
		}
		else if (Type->IsChildOf(FComponentTag::StaticStruct()))
		{
			Tags.Add(*Type);
		}
		else if (Type->IsChildOf(FLWChunkComponent::StaticStruct()))
		{
			ChunkComponents.Add(*Type);
		}
		else
		{
			UE_LOG(LogAggregateTicking, Warning, TEXT("%s: %s is not a valid component nor tag type. Ignoring.")
				, ANSI_TO_TCHAR(__FUNCTION__), *GetNameSafe(Type));
		}
	}

	return CreateArchetype(FLWCompositionDescriptor(FLWComponentBitSet(ComponentList), Tags, ChunkComponents));
}

FArchetypeHandle UEntitySubsystem::CreateArchetype(TConstArrayView<const UScriptStruct*> ComponentList, const FLWTagBitSet& Tags, const FLWChunkComponentBitSet& ChunkComponents)
{
	return CreateArchetype(FLWCompositionDescriptor(ComponentList, Tags, ChunkComponents));
}

FArchetypeHandle UEntitySubsystem::CreateArchetype(const TSharedPtr<FArchetypeData>& SourceArchetype, const FLWComponentBitSet& NewComponents)
{
	check(SourceArchetype.IsValid());
	checkf(NewComponents.IsEmpty() == false, TEXT("%s Adding an empty component list to an archetype is not supported."), ANSI_TO_TCHAR(__FUNCTION__));

	return CreateArchetype(FLWCompositionDescriptor(NewComponents + SourceArchetype->GetComponentBitSet(), SourceArchetype->GetTagBitSet(), SourceArchetype->GetChunkComponentBitSet()));
}

FArchetypeHandle UEntitySubsystem::CreateArchetype(const FLWCompositionDescriptor& Composition)
{
	const uint32 TypeHash = Composition.CalculateHash();

	TArray<TSharedPtr<FArchetypeData>>& HashRow = ComponentHashToArchetypeMap.FindOrAdd(TypeHash);

	FArchetypeHandle Result;
	for (TSharedPtr<FArchetypeData>& Ptr : HashRow)
	{
		if (Ptr->IsEquivalent(Composition))
		{
			Result.DataPtr = Ptr;
			break;
		}
	}

	if (!Result.DataPtr.IsValid())
	{
		// Create a new archetype
		TSharedPtr<FArchetypeData> NewArchetype = MakeShareable(new FArchetypeData);
		NewArchetype->Initialize(Composition.Components, Composition.Tags, Composition.ChunkComponents);
		HashRow.Add(NewArchetype);

		for (const FArchetypeComponentConfig& ComponentConfig : NewArchetype->GetComponentConfigs())
		{
			checkSlow(ComponentConfig.ComponentType)
			ComponentTypeToArchetypeMap.FindOrAdd(ComponentConfig.ComponentType).Add(NewArchetype);
		}

		Result.DataPtr = NewArchetype;

		++ArchetypeDataVersion;
	}

	return MoveTemp(Result);
}

FArchetypeHandle UEntitySubsystem::InternalCreateSiblingArchetype(const TSharedPtr<FArchetypeData>& SourceArchetype, const FLWTagBitSet& OverrideTags)
{
	checkSlow(SourceArchetype.IsValid());
	const FArchetypeData& SourceArchetypeRef = *SourceArchetype.Get();
	const uint32 TypeHash = FLWCompositionDescriptor::CalculateHash(SourceArchetypeRef.GetComponentBitSet(), OverrideTags, SourceArchetypeRef.GetChunkComponentBitSet());

	TArray<TSharedPtr<FArchetypeData>>& HashRow = ComponentHashToArchetypeMap.FindOrAdd(TypeHash);

	FArchetypeHandle Result;
	for (TSharedPtr<FArchetypeData>& Ptr : HashRow)
	{
		if (Ptr->IsEquivalent(SourceArchetypeRef.GetComponentBitSet(), OverrideTags, SourceArchetypeRef.GetChunkComponentBitSet()))
		{
			Result.DataPtr = Ptr;
			break;
		}
	}

	if (!Result.DataPtr.IsValid())
	{
		// Create a new archetype
		TSharedPtr<FArchetypeData> NewArchetype = MakeShareable(new FArchetypeData);
		NewArchetype->InitializeWithSibling(SourceArchetypeRef, OverrideTags);
		HashRow.Add(NewArchetype);

		for (const FArchetypeComponentConfig& ComponentConfig : NewArchetype->GetComponentConfigs())
		{
			checkSlow(ComponentConfig.ComponentType)
			ComponentTypeToArchetypeMap.FindOrAdd(ComponentConfig.ComponentType).Add(NewArchetype);
		}

		Result.DataPtr = NewArchetype;

		++ArchetypeDataVersion;
	}

	return MoveTemp(Result);
}

FArchetypeHandle UEntitySubsystem::GetArchetypeForEntity(FLWEntity Entity) const
{
	FArchetypeHandle Result;
	if (IsEntityValid(Entity))
	{
		Result.DataPtr = Entities[Entity.Index].CurrentArchetype;
	}
	return Result;
}

void UEntitySubsystem::ForEachArchetypeComponentType(const FArchetypeHandle Archetype, TFunction< void(const UScriptStruct* /*ComponentType*/)> Function)
{
	check(Archetype.DataPtr.IsValid());
	const FArchetypeData& ArchetypeData = *Archetype.DataPtr.Get();
	ArchetypeData.ForEachComponentType(Function);
}

void UEntitySubsystem::SetDefaultChunkComponentValue(const FArchetypeHandle Archetype, FConstStructView InstancedStruct)
{
	check(Archetype.DataPtr.IsValid());
	FArchetypeData& ArchetypeData = *Archetype.DataPtr.Get();
	ArchetypeData.SetDefaultChunkComponentValue(InstancedStruct);
}

void UEntitySubsystem::DoEntityCompaction(const double TimeAllowed)
{
	const double TimeAllowedEnd = FPlatformTime::Seconds() + TimeAllowed;

	bool bReachedTimeLimit = false;
	for (const auto& KVP : ComponentHashToArchetypeMap)
	{
		for (const TSharedPtr<FArchetypeData>& ArchetypePtr : KVP.Value)
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

FLWEntity UEntitySubsystem::CreateEntity(const FArchetypeHandle Archetype)
{
	check(Archetype.DataPtr.IsValid());

	FLWEntity Entity = ReserveEntity();
	InternalBuildEntity(Entity, Archetype);
	return Entity;
}

FLWEntity UEntitySubsystem::CreateEntity(TConstArrayView<FInstancedStruct> ComponentInstanceList)
{
	check(ComponentInstanceList.Num() > 0);

	FArchetypeHandle Archetype = CreateArchetype(FLWCompositionDescriptor(ComponentInstanceList, FLWTagBitSet(), FLWChunkComponentBitSet()));
	check(Archetype.DataPtr.IsValid());

	FLWEntity Entity = ReserveEntity();
	InternalBuildEntity(Entity, Archetype);

	FEntityData& EntityData = Entities[Entity.Index];
	EntityData.CurrentArchetype->SetComponentsData(Entity, ComponentInstanceList);

	return Entity;
}

FLWEntity UEntitySubsystem::ReserveEntity()
{
	// @todo: Need to add thread safety to the reservation of an entity
	FLWEntity Result;
	Result.Index = (EntityFreeIndexList.Num() > 0) ? EntityFreeIndexList.Pop(/*bAllowShrinking=*/ false) : Entities.Add();
	Result.SerialNumber = SerialNumberGenerator.fetch_add(1);
	Entities[Result.Index].SerialNumber = Result.SerialNumber;

	return Result;
}

void UEntitySubsystem::ReleaseReservedEntity(FLWEntity Entity)
{
	checkf(!IsEntityBuilt(Entity), TEXT("Entity is already built, use DestroyEntity() instead"));

	InternalReleaseEntity(Entity);
}

void UEntitySubsystem::BuildEntity(FLWEntity Entity, FArchetypeHandle Archetype)
{
	checkf(!IsEntityBuilt(Entity), TEXT("Expecting an entity that is not already built"));
	check(Archetype.DataPtr.IsValid());

	InternalBuildEntity(Entity, Archetype);
}

void UEntitySubsystem::BuildEntity(FLWEntity Entity, TConstArrayView<FInstancedStruct> ComponentInstanceList)
{
	check(ComponentInstanceList.Num() > 0);
	checkf(!IsEntityBuilt(Entity), TEXT("Expecting an entity that is not already built"));

	FArchetypeHandle Archetype = CreateArchetype(FLWCompositionDescriptor(ComponentInstanceList, FLWTagBitSet(), FLWChunkComponentBitSet()));
	check(Archetype.DataPtr.IsValid());

	InternalBuildEntity(Entity, Archetype);

	FEntityData& EntityData = Entities[Entity.Index];
	EntityData.CurrentArchetype->SetComponentsData(Entity, ComponentInstanceList);
}

int32 UEntitySubsystem::BatchCreateEntities(const FArchetypeHandle Archetype, const int32 Count, TArray<FLWEntity>& OutEntities)
{
	FArchetypeData* ArchetypePtr = Archetype.DataPtr.Get();
	check(ArchetypePtr);
	check(Count >= 0);
	
	int32 Index = OutEntities.Num();
	OutEntities.AddDefaulted(Count);

	// @todo optimize
	for (; Index < OutEntities.Num(); ++Index)
	{
		FLWEntity& Result = OutEntities[Index];
		Result.Index = (EntityFreeIndexList.Num() > 0) ? EntityFreeIndexList.Pop(/*bAllowShrinking=*/ false) : Entities.Add();
		Result.SerialNumber = SerialNumberGenerator++;

		FEntityData& EntityData = Entities[Result.Index];
		EntityData.CurrentArchetype = Archetype.DataPtr;
		EntityData.SerialNumber = Result.SerialNumber;
		
		ArchetypePtr->AddEntity(Result);
	}

	return Count;
}

void UEntitySubsystem::DestroyEntity(FLWEntity Entity)
{
	checkf(IsProcessing() == false, TEXT("Synchronous API function %s called during mass processing. Use asynchronous API instead."), ANSI_TO_TCHAR(__FUNCTION__));
	
	CheckIfEntityIsActive(Entity);

	FEntityData& EntityData = Entities[Entity.Index];
	FArchetypeData* Archetype = EntityData.CurrentArchetype.Get();
	check(Archetype);
	Archetype->RemoveEntity(Entity);

	InternalReleaseEntity(Entity);

	if (Archetype->GetNumEntities() == 0)
	{
		//@TODO: Drop our reference to the archetype?
	}
}

void UEntitySubsystem::BatchDestroyEntities(TConstArrayView<FLWEntity> InEntities)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("BatchDestroyEntities");

	checkf(IsProcessing() == false, TEXT("Synchronous API function %s called during mass processing. Use asynchronous API instead."), ANSI_TO_TCHAR(__FUNCTION__));
	
	EntityFreeIndexList.Reserve(EntityFreeIndexList.Num() + InEntities.Num());

	// @todo optimize, we can make savings by implementing Archetype->RemoveEntities()
	for (const FLWEntity Entity : InEntities)
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

		FArchetypeData* Archetype = EntityData.CurrentArchetype.Get();
		check(Archetype);
		Archetype->RemoveEntity(Entity);

		EntityData.Reset();
		EntityFreeIndexList.Add(Entity.Index);
	}
}

void UEntitySubsystem::BatchDestroyEntityChunks(const FArchetypeChunkCollection& Chunks)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("BatchDestroyEntityChunks");

	checkf(IsProcessing() == false, TEXT("Synchronous API function %s called during mass processing. Use asynchronous API instead."), ANSI_TO_TCHAR(__FUNCTION__));

	check(Chunks.GetArchetype().IsValid());
	TArray<FLWEntity> EntitiesRemoved;
	Chunks.GetArchetype().DataPtr->BatchDestroyEntityChunks(Chunks, EntitiesRemoved);
	
	EntityFreeIndexList.Reserve(EntityFreeIndexList.Num() + EntitiesRemoved.Num());

	for (const FLWEntity& Entity : EntitiesRemoved)
	{
		check(Entities.IsValidIndex(Entity.Index));

		FEntityData& EntityData = Entities[Entity.Index];
		if (EntityData.SerialNumber == Entity.SerialNumber)
		{
			EntityData.Reset();
			EntityFreeIndexList.Add(Entity.Index);
		}
	}
}

void UEntitySubsystem::AddComponentToEntity(FLWEntity Entity, const UScriptStruct* ComponentType)
{
	checkf(ComponentType, TEXT("Null component type passed in to %s"), ANSI_TO_TCHAR(__FUNCTION__));
	checkf(IsProcessing() == false, TEXT("Synchronous API function %s called during mass processing. Use asynchronous API instead."), ANSI_TO_TCHAR(__FUNCTION__));

	CheckIfEntityIsActive(Entity);

	InternalAddComponentListToEntityChecked(Entity, FLWComponentBitSet(*ComponentType));
}

void UEntitySubsystem::AddComponentListToEntity(FLWEntity Entity, TConstArrayView<const UScriptStruct*> ComponentList)
{
	CheckIfEntityIsActive(Entity);

	InternalAddComponentListToEntityChecked(Entity, FLWComponentBitSet(ComponentList));
}

void UEntitySubsystem::AddCompositionToEntity_GetDelta(FLWEntity Entity, FLWCompositionDescriptor& Composition)
{
	CheckIfEntityIsActive(Entity);

	FEntityData& EntityData = Entities[Entity.Index];
	FArchetypeData* OldArchetype = EntityData.CurrentArchetype.Get();
	check(OldArchetype);

	Composition.Components -= OldArchetype->GetCompositionDescriptor().Components;
	Composition.Tags -= OldArchetype->GetCompositionDescriptor().Tags;

	if (Composition.IsEmpty() == false)
	{
		FLWCompositionDescriptor NewDescriptor = OldArchetype->GetCompositionDescriptor();
		NewDescriptor.Components += Composition.Components;
		NewDescriptor.Tags+= Composition.Tags;

		const FArchetypeHandle NewArchetypeHandle = CreateArchetype(NewDescriptor);

		if (ensure(NewArchetypeHandle.DataPtr != EntityData.CurrentArchetype))
		{
			// Move the entity over
			FArchetypeData* NewArchetype = NewArchetypeHandle.DataPtr.Get();
			check(NewArchetype);
			EntityData.CurrentArchetype->MoveEntityToAnotherArchetype(Entity, *NewArchetype);
			EntityData.CurrentArchetype = NewArchetypeHandle.DataPtr;
		}
	}
}

void UEntitySubsystem::RemoveCompositionFromEntity(FLWEntity Entity, const FLWCompositionDescriptor& InDescriptor)
{
	CheckIfEntityIsActive(Entity);

	FEntityData& EntityData = Entities[Entity.Index];
	FArchetypeData* OldArchetype = EntityData.CurrentArchetype.Get();
	check(OldArchetype);

	FLWCompositionDescriptor NewDescriptor = OldArchetype->GetCompositionDescriptor();
	NewDescriptor.Components -= InDescriptor.Components;
	NewDescriptor.Tags -= InDescriptor.Tags;

	if (NewDescriptor.IsEmpty() == false)
	{
		const FArchetypeHandle NewArchetypeHandle = CreateArchetype(NewDescriptor);

		if (ensure(NewArchetypeHandle.DataPtr != EntityData.CurrentArchetype))
		{
			// Move the entity over
			FArchetypeData* NewArchetype = NewArchetypeHandle.DataPtr.Get();
			check(NewArchetype);
			EntityData.CurrentArchetype->MoveEntityToAnotherArchetype(Entity, *NewArchetype);
			EntityData.CurrentArchetype = NewArchetypeHandle.DataPtr;
		}
	}
}

void UEntitySubsystem::InternalBuildEntity(FLWEntity Entity, const FArchetypeHandle Archetype)
{
	FEntityData& EntityData = Entities[Entity.Index];
	EntityData.CurrentArchetype = Archetype.DataPtr;
	EntityData.CurrentArchetype->AddEntity(Entity);
}

void UEntitySubsystem::InternalReleaseEntity(FLWEntity Entity)
{
	FEntityData& EntityData = Entities[Entity.Index];
	EntityData.Reset();
	EntityFreeIndexList.Add(Entity.Index);
}

void UEntitySubsystem::InternalAddComponentListToEntityChecked(FLWEntity Entity, const FLWComponentBitSet& InComponents)
{
	FEntityData& EntityData = Entities[Entity.Index];
	FArchetypeData* OldArchetype = EntityData.CurrentArchetype.Get();
	check(OldArchetype);
	
	ensureMsgf(OldArchetype->GetComponentBitSet().HasAny(InComponents) == false, TEXT("Trying to add a new component type to an entity, but it already has some of them."));

	const FLWComponentBitSet NewComponents = InComponents - OldArchetype->GetComponentBitSet();
	if (NewComponents.IsEmpty() == false)
	{
		InternalAddComponentListToEntity(Entity, NewComponents);
	}
}

void UEntitySubsystem::InternalAddComponentListToEntity(FLWEntity Entity, const FLWComponentBitSet& NewComponents)
{
	checkf(NewComponents.IsEmpty() == false, TEXT("%s is intended for internal calls with non empty NewComponents parameter"), ANSI_TO_TCHAR(__FUNCTION__));
	check(Entities.IsValidIndex(Entity.Index));
	FEntityData& EntityData = Entities[Entity.Index];
	check(EntityData.CurrentArchetype.IsValid());

	// fetch or create the new archetype
	const FArchetypeHandle NewArchetypeHandle = CreateArchetype(EntityData.CurrentArchetype, NewComponents);

	if (NewArchetypeHandle.DataPtr != EntityData.CurrentArchetype)
	{
		// Move the entity over
		FArchetypeData* NewArchetype = NewArchetypeHandle.DataPtr.Get();
		check(NewArchetype);
		EntityData.CurrentArchetype->MoveEntityToAnotherArchetype(Entity, *NewArchetype);
		EntityData.CurrentArchetype = NewArchetypeHandle.DataPtr;
	}
}

void UEntitySubsystem::AddComponentInstanceListToEntity(FLWEntity Entity, TConstArrayView<FInstancedStruct> ComponentInstanceList)
{
	checkf(IsProcessing() == false, TEXT("Synchronous API function %s called during mass processing. Use asynchronous API instead."), ANSI_TO_TCHAR(__FUNCTION__));

	CheckIfEntityIsActive(Entity);
	checkf(ComponentInstanceList.Num() > 0, TEXT("Need to specify at least one component instances for this operation"));

	InternalAddComponentListToEntityChecked(Entity, FLWComponentBitSet(ComponentInstanceList));

	FEntityData& EntityData = Entities[Entity.Index];
	EntityData.CurrentArchetype->SetComponentsData(Entity, ComponentInstanceList);
}

void UEntitySubsystem::RemoveComponentFromEntity(FLWEntity Entity, const UScriptStruct* ComponentType)
{
	RemoveComponentListFromEntity(Entity, MakeArrayView(&ComponentType, 1));
}

void UEntitySubsystem::RemoveComponentListFromEntity(FLWEntity Entity, TConstArrayView<const UScriptStruct*> ComponentList)
{
	checkf(IsProcessing() == false, TEXT("Synchronous API function %s called during mass processing. Use asynchronous API instead."), ANSI_TO_TCHAR(__FUNCTION__));

	CheckIfEntityIsActive(Entity);
	
	FEntityData& EntityData = Entities[Entity.Index];
	FArchetypeData* OldArchetype = EntityData.CurrentArchetype.Get();
	check(OldArchetype);

	const FLWComponentBitSet ComponentsToRemove(ComponentList);

	if (ensureMsgf(OldArchetype->GetComponentBitSet().HasAny(ComponentsToRemove), TEXT("Trying to remove a list of components from an entity but none of the components given was found.")))
	{
		// If all the components got removed this will result in fetching of the empty archetype
		const FArchetypeHandle NewArchetypeHandle = CreateArchetype(FLWCompositionDescriptor(OldArchetype->GetComponentBitSet() - ComponentsToRemove, OldArchetype->GetTagBitSet(), OldArchetype->GetChunkComponentBitSet()));

		// Move the entity over
		FArchetypeData* NewArchetype = NewArchetypeHandle.DataPtr.Get();
		check(NewArchetype);
		OldArchetype->MoveEntityToAnotherArchetype(Entity, *NewArchetype);
		EntityData.CurrentArchetype = NewArchetypeHandle.DataPtr;
	}
}

void UEntitySubsystem::SwapTagsForEntity(FLWEntity Entity, const UScriptStruct* OldTagType, const UScriptStruct* NewTagType)
{
	checkf(IsProcessing() == false, TEXT("Synchronous API function %s called during mass processing. Use asynchronous API instead."), ANSI_TO_TCHAR(__FUNCTION__));

	CheckIfEntityIsActive(Entity);

	checkf((OldTagType != nullptr) && OldTagType->IsChildOf(FComponentTag::StaticStruct()), TEXT("%s works only with tags while '%s' is not one."), ANSI_TO_TCHAR(__FUNCTION__), *GetPathNameSafe(OldTagType));
	checkf((NewTagType != nullptr) && NewTagType->IsChildOf(FComponentTag::StaticStruct()), TEXT("%s works only with tags while '%s' is not one."), ANSI_TO_TCHAR(__FUNCTION__), *GetPathNameSafe(NewTagType));

	FEntityData& EntityData = Entities[Entity.Index];
	FArchetypeData* CurrentArchetype = EntityData.CurrentArchetype.Get();
	check(CurrentArchetype);

	FLWTagBitSet NewTagBitSet = CurrentArchetype->GetTagBitSet();
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

void UEntitySubsystem::AddTagToEntity(FLWEntity Entity, const UScriptStruct* TagType)
{
	checkf((TagType != nullptr) && TagType->IsChildOf(FComponentTag::StaticStruct()), TEXT("%s works only with tags while '%s' is not one."), ANSI_TO_TCHAR(__FUNCTION__), *GetPathNameSafe(TagType));

	CheckIfEntityIsActive(Entity);

	FEntityData& EntityData = Entities[Entity.Index];
	FArchetypeData* CurrentArchetype = EntityData.CurrentArchetype.Get();
	check(CurrentArchetype);

	if (CurrentArchetype->HasTagType(TagType) == false)
	{
		//FLWTagBitSet NewTags = CurrentArchetype->GetTagBitSet() - *TagType;
		FLWTagBitSet NewTags = CurrentArchetype->GetTagBitSet();
		NewTags.Add(*TagType);
		FArchetypeHandle NewArchetypeHandle = InternalCreateSiblingArchetype(EntityData.CurrentArchetype, NewTags);
		checkSlow(NewArchetypeHandle.IsValid());

		// Move the entity over
		EntityData.CurrentArchetype->MoveEntityToAnotherArchetype(Entity, *NewArchetypeHandle.DataPtr.Get());
		EntityData.CurrentArchetype = NewArchetypeHandle.DataPtr;
	}
}
	
void UEntitySubsystem::RemoveTagFromEntity(FLWEntity Entity, const UScriptStruct* TagType)
{
	checkf((TagType != nullptr) && TagType->IsChildOf(FComponentTag::StaticStruct()), TEXT("%s works only with tags while '%s' is not one."), ANSI_TO_TCHAR(__FUNCTION__), *GetPathNameSafe(TagType));

	CheckIfEntityIsActive(Entity);

	FEntityData& EntityData = Entities[Entity.Index];
	FArchetypeData* CurrentArchetype = EntityData.CurrentArchetype.Get();
	check(CurrentArchetype);

	if (CurrentArchetype->HasTagType(TagType))
	{
		// CurrentArchetype->GetTagBitSet() -  *TagType
		FLWTagBitSet NewTags = CurrentArchetype->GetTagBitSet();
		NewTags.Remove(*TagType);
		FArchetypeHandle NewArchetypeHandle = InternalCreateSiblingArchetype(EntityData.CurrentArchetype, NewTags);
		checkSlow(NewArchetypeHandle.IsValid());

		// Move the entity over
		EntityData.CurrentArchetype->MoveEntityToAnotherArchetype(Entity, *NewArchetypeHandle.DataPtr.Get());
		EntityData.CurrentArchetype = NewArchetypeHandle.DataPtr;
	}
}


void UEntitySubsystem::MoveEntityToAnotherArchetype(FLWEntity Entity, FArchetypeHandle NewArchetypeHandle)
{
	CheckIfEntityIsActive(Entity);

	FArchetypeData* NewArchetype = NewArchetypeHandle.DataPtr.Get();
	check(NewArchetype);

	// Move the entity over
	FEntityData& EntityData = Entities[Entity.Index];
	EntityData.CurrentArchetype->MoveEntityToAnotherArchetype(Entity, *NewArchetype);
	EntityData.CurrentArchetype = NewArchetypeHandle.DataPtr;
}

void UEntitySubsystem::SetEntityComponentsValues(FLWEntity Entity, TArrayView<const FInstancedStruct> ComponentInstanceList)
{
	CheckIfEntityIsActive(Entity);

	FEntityData& EntityData = Entities[Entity.Index];
	EntityData.CurrentArchetype->SetComponentsData(Entity, ComponentInstanceList);
}

void UEntitySubsystem::BatchSetEntityComponentsValues(const FArchetypeChunkCollection& SparseEntities, TArrayView<const FInstancedStruct> ComponentInstanceList)
{
	FArchetypeData* Archetype = SparseEntities.GetArchetype().DataPtr.Get();
	check(Archetype);

	for (const FInstancedStruct& ComponentTemplate : ComponentInstanceList)
	{
		Archetype->SetComponentData(SparseEntities, ComponentTemplate);
	}
}

void* UEntitySubsystem::InternalGetComponentDataChecked(FLWEntity Entity, const UScriptStruct* ComponentType) const
{
	CheckIfEntityIsActive(Entity);

	checkf((ComponentType != nullptr) && ComponentType->IsChildOf(FLWComponentData::StaticStruct()), TEXT("InternalGetComponentDataChecked called with an invalid component type '%s'"), *GetPathNameSafe(ComponentType));
	const FEntityData& EntityData = Entities[Entity.Index];
	return EntityData.CurrentArchetype->GetComponentDataForEntityChecked(ComponentType, Entity.Index);
}

void* UEntitySubsystem::InternalGetComponentDataPtr(FLWEntity Entity, const UScriptStruct* ComponentType) const
{
	CheckIfEntityIsActive(Entity);
	checkf((ComponentType != nullptr) && ComponentType->IsChildOf(FLWComponentData::StaticStruct()), TEXT("InternalGetComponentData called with an invalid component type '%s'"), *GetPathNameSafe(ComponentType));
	const FEntityData& EntityData = Entities[Entity.Index];
	return EntityData.CurrentArchetype->GetComponentDataForEntity(ComponentType, Entity.Index);
}

bool UEntitySubsystem::IsEntityValid(FLWEntity Entity) const
{
	return (Entity.Index > 0) && Entities.IsValidIndex(Entity.Index) && (Entities[Entity.Index].SerialNumber == Entity.SerialNumber);
}

bool UEntitySubsystem::IsEntityBuilt(FLWEntity Entity) const
{
	CheckIfEntityIsValid(Entity);
	return Entities[Entity.Index].CurrentArchetype.IsValid();
}

void UEntitySubsystem::CheckIfEntityIsValid(FLWEntity Entity) const
{
	checkf(IsEntityValid(Entity), TEXT("Invalid entity (ID: %d, SN:%d, %s)"), Entity.Index, Entity.SerialNumber,
		   (Entity.Index == 0) ? TEXT("was never initialized") : TEXT("already destroyed"));
}

void UEntitySubsystem::CheckIfEntityIsActive(FLWEntity Entity) const
{
	checkf(IsEntityBuilt(Entity), TEXT("Entity not yet created(ID: %d, SN:%d)"));
}

void UEntitySubsystem::GetValidArchetypes(const FLWComponentQuery& Query, TArray<FArchetypeHandle>& OutValidArchetypes)
{
	//@TODO: Not optimized yet, but we call this rarely now, so not a big deal.

	// First get set of all archetypes that contain *any* component
	TSet<TSharedPtr<FArchetypeData>> AnyArchetypes;
	for (const FLWComponentRequirement& Requirement : Query.GetRequirements())
	{
		check(Requirement.StructType);
		if (Requirement.Presence != ELWComponentPresence::None)
		{
			if (TArray<TSharedPtr<FArchetypeData>>* pData = ComponentTypeToArchetypeMap.Find(Requirement.StructType))
			{
				AnyArchetypes.Append(*pData);
			}
		}
	}

	// Then verify that they contain *all* required components
	for (TSharedPtr<FArchetypeData>& ArchetypePtr : AnyArchetypes)
	{
		FArchetypeData& Archetype = *(ArchetypePtr.Get());

		if (Archetype.GetTagBitSet().HasAll(Query.GetRequiredAllTags()) == false)
		{
			// missing some required tags, skip.
#if WITH_AGGREGATETICKING_DEBUG
			const FLWTagBitSet UnsatisfiedTags = Query.GetRequiredAllTags() - Archetype.GetTagBitSet();
			FStringOutputDevice Description;
			UnsatisfiedTags.DebugGetStringDesc(Description);
			UE_LOG(LogAggregateTicking, VeryVerbose, TEXT("Archetype did not match due to missing tags: %s")
				, *Description);
#endif // WITH_AGGREGATETICKING_DEBUG
			continue;
		}

		if (Archetype.GetTagBitSet().HasNone(Query.GetRequiredNoneTags()) == false)
		{
			// has some tags required to be absent
#if WITH_AGGREGATETICKING_DEBUG
			const FLWTagBitSet UnwantedTags = Query.GetRequiredAllTags().GetOverlap(Archetype.GetTagBitSet());
			FStringOutputDevice Description;
			UnwantedTags.DebugGetStringDesc(Description);
			UE_LOG(LogAggregateTicking, VeryVerbose, TEXT("Archetype has tags required absent: %s")
				, *Description);
#endif // WITH_AGGREGATETICKING_DEBUG
			continue;
		}

		if (Query.GetRequiredAnyTags().IsEmpty() == false 
			&& Archetype.GetTagBitSet().HasAny(Query.GetRequiredAnyTags()) == false)
		{
#if WITH_AGGREGATETICKING_DEBUG
			FStringOutputDevice Description;
			Query.GetRequiredAnyTags().DebugGetStringDesc(Description);
			UE_LOG(LogAggregateTicking, VeryVerbose, TEXT("Archetype did not match due to missing \'any\' tags: %s")
				, *Description);
#endif // WITH_AGGREGATETICKING_DEBUG
			continue;
		}
		
		if (Archetype.GetComponentBitSet().HasAll(Query.GetRequiredAllComponents()) == false)
		{
			// missing some required components, skip.
#if WITH_AGGREGATETICKING_DEBUG
			const FLWComponentBitSet UnsatisfiedComponents = Query.GetRequiredAllComponents() - Archetype.GetComponentBitSet();
			FStringOutputDevice Description;
			UnsatisfiedComponents.DebugGetStringDesc(Description);
			UE_LOG(LogAggregateTicking, VeryVerbose, TEXT("Archetype did not match due to missing Components: %s")
				, *Description);
#endif // WITH_AGGREGATETICKING_DEBUG
			continue;
		}

		if (Archetype.GetComponentBitSet().HasNone(Query.GetRequiredNoneComponents()) == false)
		{
			// has some Components required to be absent
#if WITH_AGGREGATETICKING_DEBUG
			const FLWComponentBitSet UnwantedComponents = Query.GetRequiredAllComponents().GetOverlap(Archetype.GetComponentBitSet());
			FStringOutputDevice Description;
			UnwantedComponents.DebugGetStringDesc(Description);
			UE_LOG(LogAggregateTicking, VeryVerbose, TEXT("Archetype has Components required absent: %s")
				, *Description);
#endif // WITH_AGGREGATETICKING_DEBUG
			continue;
		}

		if (Query.GetRequiredAnyComponents().IsEmpty() == false 
			&& Archetype.GetComponentBitSet().HasAny(Query.GetRequiredAnyComponents()) == false)
		{
#if WITH_AGGREGATETICKING_DEBUG
			FStringOutputDevice Description;
			Query.GetRequiredAnyComponents().DebugGetStringDesc(Description);
			UE_LOG(LogAggregateTicking, VeryVerbose, TEXT("Archetype did not match due to missing \'any\' components: %s")
				, *Description);
#endif // WITH_AGGREGATETICKING_DEBUG
			continue;
		}

		if (Archetype.GetChunkComponentBitSet().HasAll(Query.GetRequiredAllChunkComponents()) == false)
		{
			// missing some required components, skip.
#if WITH_AGGREGATETICKING_DEBUG
			const FLWChunkComponentBitSet UnsatisfiedComponents = Query.GetRequiredAllChunkComponents() - Archetype.GetChunkComponentBitSet();
			FStringOutputDevice Description;
			UnsatisfiedComponents.DebugGetStringDesc(Description);
			UE_LOG(LogAggregateTicking, VeryVerbose, TEXT("Archetype did not match due to missing Chunk Components: %s")
				, *Description);
#endif // WITH_AGGREGATETICKING_DEBUG
			continue;
		}

		if (Archetype.GetChunkComponentBitSet().HasNone(Query.GetRequiredNoneChunkComponents()) == false)
		{
			// has some Components required to be absent
#if WITH_AGGREGATETICKING_DEBUG
			const FLWChunkComponentBitSet UnwantedComponents = Query.GetRequiredNoneChunkComponents().GetOverlap(Archetype.GetChunkComponentBitSet());
			FStringOutputDevice Description;
			UnwantedComponents.DebugGetStringDesc(Description);
			UE_LOG(LogAggregateTicking, VeryVerbose, TEXT("Archetype has Chunk Components required absent: %s")
				, *Description);
#endif // WITH_AGGREGATETICKING_DEBUG
			continue;
		}

		OutValidArchetypes.Add(ArchetypePtr);
	}
}

FLWComponentSystemExecutionContext UEntitySubsystem::CreateExecutionContext(const float DeltaSeconds) const
{
	FLWComponentSystemExecutionContext ExecutionContext(DeltaSeconds);
	ExecutionContext.SetDeferredCommandBuffer(DeferredCommandBuffer);
	return MoveTemp(ExecutionContext);
}

#if WITH_AGGREGATETICKING_DEBUG
void UEntitySubsystem::DebugPrintArchetypes(FOutputDevice& Ar) const
{
	Ar.Logf(ELogVerbosity::Log, TEXT("Listing archetypes contained in %s"), *GetPathNameSafe(this));

	int32 NumBuckets = 0;
	int32 NumArchetypes = 0;
	int32 LongestArchetypeBucket = 0;
	for (const auto& KVP : ComponentHashToArchetypeMap)
	{
		for (const TSharedPtr<FArchetypeData>& ArchetypePtr : KVP.Value)
		{
			ArchetypePtr->DebugPrintArchetype(Ar);
		}

		const int32 NumArchetypesInBucket = KVP.Value.Num();
		LongestArchetypeBucket = FMath::Max(LongestArchetypeBucket, NumArchetypesInBucket);
		NumArchetypes += NumArchetypesInBucket;
		++NumBuckets;
	}

	Ar.Logf(ELogVerbosity::Log, TEXT("ComponentHashToArchetypeMap: %d archetypes across %d buckets, longest bucket is %d"),
		NumArchetypes, NumBuckets, LongestArchetypeBucket);
}

void UEntitySubsystem::DebugPrintEntity(int32 Index, FOutputDevice& Ar, const TCHAR* InPrefix) const
{
	if (Index >= Entities.Num())
	{
		Ar.Logf(ELogVerbosity::Log, TEXT("Unable to list components values for out of range index in %s"), *GetPathNameSafe(this));
		return;
	}

	const FEntityData& EntityData = Entities[Index];
	if (!EntityData.IsValid())
	{
		Ar.Logf(ELogVerbosity::Log, TEXT("Unable to list components values for invalid entity in %s"), *GetPathNameSafe(this));
	}

	FLWEntity Entity;
	Entity.Index = Index;
	Entity.SerialNumber = EntityData.SerialNumber;
	DebugPrintEntity(Entity, Ar, InPrefix);
}

void UEntitySubsystem::DebugPrintEntity(FLWEntity Entity, FOutputDevice& Ar, const TCHAR* InPrefix) const
{
	if (!IsEntityActive(Entity))
	{
		Ar.Logf(ELogVerbosity::Log, TEXT("Unable to list components values for invalid entity in %s"), *GetPathNameSafe(this));
	}

	Ar.Logf(ELogVerbosity::Log, TEXT("Listing components values for Entity[%s] in %s"), *Entity.DebugGetDescription(), *GetPathNameSafe(this));

	const FEntityData& EntityData = Entities[Entity.Index];
	FArchetypeData* Archetype = EntityData.CurrentArchetype.Get();
	if (Archetype == nullptr)
	{
		Ar.Logf(ELogVerbosity::Log, TEXT("Unable to list components values for invalid entity in %s"), *GetPathNameSafe(this));
	}
	else
	{
		Archetype->DebugPrintEntity(Entity, Ar, InPrefix);
	}
}

void UEntitySubsystem::DebugGetStringDesc(const FArchetypeHandle& Archetype, FOutputDevice& Ar)
{
	Ar.Logf(TEXT("%s"), Archetype.IsValid() ? *Archetype.DataPtr->DebugGetDescription() : TEXT("INVALID"));
}

void UEntitySubsystem::DebugGetArchetypesStringDetails(FOutputDevice& Ar, const bool bIncludeEmpty)
{
#if WITH_AGGREGATETICKING_DEBUG
	Ar.SetAutoEmitLineTerminator(true);
	for (auto Pair : ComponentHashToArchetypeMap)
	{
		Ar.Logf(ELogVerbosity::Log, TEXT("\n-----------------------------------\nHash: %u"), Pair.Key);
		for (TSharedPtr<FArchetypeData> Archetype : Pair.Value)
		{
			if (Archetype.IsValid() && (bIncludeEmpty == true || Archetype->GetChunkCount() > 0))
			{
				Archetype->DebugPrintArchetype(Ar);
				Ar.Logf(ELogVerbosity::Log, TEXT("+++++++++++++++++++++++++\n"));
			}
		}
	}
#endif // WITH_AGGREGATETICKING_DEBUG
}

void UEntitySubsystem::DebugGetArchetypeComponentTypes(const FArchetypeHandle& Archetype, TArray<const UScriptStruct*>& InOutComponentList) const
{
	if (Archetype.DataPtr.IsValid())
	{
		Archetype.DataPtr->GetCompositionDescriptor().Components.DebugGetStructTypes(InOutComponentList);
	}
}

int32 UEntitySubsystem::DebugGetArchetypeEntitiesCount(const FArchetypeHandle& Archetype) const
{
	return Archetype.DataPtr.IsValid() ? Archetype.DataPtr->GetNumEntities() : 0;
}

void UEntitySubsystem::DebugRemoveAllEntities()
{
	for (int EntityIndex = NumReservedEntities; EntityIndex < Entities.Num(); ++EntityIndex)
	{
		FEntityData& EntityData = Entities[EntityIndex];
		if (EntityData.IsValid() == false)
		{
			// already dead
			continue;
		}
		const TSharedPtr<FArchetypeData>& Archetype = EntityData.CurrentArchetype;
		FLWEntity Entity;
		Entity.Index = EntityIndex;
		Entity.SerialNumber = EntityData.SerialNumber;
		Archetype->RemoveEntity(Entity);

		EntityData.Reset();
		EntityFreeIndexList.Add(EntityIndex);
	}
}

void UEntitySubsystem::DebugGetArchetypeStrings(const FArchetypeHandle& Archetype, TArray<FName>& OutComponentNames, TArray<FName>& OutTagNames)
{
	if (Archetype.IsValid() == false)
	{
		return;
	}

	FArchetypeData& ArchetypeRef = *Archetype.DataPtr.Get();
	
	OutComponentNames.Reserve(ArchetypeRef.GetComponentConfigs().Num());
	for (const FArchetypeComponentConfig& ComponentConfig : ArchetypeRef.GetComponentConfigs())
	{
		checkSlow(ComponentConfig.ComponentType);
		OutComponentNames.Add(ComponentConfig.ComponentType->GetFName());
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
	if (UEntitySubsystem* EntitySystem = World ? World->GetSubsystem<UEntitySubsystem>() : nullptr)
	{
		EntitySystem->DebugPrintArchetypes(Ar);
	}
	else
	{
		Ar.Logf(ELogVerbosity::Error, TEXT("Failed to find Entity Subsystem for world %s"), *GetPathNameSafe(World));
	}
}));
#endif // WITH_AGGREGATETICKING_DEBUG

//////////////////////////////////////////////////////////////////////
// FLWComponentSystemExecutionContext

void FLWComponentSystemExecutionContext::FlushDeferred(UEntitySubsystem& EntitySystem) const
{
	if (bFlushDeferredCommands && DeferredCommandBuffer)
	{
		DeferredCommandBuffer->ReplayBufferAgainstSystem(&EntitySystem);
	}
}

void FLWComponentSystemExecutionContext::ClearExecutionData()
{
	ComponentViews.Reset();
	CurrentArchetypesTagBitSet.Reset();
	ChunkSerialModificationNumber = -1;
}

void FLWComponentSystemExecutionContext::SetCurrentArchetypeData(FArchetypeData& ArchetypeData)
{
	CurrentArchetypesTagBitSet = ArchetypeData.GetTagBitSet();
}

void FLWComponentSystemExecutionContext::SetChunkCollection(const FArchetypeChunkCollection& InChunkCollection)
{
	check(ChunkCollection.IsEmpty());
	ChunkCollection = InChunkCollection;
}

void FLWComponentSystemExecutionContext::SetChunkCollection(FArchetypeChunkCollection&& InChunkCollection)
{
	check(ChunkCollection.IsEmpty());
	ChunkCollection = MoveTemp(InChunkCollection);
}

void FLWComponentSystemExecutionContext::SetRequirements(TConstArrayView<FLWComponentRequirement> InRequirements, TConstArrayView<FLWComponentRequirement> InChunkRequirements)
{ 
	ComponentViews.Reset();
	for (const FLWComponentRequirement& Requirement : InRequirements)
	{
		if (Requirement.RequiresBinding())
		{
			ComponentViews.Add(FComponentView(Requirement));
		}
	}

	ChunkComponents.Reset();
	for (const FLWComponentRequirement& Requirement : InChunkRequirements)
	{
		if (Requirement.RequiresBinding())
		{
			ChunkComponents.Add(FChunkComponentView(Requirement));
		}
	}
}

