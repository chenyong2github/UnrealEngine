// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Internationalization/Text.h"
#include "MetasoundArrayNodes.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundDataFactory.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundLog.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundPrimitives.h"
#include "MetasoundSourceInterface.h"
#include "MetasoundTrigger.h"
#include "MetasoundVertex.h"
#include "Misc/ScopeLock.h"

#include <type_traits>

#define LOCTEXT_NAMESPACE "MetasoundFrontend"


namespace Metasound
{
	/** Shuffle Node Vertex Names */
	namespace ArrayNodeShuffleVertexNames
	{
		/** Input Vertex Names */
		METASOUNDFRONTEND_API const FVertexName& GetInputTriggerNextName();
		METASOUNDFRONTEND_API const FVertexName& GetInputTriggerShuffleName();
		METASOUNDFRONTEND_API const FVertexName& GetInputTriggerResetName();
		METASOUNDFRONTEND_API const FVertexName& GetInputShuffleArrayName();
		METASOUNDFRONTEND_API const FVertexName& GetInputSeedName();
		METASOUNDFRONTEND_API const FVertexName& GetInputAutoShuffleName();
		METASOUNDFRONTEND_API const FVertexName& GetInputEnableSharedStateName();

		METASOUNDFRONTEND_API const FVertexName& GetOutputTriggerOnNextName();
		METASOUNDFRONTEND_API const FVertexName& GetOutputTriggerOnShuffleName();
		METASOUNDFRONTEND_API const FVertexName& GetOutputTriggerOnResetName();
		METASOUNDFRONTEND_API const FVertexName& GetOutputValueName();
	}

	class METASOUNDFRONTEND_API FArrayIndexShuffler
	{
	public:
		FArrayIndexShuffler() = default;
		FArrayIndexShuffler(int32 InSeed, int32 MaxIndices);

		void Init(int32 InSeed, int32 MaxIndices);
		void SetSeed(int32 InSeed);
		void ResetSeed();

		// Returns the next value in the array indices. Returns true if the array was re-shuffled automatically.
		bool NextValue(bool bAutoShuffle, int32& OutIndex);

		// Shuffle the array with the given max indices
		void ShuffleArray();

	private:
		// Helper function to swap the current index with a random index
		void RandomSwap(int32 InCurrentIndex, int32 InStartIndex, int32 InEndIndex);

		// The current index into the array of indicies (wraps between 0 and ShuffleIndices.Num())
		int32 CurrentIndex = 0;

		// The previously returned value. Used to avoid repeating the last value on shuffle.
		int32 PrevValue = INDEX_NONE;

		// Array of indices (in order 0 to Num), shuffled
		TArray<int32> ShuffleIndices;

		// Random stream to use to randomize the shuffling
		FRandomStream RandomStream;
	};

	class FSharedStateShuffleManager
	{
	public:
		static FSharedStateShuffleManager& Get()
		{
			static FSharedStateShuffleManager GSM;
			return GSM;
		}

		void InitSharedState(uint32 InSharedStateId, int32 InSeed, int32 InNumElements)
		{
			FScopeLock Lock(&CritSect);

			if (!Shufflers.Contains(InSharedStateId))
			{
				Shufflers.Add(InSharedStateId, MakeUnique<FArrayIndexShuffler>(InSeed, InNumElements));
			}
		}

		bool NextValue(uint32 InSharedStateId, bool bAutoShuffle, int32& OutIndex)
		{
			FScopeLock Lock(&CritSect);
			TUniquePtr<FArrayIndexShuffler>* Shuffler = Shufflers.Find(InSharedStateId);
			return (*Shuffler)->NextValue(bAutoShuffle, OutIndex);
		}

		void SetSeed(uint32 InSharedStateId, int32 InSeed)
		{
			FScopeLock Lock(&CritSect);
			TUniquePtr<FArrayIndexShuffler>* Shuffler = Shufflers.Find(InSharedStateId);
			(*Shuffler)->SetSeed(InSeed);
		}

		void ResetSeed(uint32 InSharedStateId)
		{
			FScopeLock Lock(&CritSect);
			TUniquePtr<FArrayIndexShuffler>* Shuffler = Shufflers.Find(InSharedStateId);
			(*Shuffler)->ResetSeed();
		}

		void ShuffleArray(uint32 InSharedStateId)
		{
			FScopeLock Lock(&CritSect);
			TUniquePtr<FArrayIndexShuffler>* Shuffler = Shufflers.Find(InSharedStateId);
			(*Shuffler)->ShuffleArray();
		}

	private:
		FSharedStateShuffleManager() = default;
		~FSharedStateShuffleManager() = default;

		FCriticalSection CritSect;

		TMap<uint32, TUniquePtr<FArrayIndexShuffler>> Shufflers;	
	};

	/** TArrayShuffleOperator shuffles an array on trigger and outputs values sequentially on "next". It avoids repeating shuffled elements and supports auto-shuffling.*/
	template<typename ArrayType>
	class TArrayShuffleOperator : public TExecutableOperator<TArrayShuffleOperator<ArrayType>>
	{
	public:
		using FArrayDataReadReference = TDataReadReference<ArrayType>;
		using ElementType = typename MetasoundArrayNodesPrivate::TArrayElementType<ArrayType>::Type;
		using FElementTypeWriteReference = TDataWriteReference<ElementType>;

		static const FVertexInterface& GetDefaultInterface()
		{
			using namespace ArrayNodeShuffleVertexNames;

			static const FVertexInterface DefaultInterface(
				FInputVertexInterface(
					TInputDataVertexModel<FTrigger>(GetInputTriggerNextName(), METASOUND_LOCTEXT("ShuffleOpInputTriggerNextTT", "Trigger to get the next value in the shuffled array.")),
					TInputDataVertexModel<FTrigger>(GetInputTriggerShuffleName(), METASOUND_LOCTEXT("ShuffleOpInputTriggerShuffleTT", "Trigger to shuffle the array manually.")),
					TInputDataVertexModel<FTrigger>(GetInputTriggerResetName(), METASOUND_LOCTEXT("ShuffleOpInputTriggerResetTT", "Trigger to reset the random seed stream of the shuffle node.")),
					TInputDataVertexModel<ArrayType>(GetInputShuffleArrayName(), METASOUND_LOCTEXT("ShuffleOpInputShuffleArrayTT", "Input Array.")),
					TInputDataVertexModel<int32>(GetInputSeedName(), METASOUND_LOCTEXT("ShuffleOpInputSeedTT", "Seed to use for the the random shuffle."), -1),
					TInputDataVertexModel<bool>(GetInputAutoShuffleName(), METASOUND_LOCTEXT("ShuffleOpInputAutoShuffleTT", "Set to true to automatically shuffle when the array has been read."), true),
					TInputDataVertexModel<bool>(GetInputEnableSharedStateName(), METASOUND_LOCTEXT("ShuffleOpInputEnableSharedStatTT", "Set to enabled shared state across instances of this metasound."), false)
					),
				FOutputVertexInterface(
					TOutputDataVertexModel<FTrigger>(GetOutputTriggerOnNextName(), METASOUND_LOCTEXT("ShuffleOpOutputTriggerOnNextNameTT", "Triggers when the \"Next\" input is triggered.")),
					TOutputDataVertexModel<FTrigger>(GetOutputTriggerOnShuffleName(), METASOUND_LOCTEXT("ShuffleOpOutputTriggerOnShuffleNameTT", "Triggers when the \"Shuffle\" input is triggered or if the array is auto-shuffled.")),
					TOutputDataVertexModel<FTrigger>(GetOutputTriggerOnResetName(), METASOUND_LOCTEXT("ShuffleOpOutputTriggerOnResetNameTT", "Triggers when the \"Reset Seed\" input is triggered.")),
					TOutputDataVertexModel<ElementType>(GetOutputValueName(), METASOUND_LOCTEXT("ShuffleOpOutputValueTT", "Value of the current shuffled element."))
				)
			);

			return DefaultInterface;
		}

		static const FNodeClassMetadata& GetNodeInfo()
		{
			auto CreateNodeClassMetadata = []() -> FNodeClassMetadata
			{
				const FName DataTypeName = GetMetasoundDataTypeName<ArrayType>();
				const FName OperatorName = "Shuffle";
				const FText NodeDisplayName = METASOUND_LOCTEXT_FORMAT("ArrayOpArrayShuffleDisplayNamePattern", "Shuffle ({0})", GetMetasoundDataTypeDisplayText<ArrayType>());
				const FText NodeDescription = METASOUND_LOCTEXT("ArrayOpArrayShuffleDescription", "Output next element of a shuffled array on trigger.");
				const FVertexInterface NodeInterface = GetDefaultInterface();

				return MetasoundArrayNodesPrivate::CreateArrayNodeClassMetadata(DataTypeName, OperatorName, NodeDisplayName, NodeDescription, NodeInterface);
			};

			static const FNodeClassMetadata Metadata = CreateNodeClassMetadata();

			return Metadata;
		}

		static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, TArray<TUniquePtr<IOperatorBuildError>>& OutErrors)
		{
			using namespace ArrayNodeShuffleVertexNames;
			using namespace MetasoundArrayNodesPrivate;

			const FInputVertexInterface& Inputs = InParams.Node.GetVertexInterface().GetInputInterface();

			TDataReadReference<FTrigger> InTriggerNext = InParams.InputDataReferences.GetDataReadReferenceOrConstructWithVertexDefault<FTrigger>(Inputs, GetInputTriggerNextName(), InParams.OperatorSettings);
			TDataReadReference<FTrigger> InTriggerShuffle = InParams.InputDataReferences.GetDataReadReferenceOrConstructWithVertexDefault<FTrigger>(Inputs, GetInputTriggerShuffleName(), InParams.OperatorSettings);
			TDataReadReference<FTrigger> InTriggerReset = InParams.InputDataReferences.GetDataReadReferenceOrConstructWithVertexDefault<FTrigger>(Inputs, GetInputTriggerResetName(), InParams.OperatorSettings);
			FArrayDataReadReference InInputArray = InParams.InputDataReferences.GetDataReadReferenceOrConstructWithVertexDefault<ArrayType>(Inputs, GetInputShuffleArrayName(), InParams.OperatorSettings);
			TDataReadReference<int32> InSeedValue = InParams.InputDataReferences.GetDataReadReferenceOrConstructWithVertexDefault<int32>(Inputs, GetInputSeedName(), InParams.OperatorSettings);
			TDataReadReference<bool> bInAutoShuffle = InParams.InputDataReferences.GetDataReadReferenceOrConstructWithVertexDefault<bool>(Inputs, GetInputAutoShuffleName(), InParams.OperatorSettings);
			TDataReadReference<bool> bInEnableSharedState = InParams.InputDataReferences.GetDataReadReferenceOrConstructWithVertexDefault<bool>(Inputs, GetInputEnableSharedStateName(), InParams.OperatorSettings);

			return MakeUnique<TArrayShuffleOperator>(InParams, InTriggerNext, InTriggerShuffle, InTriggerReset, InInputArray, InSeedValue, bInAutoShuffle, bInEnableSharedState);
		}

		TArrayShuffleOperator(
			const FCreateOperatorParams& InParams,
			const TDataReadReference<FTrigger>& InTriggerNext, 
			const TDataReadReference<FTrigger>& InTriggerShuffle,
			const TDataReadReference<FTrigger>& InTriggerReset,
			const FArrayDataReadReference& InInputArray,
			const TDataReadReference<int32>& InSeedValue,
			const TDataReadReference<bool>& bInAutoShuffle, 
			const TDataReadReference<bool>& bInEnableSharedState)
			: TriggerNext(InTriggerNext)
			, TriggerShuffle(InTriggerShuffle)
			, TriggerReset(InTriggerReset)
			, InputArray(InInputArray)
			, SeedValue(InSeedValue)
			, bAutoShuffle(bInAutoShuffle)
			, bEnableSharedState(bInEnableSharedState)
			, TriggerOnNext(FTriggerWriteRef::CreateNew(InParams.OperatorSettings))
			, TriggerOnShuffle(FTriggerWriteRef::CreateNew(InParams.OperatorSettings))
			, TriggerOnReset(FTriggerWriteRef::CreateNew(InParams.OperatorSettings))
			, OutValue(TDataWriteReferenceFactory<ElementType>::CreateAny(InParams.OperatorSettings))
		{
			using namespace Frontend;

			// Check to see if this is a global shuffler or a local one. 
			// Global shuffler will use a namespace to opt into it.
			PrevSeedValue = *SeedValue;

			const ArrayType& InputArrayRef = *InputArray;
			int32 ArraySize = InputArrayRef.Num();

			if (ArraySize > 0)
			{
				if (*bEnableSharedState)
				{
					// Get the environment variable for the unique ID of the sound
					SharedStateUniqueId = InParams.Environment.GetValue<uint32>(SourceInterface::Environment::SoundUniqueID);
					check(SharedStateUniqueId != INDEX_NONE);

					FSharedStateShuffleManager& SM = FSharedStateShuffleManager::Get();
					SM.InitSharedState(SharedStateUniqueId, PrevSeedValue, ArraySize);
				}
				else
				{
					ArrayIndexShuffler = MakeUnique<FArrayIndexShuffler>(PrevSeedValue, ArraySize);
				}
			}
			else 
			{
				UE_LOG(LogMetaSound, Error, TEXT("Array Shuffle: Can't shuffle an empty array"));
			}
		}

		virtual ~TArrayShuffleOperator() = default;

		virtual FDataReferenceCollection GetInputs() const override
		{
			using namespace ArrayNodeShuffleVertexNames;

			FDataReferenceCollection Inputs;
			Inputs.AddDataReadReference(GetInputTriggerNextName(), TriggerNext);
			Inputs.AddDataReadReference(GetInputTriggerShuffleName(), TriggerShuffle);
			Inputs.AddDataReadReference(GetInputTriggerResetName(), TriggerReset);
			Inputs.AddDataReadReference(GetInputShuffleArrayName(), InputArray);
			Inputs.AddDataReadReference(GetInputSeedName(), SeedValue);
			Inputs.AddDataReadReference(GetInputAutoShuffleName(), bAutoShuffle);
			Inputs.AddDataReadReference(GetInputEnableSharedStateName(), bEnableSharedState);

			return Inputs;
		}

		virtual FDataReferenceCollection GetOutputs() const override
		{
			using namespace ArrayNodeShuffleVertexNames;

			FDataReferenceCollection Outputs;
			Outputs.AddDataReadReference(GetOutputTriggerOnNextName(), TriggerOnNext);
			Outputs.AddDataReadReference(GetOutputTriggerOnShuffleName(), TriggerOnShuffle);
			Outputs.AddDataReadReference(GetOutputTriggerOnResetName(), TriggerOnReset);
			Outputs.AddDataReadReference(GetOutputValueName(), OutValue);

			return Outputs;
		}

		void Execute()
		{
			TriggerOnNext->AdvanceBlock();
			TriggerOnShuffle->AdvanceBlock();
			TriggerOnReset->AdvanceBlock();

			const ArrayType& InputArrayRef = *InputArray;
 
			// Check for a seed change
			if (PrevSeedValue != *SeedValue)
			{
				PrevSeedValue = *SeedValue;

				if (SharedStateUniqueId != INDEX_NONE)
				{
					FSharedStateShuffleManager& SM = FSharedStateShuffleManager::Get();
					SM.SetSeed(SharedStateUniqueId, PrevSeedValue);
				}
				else
				{
					check(ArrayIndexShuffler.IsValid());
					ArrayIndexShuffler->SetSeed(PrevSeedValue);
				}
			}

			// Don't do anything if our array is empty
			if (InputArrayRef.Num() == 0)
			{
				return;
			}

			TriggerReset->ExecuteBlock(
				[&](int32 StartFrame, int32 EndFrame)
				{
				},
				[this](int32 StartFrame, int32 EndFrame)
				{
					if (SharedStateUniqueId != INDEX_NONE)
					{
						FSharedStateShuffleManager& SM = FSharedStateShuffleManager::Get();
						SM.ResetSeed(SharedStateUniqueId);
					}
					else
					{
						check(ArrayIndexShuffler.IsValid());
						ArrayIndexShuffler->ResetSeed();
					}
					TriggerOnReset->TriggerFrame(StartFrame);
				}
			);

			TriggerShuffle->ExecuteBlock(
				[&](int32 StartFrame, int32 EndFrame)
				{
				},
				[this](int32 StartFrame, int32 EndFrame)
				{
					if (SharedStateUniqueId != INDEX_NONE)
					{
						FSharedStateShuffleManager& SM = FSharedStateShuffleManager::Get();
						SM.ShuffleArray(SharedStateUniqueId);
					}
					else
					{
						check(ArrayIndexShuffler.IsValid());
						ArrayIndexShuffler->ShuffleArray();
					}
					TriggerOnShuffle->TriggerFrame(StartFrame);
				}
			);

			TriggerNext->ExecuteBlock(
				[&](int32 StartFrame, int32 EndFrame)
				{
				},
				[this](int32 StartFrame, int32 EndFrame)
				{
					const ArrayType& InputArrayRef = *InputArray;

					bool bShuffleTriggered = false;
					int32 OutShuffleIndex = INDEX_NONE;

					if (SharedStateUniqueId != INDEX_NONE)
					{
						FSharedStateShuffleManager& SM = FSharedStateShuffleManager::Get();
						bShuffleTriggered = SM.NextValue(SharedStateUniqueId, *bAutoShuffle, OutShuffleIndex);
					}
					else
					{
						check(ArrayIndexShuffler.IsValid());
						bShuffleTriggered = ArrayIndexShuffler->NextValue(*bAutoShuffle, OutShuffleIndex);
					}

					check(OutShuffleIndex != INDEX_NONE);

					// The input array size may have changed, so make sure it's wrapped into range of the input array
					*OutValue = InputArrayRef[OutShuffleIndex % InputArrayRef.Num()];

					TriggerOnNext->TriggerFrame(StartFrame);

					// Trigger out if the array was auto-shuffled
					if (bShuffleTriggered)
					{
						TriggerOnShuffle->TriggerFrame(StartFrame);
					}
				}
			);
		}

	private:

		// Inputs
		TDataReadReference<FTrigger> TriggerNext;
		TDataReadReference<FTrigger> TriggerShuffle;
		TDataReadReference<FTrigger> TriggerReset;
		FArrayDataReadReference InputArray;
		TDataReadReference<int32> SeedValue;
		TDataReadReference<bool> bAutoShuffle;
		TDataReadReference<bool> bEnableSharedState;

		// Outputs
		TDataWriteReference<FTrigger> TriggerOnNext;
		TDataWriteReference<FTrigger> TriggerOnShuffle;
		TDataWriteReference<FTrigger> TriggerOnReset;
		TDataWriteReference<ElementType> OutValue;

		// Data
		TUniquePtr<FArrayIndexShuffler> ArrayIndexShuffler;
		int32 PrevSeedValue = INDEX_NONE;
		uint32 SharedStateUniqueId = INDEX_NONE;
	};

	template<typename ArrayType>
	class TArrayShuffleNode : public FNodeFacade
	{
	public:
		TArrayShuffleNode(const FNodeInitData& InInitData)
			: FNodeFacade(InInitData.InstanceName, InInitData.InstanceID, TFacadeOperatorClass<TArrayShuffleOperator<ArrayType>>())
		{
		}

		virtual ~TArrayShuffleNode() = default;
	};
}

#undef LOCTEXT_NAMESPACE
