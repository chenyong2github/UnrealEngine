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


enum class EComplexInclusivityFilterMode
{
	AllOf,
	AnyOf
};


struct FComplexInclusivityFilter
{
	FComponentMask Mask;
	EComplexInclusivityFilterMode Mode;

	FComplexInclusivityFilter(const FComponentMask& InMask, EComplexInclusivityFilterMode InMode)
		: Mask(InMask), Mode(InMode)
	{}

	static FComplexInclusivityFilter All(std::initializer_list<FComponentTypeID> InComponentTypes)
	{
		return FComplexInclusivityFilter(FComponentMask(InComponentTypes), EComplexInclusivityFilterMode::AllOf);
	}

	static FComplexInclusivityFilter Any(std::initializer_list<FComponentTypeID> InComponentTypes)
	{
		return FComplexInclusivityFilter(FComponentMask(InComponentTypes), EComplexInclusivityFilterMode::AnyOf);
	}

	bool Match(FComponentMask Input) const
	{
		switch (Mode)
		{
			case EComplexInclusivityFilterMode::AllOf:
				{
					FComponentMask Temp = Mask;
					Temp.CombineWithBitwiseAND(Input, EBitwiseOperatorFlags::MaintainSize);
					return Temp == Mask;
				}
				break;
			case EComplexInclusivityFilterMode::AnyOf:
				{
					FComponentMask Temp = Mask;
					Temp.CombineWithBitwiseAND(Input, EBitwiseOperatorFlags::MaintainSize);
					return Temp.Find(true) != INDEX_NONE;
				}
				break;
			default:
				checkf(false, TEXT("Not implemented"));
				return false;
		}
	}
};


struct FComplexInclusivity
{
	FComplexInclusivityFilter Filter;
	FComponentMask ComponentsToInclude;
};

/**
 * A class that contains all the component factory relationships.
 *
 * A source component (imported from an entity provider) can trigger the creation of other components on
 * the same entity or on children entities of its entity.
 */
struct FEntityFactories
{
	/**
	 * Defines a component as something that should always be created on every child entity.
	 */
	void DefineChildComponent(FComponentTypeID InChildComponent)
	{
		ParentToChildComponentTypes.AddUnique(FComponentTypeID::Invalid(), InChildComponent);
	}

	/**
	 * Specifies that if a component is present on a parent entity, the given child component should
	 * be created on any child entity.
	 */
	void DefineChildComponent(FComponentTypeID InParentComponent, FComponentTypeID InChildComponent)
	{
		ParentToChildComponentTypes.AddUnique(InParentComponent, InChildComponent);
	}

	/**
	 * Makes the given component automatically copied from a parent entity to all its children entities.
	 */
	template<typename ComponentType>
	void DuplicateChildComponent(TComponentTypeID<ComponentType> InComponent)
	{
		DefineChildComponent(TDuplicateChildEntityInitializer<ComponentType>(InComponent));
	}

	/**
	 * Specifies that if a component is present on a parent entity, the given child component should
	 * be created on any child entity, and initialized with the given initializer.
	 */
	template<typename ParentComponent, typename ChildComponent, typename InitializerCallback>
	void DefineChildComponent(TComponentTypeID<ParentComponent> InParentType, TComponentTypeID<ChildComponent> InChildType, InitializerCallback&& InInitializer)
	{
		using FInitializer = TStaticChildEntityInitializer<ParentComponent, ChildComponent, InitializerCallback>;

		DefineChildComponent(InParentType, InChildType);
		ChildInitializers.Add(FInitializer(InParentType, InChildType, Forward<InitializerCallback>(InInitializer)));
	}

	/**
	 * Adds the definition for a child component. The helper methods above are easier and preferrable.
	 */
	MOVIESCENE_API void DefineChildComponent(TInlineValue<FChildEntityInitializer>&& InInitializer);

	/**
	 * Indicates that if the first component exists on an entity, the second component should be created on
	 * that entity too.
	 *
	 * @note: the inverse is not implied (ie B can still exist without A)
     */
	MOVIESCENE_API void DefineMutuallyInclusiveComponent(FComponentTypeID InComponentA, FComponentTypeID InComponentB);

	/**
	 * Specifies a mutual inclusivity relationship. The helper method above is easier and preferrable.
	 */
	MOVIESCENE_API void DefineMutuallyInclusiveComponent(TInlineValue<FMutualEntityInitializer>&& InInitializer);

	/**
	 * Specifies that if an entity matches the given filter, the specified components should be created on it.
	 */
	template<typename... ComponentTypes>
	void DefineComplexInclusiveComponents(const FComplexInclusivityFilter& InFilter, ComponentTypes... InComponents)
	{
		FComponentMask ComponentsToInclude { InComponents... };
		FComplexInclusivity NewComplexInclusivity { InFilter, ComponentsToInclude };
		DefineComplexInclusiveComponents(NewComplexInclusivity);
	}

	/**
	 * Specifies that if an entity matches the given filter, the specified component should be created on it.
	 */
	MOVIESCENE_API void DefineComplexInclusiveComponents(const FComplexInclusivityFilter& InFilter, FComponentTypeID InComponent);

	/**
	 * Defines a new complex inclusivity relationship. The helper methods above are easier and preferrable.
	 */
	MOVIESCENE_API void DefineComplexInclusiveComponents(const FComplexInclusivity& InInclusivity);

	/**
	 * Given a set of components on a parent entity, compute what components should exist on a child entity.
	 *
	 * This resolves all the parent-to-child relationships.
	 */
	MOVIESCENE_API int32 ComputeChildComponents(const FComponentMask& ParentComponentMask, FComponentMask& ChildComponentMask);

	/**
	 * Given a set of components on an entity, computes what other components should also exist on this entity.
	 *
	 * This resolves all the mutual and complex inclusivity relationships.
	 */
	MOVIESCENE_API int32 ComputeMutuallyInclusiveComponents(FComponentMask& ComponentMask);

	void RunInitializers(const FComponentMask& ParentType, const FComponentMask& ChildType, const FEntityAllocation* ParentAllocation, TArrayView<const int32> ParentAllocationOffsets, const FEntityRange& InChildEntityRange);

	TArray<TInlineValue<FChildEntityInitializer>> ChildInitializers;
	TArray<TInlineValue<FMutualEntityInitializer>> MutualInitializers;

	TMultiMap<FComponentTypeID, FComponentTypeID> ParentToChildComponentTypes;
	FMovieSceneEntitySystemDirectedGraph MutualInclusivityGraph;
	TArray<FComplexInclusivity> ComplexInclusivity;

	struct
	{
		FComponentMask AllMutualFirsts;
		FComponentMask AllComplexFirsts;
	}
	Masks;
};


}	// using namespace MovieScene
}	// using namespace UE
