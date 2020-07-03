// Copyright Epic Games, Inc. All Rights Reserved.

#include "EntitySystem/MovieSceneEntityFactory.h"
#include "EntitySystem/MovieSceneComponentRegistry.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/MovieSceneEntityManager.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieSceneEntitySystemTask.h"

namespace UE
{
namespace MovieScene
{


int32 FChildEntityFactory::Num() const
{
	return ParentEntityOffsets.Num();
}

int32 FChildEntityFactory::GetCurrentIndex() const
{
	if (const int32* CurrentOffset = CurrentEntityOffsets.GetData())
	{
		return CurrentOffset - ParentEntityOffsets.GetData();
	}
	return INDEX_NONE;
}

void FChildEntityFactory::Apply(UMovieSceneEntitySystemLinker* Linker, const FEntityAllocation* ParentAllocation)
{
	FComponentMask DerivedEntityType;
	GenerateDerivedType(DerivedEntityType);

	FComponentMask ParentType;
	for (const FComponentHeader& Header : ParentAllocation->GetComponentHeaders())
	{
		ParentType.Set(Header.ComponentType);
	}
	Linker->EntityManager.GetComponents()->Factories.ComputeChildComponents(ParentType, DerivedEntityType);
	Linker->EntityManager.GetComponents()->Factories.ComputeMutuallyInclusiveComponents(DerivedEntityType);

	const bool bHasAnyType = DerivedEntityType.Find(true) != INDEX_NONE;
	if (!bHasAnyType)
	{
		return;
	}

	const int32 NumToAdd = Num();

	int32 CurrentParentOffset = 0;

	// We attempt to allocate all the linker entities contiguously in memory for efficient initialization,
	// but we may reach capacity constraints within allocations so we may have to run the factories more than once
	while(CurrentParentOffset < NumToAdd)
	{
		// Ask to allocate as many as possible - we may only manage to allocate a smaller number contiguously this iteration however
		int32 NumAdded = NumToAdd - CurrentParentOffset;

		FEntityDataLocation NewLinkerEntities = Linker->EntityManager.AllocateContiguousEntities(DerivedEntityType, &NumAdded);
		FEntityRange ChildRange{ NewLinkerEntities.Allocation, NewLinkerEntities.ComponentOffset, NumAdded };

		CurrentEntityOffsets = MakeArrayView(ParentEntityOffsets.GetData() + CurrentParentOffset, NumAdded);

		Linker->EntityManager.InitializeChildAllocation(ParentType, DerivedEntityType, ParentAllocation, CurrentEntityOffsets, ChildRange);

		// Important: This must go after Linker->EntityManager.InitializeChildAllocation so that we know that parent entity IDs are initialized correctly
		InitializeAllocation(Linker, ParentType, DerivedEntityType, ParentAllocation, CurrentEntityOffsets, ChildRange);

		CurrentParentOffset += NumAdded;
	}

	PostInitialize(Linker);
}


void FObjectFactoryBatch::Add(int32 EntityIndex, UObject* BoundObject)
{
	ParentEntityOffsets.Add(EntityIndex);
	ObjectsToAssign.Add(BoundObject);
}

void FObjectFactoryBatch::GenerateDerivedType(FComponentMask& OutNewEntityType)
{
	OutNewEntityType.Set(FBuiltInComponentTypes::Get()->BoundObject);
}

void FObjectFactoryBatch::InitializeAllocation(UMovieSceneEntitySystemLinker* Linker, const FComponentMask& ParentType, const FComponentMask& ChildType, const FEntityAllocation* ParentAllocation, TArrayView<const int32> ParentAllocationOffsets, const FEntityRange& InChildEntityRange)
{
	TSortedMap<UObject*, FMovieSceneEntityID, TInlineAllocator<8>> ChildMatchScratch;

	TComponentTypeID<UObject*>            BoundObject = FBuiltInComponentTypes::Get()->BoundObject;
	TComponentTypeID<FMovieSceneEntityID> ParentEntity = FBuiltInComponentTypes::Get()->ParentEntity;

	TArrayView<const FMovieSceneEntityID> ParentIDs = ParentAllocation->GetEntityIDs();

	int32 Index = GetCurrentIndex();
	for (TEntityPtr<const FMovieSceneEntityID, const FMovieSceneEntityID, UObject*> Tuple : FEntityTaskBuilder().ReadEntityIDs().Read(ParentEntity).Write(BoundObject).IterateRange(InChildEntityRange))
	{
		FMovieSceneEntityID Parent = Tuple.Get<1>();
		FMovieSceneEntityID Child = Tuple.Get<0>();

		UObject* Object = ObjectsToAssign[Index++];
		Tuple.Get<2>() = Object;

		if (FMovieSceneEntityID OldEntityToPreserve = StaleEntitiesToPreserve->FindRef(MakeTuple(Object, Parent)))
		{
			PreservedEntities.Add(Child, OldEntityToPreserve);
		}
		Linker->EntityManager.AddChild(Parent, Child);
	}
}

void FObjectFactoryBatch::PostInitialize(UMovieSceneEntitySystemLinker* InLinker)
{
	FComponentMask PreservationMask = InLinker->EntityManager.GetComponents()->GetPreservationMask();

	for (TTuple<FMovieSceneEntityID, FMovieSceneEntityID> Pair : PreservedEntities)
	{
		InLinker->EntityManager.CombineComponents(Pair.Key, Pair.Value, &PreservationMask);
	}
}

FBoundObjectTask::FBoundObjectTask(UMovieSceneEntitySystemLinker* InLinker)
	: Linker(InLinker)
{}

void FBoundObjectTask::ForEachAllocation(const FEntityAllocation* Allocation, FReadEntityIDs EntityIDAccessor, TRead<FInstanceHandle> InstanceAccessor, TRead<FGuid> ObjectBindingAccessor)
{
	FObjectFactoryBatch& Batch = AddBatch(Allocation);
	Batch.StaleEntitiesToPreserve = &StaleEntitiesToPreserve;

	const int32 Num = Allocation->Num();
	const FMovieSceneEntityID* EntityIDs      = Allocation->GetRawEntityIDs();
	const FInstanceHandle*     Instances      = InstanceAccessor.Resolve(Allocation);
	const FGuid*               ObjectBindings = ObjectBindingAccessor.Resolve(Allocation);

	FInstanceRegistry* InstanceRegistry = Linker->GetInstanceRegistry();

	// Keep track of existing bindings so we can preserve any components on them
	TComponentTypeID<UObject*> BoundObjectComponent = FBuiltInComponentTypes::Get()->BoundObject;

	for (int32 Index = 0; Index < Num; ++Index)
	{
		FMovieSceneEntityID ParentID = EntityIDs[Index];

		// Discard existing children
		const int32 StartNum = EntitiesToDiscard.Num();
		Linker->EntityManager.GetImmediateChildren(ParentID, EntitiesToDiscard);

		// Keep track of any existing object bindings so we can preserve components on them if they are resolved to the same thing
		for (int32 ChildIndex = StartNum; ChildIndex < EntitiesToDiscard.Num(); ++ChildIndex)
		{
			FMovieSceneEntityID ChildID = EntitiesToDiscard[ChildIndex];
			TComponentPtr<UObject* const> ObjectPtr = Linker->EntityManager.ReadComponent(ChildID,  BoundObjectComponent);
			if (ObjectPtr)
			{
				StaleEntitiesToPreserve.Add(MakeTuple(*ObjectPtr, ParentID), ChildID);
			}
		}

		Batch.ResolveObjects(InstanceRegistry, Instances[Index], Index, ObjectBindings[Index]);
	}
}

void FBoundObjectTask::PostTask()
{
	Apply();

	FComponentTypeID NeedsUnlink = FBuiltInComponentTypes::Get()->Tags.NeedsUnlink;
	for (FMovieSceneEntityID Discard : EntitiesToDiscard)
	{
		Linker->EntityManager.AddComponent(Discard, NeedsUnlink, EEntityRecursion::Full);
	}
}

void FEntityFactories::RunInitializers(const FComponentMask& ParentType, const FComponentMask& ChildType, const FEntityAllocation* ParentAllocation, TArrayView<const int32> ParentAllocationOffsets, const FEntityRange& InChildEntityRange)
{
	// First off, run child initializers
	for (TInlineValue<FChildEntityInitializer>& ChildInit : ChildInitializers)
	{
		if (ChildInit->IsRelevant(ParentType, ChildType))
		{
			ChildInit->Run(InChildEntityRange, ParentAllocation, ParentAllocationOffsets);
		}
	}

	// First off, run child initializers
	for (TInlineValue<FMutualEntityInitializer>& MutualInit : MutualInitializers)
	{
		if (MutualInit->IsRelevant(ChildType))
		{
			MutualInit->Run(InChildEntityRange);
		}
	}
}

}	// using namespace MovieScene
}	// using namespace UE