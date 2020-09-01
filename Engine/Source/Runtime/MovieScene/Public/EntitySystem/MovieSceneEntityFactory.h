// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Containers/Map.h"
#include "Containers/SortedMap.h"
#include "Misc/InlineValue.h"

#include "EntitySystem/MovieSceneEntityIDs.h"
#include "EntitySystem/MovieSceneEntityRange.h"
#include "EntitySystem/MovieSceneEntitySystemTask.h"
#include "EntitySystem/MovieSceneEntitySystemTypes.h"
#include "EntitySystem/MovieSceneEntityFactoryTypes.h"
#include "EntitySystem/MovieSceneComponentAccessors.h"
#include "EntitySystem/MovieSceneSequenceInstanceHandle.h"
#include "EntitySystem/MovieSceneEntitySystemDirectedGraph.h"


namespace UE
{
namespace MovieScene
{

struct FInstanceRegistry;

template<typename ParentComponentType, typename ChildComponentType>
struct TChildEntityInitializer : FChildEntityInitializer
{
	explicit TChildEntityInitializer(TComponentTypeID<ParentComponentType> InParentComponent, TComponentTypeID<ChildComponentType> InChildComponent)
		: FChildEntityInitializer(InParentComponent, InChildComponent)
	{}

	TComponentTypeID<ParentComponentType> GetParentComponent() const
	{
		return ParentComponent.ReinterpretCast<ParentComponentType>();
	}

	TComponentTypeID<ChildComponentType> GetChildComponent() const
	{
		return ChildComponent.ReinterpretCast<ChildComponentType>();
	}

	TArrayView<const ParentComponentType> GetParentComponents(const FEntityAllocation* Allocation) const
	{
		return TRead<ParentComponentType>(GetParentComponent()).ResolveAsArray(Allocation);
	}

	TArrayView<ChildComponentType> GetChildComponents(const FEntityAllocation* Allocation) const
	{
		return TWrite<ChildComponentType>(GetChildComponent()).ResolveAsArray(Allocation);
	}
};

// Callback must be compatible with form void(const ParentComponentType, ChildComponentType&);
template<typename ParentComponentType, typename ChildComponentType, typename InitializerType>
struct TStaticChildEntityInitializer : TChildEntityInitializer<ParentComponentType, ChildComponentType>
{
	InitializerType Callback;

	explicit TStaticChildEntityInitializer(TComponentTypeID<ParentComponentType> InParentComponent, TComponentTypeID<ChildComponentType> InChildComponent, InitializerType InCallback)
		: TChildEntityInitializer<ParentComponentType, ChildComponentType>(InParentComponent, InChildComponent)
		, Callback(InCallback)
	{}

	virtual void Run(const FEntityRange& ChildRange, const FEntityAllocation* ParentAllocation, TArrayView<const int32> ParentAllocationOffsets)
	{
		TArrayView<const ParentComponentType> ParentComponents = TRead<ParentComponentType>(this->GetParentComponent()).ResolveAsArray(ParentAllocation);
		TArrayView<ChildComponentType>        ChildComponents  = TWrite<ChildComponentType>(this->GetChildComponent()).ResolveAsArray(ChildRange.Allocation);

		for (int32 Index = 0; Index < ChildRange.Num; ++Index)
		{
			const int32 ParentIndex = ParentAllocationOffsets[Index];
			const int32 ChildIndex  = ChildRange.ComponentStartOffset + Index;

			Callback(ParentComponents[ParentIndex], ChildComponents[ChildIndex]);
		}
	}
};

template<typename ComponentType>
struct TDuplicateChildEntityInitializer : FChildEntityInitializer
{
	explicit TDuplicateChildEntityInitializer(TComponentTypeID<ComponentType> InComponent)
		: FChildEntityInitializer(InComponent, InComponent)
	{}

	TComponentTypeID<ComponentType> GetComponent() const
	{
		return ParentComponent.ReinterpretCast<ComponentType>();
	}

	virtual void Run(const FEntityRange& ChildRange, const FEntityAllocation* ParentAllocation, TArrayView<const int32> ParentAllocationOffsets)
	{
		TArrayView<const ComponentType> ParentComponents = TRead<ComponentType>(GetComponent()).ResolveAsArray(ParentAllocation);
		TArrayView<ComponentType>       ChildComponents  = TWrite<ComponentType>(GetComponent()).ResolveAsArray(ChildRange.Allocation).Slice(ChildRange.ComponentStartOffset, ChildRange.Num);

		for (int32 Index = 0; Index < ParentAllocationOffsets.Num(); ++Index)
		{
			ChildComponents[Index] = ParentComponents[ParentAllocationOffsets[Index]];
		}
	}
};

struct FObjectFactoryBatch : FChildEntityFactory
{
	void Add(int32 EntityIndex, UObject* BoundObject);

	virtual void GenerateDerivedType(FComponentMask& OutNewEntityType) override;

	virtual void InitializeAllocation(UMovieSceneEntitySystemLinker* Linker, const FComponentMask& ParentType, const FComponentMask& ChildType, const FEntityAllocation* ParentAllocation, TArrayView<const int32> ParentAllocationOffsets, const FEntityRange& InChildEntityRange) override;

	virtual void PostInitialize(UMovieSceneEntitySystemLinker* InLinker) override;

	virtual void ResolveObjects(FInstanceRegistry* InstanceRegistry, FInstanceHandle InstanceHandle, int32 InEntityIndex, const FGuid& ObjectBinding) = 0;

	TMap<TTuple<UObject*, FMovieSceneEntityID>, FMovieSceneEntityID>* StaleEntitiesToPreserve;

private:
	TSortedMap<FMovieSceneEntityID, FMovieSceneEntityID> PreservedEntities;
	TArray<UObject*> ObjectsToAssign;
};

struct FBoundObjectTask
{
	FBoundObjectTask(UMovieSceneEntitySystemLinker* InLinker);
	virtual ~FBoundObjectTask(){}

	virtual FObjectFactoryBatch& AddBatch(const FEntityAllocation* Parent) = 0;
	virtual void Apply() = 0;

	void ForEachAllocation(const FEntityAllocation* Allocation, FReadEntityIDs EntityIDAccessor, TRead<FInstanceHandle> InstanceAccessor, TRead<FGuid> ObjectBindingAccessor);

	void PostTask();

private:

	TMap<TTuple<UObject*, FMovieSceneEntityID>, FMovieSceneEntityID> StaleEntitiesToPreserve;
	TArray<FMovieSceneEntityID> EntitiesToDiscard;

protected:

	UMovieSceneEntitySystemLinker* Linker;
};

template<typename BatchType>
struct TBoundObjectTask : FBoundObjectTask
{
	TBoundObjectTask(UMovieSceneEntitySystemLinker* InLinker)
		: FBoundObjectTask(InLinker)
	{}

private:

	virtual FObjectFactoryBatch& AddBatch(const FEntityAllocation* Parent) override
	{
		return Batches.Add(Parent);
	}

	virtual void Apply() override
	{
		for (TTuple<const FEntityAllocation*, BatchType>& Pair : Batches)
		{
			// Determine the type for the new entities
			const FEntityAllocation* ParentAllocation = Pair.Key;
			if (Pair.Value.Num() != 0)
			{
				Pair.Value.Apply(Linker, ParentAllocation);
			}
		}
	}

	TMap<const FEntityAllocation*, BatchType> Batches;
};



template<typename ComponentTypeA, typename ComponentTypeB>
struct TMutualEntityInitializer : FMutualEntityInitializer
{
	using CallbackType = void (*)(ComponentTypeA* ComponentsA, ComponentTypeB* ComponentsB, int32 Num);

	explicit TMutualEntityInitializer(TComponentTypeID<ComponentTypeA> InComponentA, TComponentTypeID<ComponentTypeB> InComponentB, CallbackType InCallback)
		: FMutualEntityInitializer(InComponentA, InComponentB)
		, Callback(InCallback)
	{}

private:

	virtual void Run(const FEntityRange& Range) override
	{
		TEntityRange<ComponentTypeA, ComponentTypeB> Iter = FEntityTaskBuilder()
			.Write(ComponentA.ReinterpretCast<ComponentTypeA>())
			.Write(ComponentB.ReinterpretCast<ComponentTypeB>())
			.IterateRange(Range);

		Callback(Iter.template GetRawUnchecked<0>(), Iter.template GetRawUnchecked<1>(), Iter.Num());
	}

	CallbackType Callback;
};




struct FEntityFactories
{
	void DefineChildComponent(FComponentTypeID InChildComponent)
	{
		ParentToChildComponentTypes.AddUnique(FComponentTypeID::Invalid(), InChildComponent);
	}

	void DefineChildComponent(FComponentTypeID InParentComponent, FComponentTypeID InChildComponent)
	{
		ParentToChildComponentTypes.AddUnique(InParentComponent, InChildComponent);
	}

	void DefineChildComponent(TInlineValue<FChildEntityInitializer>&& InInitializer)
	{
		check(InInitializer.IsValid());

		DefineChildComponent(InInitializer->GetParentComponent(), InInitializer->GetChildComponent());
		// Note: after this line, InInitializer is reset
		ChildInitializers.Add(MoveTemp(InInitializer));
	}

	template<typename ComponentType>
	void DuplicateChildComponent(TComponentTypeID<ComponentType> InComponent)
	{
		DefineChildComponent(TDuplicateChildEntityInitializer<ComponentType>(InComponent));
	}

	template<typename ParentComponent, typename ChildComponent, typename InitializerCallback>
	void DefineChildComponent(TComponentTypeID<ParentComponent> InParentType, TComponentTypeID<ChildComponent> InChildType, InitializerCallback&& InInitializer)
	{
		using FInitializer = TStaticChildEntityInitializer<ParentComponent, ChildComponent, InitializerCallback>;

		DefineChildComponent(InParentType, InChildType);
		ChildInitializers.Add(FInitializer(InParentType, InChildType, Forward<InitializerCallback>(InInitializer)));
	}

	// Indicate that if component A exists, component B must also exist on an entity.
	// @note: the inverse is not implied (ie B can still exist without A)
	void DefineMutuallyInclusiveComponent(FComponentTypeID InComponentA, FComponentTypeID InComponentB)
	{
		MutualInclusivityGraph.AllocateNode(InComponentA.BitIndex());
		MutualInclusivityGraph.AllocateNode(InComponentB.BitIndex());
		MutualInclusivityGraph.MakeEdge(InComponentA.BitIndex(), InComponentB.BitIndex());
	}

	void DefineMutuallyInclusiveComponent(TInlineValue<FMutualEntityInitializer>&& InInitializer)
	{
		check(InInitializer.IsValid());

		DefineChildComponent(InInitializer->GetComponentA(), InInitializer->GetComponentB());
		// Note: after this line, InInitializer is reset
		MutualInitializers.Add(MoveTemp(InInitializer));
	}

	int32 ComputeChildComponents(const FComponentMask& ParentComponentMask, FComponentMask& ChildComponentMask)
	{
		int32 NumNewComponents = 0;

		// Any child components keyed off an invalid parent component type are always relevant
		for (auto Child = ParentToChildComponentTypes.CreateConstKeyIterator(FComponentTypeID::Invalid()); Child; ++Child)
		{
			if (!ChildComponentMask.Contains(Child.Value()))
			{
				ChildComponentMask.Set(Child.Value());
				++NumNewComponents;
			}
		}

		for (FComponentMaskIterator It = ParentComponentMask.Iterate(); It; ++It)
		{
			FComponentTypeID ParentComponent = FComponentTypeID::FromBitIndex(It.GetIndex());
			for (auto Child = ParentToChildComponentTypes.CreateConstKeyIterator(ParentComponent); Child; ++Child)
			{
				if (!ChildComponentMask.Contains(Child.Value()))
				{
					ChildComponentMask.Set(Child.Value());
					++NumNewComponents;
				}
			}
		}

		return NumNewComponents;
	}

	int32 ComputeMutuallyInclusiveComponents(FComponentMask& ComponentMask)
	{
		FMovieSceneEntitySystemDirectedGraph::FBreadthFirstSearch BFS(&MutualInclusivityGraph);

		int32 NumNewComponents = 0;

		for (FComponentMaskIterator It = ComponentMask.Iterate(); It; ++It)
		{
			const uint16 NodeID = static_cast<uint16>(It.GetIndex());
			if (MutualInclusivityGraph.IsNodeAllocated(NodeID))
			{
				BFS.Search(NodeID);
			}
		}

		// Ideally would do a bitwise OR here
		for (TConstSetBitIterator<> It(BFS.GetVisited()); It; ++It)
		{
			FComponentTypeID ComponentType = FComponentTypeID::FromBitIndex(It.GetIndex());
			if (!ComponentMask.Contains(ComponentType))
			{
				ComponentMask.Set(ComponentType);
				++NumNewComponents;
			}
		}
		return NumNewComponents;
	}

	void RunInitializers(const FComponentMask& ParentType, const FComponentMask& ChildType, const FEntityAllocation* ParentAllocation, TArrayView<const int32> ParentAllocationOffsets, const FEntityRange& InChildEntityRange);

	TArray<TInlineValue<FChildEntityInitializer>> ChildInitializers;
	TArray<TInlineValue<FMutualEntityInitializer>> MutualInitializers;

	TMultiMap<FComponentTypeID, FComponentTypeID> ParentToChildComponentTypes;
	FMovieSceneEntitySystemDirectedGraph MutualInclusivityGraph;
};



}	// using namespace MovieScene
}	// using namespace UE