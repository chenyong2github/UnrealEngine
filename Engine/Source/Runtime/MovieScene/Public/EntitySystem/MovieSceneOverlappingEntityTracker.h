// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/Map.h"
#include "Containers/SparseArray.h"

#include "EntitySystem/MovieSceneEntityIDs.h"
#include "EntitySystem/MovieSceneEntitySystemTypes.h"
#include "EntitySystem/MovieSceneComponentAccessors.h"
#include "EntitySystem/MovieSceneEntitySystemTask.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/BuiltInComponentTypes.h"

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


/**
 * Templated utility class that assists in tracking the state of many -> one data relationships in an FEntityManager.
 * KeyType defines the component type which defines the key that determines whether an entity animates the same output.
 * OutputType defines the user-specfied data to be associated with the multiple inputs (ie, its output)
 * NOTE: Where KeyType is a UObject* it is recommended TOverlappingEntityTracker_BoundObject is used instead, as it provides
 * garbage collection and reference counting functions.
 */
template<typename KeyType, typename OutputType>
struct TOverlappingEntityTracker
{
	using ParamType = typename TCallTraits<KeyType>::ParamType;

	/**
	 * Update this tracker by iterating any entity that contains InKeyComponent, and matches the additional optional filter
	 * Only entities tagged as NeedsLink or NeedsUnlink are iterated, invalidating their outputs
	 */
	void Update(UMovieSceneEntitySystemLinker* Linker, TComponentTypeID<KeyType> InKeyComponent, const FEntityComponentFilter& InFilter)
	{
		FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();

		// Visit unlinked entities
		TFilteredEntityTask<>(TEntityTaskComponents<>())
		.CombineFilter(InFilter)
		.FilterAll({ BuiltInComponents->Tags.NeedsUnlink, InKeyComponent })
		.Iterate_PerAllocation(&Linker->EntityManager, [this](const FEntityAllocation* Allocation){ this->VisitUnlinkedAllocation(Allocation); });

		// Visit newly or re-linked entities
		FEntityTaskBuilder()
		.Read(InKeyComponent)
		.CombineFilter(InFilter)
		.FilterAll({ BuiltInComponents->Tags.NeedsLink })
		.Iterate_PerAllocation(&Linker->EntityManager, [this](const FEntityAllocation* Allocation, TRead<KeyType> ReadKeys){ this->VisitLinkedAllocation(Allocation, ReadKeys); });
	}

	/**
	 * Update this tracker by iterating any entity that contains InKeyComponent, and matches the additional optional filter
	 * Only entities tagged as NeedsUnlink are iterated, invalidating their outputs
	 */
	void UpdateUnlinkedOnly(UMovieSceneEntitySystemLinker* Linker, TComponentTypeID<KeyType> InKeyComponent, const FEntityComponentFilter& InFilter)
	{
		FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();

		// Visit unlinked entities
		TFilteredEntityTask<>(TEntityTaskComponents<>())
		.CombineFilter(InFilter)
		.FilterAll({ BuiltInComponents->Tags.NeedsUnlink, InKeyComponent })
		.Iterate_PerAllocation(&Linker->EntityManager, [this](const FEntityAllocation* Allocation){ this->VisitUnlinkedAllocation(Allocation); });
	}

	/**
	 * Update this tracker by (re)linking the specified allocation
	 */
	void VisitLinkedAllocation(const FEntityAllocation* Allocation, TRead<KeyType> ReadKeys)
	{
		VisitLinkedAllocationImpl(Allocation, ReadKeys);
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
	 *                  void InitializeOutput(KeyType Object, TArrayView<const FMovieSceneEntityID> Inputs, OutputType* Output, FEntityOutputAggregate Aggregate);
	 *
	 *                  // Called when an output has been updated with new inputs
	 *                  void UpdateOutput(KeyType Object, TArrayView<const FMovieSceneEntityID> Inputs, OutputType* Output, FEntityOutputAggregate Aggregate);
	 *
	 *                  // Called when all an output's inputs are no longer relevant, and as such the output should be destroyed (or restored)
	 *                  void DestroyOutput(KeyType Object, OutputType* Output, FEntityOutputAggregate Aggregate);
	 */
	template<typename HandlerType>
	void ProcessInvalidatedOutputs(HandlerType&& InHandler)
	{
		if (InvalidatedOutputs.Num() != 0)
		{
			TArray<FMovieSceneEntityID, TInlineAllocator<8>> InputArray;

			int32 NumRemoved = 0;

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
					if (NewOutputs.IsValidIndex(OutputIndex) && NewOutputs[OutputIndex] == true)
					{
						InHandler.InitializeOutput(Output.Key, InputArray, &Output.OutputData, Output.Aggregate);
					}
					else
					{
						InHandler.UpdateOutput(Output.Key, InputArray, &Output.OutputData, Output.Aggregate);
					}
				}
				else
				{
					InHandler.DestroyOutput(Output.Key, &Output.OutputData, Output.Aggregate);
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
			InHandler.DestroyOutput(Output.Key, &Output.OutputData, Output.Aggregate);
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

	void VisitLinkedAllocationImpl(const FEntityAllocation* Allocation, TRead<KeyType> Keys)
	{
		const int32 Num = Allocation->Num();

		const FMovieSceneEntityID* EntityIDs = Allocation->GetRawEntityIDs();

		if (Allocation->HasComponent(FBuiltInComponentTypes::Get()->Tags.RestoreState))
		{
			for (int32 Index = 0; Index < Num; ++Index)
			{
				const uint16 OutputIndex = MakeOutput(EntityIDs[Index], Keys[Index]);
				Outputs[OutputIndex].Aggregate.bNeedsRestoration = true;
			}
		}
		else
		{
			for (int32 Index = 0; Index < Num; ++Index)
			{
				MakeOutput(EntityIDs[Index], Keys[Index]);
			}
		}
	}

	void VisitUnlinkedAllocationImpl(const FEntityAllocation* Allocation)
	{
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

	static constexpr uint16 NO_OUTPUT = MAX_uint16;
};


template<typename OutputType>
struct TOverlappingEntityTracker_BoundObject : TOverlappingEntityTracker<UObject*, OutputType>
{
	using FOutput = typename TOverlappingEntityTracker<UObject*, OutputType>::FOutput;

	void CleanupGarbage()
	{
		for (int32 Index = this->Outputs.GetMaxIndex()-1; Index >= 0; --Index)
		{
			if (!this->Outputs.IsValidIndex(Index))
			{
				continue;
			}
			const uint16 OutputIndex = static_cast<uint16>(Index);

			FOutput& Output = this->Outputs[Index];
			if (FBuiltInComponentTypes::IsBoundObjectGarbage(Output.Key))
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
			UObject* Key = It.Key();
			if (FBuiltInComponentTypes::IsBoundObjectGarbage(Key))
			{
				It.RemoveCurrent();
			}
		}
	}

	void AddReferencedObjects(FReferenceCollector& ReferenceCollector)
	{
		ReferenceCollector.AddReferencedObjects(this->KeyToOutput);

		for (FOutput& Output : this->Outputs)
		{
			ReferenceCollector.AddReferencedObject(Output.Key);
		}
	}
};



} // namespace MovieScene
} // namespace UE
