// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundArrayRandomNode.h"
#include "CoreMinimal.h"
#include "Internationalization/Text.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundDataFactory.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundLog.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundPrimitives.h"
#include "MetasoundTrigger.h"
#include "Containers/CircularQueue.h"
#include "MetasoundArrayNodes.h"

#include <type_traits>

#define LOCTEXT_NAMESPACE "MetasoundFrontend_RandomArrayGet"

namespace Metasound
{
	FArrayRandomGet::FArrayRandomGet(int32 InSeed, int32 InMaxIndex, const TArray<float>& InWeights, int32 InNoRepeatOrder)
	{
		Init(InSeed, InMaxIndex, InWeights, InNoRepeatOrder);
	}

	void FArrayRandomGet::Init(int32 InSeed, int32 InMaxIndex, const TArray<float>& InWeights, int32 InNoRepeatOrder)
	{
		SetSeed(InSeed);
		MaxIndex = InMaxIndex;
		SetNoRepeatOrder(InNoRepeatOrder);
		check(!InNoRepeatOrder || !PreviousIndicesQueue->IsFull());
		RandomWeights = InWeights;
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
			PreviousIndicesQueue = MakeUnique<TCircularQueue<int32>>(InNoRepeatOrder + 2);
			PreviousIndices.Reset();
			NoRepeatOrder = InNoRepeatOrder;
		}
	}

	void FArrayRandomGet::SetRandomWeights(const TArray<float>& InRandomWeights)
	{
		RandomWeights = InRandomWeights;
	}

	void FArrayRandomGet::ResetSeed()
	{
		RandomStream.Reset();
	}

	float FArrayRandomGet::ComputeTotalWeight()
	{
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
		return TotalWeight;
	}

	// Returns the next random weighted value in the array indices. 
	int32 FArrayRandomGet::NextValue()
	{
		// First compute the total size of the weights
		float TotalWeight = ComputeTotalWeight();
		bool bNoWeights = false;
		if (TotalWeight == 0.0f)
		{
			// Reset the previous indices if we ran out of choices left after weighting
			if (PreviousIndices.Num() > 0)
			{
				PreviousIndices.Reset();
				PreviousIndicesQueue->Empty();
				TotalWeight = ComputeTotalWeight();
			}
			else
			{
				// If we don't have a random weights array, everything is equal weight so we only need to consider the total number of un-picked indices
				TotalWeight = (float)(FMath::Max(MaxIndex - PreviousIndices.Num(), 1));
				bNoWeights = true;
			}
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
			if (bNoWeights)
			{
				NextTotalWeight += 1.0f;
			}
			else
			{
				NextTotalWeight += RandomWeights[i % RandomWeights.Num()];
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
		if (NoRepeatOrder > 0)
		{
			if (PreviousIndices.Num() == NoRepeatOrder)
			{
				check(PreviousIndicesQueue->Count() == PreviousIndices.Num());
				int32 OldPrevIndex;
				PreviousIndicesQueue->Dequeue(OldPrevIndex);
				PreviousIndices.Remove(OldPrevIndex);
				check(PreviousIndices.Num() == NoRepeatOrder - 1);
			}

			check(PreviousIndices.Num() < NoRepeatOrder);
			check(!PreviousIndicesQueue->IsFull());

			bool bSuccess = PreviousIndicesQueue->Enqueue(ChosenIndex);
			check(bSuccess);
			check(!PreviousIndices.Contains(ChosenIndex));
			PreviousIndices.Add(ChosenIndex);
			check(PreviousIndicesQueue->Count() == PreviousIndices.Num());
		}

		return ChosenIndex;
	}

	FSharedStateRandomGetManager& FSharedStateRandomGetManager::Get()
	{
		static FSharedStateRandomGetManager RGM;
		return RGM;
	}

	void FSharedStateRandomGetManager::InitSharedState(uint32 InSharedStateId, int32 InSeed, int32 InNumElements, const TArray<float>& InWeights, int32 InNoRepeatOrder)
	{
		FScopeLock Lock(&CritSect);

		if (!RandomGets.Contains(InSharedStateId))
		{
			RandomGets.Add(InSharedStateId, MakeUnique<FArrayRandomGet>(InSeed, InNumElements, InWeights, InNoRepeatOrder));
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
