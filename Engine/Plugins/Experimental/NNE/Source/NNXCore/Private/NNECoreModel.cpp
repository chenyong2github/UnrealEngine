// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNECoreModel.h"

#include "NNECore.h"

#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"

#include "GenericPlatform/GenericPlatformProcess.h"

TArray<FString> UNNEModel::GetRuntimeNames()
{
	using namespace UE::NNECore;

	TArray<FString> Result;
	TArrayView<TWeakInterfacePtr<INNERuntime>> Runtimes = GetAllRuntimes();
	for (int32 i = 0; i < Runtimes.Num(); i++)
	{
		if (Runtimes[i].IsValid())
		{
			Result.Add(Runtimes[i]->GetRuntimeName());
		}
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

	using namespace UE::NNECore;

	if (IsAsyncRunning.IsValid() && *(IsAsyncRunning.Get()))
	{
		return false;
	}

	TWeakInterfacePtr<INNERuntimeCPU> Runtime = GetRuntime<INNERuntimeCPU>(RuntimeName);
	if (!Runtime.IsValid())
	{
		UE_LOG(LogNNE, Error, TEXT("UNNEModel: No runtime '%s' found. Valid runtimes are: "), *RuntimeName);
		TArray<FString> Names = GetRuntimeNames();
		for (int32 i = 0; i < Names.Num(); i++)
		{
			UE_LOG(LogNNE, Error, TEXT("- %s"), *Names[i]);
		}
		return false;
	}

	if (!ModelData)
	{
		UE_LOG(LogNNE, Error, TEXT("UNNEModel: Valid model data required to load the model"));
		return false;
	}

	TUniquePtr<IModelCPU> UniqueModel = Runtime->CreateModelCPU(ModelData);
	if (!UniqueModel.IsValid())
	{
		UE_LOG(LogNNE, Error, TEXT("UNNEModel: Could not create the model"));
		return false;
	}

	Model = TSharedPtr<IModelCPU>(UniqueModel.Release());
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

	using namespace UE::NNECore;

	if ((IsAsyncRunning.IsValid() && *(IsAsyncRunning.Get())) || !Model.IsValid())
	{
		return TArray<int32>();
	}

	TConstArrayView<FTensorDesc> Desc = Model->GetInputTensorDescs();
	if (Index < 0 || Index >= Desc.Num())
	{
		return TArray<int32>();
	}

	return TArray<int32>(Desc[Index].GetShape().GetData());
}

TArray<int32> UNNEModel::GetOutputShapes(int32 Index)
{
	check(IsInGameThread());

	using namespace UE::NNECore;

	if ((IsAsyncRunning.IsValid() && *(IsAsyncRunning.Get())) || !Model.IsValid())
	{
		return TArray<int32>();
	}

	TConstArrayView<FTensorDesc> Desc = Model->GetOutputTensorDescs();
	if (Index < 0 || Index >= Desc.Num())
	{
		return TArray<int32>();
	}

	return TArray<int32>(Desc[Index].GetShape().GetData());
}

bool UNNEModel::SetInput(const TArray<FNNETensor>& Input)
{
	check(IsInGameThread());

	using namespace UE::NNECore;

	if ((IsAsyncRunning.IsValid() && *(IsAsyncRunning.Get())) || !Model.IsValid())
	{
		return false;
	}

	InputBindings.Empty();
	InputShapes.Empty();

	TConstArrayView<FTensorDesc> InputDescs = Model->GetInputTensorDescs();
	if (InputDescs.Num() != Input.Num())
	{
		UE_LOG(LogNNE, Error, TEXT("UNNEModel: Invalid number of input tensors provided"));
		return false;
	}

	InputBindings.SetNum(Input.Num());
	InputShapes.SetNum(Input.Num());
	for (int32 i = 0; i < Input.Num(); i++)
	{
		InputBindings[i].Data = (void*)Input[i].Data.GetData();
		InputBindings[i].SizeInBytes = Input[i].Data.Num() * sizeof(float);
		InputShapes[i] = FTensorShape::MakeFromSymbolic(FSymbolicTensorShape::Make(Input[i].Shape));
	}

	if (Model->SetInputTensorShapes(InputShapes) != 0)
	{
		UE_LOG(LogNNE, Error, TEXT("UNNEModel: Failed to set input shapes"));
		return false;
	}

	return true;
}

bool UNNEModel::RunSync(UPARAM(ref) TArray<FNNETensor>& Output)
{
	check(IsInGameThread());

	using namespace UE::NNECore;

	if ((IsAsyncRunning.IsValid() && *(IsAsyncRunning.Get())) || !Model.IsValid())
	{
		return false;
	}

	TConstArrayView<FTensorDesc> OutputDescs = Model->GetOutputTensorDescs();
	if (OutputDescs.Num() != Output.Num())
	{
		UE_LOG(LogNNE, Error, TEXT("UNNEModel: Invalid number of output tensors provided"));
		return false;
	}

	TArray<FTensorBindingCPU> OutputBindings;
	OutputBindings.SetNum(Output.Num());
	for (int32 i = 0; i < Output.Num(); i++)
	{
		OutputBindings[i].Data = (void*)Output[i].Data.GetData();
		OutputBindings[i].SizeInBytes = Output[i].Data.Num() * sizeof(float);
	}

	return Model->RunSync(InputBindings, OutputBindings) == 0;
}

bool UNNEModel::RunAsync(FNNETaskPriority TaskPriority, FNNEModelOnAsyncResult OnAsyncResult)
{
	check(IsInGameThread());

	using namespace UE::NNECore;

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
		InputData[i].SetNum((int32)(InputBindings[i].SizeInBytes / sizeof(float)));
		FGenericPlatformMemory::Memcpy((void*)InputData[i].GetData(), InputBindings[i].Data, InputBindings[i].SizeInBytes);
	}

	// Launch a thread passing the model and a copy of the data to work on
	TSharedPtr<IModelCPU> ThreadModel = Model;
	TSharedPtr<bool> LocalIsAsyncRunning = IsAsyncRunning;
	AsyncTask(TaskPriority == FNNETaskPriority::Low ? ENamedThreads::AnyBackgroundThreadNormalTask : (TaskPriority == FNNETaskPriority::High ? ENamedThreads::AnyHiPriThreadHiPriTask : ENamedThreads::AnyNormalThreadNormalTask), [ThreadModel, ThreadInputData = MoveTemp(InputData), LocalIsAsyncRunning, OnAsyncResult]()
	{
		// Create input bindings
		TArray<FTensorBindingCPU> ThreadInputBindings;
		ThreadInputBindings.SetNum(ThreadInputData.Num());
		for (int32 i = 0; i < ThreadInputData.Num(); i++)
		{
			ThreadInputBindings[i].Data = (void*)ThreadInputData[i].GetData();
			ThreadInputBindings[i].SizeInBytes = ThreadInputData[i].Num() * sizeof(float);
		}

		// Create output data and bindings
		TArray<FNNETensor> OutputTensors;
		TArray<FTensorBindingCPU> ThreadOutputBindings;
		TConstArrayView<FTensorDesc> OutputDescs = ThreadModel->GetOutputTensorDescs();
		ThreadOutputBindings.SetNum(OutputDescs.Num());
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
				OutputTensors[i].Data.SetNum(Size);
			}

			ThreadOutputBindings[i].Data = (void*)OutputTensors[i].Data.GetData();
			ThreadOutputBindings[i].SizeInBytes = OutputTensors[i].Data.Num() * sizeof(float);
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