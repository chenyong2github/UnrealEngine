// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsNeuralNetwork.h"

#include "LearningNeuralNetwork.h"
#include "LearningLog.h"

ULearningAgentsNeuralNetwork::ULearningAgentsNeuralNetwork() {}
ULearningAgentsNeuralNetwork::ULearningAgentsNeuralNetwork(FVTableHelper& Helper) : ULearningAgentsNeuralNetwork() {}
ULearningAgentsNeuralNetwork::~ULearningAgentsNeuralNetwork() {}

namespace UE::Learning::Agents
{
	ELearningAgentsActivationFunction GetLearningAgentsActivationFunction(const EActivationFunction ActivationFunction)
	{
		switch (ActivationFunction)
		{
		case EActivationFunction::ReLU: return ELearningAgentsActivationFunction::ReLU;
		case EActivationFunction::TanH: return ELearningAgentsActivationFunction::TanH;
		case EActivationFunction::ELU: return ELearningAgentsActivationFunction::ELU;
		default:UE_LOG(LogLearning, Error, TEXT("Unknown Activation Function.")); return ELearningAgentsActivationFunction::ELU;
		}
	}

	EActivationFunction GetActivationFunction(const ELearningAgentsActivationFunction ActivationFunction)
	{
		switch (ActivationFunction)
		{
		case ELearningAgentsActivationFunction::ReLU: return EActivationFunction::ReLU;
		case ELearningAgentsActivationFunction::TanH: return EActivationFunction::TanH;
		case ELearningAgentsActivationFunction::ELU: return EActivationFunction::ELU;
		default:UE_LOG(LogLearning, Error, TEXT("Unknown Activation Function.")); return EActivationFunction::ELU;
		}
	}
}

void ULearningAgentsNeuralNetwork::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if (Ar.IsLoading())
	{
		bool bValid;
		Ar << bValid;
		if (bValid)
		{
			int32 InputNum;
			int32 OutputNum;
			int32 HiddenNum;
			int32 LayerNum;
			UE::Learning::EActivationFunction Activation;
			Ar << InputNum;
			Ar << OutputNum;
			Ar << HiddenNum;
			Ar << LayerNum;
			Ar << Activation;
			NeuralNetwork = MakeShared<UE::Learning::FNeuralNetwork>();
			NeuralNetwork->Resize(InputNum, OutputNum, HiddenNum, LayerNum);
			NeuralNetwork->ActivationFunction = Activation;
			TArray<uint8> Bytes;
			Ar << Bytes;
			NeuralNetwork->DeserializeFromBytes(Bytes);
		}
		else
		{
			NeuralNetwork.Reset();
		}
	}
	else if (Ar.IsSaving())
	{
		bool bValid = NeuralNetwork.IsValid();
		Ar << bValid;
		if (bValid)
		{
			int32 InputNum = NeuralNetwork->GetInputNum();
			int32 OutputNum = NeuralNetwork->GetOutputNum();
			int32 HiddenNum = NeuralNetwork->GetHiddenNum();
			int32 LayerNum = NeuralNetwork->GetLayerNum();
			UE::Learning::EActivationFunction Activation = NeuralNetwork->ActivationFunction;
			Ar << InputNum;
			Ar << OutputNum;
			Ar << HiddenNum;
			Ar << LayerNum;
			Ar << Activation;
			TArray<uint8> Bytes;
			Bytes.SetNumUninitialized(UE::Learning::FNeuralNetwork::GetSerializationByteNum(InputNum, OutputNum, HiddenNum, LayerNum));
			NeuralNetwork->SerializeToBytes(Bytes);
			Ar << Bytes;
		}
	}
}