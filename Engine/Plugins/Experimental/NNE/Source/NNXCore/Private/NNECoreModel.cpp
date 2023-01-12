// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNECoreModel.h"

#include "NNXCore.h"
#include "NNXRuntimeFormat.h"
#include "NNXInferenceModel.h"

#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"

#include "GenericPlatform/GenericPlatformProcess.h"

TArray<FString> UNNEModel::GetRuntimeNames()
{
	using namespace NNX;
	TArray<FString> Result;
	TArray<NNX::IRuntime*> Runtimes = GetAllRuntimes();
	for (int32 i = 0; i < Runtimes.Num(); i++)
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
	check(IsInGameThread());

	if (IsAsyncRunning.IsValid() && *(IsAsyncRunning.Get()))
	{
		return false;
	}

	using namespace NNX;

	IRuntime* Runtime = GetRuntime(RuntimeName);
	if (!Runtime)
	{
		UE_LOG(LogNNX, Error, TEXT("UNNEModel: No runtime '%s' found. Valid runtimes are: "), *RuntimeName);
		TArray<NNX::IRuntime*> Runtimes = GetAllRuntimes();
		for (int32 i = 0; i < Runtimes.Num(); i++)
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

	Model = TSharedPtr<NNX::FMLInferenceModel>(Runtime->CreateModel(Data).Release());

	return Model.IsValid();
}

int32 UNNEModel::NumInputs()
{
	check(IsInGameThread());

	if ((IsAsyncRunning.IsValid() && *(IsAsyncRunning.Get())) || !Model.IsValid())
	{
		return 0;
	}
	return Model->GetInputTensorDescs().Num();
}


int32 UNNEModel::NumOutputs()
{
	check(IsInGameThread());

	if ((IsAsyncRunning.IsValid() && *(IsAsyncRunning.Get())) || !Model.IsValid())
	{
		return 0;
	}
	return Model->GetOutputTensorDescs().Num();
}


TArray<int32> UNNEModel::GetInputShapes(int32 Index)
{
	check(IsInGameThread());

	if ((IsAsyncRunning.IsValid() && *(IsAsyncRunning.Get())) || !Model.IsValid())
	{
		return TArray<int32>();
	}

	TConstArrayView<NNX::FTensorDesc> Desc = Model->GetInputTensorDescs();
	if (Index < 0 || Index >= Desc.Num())
	{
		return TArray<int32>();
	}

	return TArray<int32>(Desc[Index].GetShape().GetData());
}

TArray<int32> UNNEModel::GetOutputShapes(int32 Index)
{
	check(IsInGameThread());

	if ((IsAsyncRunning.IsValid() && *(IsAsyncRunning.Get())) || !Model.IsValid())
	{
		return TArray<int32>();
	}

	TConstArrayView<NNX::FTensorDesc> Desc = Model->GetOutputTensorDescs();
	if (Index < 0 || Index >= Desc.Num())
	{
		return TArray<int32>();
	}

	return TArray<int32>(Desc[Index].GetShape().GetData());
}

bool UNNEModel::SetInput(const TArray<FNNETensor>& Input)
{
	check(IsInGameThread());

	if ((IsAsyncRunning.IsValid() && *(IsAsyncRunning.Get())) || !Model.IsValid())
	{
		return false;
	}

	InputBindings.Empty();
	InputShapes.Empty();

	TConstArrayView<NNX::FTensorDesc> InputDescs = Model->GetInputTensorDescs();
	if (InputDescs.Num() != Input.Num())
	{
		UE_LOG(LogNNX, Error, TEXT("UNNEModel: Invalid number of input tensors provided"));
		return false;
	}

	for (int32 i = 0; i < Input.Num(); i++)
	{
		InputBindings.Add(NNX::FMLTensorBinding::FromCPU((void*)(Input[i].Data.GetData()), sizeof(float) * Input[i].Data.Num()));
		InputShapes.Add(NNX::FTensorShape::MakeFromSymbolic(UE::NNECore::FSymbolicTensorShape::Make(Input[i].Shape)));
	}

	if (Model->SetInputTensorShapes(InputShapes) != 0)
	{
		UE_LOG(LogNNX, Error, TEXT("UNNEModel: Failed to set input shapes"));
		return false;
	}

	return true;
}

bool UNNEModel::RunSync(UPARAM(ref) TArray<FNNETensor>& Output)
{
	check(IsInGameThread());

	if ((IsAsyncRunning.IsValid() && *(IsAsyncRunning.Get())) || !Model.IsValid())
	{
		return false;
	}

	TArray<NNX::FMLTensorBinding> OutputBindings;

	TConstArrayView<NNX::FTensorDesc> OutputDescs = Model->GetOutputTensorDescs();
	if (OutputDescs.Num() != Output.Num())
	{
		UE_LOG(LogNNX, Error, TEXT("UNNEModel: Invalid number of output tensors provided"));
		return false;
	}

	for (int32 i = 0; i < Output.Num(); i++)
	{
		OutputBindings.Add(NNX::FMLTensorBinding::FromCPU((void*)(Output[i].Data.GetData()), sizeof(float) * Output[i].Data.Num()));
	}

	return Model->RunSync(InputBindings, OutputBindings) == 0;
}

bool UNNEModel::RunAsync(FNNETaskPriority TaskPriority, FNNEModelOnAsyncResult OnAsyncResult)
{
	check(IsInGameThread());

	if ((IsAsyncRunning.IsValid() && *(IsAsyncRunning.Get())) ||  !Model.IsValid())
	{
		return false;
	}

	// Set the signal that the async thread is running to prevent other operations to happen on the model
	if (!IsAsyncRunning.IsValid())
	{
		IsAsyncRunning = MakeShared<bool>();
	}
	*(IsAsyncRunning.Get()) = true;

	// Allocate new memory which can be owned by the thread to prevent access errors when the model evaluation survives this object's lifetime
	TArray<TArray<float>> InputData;
	for (int32 i = 0; i < InputBindings.Num(); i++)
	{
		InputData.Add(TArray<float>());
		InputData[i].SetNumUninitialized((int32)(InputBindings[i].SizeInBytes / sizeof(float)));
		FGenericPlatformMemory::Memcpy((void*)InputData[i].GetData(), InputBindings[i].CpuMemory, InputBindings[i].SizeInBytes);
	}

	// Launch a thread passing the model and a copy of the data to work on
	TSharedPtr<NNX::FMLInferenceModel> ThreadModel = Model;
	TSharedPtr<bool> LocalIsAsyncRunning = IsAsyncRunning;
	AsyncTask(TaskPriority == FNNETaskPriority::Low ? ENamedThreads::AnyBackgroundThreadNormalTask : (TaskPriority == FNNETaskPriority::High ? ENamedThreads::AnyHiPriThreadHiPriTask : ENamedThreads::AnyNormalThreadNormalTask), [ThreadModel, ThreadInputData = MoveTemp(InputData), LocalIsAsyncRunning, OnAsyncResult]()
	{
		// Create input bindings
		TArray<NNX::FMLTensorBinding> ThreadInputBindings;
		for (int32 i = 0; i < ThreadInputData.Num(); i++)
		{
			ThreadInputBindings.Add(NNX::FMLTensorBinding::FromCPU((void*)(ThreadInputData[i].GetData()), sizeof(float) * ThreadInputData[i].Num()));
		}

		// Create output data and bindings
		TArray<FNNETensor> OutputTensors;
		TArray<NNX::FMLTensorBinding> ThreadOutputBindings;
		TConstArrayView<NNX::FTensorDesc> OutputDescs = ThreadModel->GetOutputTensorDescs();
		for (int32 i = 0; i < OutputDescs.Num(); i++)
		{
			OutputTensors.Add(FNNETensor());
			OutputTensors[i].Shape = OutputDescs[i].GetShape().GetData();
			int32 Size = OutputTensors[i].Shape.Num() > 0 ? 1 : 0;
			for (int32 j = 0; j < OutputTensors[i].Shape.Num(); j++)
			{
				Size *= OutputTensors[i].Shape[j] > 0 ? OutputTensors[i].Shape[j] : 0;
			}
			if (Size > 0)
			{
				OutputTensors[i].Data.SetNumUninitialized(Size);
			}
			ThreadOutputBindings.Add(NNX::FMLTensorBinding::FromCPU((void*)(OutputTensors[i].Data.GetData()), sizeof(float) * OutputTensors[i].Data.Num()));
		}

		bool bResult = ThreadModel->RunSync(ThreadInputBindings, ThreadOutputBindings) == 0;
		
		AsyncTask(ENamedThreads::GameThread, [LocalIsAsyncRunning, ThreadOutputTensors= MoveTemp(OutputTensors), bResult, OnAsyncResult]()
		{
			check(IsInGameThread());

			*(LocalIsAsyncRunning.Get()) = false;
			OnAsyncResult.ExecuteIfBound(ThreadOutputTensors, bResult);
		});
	});

	return true;
}

bool UNNEModel::IsRunning()
{
	check(IsInGameThread());

	if (IsAsyncRunning.IsValid() && *(IsAsyncRunning.Get()))
	{
		return true;
	}

	return false;
}