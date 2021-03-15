// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

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

#define LOCTEXT_NAMESPACE "MetasoundFrontend"


namespace Metasound
{
	/** Random Get Node Vertex Names */
	namespace ArrayNodeRandomGetVertexNames
	{
		METASOUNDFRONTEND_API const FString& GetInputTriggerNextName();
		METASOUNDFRONTEND_API const FString& GetInputTriggerResetName();
		METASOUNDFRONTEND_API const FString& GetInputRandomArrayName();
		METASOUNDFRONTEND_API const FString& GetInputWeightsName();
		METASOUNDFRONTEND_API const FString& GetInputSeedName();
		METASOUNDFRONTEND_API const FString& GetInputNoRepeatOrderName();
		METASOUNDFRONTEND_API const FString& GetInputEnableSharedStateName();

		METASOUNDFRONTEND_API const FString& GetOutputTriggerOnNextName();
		METASOUNDFRONTEND_API const FString& GetOutputTriggerOnResetName();
		METASOUNDFRONTEND_API const FString& GetOutputValueName();
	}

	class METASOUNDFRONTEND_API FArrayRandomGet
	{
	public:
		FArrayRandomGet() = default;
		FArrayRandomGet(int32 InSeed, int32 InMaxIndex, int32 InNoRepeatOrder);
		void Init(int32 InSeed, int32 InMaxIndex, int32 InNoRepeatOrder);
		void SetSeed(int32 InSeed);
		void SetNoRepeatOrder(int32 InNoRepeatOrder);
		void ResetSeed();
		int32 NextValue();

	private:
		// The current index into the array of indicies (wraps between 0 and ShuffleIndices.Num())
		TArray<int32> PreviousIndices;
		TUniquePtr<TCircularQueue<int32>> PreviousIndicesQueue;
		int32 NoRepeatOrder = 0;

		// Array of indices (in order 0 to Num)
		int32 MaxIndex = 0;
		TArray<float> RandomWeights;

		// Random stream to use to randomize the shuffling
		FRandomStream RandomStream;
	};

	class METASOUNDFRONTEND_API FSharedStateRandomGetManager
	{
	public:
		static FSharedStateRandomGetManager& Get();

		void InitSharedState(uint32 InSharedStateId, int32 InSeed, int32 InNumElements, int32 InNoRepeatOrder);
		int32 NextValue(uint32 InSharedStateId);
		void SetSeed(uint32 InSharedStateId, int32 InSeed);
		void SetNoRepeatOrder(uint32 InSharedStateId, int32 InNoRepeatOrder);
		void ResetSeed(uint32 InSharedStateId);

	private:
		FSharedStateRandomGetManager() = default;
		~FSharedStateRandomGetManager() = default;

		FCriticalSection CritSect;

		TMap<uint32, TUniquePtr<FArrayRandomGet>> RandomGets;
	};

	/** TArrayShuffleOperator shuffles an array on trigger and outputs values sequentially on "next". It avoids repeating shuffled elements and supports auto-shuffling.*/
	template<typename ArrayType>
	class TArrayRandomGetOperator : public TExecutableOperator<TArrayRandomGetOperator<ArrayType>>
	{
	public:
		using FArrayDataReadReference = TDataReadReference<ArrayType>;
		using ElementType = typename MetasoundArrayNodesPrivate::TArrayElementType<ArrayType>::Type;
		using FElementTypeWriteReference = TDataWriteReference<ElementType>;
		using WeightArrayType = TArray<float>;

		static const FVertexInterface& GetDefaultInterface()
		{
			using namespace ArrayNodeRandomGetVertexNames;

			static const FVertexInterface DefaultInterface(
				FInputVertexInterface(
					TInputDataVertexModel<FTrigger>(GetInputTriggerNextName(), LOCTEXT("RandomGetArrayOpInputTriggerNextTT", "Trigger to get the next value in the randomized array.")),
					TInputDataVertexModel<FTrigger>(GetInputTriggerResetName(), LOCTEXT("RandomGetArrayOpInputTriggerResetTT", "Trigger to reset the seed for the randomized array.")),
					TInputDataVertexModel<ArrayType>(GetInputRandomArrayName(), LOCTEXT("RandomGetArrayOpArrayTT", "Input array to randomized.")),
					TInputDataVertexModel<WeightArrayType>(GetInputWeightsName(), LOCTEXT("RandomGetOpInputSeedTT", "Input array of weights to use for random selection. Will repeat if this array is shorter than the input array to select from.")),
					TInputDataVertexModel<int32>(GetInputSeedName(), LOCTEXT("RandomGetOpInputSeedTT", "Seed to use for the the random shuffle."), -1),
					TInputDataVertexModel<int32>(GetInputNoRepeatOrderName(), LOCTEXT("RandomGetOpInputNoRepeatOrderTT", "The number of elements to track to avoid repeating in a row."), 1),
					TInputDataVertexModel<bool>(GetInputEnableSharedStateName(), LOCTEXT("RandomGetOpEnableSharedStateNameTT", "Set to enabled shared state across instances of this metasound."), false)
				),
				FOutputVertexInterface(
					TOutputDataVertexModel<FTrigger>(GetOutputTriggerOnNextName(), LOCTEXT("RandomGetOpOutputTriggerOnNextNameTT", "Triggers when the \"Next\" input is triggered.")),
					TOutputDataVertexModel<FTrigger>(GetOutputTriggerOnResetName(), LOCTEXT("RandomGetOpOutputTriggerOnShuffleNameTT", "Triggers when the \"Shuffle\" input is triggered or if the array is auto-shuffled.")),
					TOutputDataVertexModel<ElementType>(GetOutputValueName(), LOCTEXT("RandomGetOpOutputValueTT", "Value of the current shuffled element."))
				)
			);

			return DefaultInterface;
		}

		static const FNodeClassMetadata& GetNodeInfo()
		{
			auto CreateNodeClassMetadata = []() -> FNodeClassMetadata
			{
				FName DataTypeName = GetMetasoundDataTypeName<ArrayType>();
				FName OperatorName = TEXT("Random Get");
				FText NodeDisplayName = FText::Format(LOCTEXT("RandomGetArrayOpDisplayNamePattern", "Random Get ({0})"), FText::FromString(GetMetasoundDataTypeString<ArrayType>()));
				FText NodeDescription = LOCTEXT("RandomGetArrayDescription", "Randomly retrieve data from input array using the supplied weights.");
				FVertexInterface NodeInterface = GetDefaultInterface();

				return MetasoundArrayNodesPrivate::CreateArrayNodeClassMetadata(DataTypeName, OperatorName, NodeDisplayName, NodeDescription, NodeInterface);
			};

			static const FNodeClassMetadata Metadata = CreateNodeClassMetadata();

			return Metadata;
		}

		static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, TArray<TUniquePtr<IOperatorBuildError>>& OutErrors)
		{
			using namespace ArrayNodeRandomGetVertexNames;
			using namespace MetasoundArrayNodesPrivate;

			const FInputVertexInterface& Inputs = InParams.Node.GetVertexInterface().GetInputInterface();

			FTriggerReadRef InTriggerNext = GetDataReadReferenceOrConstructWithVertexDefault<FTrigger>(InParams.InputDataReferences, Inputs, GetInputTriggerNextName(), InParams.OperatorSettings);
			FTriggerReadRef InTriggerReset = GetDataReadReferenceOrConstructWithVertexDefault<FTrigger>(InParams.InputDataReferences, Inputs, GetInputTriggerResetName(), InParams.OperatorSettings);
			FArrayDataReadReference InInputArray = GetDataReadReferenceOrConstructWithVertexDefault<ArrayType>(InParams.InputDataReferences, Inputs, GetInputRandomArrayName(), InParams.OperatorSettings);
			TDataReadReference<WeightArrayType> InInputWeightsArray = GetDataReadReferenceOrConstructWithVertexDefault<WeightArrayType>(InParams.InputDataReferences, Inputs, GetInputRandomArrayName(), InParams.OperatorSettings);
			FInt32ReadRef InSeedValue = GetDataReadReferenceOrConstructWithVertexDefault<int32>(InParams.InputDataReferences, Inputs, GetInputSeedName(), InParams.OperatorSettings);
			FInt32ReadRef InNoRepeatOrder = GetDataReadReferenceOrConstructWithVertexDefault<int32>(InParams.InputDataReferences, Inputs, GetInputNoRepeatOrderName(), InParams.OperatorSettings);
			FBoolReadRef bInEnableSharedState = GetDataReadReferenceOrConstructWithVertexDefault<bool>(InParams.InputDataReferences, Inputs, GetInputEnableSharedStateName(), InParams.OperatorSettings);

			return MakeUnique<TArrayRandomGetOperator>(InParams, InTriggerNext, InTriggerReset, InInputArray, InInputWeightsArray, InSeedValue, InNoRepeatOrder, bInEnableSharedState);
		}

		TArrayRandomGetOperator(
			const FCreateOperatorParams& InParams,
			const FTriggerReadRef& InTriggerNext,
			const FTriggerReadRef& InTriggerReset,
			const FArrayDataReadReference& InInputArray,
			const TDataReadReference<WeightArrayType>& InInputWeightsArray,
			const FInt32ReadRef& InSeedValue,
			const FInt32ReadRef& InNoRepeatOrder,
			const FBoolReadRef& bInEnableSharedState)
			: TriggerNext(InTriggerNext)
			, TriggerReset(InTriggerReset)
			, InputArray(InInputArray)
			, InputWeightsArray(InInputWeightsArray)
			, SeedValue(InSeedValue)
			, NoRepeatOrder(InNoRepeatOrder)
			, bEnableSharedState(bInEnableSharedState)
			, TriggerOnNext(FTriggerWriteRef::CreateNew(InParams.OperatorSettings))
			, TriggerOnReset(FTriggerWriteRef::CreateNew(InParams.OperatorSettings))
			, OutValue(TDataWriteReferenceFactory<ElementType>::CreateAny(InParams.OperatorSettings))
		{
			// Check to see if this is a global shuffler or a local one. 
			// Global shuffler will use a namespace to opt into it.
			PrevSeedValue = *SeedValue;
			PrevNoRepeatOrder = FMath::Max(*NoRepeatOrder, 0);

			const ArrayType& InputArrayRef = *InputArray;
			int32 ArraySize = InputArrayRef.Num();

			if (ArraySize > 0)
			{
				if (*bEnableSharedState)
				{
					// Get the environment variable for the unique ID of the sound
					SharedStateUniqueId = InParams.Environment.GetValue<uint32>(TEXT("SoundUniqueId"));
					check(SharedStateUniqueId != INDEX_NONE);

					FSharedStateRandomGetManager& RGM = FSharedStateRandomGetManager::Get();
					RGM.InitSharedState(SharedStateUniqueId, PrevSeedValue, ArraySize, PrevNoRepeatOrder);
				}
				else
				{
					ArrayRandomGet = MakeUnique<FArrayRandomGet>(PrevSeedValue, ArraySize, PrevNoRepeatOrder);
				}
			}
			else
			{
				UE_LOG(LogMetasound, Error, TEXT("Array Random Get: Can't retrieve random elements from an empty array"));
			}
		}

		virtual ~TArrayRandomGetOperator() = default;

		virtual FDataReferenceCollection GetInputs() const override
		{
			using namespace ArrayNodeRandomGetVertexNames;

			FDataReferenceCollection Inputs;
			Inputs.AddDataReadReference(GetInputTriggerNextName(), TriggerNext);
			Inputs.AddDataReadReference(GetInputTriggerResetName(), TriggerReset);
			Inputs.AddDataReadReference(GetInputRandomArrayName(), InputArray);
			Inputs.AddDataReadReference(GetInputWeightsName(), InputWeightsArray);
			Inputs.AddDataReadReference(GetInputSeedName(), SeedValue);
			Inputs.AddDataReadReference(GetInputNoRepeatOrderName(), NoRepeatOrder);
			Inputs.AddDataReadReference(GetInputEnableSharedStateName(), bEnableSharedState);

			return Inputs;
		}

		virtual FDataReferenceCollection GetOutputs() const override
		{
			using namespace ArrayNodeRandomGetVertexNames;

			FDataReferenceCollection Outputs;
			Outputs.AddDataReadReference(GetOutputTriggerOnNextName(), TriggerOnNext);
			Outputs.AddDataReadReference(GetOutputTriggerOnResetName(), TriggerOnReset);
			Outputs.AddDataReadReference(GetOutputValueName(), OutValue);

			return Outputs;
		}

		void Execute()
		{
			TriggerOnNext->AdvanceBlock();
			TriggerOnReset->AdvanceBlock();
 
 			const ArrayType& InputArrayRef = *InputArray;
 
 			// Check for a seed change
 			if (PrevSeedValue != *SeedValue)
 			{
				PrevSeedValue = *SeedValue;

				if (SharedStateUniqueId != INDEX_NONE)
				{
					FSharedStateRandomGetManager& RGM = FSharedStateRandomGetManager::Get();
					RGM.SetSeed(SharedStateUniqueId, PrevSeedValue);
				}
				else
				{
					check(ArrayRandomGet.IsValid());
					ArrayRandomGet->SetSeed(PrevSeedValue);
				}
			}

			if (PrevNoRepeatOrder != *NoRepeatOrder)
			{	
				PrevNoRepeatOrder = *NoRepeatOrder;
				if (SharedStateUniqueId != INDEX_NONE)
				{
					FSharedStateRandomGetManager& RGM = FSharedStateRandomGetManager::Get();
					RGM.SetNoRepeatOrder(SharedStateUniqueId, PrevNoRepeatOrder);
				}
				else
				{
					check(ArrayRandomGet.IsValid());
					ArrayRandomGet->SetNoRepeatOrder(PrevNoRepeatOrder);
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
						FSharedStateRandomGetManager& RGM = FSharedStateRandomGetManager::Get();
						RGM.ResetSeed(SharedStateUniqueId);
					}
					else
					{
						check(ArrayRandomGet.IsValid());
						ArrayRandomGet->ResetSeed();
					}
					TriggerOnReset->TriggerFrame(StartFrame);
				}
			);
 
			TriggerNext->ExecuteBlock(
				[&](int32 StartFrame, int32 EndFrame)
				{
				},
				[this](int32 StartFrame, int32 EndFrame)
				{
					const ArrayType& InputArrayRef = *InputArray;
					int32 OutRandomIndex = INDEX_NONE;

					if (SharedStateUniqueId != INDEX_NONE)
					{
						FSharedStateRandomGetManager& RGM = FSharedStateRandomGetManager::Get();
						OutRandomIndex = RGM.NextValue(SharedStateUniqueId);
					}
					else
					{
						check(ArrayRandomGet.IsValid());
						OutRandomIndex = ArrayRandomGet->NextValue();
					}

					check(OutRandomIndex != INDEX_NONE);

					// The input array size may have changed, so make sure it's wrapped into range of the input array
					*OutValue = InputArrayRef[OutRandomIndex % InputArrayRef.Num()];

					TriggerOnNext->TriggerFrame(StartFrame);
				}
			);
		}

	private:

		// Inputs
		FTriggerReadRef TriggerNext;
		FTriggerReadRef TriggerReset;
		FArrayDataReadReference InputArray;
		TDataReadReference<WeightArrayType> InputWeightsArray;
		FInt32ReadRef SeedValue;
		FInt32ReadRef NoRepeatOrder;
		FBoolReadRef bEnableSharedState;

		// Outputs
		FTriggerWriteRef TriggerOnNext;
		FTriggerWriteRef TriggerOnReset;
		TDataWriteReference<ElementType> OutValue;

		// Data
		TUniquePtr<FArrayRandomGet> ArrayRandomGet;
		int32 PrevSeedValue = INDEX_NONE;
		int32 PrevNoRepeatOrder = INDEX_NONE;
		uint32 SharedStateUniqueId = INDEX_NONE;
	};

	template<typename ArrayType>
	class TArrayRandomGetNode : public FNodeFacade
	{
	public:
		TArrayRandomGetNode(const FNodeInitData& InInitData)
			: FNodeFacade(InInitData.InstanceName, InInitData.InstanceID, TFacadeOperatorClass<TArrayRandomGetOperator<ArrayType>>())
		{
		}

		virtual ~TArrayRandomGetNode() = default;
	};


}

#undef LOCTEXT_NAMESPACE
