// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/CircularQueue.h"

#include "MetasoundArrayNodes.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundSourceInterface.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundTrigger.h"

#define LOCTEXT_NAMESPACE "MetasoundFrontend"

namespace Metasound
{
	namespace ArrayNodeRandomGetVertexNames
	{
		static const FName InputTriggerNextName = "Next";
		static const FName InputTriggerResetName = "Reset";
		static const FName InputRandomArrayName = "In Array";
		static const FName InputWeightsName = "Weights";
		static const FName InputSeedName = "Seed";
		static const FName InputNoRepeatOrderName = "No Repeats";
		static const FName InputEnableSharedStateName = "Enable Shared State";
		static const FName OutputTriggerOnNextName = "On Next";
		static const FName OutputTriggerOnResetName = "On Reset";
		static const FName OutputValueName = "Value";

		static const FText InputTriggerNextTooltip = METASOUND_LOCTEXT("RandomArrayGetNode_TriggerNextTooltip", "Trigger to get the next value in the randomized array.");
		static const FText InputTriggerResetTooltip = METASOUND_LOCTEXT("RandomArrayGetNode_TriggerResetTooltip", "Trigger to reset the seed for the randomized array.");
		static const FText InputRandomArrayTooltip = METASOUND_LOCTEXT("RandomArrayGetNode_RandomArrayTooltip", "Input array to randomized.");
		static const FText InputWeightsTooltip = METASOUND_LOCTEXT("RandomArrayGetNode_WeightsTooltip", "Input array of weights to use for random selection. Will repeat if this array is shorter than the input array to select from.");
		static const FText InputSeedTooltip = METASOUND_LOCTEXT("RandomArrayGetNode_SeedTooltip", "Seed to use for the random shuffle.");
		static const FText InputNoRepeatOrderTooltip = METASOUND_LOCTEXT("RandomArrayGetNode_NoRepeatOrderTooltip", "The number of elements to track to avoid repeating in a row.");
		static const FText InputEnableSharedStateTooltip = METASOUND_LOCTEXT("RandomArrayGetNode_EnableSharedStateTooltip", "Set to enabled to share state across instances of this MetaSound.");
		static const FText OutputTriggerOnNextTooltip = METASOUND_LOCTEXT("RandomArrayGetNode_TriggerOnNextTooltip", "Triggers when the \"Next\" input is triggered.");
		static const FText OutputTriggerOnResetTooltip = METASOUND_LOCTEXT("RandomArrayGetNode_TriggerOnResetTooltip", "Triggers when the \"Shuffle\" input is triggered or if the array is auto-shuffled.");
		static const FText OutputValueTooltip = METASOUND_LOCTEXT("RandomArrayGetNode_ValueTooltip", "Value of the current shuffled element.");
	}

	class METASOUNDFRONTEND_API FArrayRandomGet
	{
	public:
		FArrayRandomGet() = default;
		FArrayRandomGet(int32 InSeed, int32 InMaxIndex, const TArray<float>& InWeights, int32 InNoRepeatOrder);
		~FArrayRandomGet() = default;

		void Init(int32 InSeed, int32 InMaxIndex, const TArray<float>& InWeights, int32 InNoRepeatOrder);
		void SetSeed(int32 InSeed);
		void SetNoRepeatOrder(int32 InNoRepeatOrder);
		void SetRandomWeights(const TArray<float>& InRandomWeights);
		void ResetSeed();
		int32 NextValue();

		int32 GetNoRepeatOrder() const { return NoRepeatOrder; }
		int32 GetMaxIndex() const { return MaxIndex; }

	private:
		float ComputeTotalWeight();

		// The current index into the array of indicies (wraps between 0 and ShuffleIndices.Num())
		TArray<int32> PreviousIndices;
		TUniquePtr<TCircularQueue<int32>> PreviousIndicesQueue;
		int32 NoRepeatOrder = INDEX_NONE;

		// Array of indices (in order 0 to Num)
		int32 MaxIndex = 0;
		TArray<float> RandomWeights;

		// Random stream to use to randomize the shuffling
		FRandomStream RandomStream;
	};

	struct InitSharedStateArgs
	{
		FGuid SharedStateId;
		int32 Seed = INDEX_NONE;
		int32 NumElements = 0;
		int32 NoRepeatOrder = 0;
		bool bIsPreviewSound = false;
		TArray<float> Weights;
	};

	class METASOUNDFRONTEND_API FSharedStateRandomGetManager
	{
	public:
		static FSharedStateRandomGetManager& Get();

		void InitSharedState(InitSharedStateArgs& InArgs);
		int32 NextValue(const FGuid& InSharedStateId);
		void SetSeed(const FGuid& InSharedStateId, int32 InSeed);
		void SetNoRepeatOrder(const FGuid& InSharedStateId, int32 InNoRepeatOrder);
		void SetRandomWeights(const FGuid& InSharedStateId, const TArray<float>& InRandomWeights);
		void ResetSeed(const FGuid& InSharedStateId);

	private:
		FSharedStateRandomGetManager() = default;
		~FSharedStateRandomGetManager() = default;

		FCriticalSection CritSect;

		TMap<FGuid, TUniquePtr<FArrayRandomGet>> RandomGets;
	};

	template<typename ArrayType>
	class TArrayRandomGetOperator : public TExecutableOperator<TArrayRandomGetOperator<ArrayType>>
	{
	public:
		using FArrayDataReadReference = TDataReadReference<ArrayType>;
		using FArrayWeightReadReference = TDataReadReference<TArray<float>>;
		using WeightsArrayType = TArray<float>;
		using ElementType = typename MetasoundArrayNodesPrivate::TArrayElementType<ArrayType>::Type;
		using FElementTypeWriteReference = TDataWriteReference<ElementType>;

		static const FVertexInterface& GetDefaultInterface()
		{
			using namespace ArrayNodeRandomGetVertexNames;

			static const FVertexInterface DefaultInterface(
				FInputVertexInterface(
					TInputDataVertexModel<FTrigger>(InputTriggerNextName, InputTriggerNextTooltip),
					TInputDataVertexModel<FTrigger>(InputTriggerResetName, InputTriggerResetTooltip),
					TInputDataVertexModel<ArrayType>(InputRandomArrayName, InputRandomArrayTooltip),
					TInputDataVertexModel<WeightsArrayType>(InputWeightsName, InputWeightsTooltip),
					TInputDataVertexModel<int32>(InputSeedName, InputSeedTooltip, -1),
					TInputDataVertexModel<int32>(InputNoRepeatOrderName, InputNoRepeatOrderTooltip, 1),
					TInputDataVertexModel<bool>(InputEnableSharedStateName, InputEnableSharedStateTooltip, false)
				),
				FOutputVertexInterface(
					TOutputDataVertexModel<FTrigger>(OutputTriggerOnNextName, OutputTriggerOnNextTooltip),
					TOutputDataVertexModel<FTrigger>(OutputTriggerOnResetName, OutputTriggerOnResetTooltip),
					TOutputDataVertexModel<ElementType>(OutputValueName, OutputValueTooltip)
				)
			);

			return DefaultInterface;
		}

		static const FNodeClassMetadata& GetNodeInfo()
		{
			auto CreateNodeClassMetadata = []() -> FNodeClassMetadata
			{
				FName DataTypeName = GetMetasoundDataTypeName<ArrayType>();
				FName OperatorName = "Random Get";
				FText NodeDisplayName = METASOUND_LOCTEXT_FORMAT("RandomArrayGetNode_OpDisplayNamePattern", "Random Get ({0})", GetMetasoundDataTypeDisplayText<ArrayType>());
				FText NodeDescription = METASOUND_LOCTEXT("RandomArrayGetNode_Description", "Randomly retrieve data from input array using the supplied weights.");
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

			FTriggerReadRef InTriggerNext = InParams.InputDataReferences.GetDataReadReferenceOrConstructWithVertexDefault<FTrigger>(Inputs, InputTriggerNextName, InParams.OperatorSettings);
			FTriggerReadRef InTriggerReset = InParams.InputDataReferences.GetDataReadReferenceOrConstructWithVertexDefault<FTrigger>(Inputs, InputTriggerResetName, InParams.OperatorSettings);
			FArrayDataReadReference InInputArray = InParams.InputDataReferences.GetDataReadReferenceOrConstructWithVertexDefault<ArrayType>(Inputs, InputRandomArrayName, InParams.OperatorSettings);
			FArrayWeightReadReference InInputWeightsArray = InParams.InputDataReferences.GetDataReadReferenceOrConstructWithVertexDefault<WeightsArrayType>(Inputs, InputWeightsName, InParams.OperatorSettings);
			FInt32ReadRef InSeedValue = InParams.InputDataReferences.GetDataReadReferenceOrConstructWithVertexDefault<int32>(Inputs, InputSeedName, InParams.OperatorSettings);
			FInt32ReadRef InNoRepeatOrder = InParams.InputDataReferences.GetDataReadReferenceOrConstructWithVertexDefault<int32>(Inputs, InputNoRepeatOrderName, InParams.OperatorSettings);
			FBoolReadRef bInEnableSharedState = InParams.InputDataReferences.GetDataReadReferenceOrConstructWithVertexDefault<bool>(Inputs, InputEnableSharedStateName, InParams.OperatorSettings);

			return MakeUnique<TArrayRandomGetOperator<ArrayType>>(InParams, InTriggerNext, InTriggerReset, InInputArray, InInputWeightsArray, InSeedValue, InNoRepeatOrder, bInEnableSharedState);
		}

		TArrayRandomGetOperator(
			const FCreateOperatorParams& InParams,
			const FTriggerReadRef& InTriggerNext,
			const FTriggerReadRef& InTriggerReset,
			const FArrayDataReadReference& InInputArray,
			const TDataReadReference<WeightsArrayType>& InInputWeightsArray,
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
			using namespace Frontend;

#if WITH_METASOUND_DEBUG_ENVIRONMENT
			GraphName = *InParams.Environment.GetValue<FString>(SourceInterface::Environment::GraphName);
#endif // WITH_METASOUND_DEBUG_ENVIRONMENT

			// Check to see if this is a global shuffler or a local one. 
			// Global shuffler will use a namespace to opt into it.
			PrevSeedValue = *SeedValue;
			PrevNoRepeatOrder = FMath::Max(*NoRepeatOrder, 0);

			WeightsArray = *InputWeightsArray;

			const ArrayType& InputArrayRef = *InputArray;
			PrevArraySize = InputArrayRef.Num();

			if (PrevArraySize > 0)
			{
				if (*bEnableSharedState)
				{
					// Get the environment variable for the unique ID of the sound
					SharedStateUniqueId = InParams.Node.GetInstanceID();
					check(SharedStateUniqueId.IsValid());

					bIsPreviewSound = InParams.Environment.GetValue<bool>(SourceInterface::Environment::IsPreview);

					FSharedStateRandomGetManager& RGM = FSharedStateRandomGetManager::Get();

					InitSharedStateArgs Args;
					Args.SharedStateId = SharedStateUniqueId;
					Args.Seed = PrevSeedValue;
					Args.NumElements = PrevArraySize;
					Args.NoRepeatOrder = PrevNoRepeatOrder;
					Args.bIsPreviewSound = bIsPreviewSound;
					Args.Weights = WeightsArray;

					RGM.InitSharedState(Args);
				}
				else
				{
					ArrayRandomGet = MakeUnique<FArrayRandomGet>(PrevSeedValue, PrevArraySize, WeightsArray, PrevNoRepeatOrder);
				}
			}
#if WITH_METASOUND_DEBUG_ENVIRONMENT
			else
			{
 				UE_LOG(LogMetaSound, Verbose, TEXT("Array Random Get: Can't retrieve random elements from an empty array in graph '%s'"), *GraphName);
			}
#endif // WITH_METASOUND_DEBUG_ENVIRONMENT
		}

		virtual ~TArrayRandomGetOperator() = default;

		virtual FDataReferenceCollection GetInputs() const override
		{
			using namespace ArrayNodeRandomGetVertexNames;

			FDataReferenceCollection Inputs;
			Inputs.AddDataReadReference(InputTriggerNextName, TriggerNext);
			Inputs.AddDataReadReference(InputTriggerResetName, TriggerReset);
			Inputs.AddDataReadReference(InputRandomArrayName, InputArray);
			Inputs.AddDataReadReference(InputWeightsName, InputWeightsArray);
			Inputs.AddDataReadReference(InputSeedName, SeedValue);
			Inputs.AddDataReadReference(InputNoRepeatOrderName, NoRepeatOrder);
			Inputs.AddDataReadReference(InputEnableSharedStateName, bEnableSharedState);

			return Inputs;
		}

		virtual FDataReferenceCollection GetOutputs() const override
		{
			using namespace ArrayNodeRandomGetVertexNames;

			FDataReferenceCollection Outputs;
			Outputs.AddDataReadReference(OutputTriggerOnNextName, TriggerOnNext);
			Outputs.AddDataReadReference(OutputTriggerOnResetName, TriggerOnReset);
			Outputs.AddDataReadReference(OutputValueName, OutValue);

			return Outputs;
		}

		void Execute()
		{
			TriggerOnNext->AdvanceBlock();
			TriggerOnReset->AdvanceBlock();

			const ArrayType& InputArrayRef = *InputArray;

			// If the array size has changed, we need to reinit before getting the next value
			if (PrevArraySize != InputArrayRef.Num())
			{
				PrevArraySize = InputArrayRef.Num();
				if (PrevArraySize != 0)
				{
					if (!ArrayRandomGet.IsValid())
					{
						ArrayRandomGet = MakeUnique<FArrayRandomGet>(PrevSeedValue, PrevArraySize, WeightsArray, PrevNoRepeatOrder);
					}
					ArrayRandomGet->Init(PrevSeedValue, PrevArraySize, WeightsArray, PrevNoRepeatOrder);
				}
			}

			if (PrevArraySize == 0)
			{
				if (!bHasLoggedEmptyArrayWarning)
				{
#if WITH_METASOUND_DEBUG_ENVIRONMENT
					UE_LOG(LogMetaSound, Verbose, TEXT("Array Random Get: empty array input (Graph '%s')"), *GraphName);
#endif // WITH_METASOUND_DEBUG_ENVIRONMENT
					bHasLoggedEmptyArrayWarning = true;
				}
				return;
			}

			// Check for a seed change
			if (PrevSeedValue != *SeedValue)
			{
				PrevSeedValue = *SeedValue;

				if (SharedStateUniqueId.IsValid())
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
				if (SharedStateUniqueId.IsValid())
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

			WeightsArray = *InputWeightsArray;
			if (SharedStateUniqueId.IsValid())
			{
				FSharedStateRandomGetManager& RGM = FSharedStateRandomGetManager::Get();
				RGM.SetRandomWeights(SharedStateUniqueId, WeightsArray);
			}
			else
			{
				check(ArrayRandomGet.IsValid());
				ArrayRandomGet->SetRandomWeights(WeightsArray);
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
					if (SharedStateUniqueId.IsValid())
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

					if (SharedStateUniqueId.IsValid())
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
		TDataReadReference<WeightsArrayType> InputWeightsArray;
		FInt32ReadRef SeedValue;
		FInt32ReadRef NoRepeatOrder;
		FBoolReadRef bEnableSharedState;

		// Outputs
		FTriggerWriteRef TriggerOnNext;
		FTriggerWriteRef TriggerOnReset;
		TDataWriteReference<ElementType> OutValue;

#if WITH_METASOUND_DEBUG_ENVIRONMENT
		FString GraphName;
#endif // WITH_METASOUND_DEBUG_ENVIRONMENT

		// Data
		TUniquePtr<FArrayRandomGet> ArrayRandomGet;
		TArray<float> WeightsArray;
		int32 PrevSeedValue = INDEX_NONE;
		int32 PrevNoRepeatOrder = INDEX_NONE;
		FGuid SharedStateUniqueId;
		int32 PrevArraySize = 0;
		bool bIsPreviewSound = false;
		bool bHasLoggedEmptyArrayWarning = false;
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
