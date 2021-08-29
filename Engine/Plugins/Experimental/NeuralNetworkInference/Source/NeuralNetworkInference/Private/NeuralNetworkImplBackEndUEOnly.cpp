// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralNetworkImplBackEndUEOnly.h"
#include "GraphProtoToNeuralNetworkConverter.h"
#include "NeuralNetworkInferenceUtils.h"
#include "RenderGraphBuilder.h"
#include "RenderingThread.h"
#include "RHI.h"

#if WITH_EDITOR
#include "ModelProtoFileReader.h"
#endif //WITH_EDITOR



/* FImplBackEndUEOnly public functions
 *****************************************************************************/

bool UNeuralNetwork::FImplBackEndUEOnly::Load(TSharedPtr<FImplBackEndUEOnly>& InOutImplBackEndUEOnly, const FString& InModelFilePath)
{
#if WITH_EDITOR
	// Initialize InOutImplBackEndUEOnly
	if (!InOutImplBackEndUEOnly.IsValid())
	{
		InOutImplBackEndUEOnly = MakeShared<FImplBackEndUEOnly>();
	}
	// Clean previous networks
	InOutImplBackEndUEOnly->Operators.Empty();
	InOutImplBackEndUEOnly->ModelProto = FModelProto();
	InOutImplBackEndUEOnly->bAreTensorsInGpu = false;
	// Read ModelProto
	if (!FModelProtoFileReader::ReadModelProtoFromFile(InOutImplBackEndUEOnly->ModelProto, InModelFilePath) || !InOutImplBackEndUEOnly->ModelProto.IsLoaded())
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::FImplBackEndUEOnly::Load(): Model could not be loaded from %s."), *InModelFilePath);
		return false;
	}
	// Turn ModelProto into Operators
	if (!FGraphProtoToNeuralNetworkConverter::Translate(InOutImplBackEndUEOnly->Operators, InOutImplBackEndUEOnly->TensorManager, InOutImplBackEndUEOnly->ModelProto.GetGraph(), InModelFilePath) || !InOutImplBackEndUEOnly->TensorManager.IsLoaded())
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::FImplBackEndUEOnly::Load(): TensorManager could not be loaded from %s."), *InModelFilePath);
		return false;
	}
	// Everything worked out, return true
	return true;

#else //WITH_EDITOR
	UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::FImplBackEndUEOnly::Load(): Only implemented for Editor mode."));
	return false;
#endif //WITH_EDITOR
}

//bool UNeuralNetwork::FImplBackEndUEOnly::Load(TSharedPtr<FImplBackEndUEOnly>& InOutImplBackEndUEOnly, FNeuralTensorManager& InTensorManager, const TArray<TSharedPtr<FNeuralOperator>>& InOperators)
//{
//	// Initialize InOutImplBackEndUEOnly
//	if (!InOutImplBackEndUEOnly.IsValid())
//	{
//		InOutImplBackEndUEOnly = MakeShared<FImplBackEndUEOnly>();
//	}
//	// Load
//	if (!InTensorManager.IsLoaded())
//	{
//		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::FImplBackEndUEOnly::Load(): TensorManager could not be loaded."));
//	}
//	Swap(InOutImplBackEndUEOnly->TensorManager, InTensorManager);
//	InOutImplBackEndUEOnly->Operators = InOperators;
//	return (InOutImplBackEndUEOnly->Operators.Num() > 0 && InOutImplBackEndUEOnly->TensorManager.IsLoaded());
//}

void UNeuralNetwork::FImplBackEndUEOnly::Run(FOnAsyncRunCompleted& InOutOnAsyncRunCompletedDelegate, const ENeuralNetworkSynchronousMode InSynchronousMode, const ENeuralDeviceType InDeviceType, const ENeuralDeviceType InInputDeviceType, const ENeuralDeviceType InOutputDeviceType)
{
	// Run UNeuralNetworkLegacy
	if (Operators.Num() > 0)
	{
		// Run graph
		if (InDeviceType == ENeuralDeviceType::CPU)
		{
			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("UNeuralNetwork_UEOnly_Run::Forward_CPU"), STAT_UNeuralNetwork_UEOnly_Run_Forward_CPU, STATGROUP_MachineLearning);
			// Run each operator forward pass
			for (TSharedPtr<FNeuralOperator>& Operator : Operators)
			{
				Operator->ForwardCPU();
			}
			// Run each operator post forward pass
			for (TSharedPtr<FNeuralOperator>& Operator : Operators)
			{
				Operator->PostForwardCPU();
			}
		}
		else if (InDeviceType == ENeuralDeviceType::GPU)
		{
			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("UNeuralNetwork_UEOnly_Run::Forward_GPU"), STAT_UNeuralNetwork_UEOnly_Run_Forward_GPU, STATGROUP_MachineLearning);
			// Sanity check
			if (TensorManager.GetTensorsMutable().Num() < 1)
			{
				UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::FImplBackEndUEOnly::Run(): Tensors.Num() = %d (should be > 0)."), TensorManager.GetTensorsMutable().Num());
				return;
			}

			// On RHI thread
			ENQUEUE_RENDER_COMMAND(UNeuralNetwork_UEOnly_Run_RenderThread)(
				[this, &InOutOnAsyncRunCompletedDelegate, InSynchronousMode, InInputDeviceType, InOutputDeviceType](FRHICommandListImmediate& RHICmdList)
				{
					FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("UNeuralNetwork::FImplBackEndUEOnly::Run()"));

					// Move memory from CPU to GPU
					TArray<FNeuralTensor>& Tensors = TensorManager.GetTensorsMutable();
					const bool bIsInputInCPU = (InInputDeviceType == ENeuralDeviceType::CPU);
					// Move only input tensors to GPU (once per UNeuralNetwork::FImplBackEndUEOnly::Run())
					if (bAreTensorsInGpu)
					{
						// If input is on CPU, move inputs to GPU
						if (bIsInputInCPU)
						{
							for (const int32 InputIndex : TensorManager.GetInputIndexes())
							{
								Tensors[InputIndex].ToGPU_RenderThread(&GraphBuilder);
							}
							for (const int32 NonInputIndex : TensorManager.GetNonInputIndexes())
							{
								Tensors[NonInputIndex].UpdateSRVAndOrUAV_RenderThread(&GraphBuilder);
							}
						}
						// If input is already on GPU, just refresh all w.r.t. GraphBuilder
						else
						{
							for (FNeuralTensor& Tensor : Tensors)
							{
								Tensor.UpdateSRVAndOrUAV_RenderThread(&GraphBuilder);
							}
						}
					}
					// Move all (input, intermediate, weight, output) tensors to GPU and also call Operators.ToGPU() to move their auxiliary memory into GPU
					// Only once per UNeuralNetworkLegacy instance, or until Load() is called again
					else
					{
						// If input is on CPU, move inputs to GPU
						if (bIsInputInCPU)
						{
							// Move tensors to GPU
							for (FNeuralTensor& Tensor : Tensors)
							{
								Tensor.ToGPU_RenderThread(&GraphBuilder);
							}
						}
						// If input is already on GPU, just refresh inputs
						else
						{
							for (const int32 InputIndex : TensorManager.GetInputIndexes())
							{
								Tensors[InputIndex].UpdateSRVAndOrUAV_RenderThread(&GraphBuilder);
							}
							for (const int32 NonInputIndex : TensorManager.GetNonInputIndexes())
							{
								Tensors[NonInputIndex].ToGPU_RenderThread(&GraphBuilder);
							}
						}
						// Operators->ToGPU()
						for (TSharedPtr<FNeuralOperator>& Operator : Operators)
						{
							Operator->ToGPU_RenderThread();
						}
						// bAreTensorsInGpu is true
						bAreTensorsInGpu = true;
					}

					// Run each operator forward pass
					for (TSharedPtr<FNeuralOperator>& Operator : this->Operators)
					{
						Operator->ForwardGPU_RenderThread(&GraphBuilder);
					}

					// Run each operator post forward pass
					for (TSharedPtr<FNeuralOperator>& Operator : this->Operators)
					{
						Operator->PostForwardGPU_RenderThread(&GraphBuilder);
					}

					// Move memory from GPU to CPU
					const bool bIsOutputInCPU = (InOutputDeviceType == ENeuralDeviceType::CPU);
					if (bIsOutputInCPU)
					{
						for (const int32 OutputIndex : TensorManager.GetOutputIndexes())
						{
							Tensors[OutputIndex].ToCPU_RenderThread(&GraphBuilder);
						}
					}

					// Broadcast delegates (from the render thread)
					if (InSynchronousMode == ENeuralNetworkSynchronousMode::Asynchronous)
					{
						GraphBuilder.AddPass(
							RDG_EVENT_NAME("Async delegate broadcast"),
							ERDGPassFlags::None,
							[&InOutOnAsyncRunCompletedDelegate](FRHICommandListImmediate& RHICmdList)
						{
							InOutOnAsyncRunCompletedDelegate.ExecuteIfBound();
						});
					}

					// Execute Render Graph
					GraphBuilder.Execute();
				}
			);

			// Block thread until GPU has finished
			if (InSynchronousMode == ENeuralNetworkSynchronousMode::Synchronous)
			{
				FNeuralNetworkInferenceUtils::WaitUntilRHIFinished();
			}
		}
		else
		{
			UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::FImplBackEndUEOnly::Run(): Unknown DeviceType = %d."), (int32)InDeviceType);
		}
	}
	else
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::FImplBackEndUEOnly::Run() called with an empty model."));
	}
}

//FString UNeuralNetwork::FImplBackEndUEOnly::ToString() const
//{
//	// Add GraphProto
//	FString String = ModelProto.GetGraph().ToString();
//	// Add FNeuralTensor(s)
//	String += TEXT("TensorManager:\n");
//	const TMap<FString, int32>& NameIndexMap = TensorManager.GetNameIndexMap();
//	const TArray<FNeuralTensor>& Tensors = TensorManager.GetTensors();
//	if (NameIndexMap.Num() > 0)
//	{
//		for (const auto& NameIndexPair : NameIndexMap)
//		{
//			String += FString::Format(TEXT(" -{0}: {1}\n"), { NameIndexPair.Key, Tensors[NameIndexPair.Value].ToString(20) });
//		}
//	}
//	else
//	{
//		for (const FNeuralTensor& Tensor : Tensors)
//		{
//			String += FString::Format(TEXT(" -{0}\n"), { Tensor.ToString(20) });
//		}
//	}
//	String += TEXT("InputTensorMap:\n");
//	for (const auto& NameIndexPair : TensorManager.GetInputNameIndexMap())
//	{
//		String += FString::Format(TEXT(" -{0}: {1}\n"), { NameIndexPair.Key, Tensors[NameIndexPair.Value].ToString(20) });
//	}
//	String += TEXT("OutputTensorMap:\n");
//	for (const auto& NameIndexPair : TensorManager.GetOutputNameIndexMap())
//	{
//		String += FString::Format(TEXT(" -{0}: {1}\n"), { NameIndexPair.Key, Tensors[NameIndexPair.Value].ToString(20) });
//	}
//	// Add FNeuralOperator(s)
//	String += TEXT("Operators:\n");
//	for (const TSharedPtr<FNeuralOperator>& Operator : Operators)
//	{
//		String += FString::Format(TEXT(" -{0}\n"), { Operator->ToString() });
//	}
//	// Return result
//	return String;
//}
