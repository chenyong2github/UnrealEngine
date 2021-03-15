// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundArrayRandomNode.h"

#define LOCTEXT_NAMESPACE "MetasoundFrontend"

namespace Metasound
{
	namespace ArrayNodeRandomGetVertexNames
	{
		const FString& GetInputTriggerNextName()
		{
			static const FString Name = TEXT("Next");
			return Name;
		}

		const FString& GetInputTriggerResetName()
		{
			static const FString Name = TEXT("Reset");
			return Name;
		}

		const FString& GetInputRandomArrayName()
		{
			static const FString Name = TEXT("In Array");
			return Name;
		}

		const FString& GetInputWeightsName()
		{
			static const FString Name = TEXT("Weights");
			return Name;
		}

		const FString& GetInputSeedName()
		{
			static const FString Name = TEXT("Seed");
			return Name;
		}

		const FString& GetInputNoRepeatOrderName()
		{
			static const FString Name = TEXT("No Repeats");
			return Name;
		}

		const FString& GetInputEnableSharedStateName()
		{
			static const FString Name = TEXT("Enable Shared State");
			return Name;
		}

		const FString& GetOutputTriggerOnNextName()
		{
			static const FString Name = TEXT("On Next");
			return Name;
		}

		const FString& GetOutputTriggerOnResetName()
		{
			static const FString Name = TEXT("On Reset");
			return Name;
		}

		const FString& GetOutputValueName()
		{
			static const FString Name = TEXT("Value");
			return Name;
		}
	}

	FArrayRandomGet::FArrayRandomGet(int32 InSeed, int32 InMaxIndex, int32 InNoRepeatOrder)
	{
		Init(InSeed, InMaxIndex, InNoRepeatOrder);
	}

	void FArrayRandomGet::Init(int32 InSeed, int32 InMaxIndex, int32 InNoRepeatOrder)
	{
		SetSeed(InSeed);
		MaxIndex = InMaxIndex;
		SetNoRepeatOrder(InNoRepeatOrder);
		PreviousIndicesQueue = MakeUnique<TCircularQueue<int32>>(InNoRepeatOrder);
		PreviousIndices.Reset();

	}

	void FArrayRandomGet::SetSeed(int32 InSeed)
	{
		if (InSeed == INDEX_NONE)
		{
			RandomStream.Initialize(FPlatformTime::Cycles());
		}
		else
		{
			RandomStream.Initialize(InSeed);
		}

		ResetSeed();
	}

	void FArrayRandomGet::SetNoRepeatOrder(int32 InNoRepeatOrder)
	{
		// Make sure the no repeat order is between 0 and one less than our max index
		InNoRepeatOrder = FMath::Clamp(InNoRepeatOrder, 0, MaxIndex - 1);

		if (InNoRepeatOrder != NoRepeatOrder)
		{
			PreviousIndicesQueue = MakeUnique<TCircularQueue<int32>>(InNoRepeatOrder);
			PreviousIndices.Reset();
			NoRepeatOrder = InNoRepeatOrder;
		}
	}

	void FArrayRandomGet::ResetSeed()
	{
		RandomStream.Reset();
	}

	// Returns the next random weighted value in the array indices. 
	int32 FArrayRandomGet::NextValue()
	{
		// First compute the total size of the weights
		float TotalWeight = 0.0f;
		if (RandomWeights.Num() > 0)
		{
			for (int32 i = 0; i < MaxIndex; ++i)
			{
				// If the index exists in previous indices, continue
				if (PreviousIndices.Contains(i))
				{
					continue;
				}

				// We modulus on the weight array to determine weights for the input array
				// I.e. if weights is 2 elements, the weights will alternate in application to the input array
				TotalWeight += RandomWeights[i % RandomWeights.Num()];
			}
		}
		else
		{
			// If we don't have a random weights array, everything is equal weight so we only need to consider the total number of un-picked indices
			TotalWeight = (float)(FMath::Max(MaxIndex - PreviousIndices.Num(), 1));
		}

		// Make a random choice based on the total weight
		float Choice = RandomStream.FRandRange(0.0f, TotalWeight);

		// Now find the index this choice matches up to
		TotalWeight = 0.0f;
		int32 ChosenIndex = INDEX_NONE;
		for (int32 i = 0; i < MaxIndex; ++i)
		{
			if (PreviousIndices.Contains(i))
			{
				continue;
			}

			float NextTotalWeight = TotalWeight;
			if (RandomWeights.Num() > 0)
			{
				NextTotalWeight += RandomWeights[i % RandomWeights.Num()];
			}
			else
			{
				NextTotalWeight += 1.0f;
			}

			if (Choice >= TotalWeight && Choice < NextTotalWeight)
			{
				ChosenIndex = i;
				break;
			}
			TotalWeight = NextTotalWeight;
		}
		check(ChosenIndex != INDEX_NONE);

		// Dequeue and remove the oldest previous index
		if (PreviousIndices.Num() == NoRepeatOrder)
		{
			int32 OldPrevIndex;
			PreviousIndicesQueue->Dequeue(OldPrevIndex);
			PreviousIndices.Remove(OldPrevIndex);
			check(PreviousIndices.Num() == NoRepeatOrder - 1);
		}

		// We should only have less than *or* equal to the NoRepeatOrder
		check(PreviousIndices.Num() < NoRepeatOrder);
		PreviousIndicesQueue->Enqueue(ChosenIndex);
		check(!PreviousIndices.Contains(ChosenIndex));
		PreviousIndices.Add(ChosenIndex);

		return ChosenIndex;
	}

	FSharedStateRandomGetManager& FSharedStateRandomGetManager::Get()
	{
		static FSharedStateRandomGetManager RGM;
		return RGM;
	}

	void FSharedStateRandomGetManager::InitSharedState(uint32 InSharedStateId, int32 InSeed, int32 InNumElements, int32 InNoRepeatOrder)
	{
		FScopeLock Lock(&CritSect);

		if (!RandomGets.Contains(InSharedStateId))
		{
			RandomGets.Add(InSharedStateId, MakeUnique<FArrayRandomGet>(InSeed, InNumElements, InNoRepeatOrder));
		}
	}

	int32 FSharedStateRandomGetManager::NextValue(uint32 InSharedStateId)
	{
		FScopeLock Lock(&CritSect);
		TUniquePtr<FArrayRandomGet>* RG = RandomGets.Find(InSharedStateId);
		return (*RG)->NextValue();
	}

	void FSharedStateRandomGetManager::SetSeed(uint32 InSharedStateId, int32 InSeed)
	{
		FScopeLock Lock(&CritSect);
		TUniquePtr<FArrayRandomGet>* RG = RandomGets.Find(InSharedStateId);
		(*RG)->SetSeed(InSeed);
	}

	void FSharedStateRandomGetManager::SetNoRepeatOrder(uint32 InSharedStateId, int32 InNoRepeatOrder)
	{
		FScopeLock Lock(&CritSect);
		TUniquePtr<FArrayRandomGet>* RG = RandomGets.Find(InSharedStateId);
		(*RG)->SetNoRepeatOrder(InNoRepeatOrder);
	}

	void FSharedStateRandomGetManager::ResetSeed(uint32 InSharedStateId)
	{
		FScopeLock Lock(&CritSect);
		TUniquePtr<FArrayRandomGet>* RG = RandomGets.Find(InSharedStateId);
		(*RG)->ResetSeed();
	}

}

#undef LOCTEXT_NAMESPACE
