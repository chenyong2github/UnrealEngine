// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/Map.h"
#include "Containers/SparseArray.h"
#include "Algo/AnyOf.h"

#include "EntitySystem/MovieSceneEntityIDs.h"
#include "EntitySystem/MovieSceneEntitySystemTypes.h"
#include "EntitySystem/MovieSceneComponentAccessors.h"
#include "EntitySystem/MovieSceneEntitySystemTask.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieSceneComponentTypeInfo.h"

namespace UE
{
namespace MovieScene
{

/**
 * Aggregate of multiple input entities for an output defined in a TOverlappingEntityTracker
 */
struct FEntityOutputAggregate
{
	bool bNeedsRestoration = false;
};


struct FGarbageTraits
{
	FORCEINLINE static constexpr bool IsGarbage(...)
	{
		return false;
	}
	FORCEINLINE static bool IsGarbage(UObject* InObject)
	{
		return FBuiltInComponentTypes::IsBoundObjectGarbage(InObject);
	}
	FORCEINLINE static constexpr void AddReferencedObjects(FReferenceCollector& ReferenceCollector, ...)
	{}
	template<typename T>
	FORCEINLINE static typename TEnableIf<TPointerIsConvertibleFromTo<T, volatile const UObject>::Value>::Type
		AddReferencedObjects(FReferenceCollector& ReferenceCollector, T*& InObjectPtr)
	{
		ReferenceCollector.AddReferencedObject(InObjectPtr);
	}
};

template<typename... T> struct TGarbageTraitsImpl;
template<typename... T> struct TOverlappingEntityInput;

template<typename... T, int... Indices>
struct TGarbageTraitsImpl<TIntegerSequence<int, Indices...>, T...>
{
	static constexpr bool bCanBeGarbage = (TPointerIsConvertibleFromTo<typename TDecay<typename TRemovePointer<T>::Type>::Type, UObject>::Value || ...);

	static bool IsGarbage(TOverlappingEntityInput<T...>& InParam)
	{
		return (FGarbageTraits::IsGarbage(InParam.Key.template Get<Indices>()) || ...);
	}

	static void AddReferencedObjects(FReferenceCollector& ReferenceCollector, TOverlappingEntityInput<T...>& InParam)
	{
		(FGarbageTraits::AddReferencedObjects(ReferenceCollector, InParam.Key.template Get<Indices>()), ...);
	}

	template<typename CallbackType>
	static void Unpack(const TTuple<T...>& InTuple, CallbackType&& Callback)
	{
		Callback(InTuple.template Get<Indices>()...);
	}
};


template<typename... T>
struct TOverlappingEntityInput
{
	using GarbageTraits = TGarbageTraitsImpl<TMakeIntegerSequence<int, sizeof...(T)>, T...>;

	TTuple<T...> Key;

	template<typename... ArgTypes>
	TOverlappingEntityInput(ArgTypes&&... InArgs)
		: Key(Forward<ArgTypes>(InArgs)...)
	{}

	friend uint32 GetTypeHash(const TOverlappingEntityInput<T...>& In)
	{
		return GetTypeHash(In.Key);
	}
	friend bool operator==(const TOverlappingEntityInput<T...>& A, const TOverlappingEntityInput<T...>& B)
	{
		return A.Key == B.Key;
	}

	template<typename CallbackType>
	void Unpack(CallbackType&& Callback)
	{
		GarbageTraits::Unpack(Key, MoveTemp(Callback));
	}
};


/**
 * Templated utility class that assists in tracking the state of many -> one data relationships in an FEntityManager.
 * InputKeyTypes defines the component type(s) which defines the key that determines whether an entity animates the same output.
 * OutputType defines the user-specfied data to be associated with the multiple inputs (ie, its output)
 * NOTE: Where any of InputKeyTypes is a UObject*, AddReferencedObjects and CleanupGarbage must be called
 */
template<typename OutputType, typename... InputKeyTypes>
struct TOverlappingEntityTrackerImpl
{
	using KeyType = TOverlappingEntityInput<InputKeyTypes...>;
	using ParamType = typename TCallTraits<KeyType>::ParamType;

	bool IsInitialized() const
	{
		return bIsInitialized;
	}

	/**
	 * Update this tracker by iterating any entity that contains InKeyComponent, and matches the additional optional filter
	 * Only entities tagged as NeedsLink or NeedsUnlink are iterated, invalidating their outputs
	 */
	void Update(UMovieSceneEntitySystemLinker* Linker, TComponentTypeID<InputKeyTypes>... InKeyComponents, const FEntityComponentFilter& InFilter)
	{
		check(bIsInitialized);

		FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();

		// Visit unlinked entities
		TFilteredEntityTask<>(TEntityTaskComponents<>())
		.CombineFilter(InFilter)
		.FilterAll({ BuiltInComponents->Tags.NeedsUnlink, FComponentTypeID(InKeyComponents)... })
		.Iterate_PerAllocation(&Linker->EntityManager, [this](const FEntityAllocation* Allocation){ this->VisitUnlinkedAllocation(Allocation); });

		// Visit newly or re-linked entities
		FEntityTaskBuilder()
		.ReadAllOf(InKeyComponents...)
		.CombineFilter(InFilter)
		.FilterAll({ BuiltInComponents->Tags.NeedsLink })
		.Iterate_PerAllocation(&Linker->EntityManager, [this](const FEntityAllocation* Allocation, TRead<InputKeyTypes>... ReadKeys){ this->VisitLinkedAllocation(Allocation, ReadKeys...); });
	}

	/**
	 * Update this tracker by iterating any entity that contains InKeyComponent, and matches the additional optional filter
	 * Only entities tagged as NeedsUnlink are iterated, invalidating their outputs
	 */
	void UpdateUnlinkedOnly(UMovieSceneEntitySystemLinker* Linker, TComponentTypeID<InputKeyTypes>... InKeyComponent, const FEntityComponentFilter& InFilter)
	{
		check(bIsInitialized);

		FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();

		// Visit unlinked entities
		TFilteredEntityTask<>(TEntityTaskComponents<>())
		.CombineFilter(InFilter)
		.FilterAll({ BuiltInComponents->Tags.NeedsUnlink, InKeyComponent... })
		.Iterate_PerAllocation(&Linker->EntityManager, [this](const FEntityAllocation* Allocation){ this->VisitUnlinkedAllocation(Allocation); });
	}

	/**
	 * Update this tracker by (re)linking the specified allocation
	 */
	void VisitLinkedAllocation(const FEntityAllocation* Allocation, TRead<InputKeyTypes>... ReadKeys)
	{
		VisitLinkedAllocationImpl(Allocation, ReadKeys...);
	}

	/**
	 * Update this tracker by unlinking the specified allocation
	 */
	void VisitUnlinkedAllocation(const FEntityAllocation* Allocation)
	{
		VisitUnlinkedAllocationImpl(Allocation);
	}

	/**
	 * Process any outputs that were invalidated as a result of Update being called using a custom handler.
	 *
	 * InHandler    Any user-defined handler type that contains the following named functions:
	 *                  // Called when an output is first created
	 *                  void InitializeOutput(InputKeyTypes... Inputs, TArrayView<const FMovieSceneEntityID> Inputs, OutputType* Output, FEntityOutputAggregate Aggregate);
	 *
	 *                  // Called when an output has been updated with new inputs
	 *                  void UpdateOutput(InputKeyTypes... Inputs, TArrayView<const FMovieSceneEntityID> Inputs, OutputType* Output, FEntityOutputAggregate Aggregate);
	 *
	 *                  // Called when all an output's inputs are no longer relevant, and as such the output should be destroyed (or restored)
	 *                  void DestroyOutput(InputKeyTypes... Inputs, OutputType* Output, FEntityOutputAggregate Aggregate);
	 */
	template<typename HandlerType>
	void ProcessInvalidatedOutputs(UMovieSceneEntitySystemLinker* Linker, HandlerType&& InHandler)
	{
		if (InvalidatedOutputs.Num() != 0)
		{
			TArray<FMovieSceneEntityID, TInlineAllocator<8>> InputArray;

			FComponentTypeID RestoreStateTag = FBuiltInComponentTypes::Get()->Tags.RestoreState;
			int32 NumRemoved = 0;

			auto RestoreStatePredicate = [Linker, RestoreStateTag](FMovieSceneEntityID InEntityID){ return Linker->EntityManager.HasComponent(InEntityID, RestoreStateTag); };

			check(InvalidatedOutputs.Num() < NO_OUTPUT);
			for (TConstSetBitIterator<> InvalidOutput(InvalidatedOutputs); InvalidOutput; ++InvalidOutput)
			{
				const uint16 OutputIndex = static_cast<uint16>(InvalidOutput.GetIndex());

				InputArray.Reset();
				for (auto Inputs = OutputToEntity.CreateConstKeyIterator(OutputIndex); Inputs; ++Inputs)
				{
					InputArray.Add(Inputs.Value());
				}

				FOutput& Output = Outputs[OutputIndex];
				if (InputArray.Num() > 0)
				{
					Output.Aggregate.bNeedsRestoration = Algo::AnyOf(InputArray, RestoreStatePredicate);

					if (NewOutputs.IsValidIndex(OutputIndex) && NewOutputs[OutputIndex] == true)
					{
						Output.Key.Unpack([&InHandler, &InputArray, &Output](typename TCallTraits<InputKeyTypes>::ParamType... InKeys){
							InHandler.InitializeOutput(InKeys..., InputArray, &Output.OutputData, Output.Aggregate);
						});
					}
					else
					{
						Output.Key.Unpack([&InHandler, &InputArray, &Output](typename TCallTraits<InputKeyTypes>::ParamType... InKeys){
							InHandler.UpdateOutput(InKeys..., InputArray, &Output.OutputData, Output.Aggregate);
						});
					}
				}
				else
				{
					Output.Key.Unpack([&InHandler, &Output](typename TCallTraits<InputKeyTypes>::ParamType... InKeys){
						InHandler.DestroyOutput(InKeys..., &Output.OutputData, Output.Aggregate);
					});
				}

				if (InputArray.Num() == 0)
				{
					KeyToOutput.Remove(Outputs[OutputIndex].Key);
					Outputs.RemoveAt(OutputIndex, 1);
				}
			}
		}

		InvalidatedOutputs.Empty();
		NewOutputs.Empty();
	}

	bool IsEmpty() const
	{
		return Outputs.Num() != 0;
	}

	/**
	 * Destroy all the outputs currently being tracked
	 */
	template<typename HandlerType>
	void Destroy(HandlerType&& InHandler)
	{
		for (FOutput& Output : Outputs)
		{
			Output.Key.Unpack([&InHandler, &Output](typename TCallTraits<InputKeyTypes>::ParamType... InKeys){
				InHandler.DestroyOutput(InKeys..., &Output.OutputData, Output.Aggregate);
			});
		}

		EntityToOutput.Empty();
		OutputToEntity.Empty();

		KeyToOutput.Empty();
		Outputs.Empty();

		InvalidatedOutputs.Empty();
		NewOutputs.Empty();
	}

	void FindEntityIDs(ParamType Key, TArray<FMovieSceneEntityID>& OutEntityIDs) const
	{
		if (const uint16* OutputIndex = KeyToOutput.Find(Key))
		{
			OutputToEntity.MultiFind(*OutputIndex, OutEntityIDs);
		}
	}

	const OutputType* FindOutput(FMovieSceneEntityID EntityID) const
	{
		if (const uint16* OutputIndex = EntityToOutput.Find(EntityID))
		{
			if (ensure(Outputs.IsValidIndex(*OutputIndex)))
			{
				return &Outputs[*OutputIndex].OutputData;
			}
		}
		return nullptr;
	}

	const OutputType* FindOutput(ParamType Key) const
	{
		if (const uint16* OutputIndex = KeyToOutput.Find(Key))
		{
			if (ensure(Outputs.IsValidIndex(*OutputIndex)))
			{
				return &Outputs[*OutputIndex].OutputData;
			}
		}
		return nullptr;
	}

	bool NeedsRestoration(ParamType Key, bool bEnsureOutput = false) const
	{
		const uint16 ExistingOutput = FindOutputByKey(Key);
		const bool bIsOutputValid = IsOutputValid(ExistingOutput);
		ensure(bIsOutputValid || !bEnsureOutput);
		if (bIsOutputValid)
		{
			return Outputs[ExistingOutput].Aggregate.bNeedsRestoration;
		}
		return false;
	}

	void SetNeedsRestoration(ParamType Key, bool bNeedsRestoration, bool bEnsureOutput = false)
	{
		const uint16 ExistingOutput = FindOutputByKey(Key);
		const bool bIsOutputValid = IsOutputValid(ExistingOutput);
		ensure(bIsOutputValid || !bEnsureOutput);
		if (bIsOutputValid)
		{
			Outputs[ExistingOutput].Aggregate.bNeedsRestoration = bNeedsRestoration;
		}
	}

protected:

	void VisitLinkedAllocationImpl(const FEntityAllocation* Allocation, TRead<InputKeyTypes>... Keys)
	{
		check(bIsInitialized);

		const int32 Num = Allocation->Num();

		const FMovieSceneEntityID* EntityIDs = Allocation->GetRawEntityIDs();

		if (Allocation->HasComponent(FBuiltInComponentTypes::Get()->Tags.RestoreState))
		{
			for (int32 Index = 0; Index < Num; ++Index)
			{
				const uint16 OutputIndex = MakeOutput(EntityIDs[Index], TOverlappingEntityInput<InputKeyTypes...>(Keys[Index]...));
				Outputs[OutputIndex].Aggregate.bNeedsRestoration = true;
			}
		}
		else
		{
			for (int32 Index = 0; Index < Num; ++Index)
			{
				MakeOutput(EntityIDs[Index], TOverlappingEntityInput<InputKeyTypes...>(Keys[Index]...));
			}
		}
	}

	void VisitUnlinkedAllocationImpl(const FEntityAllocation* Allocation)
	{
		check(bIsInitialized);

		const int32 Num = Allocation->Num();
		const FMovieSceneEntityID* EntityIDs = Allocation->GetRawEntityIDs();

		for (int32 Index = 0; Index < Num; ++Index)
		{
			ClearOutputByEntity(EntityIDs[Index]);
		}
	}

	uint16 MakeOutput(FMovieSceneEntityID EntityID, ParamType InKey)
	{
		// If this entity was already assigned an output, clear it
		ClearOutputByEntity(EntityID);

		const uint16 Output = CreateOutputByKey(InKey);

		EntityToOutput.Add(EntityID, Output);
		OutputToEntity.Add(Output, EntityID);

		return Output;
	}

	uint16 CreateOutputByKey(ParamType Key)
	{
		const uint16 ExistingOutput = FindOutputByKey(Key);
		if (ExistingOutput != NO_OUTPUT)
		{
			InvalidatedOutputs.PadToNum(ExistingOutput + 1, false);
			InvalidatedOutputs[ExistingOutput] = true;
			return ExistingOutput;
		}

		const int32 Index = Outputs.Add(FOutput{ Key, OutputType{} });
		check(Index < NO_OUTPUT);

		const uint16 NewOutput = static_cast<uint16>(Index);

		NewOutputs.PadToNum(NewOutput + 1, false);
		NewOutputs[NewOutput] = true;

		InvalidatedOutputs.PadToNum(NewOutput + 1, false);
		InvalidatedOutputs[NewOutput] = true;

		KeyToOutput.Add(Key, NewOutput);
		return NewOutput;
	}

	uint16 FindOutputByKey(ParamType Key) const
	{
		const uint16* OutputIndex = KeyToOutput.Find(Key);
		return OutputIndex ? *OutputIndex : NO_OUTPUT;
	}

	uint16 FindOutputByEntity(FMovieSceneEntityID EntityID) const
	{
		const uint16* OutputIndex = EntityToOutput.Find(EntityID);
		return OutputIndex ? *OutputIndex : NO_OUTPUT;
	}

	void ClearOutputByEntity(FMovieSceneEntityID EntityID)
	{
		const uint16 OutputIndex = FindOutputByEntity(EntityID);
		if (OutputIndex != NO_OUTPUT)
		{
			OutputToEntity.Remove(OutputIndex, EntityID);
			EntityToOutput.Remove(EntityID);

			InvalidatedOutputs.PadToNum(OutputIndex + 1, false);
			InvalidatedOutputs[OutputIndex] = true;
		}
	}

	bool IsOutputValid(uint16 OutputIndex)
	{
		return OutputIndex != NO_OUTPUT &&
			(!InvalidatedOutputs.IsValidIndex(OutputIndex) ||
			 !InvalidatedOutputs[OutputIndex]);
	}

	struct FOutput
	{
		KeyType Key;
		OutputType OutputData;
		FEntityOutputAggregate Aggregate;
	};

	TMap<FMovieSceneEntityID, uint16> EntityToOutput;
	TMultiMap<uint16, FMovieSceneEntityID> OutputToEntity;

	TMap<KeyType, uint16> KeyToOutput;
	TSparseArray< FOutput > Outputs;

	TBitArray<> InvalidatedOutputs, NewOutputs;

	bool bIsInitialized = false;

	static constexpr uint16 NO_OUTPUT = MAX_uint16;
};


template<typename OutputType, typename... KeyType>
struct TOverlappingEntityTracker_NoGarbage : TOverlappingEntityTrackerImpl<OutputType, KeyType...>
{
	void Initialize(UMovieSceneEntitySystem* OwningSystem)
	{
		this->bIsInitialized = true;
	}
};

template<typename OutputType, typename... KeyType>
struct TOverlappingEntityTracker_WithGarbage : TOverlappingEntityTrackerImpl<OutputType, KeyType...>
{
	using ThisType = TOverlappingEntityTracker_WithGarbage<OutputType, KeyType...>;
	using Super = TOverlappingEntityTrackerImpl<OutputType, KeyType...>;
	using typename Super::FOutput;
	using typename Super::KeyType;

	~TOverlappingEntityTracker_WithGarbage()
	{
		UMovieSceneEntitySystem* OwningSystem = WeakOwningSystem.GetEvenIfUnreachable();
		UMovieSceneEntitySystemLinker* Linker = OwningSystem ? OwningSystem->GetLinker() : nullptr;
		if (Linker)
		{
			Linker->Events.TagGarbage.RemoveAll(this);
			Linker->Events.AddReferencedObjects.RemoveAll(this);
		}
	}

	void Initialize(UMovieSceneEntitySystem* OwningSystem)
	{
		if (this->bIsInitialized)
		{
			return;
		}

		this->bIsInitialized = true;
		WeakOwningSystem = OwningSystem;

		OwningSystem->GetLinker()->Events.TagGarbage.AddRaw(this, &ThisType::CleanupGarbage);
		OwningSystem->GetLinker()->Events.AddReferencedObjects.AddRaw(this, &ThisType::AddReferencedObjects);
	}

	void CleanupGarbage(UMovieSceneEntitySystemLinker* Linker)
	{
		for (int32 Index = this->Outputs.GetMaxIndex()-1; Index >= 0; --Index)
		{
			if (!this->Outputs.IsValidIndex(Index))
			{
				continue;
			}
			const uint16 OutputIndex = static_cast<uint16>(Index);

			FOutput& Output = this->Outputs[Index];
			if (KeyType::GarbageTraits::IsGarbage(Output.Key))
			{
				this->Outputs.RemoveAt(Index, 1);

				for (auto It = this->OutputToEntity.CreateKeyIterator(OutputIndex); It; ++It)
				{
					this->EntityToOutput.Remove(It.Value());
					It.RemoveCurrent();
				}
			}
		}

		for (auto It = this->KeyToOutput.CreateIterator(); It; ++It)
		{
			if (KeyType::GarbageTraits::IsGarbage(It.Key()) || !this->Outputs.IsValidIndex(It.Value()))
			{
				It.RemoveCurrent();
			}
		}
	}

	void AddReferencedObjects(UMovieSceneEntitySystemLinker* Linker, FReferenceCollector& ReferenceCollector)
	{
		for (TPair<KeyType, uint16>& Pair : this->KeyToOutput)
		{
			KeyType::GarbageTraits::AddReferencedObjects(ReferenceCollector, Pair.Key);
		}

		if constexpr (THasAddReferencedObjectForComponent<KeyType>::Value || THasAddReferencedObjectForComponent<OutputType>::Value)
		{
			for (FOutput& Output : this->Outputs)
			{
				KeyType::GarbageTraits::AddReferencedObjects(ReferenceCollector, Output.Key);
				KeyType::GarbageTraits::AddReferencedObjects(ReferenceCollector, Output.OutputData);
			}
		}
	}

protected:

	TWeakObjectPtr<UMovieSceneEntitySystem> WeakOwningSystem;
};



template<typename OutputType, typename... KeyType>
using TOverlappingEntityTracker = typename TChooseClass<
	(THasAddReferencedObjectForComponent<KeyType>::Value || ...) || THasAddReferencedObjectForComponent<OutputType>::Value,
	TOverlappingEntityTracker_WithGarbage<OutputType, KeyType...>,
	TOverlappingEntityTracker_NoGarbage<OutputType, KeyType...>
>::Result;

} // namespace MovieScene
} // namespace UE
