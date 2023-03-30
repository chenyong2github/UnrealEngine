// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningPolicyObject.h"

#include "LearningNeuralNetwork.h"
#include "LearningEigen.h"
#include "LearningRandom.h"

#if UE_LEARNING_ISPC
#include "Learning.ispc.generated.h"
#endif

namespace UE::Learning
{
	namespace NeuralNetworkPolicy
	{
		static inline float ReLU(const float X)
		{
			return FMath::Max(X, 0.0f);
		}

		static inline float ELU(const float X)
		{
			return X > 0.0f ? X : FMath::InvExpApprox(-X) - 1.0f;
		}

		static inline float Sigmoid(const float X)
		{
			return 1.0f / (1.0f + FMath::InvExpApprox(X));
		}

		static inline float TanH(const float X)
		{
			return FMath::Tanh(X);
		}

		static inline void MatMulPlusBias(
			TLearningArrayView<2, float> Output,
			const TLearningArrayView<2, const float> Input,
			const TLearningArrayView<2, const float> Weights,
			const TLearningArrayView<1, const float> Biases,
			const FIndexSet Instances)
		{
			UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(Learning::NeuralNetworkPolicy::MatMulPlusBias);

			const int32 RowNum = Weights.Num<0>();
			const int32 ColNum = Weights.Num<1>();

			if (UE_LEARNING_ISPC && Instances.IsSlice())
			{
#if UE_LEARNING_ISPC
				ispc::LearningLayerMatMulPlusBias(
					Output.Slice(Instances.GetSliceStart(), Instances.GetSliceNum()).GetData(),
					Input.Slice(Instances.GetSliceStart(), Instances.GetSliceNum()).GetData(),
					Weights.GetData(),
					Biases.GetData(),
					Instances.GetSliceNum(),
					RowNum,
					ColNum);
#endif
			}
			else
			{
				OutEigenMatrix(Output).noalias() =
					(InEigenMatrix(Weights).transpose() *
						InEigenMatrix(Input).transpose()).transpose().rowwise() +
					InEigenRowVector(Biases);
			}
		}

		static inline void ActivationReLU(TLearningArrayView<2, float> InputOutput, const FIndexSet Instances)
		{
			UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(Learning::NeuralNetworkPolicy::ActivationReLU);

			const int32 HiddenNum = InputOutput.Num<1>();

			if (UE_LEARNING_ISPC && Instances.IsSlice())
			{
#if UE_LEARNING_ISPC
				ispc::LearningLayerReLU(
					InputOutput.Slice(Instances.GetSliceStart(), Instances.GetSliceNum()).GetData(),
					Instances.GetSliceNum(),
					HiddenNum);
#endif
			}
			else
			{
				for (const int32 InstanceIdx : Instances)
				{
					for (int32 HiddenIdx = 0; HiddenIdx < HiddenNum; HiddenIdx++)
					{
						InputOutput[InstanceIdx][HiddenIdx] = ReLU(InputOutput[InstanceIdx][HiddenIdx]);
					}
				}
			}
		}

		static inline void ActivationELU(TLearningArrayView<2, float> InputOutput, const FIndexSet Instances)
		{
			UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(Learning::NeuralNetworkPolicy::ActivationELU);

			const int32 HiddenNum = InputOutput.Num<1>();

			if (UE_LEARNING_ISPC && Instances.IsSlice())
			{
#if UE_LEARNING_ISPC
				ispc::LearningLayerELU(
					InputOutput.Slice(Instances.GetSliceStart(), Instances.GetSliceNum()).GetData(),
					Instances.GetSliceNum(),
					HiddenNum);
#endif
			}
			else
			{
				for (const int32 InstanceIdx : Instances)
				{
					for (int32 HiddenIdx = 0; HiddenIdx < HiddenNum; HiddenIdx++)
					{
						InputOutput[InstanceIdx][HiddenIdx] = ELU(InputOutput[InstanceIdx][HiddenIdx]);
					}
				}
			}
		}

		static inline void ActivationTanH(TLearningArrayView<2, float> InputOutput, const FIndexSet Instances)
		{
			UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(Learning::NeuralNetworkPolicy::ActivationTanH);

			const int32 HiddenNum = InputOutput.Num<1>();

			if (UE_LEARNING_ISPC && Instances.IsSlice())
			{
#if UE_LEARNING_ISPC
				ispc::LearningLayerTanH(
					InputOutput.Slice(Instances.GetSliceStart(), Instances.GetSliceNum()).GetData(),
					Instances.GetSliceNum(),
					HiddenNum);
#endif
			}
			else
			{
				for (const int32 InstanceIdx : Instances)
				{
					for (int32 HiddenIdx = 0; HiddenIdx < HiddenNum; HiddenIdx++)
					{
						InputOutput[InstanceIdx][HiddenIdx] = TanH(InputOutput[InstanceIdx][HiddenIdx]);
					}
				}
			}
		}

		static inline void ActionNoise(
			TLearningArrayView<2, float> Output,
			const TLearningArrayView<2, float> Input,
			TLearningArrayView<1, uint32> Seed,
			const TLearningArrayView<1, const float> ActionNoiseScale,
			const float LogActionNoiseMin,
			const float LogActionNoiseMax,
			const FIndexSet Instances)
		{
			UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(Learning::NeuralNetworkPolicy::ActionNoise);

			const int32 InputNum = Input.Num<1>();
			const int32 OutputNum = Output.Num<1>();

			if (UE_LEARNING_ISPC && Instances.IsSlice())
			{
#if UE_LEARNING_ISPC
				ispc::LearningLayerActionNoise(
					Output.Slice(Instances.GetSliceStart(), Instances.GetSliceNum()).GetData(),
					Input.Slice(Instances.GetSliceStart(), Instances.GetSliceNum()).GetData(),
					Seed.Slice(Instances.GetSliceStart(), Instances.GetSliceNum()).GetData(),
					ActionNoiseScale.Slice(Instances.GetSliceStart(), Instances.GetSliceNum()).GetData(),
					Instances.GetSliceNum(),
					InputNum,
					OutputNum,
					LogActionNoiseMin,
					LogActionNoiseMax);
#endif
			}
			else
			{
				for (const int32 InstanceIdx : Instances)
				{
					for (int32 OutputIdx = 0; OutputIdx < OutputNum; OutputIdx++)
					{
						const float ActionMean = Input[InstanceIdx][OutputIdx];
						const float LogActionStd = Input[InstanceIdx][OutputNum + OutputIdx];
						const float ActionStd = ActionNoiseScale[InstanceIdx] *
							FMath::Exp(Sigmoid(LogActionStd) * (LogActionNoiseMax - LogActionNoiseMin) + LogActionNoiseMin);

						Output[InstanceIdx][OutputIdx] = Random::Gaussian(
							Seed[InstanceIdx] ^ 0xab744615 ^ Random::Int(OutputIdx ^ 0xf8a88a27),
							ActionMean,
							ActionStd);
					}
				}
			}

			Random::ResampleStateArray(Seed, Instances);
		}

		static inline void EvaluateLayer(
			TLearningArrayView<2, float> Outputs,
			TArrayView<TLearningArray<2, float, TInlineAllocator<128>>> Activations,
			const TLearningArrayView<2, const float> Inputs,
			TLearningArrayView<1, uint32> Seed,
			const TLearningArrayView<1, const float> ActionNoiseScale,
			const int32 LayerIdx,
			const int32 LayerNum,
			const float LogActionNoiseMin,
			const float LogActionNoiseMax,
			const FNeuralNetwork& NeuralNetwork,
			const FIndexSet Instances)
		{
			UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(Learning::NeuralNetworkPolicy::EvaluateLayer);

			TLearningArrayView<2, float> LayerOutput = Activations[LayerIdx - 0];
			const TLearningArrayView<2, const float> LayerInput = LayerIdx == 0 ? Inputs : Activations[LayerIdx - 1];

			// Apply Linear Transformation

			MatMulPlusBias(
				LayerOutput,
				LayerInput,
				NeuralNetwork.Weights[LayerIdx],
				NeuralNetwork.Biases[LayerIdx],
				Instances);

			// Apply Noise on final layer otherwise apply activation in-place

			if (LayerIdx == LayerNum - 1)
			{
				ActionNoise(
					Outputs,
					LayerOutput,
					Seed,
					ActionNoiseScale,
					LogActionNoiseMin,
					LogActionNoiseMax,
					Instances);
			}
			else
			{
				switch (NeuralNetwork.ActivationFunction)
				{
				case EActivationFunction::ReLU: ActivationReLU(LayerOutput, Instances); break;
				case EActivationFunction::ELU: ActivationELU(LayerOutput, Instances); break;
				case EActivationFunction::TanH: ActivationTanH(LayerOutput, Instances); break;
				default: UE_LEARNING_NOT_IMPLEMENTED();
				}
			}
		}
	}

	FNeuralNetworkPolicyFunction::FNeuralNetworkPolicyFunction(
		const FName& InIdentifier,
		const TSharedRef<FArrayMap>& InInstanceData,
		const int32 InMaxInstanceNum,
		const TSharedRef<FNeuralNetwork>& InNeuralNetwork,
		const uint32 InSeed,
		const FNeuralNetworkPolicyFunctionSettings& InSettings)
		: FFunctionObject(InInstanceData)
		, NeuralNetwork(InNeuralNetwork)
		, Settings(InSettings)
	{
		SeedHandle = InstanceData->Add<1, uint32>({ InIdentifier, TEXT("Seed") }, { InMaxInstanceNum });
		InputHandle = InstanceData->Add<2, float>({ InIdentifier, TEXT("Input") }, { InMaxInstanceNum, NeuralNetwork->GetInputNum() }, 0.0f);
		OutputHandle = InstanceData->Add<2, float>({ InIdentifier, TEXT("Output") }, { InMaxInstanceNum, NeuralNetwork->GetOutputNum() / 2 }, 0.0f);
		ActionNoiseScaleHandle = InstanceData->Add<1, float>({ InIdentifier, TEXT("ActionNoiseScale") }, { InMaxInstanceNum }, Settings.ActionNoiseScale);

		Random::IntArray(InstanceData->View(SeedHandle), InSeed);

		// Allocate temporary storage for activations

		const int32 LayerNum = InNeuralNetwork->GetLayerNum();

		Activations.SetNum(LayerNum);
		for (int32 LayerIdx = 0; LayerIdx < LayerNum; LayerIdx++)
		{
			Activations[LayerIdx].SetNumUninitialized({ InMaxInstanceNum, NeuralNetwork->Weights[LayerIdx].Num(1) });
		}
	}

	void FNeuralNetworkPolicyFunction::Evaluate(const FIndexSet Instances)
	{
		UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(Learning::FNeuralNetworkPolicyFunction::Evaluate);

		const TLearningArrayView<2, const float> Inputs = InstanceData->ConstView(InputHandle);
		const TLearningArrayView<1, const float> ActionNoiseScale = InstanceData->ConstView(ActionNoiseScaleHandle);
		TLearningArrayView<2, float> Outputs = InstanceData->View(OutputHandle);
		TLearningArrayView<1, uint32> Seed = InstanceData->View(SeedHandle);

		UE_LEARNING_CHECK(NeuralNetwork->GetOutputNum() == 2 * Outputs.Num<1>());
		UE_LEARNING_CHECK(NeuralNetwork->GetInputNum() == Inputs.Num<1>());

		const int32 LayerNum = NeuralNetwork->GetLayerNum();

		if (!ensureMsgf(LayerNum > 0, TEXT("Empty Neural Network used in Policy")))
		{
			Array::Zero(Outputs, Instances);
			return;
		}

		UE_LEARNING_ARRAY_VALUE_CHECK(Settings.ActionNoiseMin >= 0.0f && Settings.ActionNoiseMax >= 0.0f);
		const float LogActionNoiseMin = FMath::Loge(Settings.ActionNoiseMin + UE_KINDA_SMALL_NUMBER);
		const float LogActionNoiseMax = FMath::Loge(Settings.ActionNoiseMax + UE_KINDA_SMALL_NUMBER);

		// Compute Layers

		for (int32 LayerIdx = 0; LayerIdx < LayerNum; LayerIdx++)
		{
			auto LayerEvaluationFunction = [&](const int32 SliceStart, const int32 SliceNum)
			{
				NeuralNetworkPolicy::EvaluateLayer(
					Outputs,
					Activations,
					Inputs,
					Seed,
					ActionNoiseScale,
					LayerIdx,
					LayerNum,
					LogActionNoiseMin,
					LogActionNoiseMax,
					*NeuralNetwork,
					Instances.Slice(SliceStart, SliceNum));
			};

			if (Settings.bParallelEvaluation && Instances.Num() > Settings.MinParallelBatchSize)
			{
				SlicedParallelFor(Instances.Num(), Settings.MinParallelBatchSize, LayerEvaluationFunction);
			}
			else
			{
				LayerEvaluationFunction(0, Instances.Num());
			}
		}

		Array::Check(Outputs, Instances);
	}
}
