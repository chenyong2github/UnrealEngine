// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundArrayNodes.h"

#define LOCTEXT_NAMESPACE "MetasoundFrontend"

namespace Metasound
{
	namespace MetasoundArrayNodesPrivate
	{
		FNodeClassMetadata CreateArrayNodeClassMetadata(const FName& InDataTypeName, const FName& InOperatorName, const FText& InDisplayName, const FText& InDescription, const FVertexInterface& InDefaultInterface)
		{
			FNodeClassMetadata Metadata
			{
				FNodeClassName{FName("Array"), InOperatorName, InDataTypeName},
				1, // Major Version
				0, // Minor Version
				InDisplayName, 
				InDescription,
				PluginAuthor,
				PluginNodeMissingPrompt,
				InDefaultInterface,
				{LOCTEXT("ArrayCategory", "Array")},
				{TEXT("Array")},
				FNodeDisplayStyle{}
			};

			return Metadata;
		}
	}

	namespace ArrayNodeVertexNames
	{
		/* Input Vertx Names */
		const FString& GetInputArrayName()
		{
			static const FString Name = TEXT("Array");
			return Name;
		}

		const FString& GetInputLeftArrayName()
		{
			static const FString Name = TEXT("Left Array");
			return Name;
		}

		const FString& GetInputRightArrayName()
		{
			static const FString Name = TEXT("Right Array");
			return Name;
		}

		const FString& GetInputTriggerName()
		{
			static const FString Name = TEXT("Trigger");
			return Name;
		}

		const FString& GetInputIndexName()
		{
			static const FString Name = TEXT("Index");
			return Name;
		}

		const FString& GetInputStartIndexName()
		{
			static const FString Name = TEXT("Start Index");
			return Name;
		}

		const FString& GetInputEndIndexName()
		{
			static const FString Name = TEXT("End Index");
			return Name;
		}

		const FString& GetInputValueName()
		{
			static const FString Name = TEXT("Value");
			return Name;
		}

		/* Output Vertex Names */
		const FString& GetOutputNumName()
		{
			static const FString Name = TEXT("Num");
			return Name;
		}

		const FString& GetOutputValueName()
		{
			static const FString Name = TEXT("Element");
			return Name;
		}

		const FString& GetOutputArrayName()
		{
			static const FString Name = TEXT("Array");
			return Name;
		}
	}

	namespace ArrayNodeShuffleVertexNames
	{
		const FString& GetInputTriggerNextName()
		{
			static const FString Name = TEXT("Next");
			return Name;
		}

		const FString& GetInputTriggerShuffleName()
		{
			static const FString Name = TEXT("Shuffle");
			return Name;
		}

		const FString& GetInputTriggerResetName()
		{
			static const FString Name = TEXT("Reset Seed");
			return Name;
		}

		const FString& GetInputShuffleArrayName()
		{
			static const FString Name = TEXT("In Array");
			return Name;
		}

		const FString& GetInputSeedName()
		{
			static const FString Name = TEXT("Seed");
			return Name;
		}

		const FString& GetInputAutoShuffleName()
		{
			static const FString Name = TEXT("Auto Shuffle");
			return Name;
		}

		const FString& GetInputNamespaceName()
		{
			static const FString Name = TEXT("Shuffle Namespace");
			return Name;
		}

		const FString& GetOutputTriggerOnNextName()
		{
			static const FString Name = TEXT("On Next");
			return Name;
		}

		const FString& GetOutputTriggerOnShuffleName()
		{
			static const FString Name = TEXT("On Shuffle");
			return Name;
		}

		const FString& GetOutputTriggerOnResetName()
		{
			static const FString Name = TEXT("On Reset Seed");
			return Name;
		}

		const FString& GetOutputValueName()
		{
			static const FString Name = TEXT("Value");
			return Name;
		}
	}

	uint32 GetTypeHash(const FGlobalArrayShuffleKey& InKey)
	{
		return InKey.Hash;
	}

	/**
	* FArrayIndexShuffler
	*/

	FArrayIndexShuffler::FArrayIndexShuffler(int32 InSeed, int32 InMaxIndicies)
	{
		Init(InSeed, InMaxIndicies);
	}

	void FArrayIndexShuffler::Init(int32 InSeed, int32 InMaxIndicies)
	{
		SetSeed(InSeed);
		if (InMaxIndicies > 0)
		{
			ShuffleIndices.AddUninitialized(InMaxIndicies);
			for (int32 i = 0; i < ShuffleIndices.Num(); ++i)
			{
				ShuffleIndices[i] = i;
			}
			ShuffleArray();
		}
	}

	void FArrayIndexShuffler::SetSeed(int32 InSeed)
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

	void FArrayIndexShuffler::ResetSeed()
	{
		RandomStream.Reset();
	}

	bool FArrayIndexShuffler::NextValue(bool bAutoShuffle, int32& OutIndex)
	{
		bool bShuffled = false;
		if (CurrentIndex == ShuffleIndices.Num())
		{
			if (bAutoShuffle)
			{
				ShuffleArray();
				bShuffled = true;
			}
			else
			{
				CurrentIndex = 0;
			}
		}

		check(CurrentIndex < ShuffleIndices.Num());
		PrevValue = ShuffleIndices[CurrentIndex];
		OutIndex = PrevValue;
		++CurrentIndex;

		return bShuffled;
	}

	// Shuffle the array with the given max indices
	void FArrayIndexShuffler::ShuffleArray()
	{
		// Randomize the shuffled array by randomly swapping indicies
		for (int32 i = 0; i < ShuffleIndices.Num(); ++i)
		{
			RandomSwap(i, 0, ShuffleIndices.Num() - 1);
		}

		// Reset the current index back to 0
		CurrentIndex = 0;

		// Fix up the new current index if the value is our previous value and we have an array larger than 1
		if (ShuffleIndices.Num() > 1 && ShuffleIndices[CurrentIndex] == PrevValue)
		{
			RandomSwap(0, 1, ShuffleIndices.Num() - 1);
		}
	}

	void FArrayIndexShuffler::RandomSwap(int32 InCurrentIndex, int32 InStartIndex, int32 InEndIndex)
	{
		int32 ShuffleIndex = RandomStream.RandRange(InStartIndex, InEndIndex);
		int32 Temp = ShuffleIndices[ShuffleIndex];
		ShuffleIndices[ShuffleIndex] = ShuffleIndices[InCurrentIndex];
		ShuffleIndices[InCurrentIndex] = Temp;
	}

	/** 
	* FGlobalArrayShuffleManager
	*/

	FGlobalArrayShuffleManager& FGlobalArrayShuffleManager::Get()
	{
		static FGlobalArrayShuffleManager GShufflerManager;
		return GShufflerManager;
	}

	bool FGlobalArrayShuffleManager::NextValue(FGlobalArrayShuffleKey& InKey, bool bAutoShuffle, int32& OutIndex)
	{
		check(!InKey.Namespace.IsEmpty());

		FScopeLock Lock(&CritSect);

		TUniquePtr<FArrayIndexShuffler>& ShufflerPtr = GetShuffler(InKey);
		return ShufflerPtr->NextValue(bAutoShuffle, OutIndex);
	}

	void FGlobalArrayShuffleManager::SetSeed(FGlobalArrayShuffleKey& InKey, int32 InSeed)
	{
		check(!InKey.Namespace.IsEmpty());

		FScopeLock Lock(&CritSect);

		TUniquePtr<FArrayIndexShuffler>& ShufflerPtr = GetShuffler(InKey);
		ShufflerPtr->SetSeed(InSeed);
	}

	void FGlobalArrayShuffleManager::ResetSeed(FGlobalArrayShuffleKey& InKey)
	{
		check(!InKey.Namespace.IsEmpty());

		FScopeLock Lock(&CritSect);

		TUniquePtr<FArrayIndexShuffler>& ShufflerPtr = GetShuffler(InKey);
		ShufflerPtr->ResetSeed();
	}

	void FGlobalArrayShuffleManager::ShuffleArray(FGlobalArrayShuffleKey& InKey)
	{
		check(!InKey.Namespace.IsEmpty());

		FScopeLock Lock(&CritSect);

		TUniquePtr<FArrayIndexShuffler>& ShufflerPtr = GetShuffler(InKey);
		ShufflerPtr->ShuffleArray();
	}

	TUniquePtr<FArrayIndexShuffler>& FGlobalArrayShuffleManager::GetShuffler(FGlobalArrayShuffleKey& InKey)
	{
		TUniquePtr<FArrayIndexShuffler>& ShufflerPtr = GlobalShuffleIndicies.FindOrAdd(InKey);
		if (!ShufflerPtr.IsValid())
		{
			ShufflerPtr = MakeUnique<FArrayIndexShuffler>(InKey.Seed, InKey.NumElements);
		}
		return ShufflerPtr;
	}
}

#undef LOCTEXT_NAMESPACE
