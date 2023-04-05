// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningNeuralNetwork.h"

namespace UE::Learning
{
	const TCHAR* GetActivationFunctionString(const EActivationFunction ActivationFunction)
	{
		switch (ActivationFunction)
		{
		case EActivationFunction::ReLU: return TEXT("ReLU");
		case EActivationFunction::ELU: return TEXT("ELU");
		case EActivationFunction::TanH: return TEXT("TanH");
		default: UE_LEARNING_NOT_IMPLEMENTED(); return TEXT("Unknown");
		}
	}

	void FNeuralNetwork::Resize(
		const int32 InputNum,
		const int32 OutputNum,
		const int32 HiddenNum,
		const int32 LayerNum)
	{
		UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(Learning::FNeuralNetwork::Resize);
		UE_LEARNING_CHECKF(LayerNum >= 2, TEXT("At least two layers required (input and output layers)"));

		Weights.SetNum(LayerNum);
		Biases.SetNum(LayerNum);

		Weights[0].SetNumUninitialized({ InputNum, HiddenNum });
		Biases[0].SetNumUninitialized({ HiddenNum });

		for (int32 LayerIdx = 0; LayerIdx < LayerNum - 2; LayerIdx++)
		{
			Weights[LayerIdx + 1].SetNumUninitialized({ HiddenNum, HiddenNum });
			Biases[LayerIdx + 1].SetNumUninitialized({ HiddenNum });
		}

		Weights[LayerNum - 1].SetNumUninitialized({ HiddenNum, OutputNum });
		Biases[LayerNum - 1].SetNumUninitialized({ OutputNum });

		for (int32 LayerIdx = 0; LayerIdx < LayerNum; LayerIdx++)
		{
			Array::Zero(Weights[LayerIdx]);
			Array::Zero(Biases[LayerIdx]);
		}
	}

	int32 FNeuralNetwork::GetInputNum() const
	{
		return Weights[0].Num<0>();
	}

	int32 FNeuralNetwork::GetOutputNum() const
	{
		return Weights[Weights.Num() - 1].Num<1>();
	}

	int32 FNeuralNetwork::GetLayerNum() const
	{
		return Weights.Num();
	}

	int32 FNeuralNetwork::GetHiddenNum() const
	{
		return Weights[0].Num<1>();
	}

	void FNeuralNetwork::DeserializeFromBytes(const TLearningArrayView<1, const uint8> RawBytes)
	{
		UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(Learning::FNeuralNetwork::DeserializeFromBytes);

		const int32 TotalByteNum = GetSerializationByteNum(GetInputNum(), GetOutputNum(), GetHiddenNum(), GetLayerNum());

		UE_LEARNING_CHECK(RawBytes.Num() == TotalByteNum);

		const int32 LayerNum = Weights.Num();

		int32 Offset = 0;

		for (int32 LayerIdx = 0; LayerIdx < LayerNum; LayerIdx++)
		{
			const int32 WeightByteNum = Weights[LayerIdx].Num() * sizeof(float);
			FMemory::Memcpy((uint8*)Weights[LayerIdx].GetData(), &RawBytes[Offset], WeightByteNum);
			Array::Check(Weights[LayerIdx]);
			Offset += WeightByteNum;

			const int32 BiasByteNum = Biases[LayerIdx].Num() * sizeof(float);
			FMemory::Memcpy((uint8*)Biases[LayerIdx].GetData(), &RawBytes[Offset], BiasByteNum);
			Array::Check(Biases[LayerIdx]);
			Offset += BiasByteNum;
		}

		UE_LEARNING_CHECK(Offset == TotalByteNum);
	}

	void FNeuralNetwork::SerializeToBytes(TLearningArrayView<1, uint8> OutRawBytes) const
	{
		UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(Learning::FNeuralNetwork::SerializeToBytes);

		const int32 TotalByteNum = GetSerializationByteNum(GetInputNum(), GetOutputNum(), GetHiddenNum(), GetLayerNum());

		UE_LEARNING_CHECK(OutRawBytes.Num() == TotalByteNum);

		const int32 LayerNum = Weights.Num();

		int32 Offset = 0;

		for (int32 LayerIdx = 0; LayerIdx < LayerNum; LayerIdx++)
		{
			const int32 WeightByteNum = Weights[LayerIdx].Num() * sizeof(float);
			FMemory::Memcpy(&OutRawBytes[Offset], (uint8*)Weights[LayerIdx].GetData(), WeightByteNum);
			Offset += WeightByteNum;

			const int32 BiasByteNum = Biases[LayerIdx].Num() * sizeof(float);
			FMemory::Memcpy(&OutRawBytes[Offset], (uint8*)Biases[LayerIdx].GetData(), BiasByteNum);
			Offset += BiasByteNum;
		}

		UE_LEARNING_CHECK(Offset == TotalByteNum);
	}

	int32 FNeuralNetwork::GetSerializationByteNum(
		const int32 InputNum,
		const int32 OutputNum,
		const int32 HiddenNum,
		const int32 LayerNum)
	{
		int32 Total = 0;

		Total += InputNum * HiddenNum * sizeof(float);
		Total += HiddenNum * sizeof(float);

		for (int32 LayerIdx = 0; LayerIdx < LayerNum - 2; LayerIdx++)
		{
			Total += HiddenNum * HiddenNum * sizeof(float);
			Total += HiddenNum * sizeof(float);
		}

		Total += HiddenNum * OutputNum * sizeof(float);
		Total += OutputNum * sizeof(float);

		return Total;
	}

}
