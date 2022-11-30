// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNEModel.h"

#include "NNXCore.h"
#include "NNXRuntimeFormat.h"
#include "NNXInferenceModel.h"

#include "GenericPlatform/GenericPlatformProcess.h"

TArray<FString> UNNEModel::GetRuntimeNames()
{
	using namespace NNX;
	TArray<FString> Result;
	TArray<NNX::IRuntime*> Runtimes = GetAllRuntimes();
	for (int i = 0; i < Runtimes.Num(); i++)
	{
		Result.Add(Runtimes[i]->GetRuntimeName());
	}
	return Result;
}

UNNEModel* UNNEModel::Create(UObject* Parent, FString RuntimeName, UNNEModelData* ModelData)
{
	UNNEModel* Result = NewObject<UNNEModel>(Parent);
	if (Result && Result->Load(RuntimeName, ModelData))
	{
		return Result;
	}
	return nullptr;
}

bool UNNEModel::Load(FString RuntimeName, UNNEModelData* ModelData)
{
	using namespace NNX;

	IRuntime* Runtime = GetRuntime(RuntimeName);
	if (!Runtime)
	{
		UE_LOG(LogNNX, Error, TEXT("UNNEModel: No runtime '%s' found. Valid runtimes are: "), *RuntimeName);
		TArray<NNX::IRuntime*> Runtimes = GetAllRuntimes();
		for (int i = 0; i < Runtimes.Num(); i++)
		{
			UE_LOG(LogNNX, Error, TEXT("- %s"), *Runtimes[i]->GetRuntimeName());
		}
		return false;
	}

	if (!ModelData)
	{
		UE_LOG(LogNNX, Error, TEXT("UNNEModel: Valid model data required to load the model"));
		return false;
	}

	TConstArrayView<uint8> Data = ModelData->GetModelData(RuntimeName);
	if (Data.Num() < 1)
	{
		UE_LOG(LogNNX, Error, TEXT("UNNEModel: No model data for %s found"), *RuntimeName);
		return false;
	}

	Model = Runtime->CreateModel(Data);

	return Model.IsValid();
}

int32 UNNEModel::NumInputs()
{
	if (!Model.IsValid())
	{
		return 0;
	}
	return Model->GetInputTensorDescs().Num();
}


int32 UNNEModel::NumOutputs()
{
	if (!Model.IsValid())
	{
		return 0;
	}
	return Model->GetOutputTensorDescs().Num();
}


TArray<int32> UNNEModel::GetInputShapes(int32 Index)
{
	if (!Model.IsValid())
	{
		return TArray<int32>();
	}

	TConstArrayView<NNX::FTensorDesc> Desc = Model->GetInputTensorDescs();
	if (Index < 0 || Index >= Desc.Num())
	{
		return TArray<int32>();
	}

	return TArray<int32>(Desc[Index].GetShape().Data);
}

TArray<int32> UNNEModel::GetOutputShapes(int32 Index)
{
	if (!Model.IsValid())
	{
		return TArray<int32>();
	}

	TConstArrayView<NNX::FTensorDesc> Desc = Model->GetOutputTensorDescs();
	if (Index < 0 || Index >= Desc.Num())
	{
		return TArray<int32>();
	}

	return TArray<int32>(Desc[Index].GetShape().Data);
}

bool UNNEModel::SetInputOutput(const TArray<FNNETensor>& Input, UPARAM(ref) TArray<FNNETensor>& Output)
{
	if (!Model.IsValid())
	{
		return false;
	}

	InputBindings.Empty();
	OutputBindings.Empty();

	TConstArrayView<NNX::FTensorDesc> InputDescs = Model->GetInputTensorDescs();
	if (InputDescs.Num() != Input.Num())
	{
		UE_LOG(LogNNX, Error, TEXT("UNNEModel: Invalid number of input tensors provided"));
		return false;
	}

	TConstArrayView<NNX::FTensorDesc> OutputDescs = Model->GetOutputTensorDescs();
	if (OutputDescs.Num() != Output.Num())
	{
		UE_LOG(LogNNX, Error, TEXT("UNNEModel: Invalid number of output tensors provided"));
		return false;
	}

	TArray<NNX::FTensorShape> InputShapes;
	for (int i = 0; i < Input.Num(); i++)
	{
		InputBindings.Add(NNX::FMLTensorBinding::FromCPU((void*)(Input[i].Data.GetData()), sizeof(float) * Input[i].Data.Num()));

		InputShapes.Add(NNX::FTensorShape::MakeFromSymbolic(NNX::FSymbolicTensorShape::Make(Input[i].Shape)));
	}

	for (int i = 0; i < Input.Num(); i++)
	{
		OutputBindings.Add(NNX::FMLTensorBinding::FromCPU((void*)(Output[i].Data.GetData()), sizeof(float) * Output[i].Data.Num()));
	}

	if (Model->SetInputTensorShapes(InputShapes) != 0)
	{
		UE_LOG(LogNNX, Error, TEXT("UNNEModel: Failed to set input shapes"));
		return false;
	}

	return true;
}

bool UNNEModel::RunSync()
{
	if (!Model.IsValid())
	{
		return false;
	}

	return Model->RunSync(InputBindings, OutputBindings) == 0;
}