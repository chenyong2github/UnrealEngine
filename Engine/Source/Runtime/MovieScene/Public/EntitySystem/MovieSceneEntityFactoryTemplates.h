// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Containers/Map.h"
#include "Containers/SortedMap.h"

#include "EntitySystem/MovieSceneEntityIDs.h"
#include "EntitySystem/MovieSceneEntityRange.h"
#include "EntitySystem/MovieSceneEntityFactory.h"
#include "EntitySystem/MovieSceneEntitySystemTask.h"
#include "EntitySystem/MovieSceneComponentRegistry.h"
#include "EntitySystem/MovieSceneEntitySystemTypes.h"
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

	TComponentLock<TRead<ParentComponentType>> GetParentComponents(const FEntityAllocation* Allocation) const
	{
		return Allocation->ReadComponents(GetParentComponent());
	}

	TComponentLock<TWrite<ChildComponentType>> GetChildComponents(const FEntityAllocation* Allocation) const
	{
		return Allocation->WriteComponents(GetChildComponent(), FEntityAllocationWriteContext::NewAllocation());
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
		TComponentLock<TRead<ParentComponentType>> ParentComponents = ParentAllocation->ReadComponents(this->GetParentComponent());
		TComponentLock<TWrite<ChildComponentType>> ChildComponents  = ChildRange.Allocation->WriteComponents(this->GetChildComponent(), FEntityAllocationWriteContext::NewAllocation());

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
		TComponentLock<TRead<ComponentType>>  ParentComponents = ParentAllocation->ReadComponents(GetComponent());
		TComponentLock<TWrite<ComponentType>> ChildComponents  = ChildRange.Allocation->WriteComponents(GetComponent(), FEntityAllocationWriteContext::NewAllocation());

		TArrayView<ComponentType> ChildComponentSlice = ChildComponents.Slice(ChildRange.ComponentStartOffset, ChildRange.Num);
		for (int32 Index = 0; Index < ParentAllocationOffsets.Num(); ++Index)
		{
			ChildComponentSlice[Index] = ParentComponents[ParentAllocationOffsets[Index]];
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

	void ForEachAllocation(const FEntityAllocation* Allocation, FReadEntityIDs EntityIDs, TRead<FInstanceHandle> Instances, TRead<FGuid> ObjectBindings);

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
		TComponentLock<TWrite<ComponentTypeA>> A = Range.Allocation->WriteComponents(ComponentA.ReinterpretCast<ComponentTypeA>(), FEntityAllocationWriteContext::NewAllocation());
		TComponentLock<TWrite<ComponentTypeB>> B = Range.Allocation->WriteComponents(ComponentB.ReinterpretCast<ComponentTypeB>(), FEntityAllocationWriteContext::NewAllocation());

		Callback(&A[Range.ComponentStartOffset], &B[Range.ComponentStartOffset], Range.Num);
	}

	CallbackType Callback;
};



template<typename ComponentType>
inline void FEntityFactories::DuplicateChildComponent(TComponentTypeID<ComponentType> InComponent)
{
	DefineChildComponent(TDuplicateChildEntityInitializer<ComponentType>(InComponent));
}

template<typename ParentComponent, typename ChildComponent, typename InitializerCallback>
inline void FEntityFactories::DefineChildComponent(TComponentTypeID<ParentComponent> InParentType, TComponentTypeID<ChildComponent> InChildType, InitializerCallback&& InInitializer)
{
	using FInitializer = TStaticChildEntityInitializer<ParentComponent, ChildComponent, InitializerCallback>;

	DefineChildComponent(InParentType, InChildType);
	ChildInitializers.Add(FInitializer(InParentType, InChildType, Forward<InitializerCallback>(InInitializer)));
}

template<typename... ComponentTypes>
inline void FEntityFactories::DefineComplexInclusiveComponents(const FComplexInclusivityFilter& InFilter, ComponentTypes... InComponents)
{
	FComponentMask ComponentsToInclude { InComponents... };
	FComplexInclusivity NewComplexInclusivity { InFilter, ComponentsToInclude };
	DefineComplexInclusiveComponents(NewComplexInclusivity);
}

template<typename T>
TComponentTypeID<T> FComponentRegistry::NewComponentType(const TCHAR* const DebugName, EComponentTypeFlags Flags)
{
	static const uint32 ComponentTypeSize = sizeof(T);
	static_assert(ComponentTypeSize < TNumericLimits<decltype(FComponentTypeInfo::Sizeof)>::Max(), "Type too large to be used as component data");

	static const uint32 Alignment = alignof(T);
	static_assert(Alignment < TNumericLimits<decltype(FComponentTypeInfo::Alignment)>::Max(), "Type alignment too large to be used as component data");

	FComponentTypeInfo NewTypeInfo;

	NewTypeInfo.Sizeof                     = ComponentTypeSize;
	NewTypeInfo.Alignment                  = Alignment;
	NewTypeInfo.bIsZeroConstructType       = TIsZeroConstructType<T>::Value;
	NewTypeInfo.bIsTriviallyDestructable   = TIsTriviallyDestructible<T>::Value;
	NewTypeInfo.bIsTriviallyCopyAssignable = TIsTriviallyCopyAssignable<T>::Value;
	NewTypeInfo.bIsPreserved               = EnumHasAnyFlags(Flags, EComponentTypeFlags::Preserved);
	NewTypeInfo.bIsMigratedToOutput        = EnumHasAnyFlags(Flags, EComponentTypeFlags::MigrateToOutput);
	NewTypeInfo.bIsCopiedToOutput          = EnumHasAnyFlags(Flags, EComponentTypeFlags::CopyToOutput);
	NewTypeInfo.bHasReferencedObjects      = !TIsSame< FNotImplemented*, decltype( AddReferencedObjectForComponent((FReferenceCollector*)0, (T*)0) ) >::Value;

#if UE_MOVIESCENE_ENTITY_DEBUG
	NewTypeInfo.DebugInfo                = MakeUnique<FComponentTypeDebugInfo>();
	NewTypeInfo.DebugInfo->DebugName     = DebugName;
	NewTypeInfo.DebugInfo->DebugTypeName = GetGeneratedTypeName<T>();
	NewTypeInfo.DebugInfo->Type          = TComponentDebugType<T>::Type;
#endif

	if (!NewTypeInfo.bIsZeroConstructType || !NewTypeInfo.bIsTriviallyDestructable || !NewTypeInfo.bIsTriviallyCopyAssignable || NewTypeInfo.bHasReferencedObjects)
	{
		NewTypeInfo.MakeComplexComponentOps<T>();
	}

	FComponentTypeID    NewTypeID = NewComponentTypeInternal(MoveTemp(NewTypeInfo));
	TComponentTypeID<T> TypedTypeID = NewTypeID.ReinterpretCast<T>();

	if (EnumHasAnyFlags(Flags, EComponentTypeFlags::CopyToChildren))
	{
		Factories.DefineChildComponent(TDuplicateChildEntityInitializer<T>(TypedTypeID));
	}

	return TypedTypeID;
}

}	// using namespace MovieScene
}	// using namespace UE
