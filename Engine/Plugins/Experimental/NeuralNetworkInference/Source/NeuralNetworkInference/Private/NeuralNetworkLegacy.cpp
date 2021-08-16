// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralNetworkLegacy.h"
#include "NeuralOperator.h"
#include "NeuralNetworkFromONNXTranslator.h"
#include "NeuralNetworkInferenceUtils.h"
#include "NeuralNetworkInferenceVersion.h"
#include "EditorFramework/AssetImportData.h"
#include "HAL/FileManager.h"
#include "RenderGraphBuilder.h"
#include "RenderingThread.h"
#include "RHI.h"

#if WITH_EDITOR
#if PLATFORM_WINDOWS
#include "ModelProtoFileReader.h"
#endif //PLATFORM_WINDOWS
#endif //WITH_EDITOR



/* UNeuralNetworkLegacy structors
 *****************************************************************************/

UNeuralNetworkLegacy::UNeuralNetworkLegacy()
	: bIsLoaded(false)
	, DeviceType(ENeuralDeviceType::GPU)
	, bAreTensorsInGpu(false)
{
}

UNeuralNetworkLegacy::~UNeuralNetworkLegacy()
{
}



/* UNeuralNetworkLegacy public functions
 *****************************************************************************/

void UNeuralNetworkLegacy::PostInitProperties()
{
#if WITH_EDITORONLY_DATA
	if (!HasAnyFlags(RF_ClassDefaultObject) && !AssetImportData)
	{
		GetAndMaybeCreateAssetImportData();
	}
#endif
	Super::PostInitProperties();
}

void UNeuralNetworkLegacy::PostLoad()
{
	Super::PostLoad();
	// Turn ModelProto into Operators
	if (bIsLoaded)
	{
		// Sanity checks
		if (!IsLoaded() || !TensorManager.IsLoaded() || !ModelProto.IsLoaded())
		{
			UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetworkLegacy::PostLoad(): 1 of the 3 IsLoaded() failed: UNeuralNetworkLegacy.IsLoaded() = %d, TensorManager.IsLoaded() = %d, ModelProto.IsLoaded() = %d."),
				IsLoaded(), TensorManager.IsLoaded(), ModelProto.IsLoaded());
			bIsLoaded = false;
		}
		else if (!FNeuralNetworkInferenceVersion::CheckVersion(Version) || !FNeuralNetworkInferenceVersion::CheckVersion(TensorManager.GetVersion()) || !FNeuralNetworkInferenceVersion::CheckVersion(ModelProto.GetVersion()))
		{
			UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetworkLegacy::PostLoad(): CheckVersion() failed."));
			bIsLoaded = false;
		}
		// Turn ModelProto into Operators
		else if (!FNeuralNetworkFromONNXTranslator::Translate(Operators, TensorManager, ModelProto.GetGraph()))
		{
			UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetworkLegacy::PostLoad(): UNeuralNetworkLegacy could not be configured from its FModelProto."));
		}
	}
}

void UNeuralNetworkLegacy::Serialize(FArchive& Archive)
{
#if WITH_EDITORONLY_DATA
	// Setup source data.
	if (Archive.IsSaving() && Archive.IsPersistent())
	{
		ReimportAssetFromEditorData();
	}
#endif // WITH_EDITORONLY_DATA
	Super::Serialize(Archive);
}

#if WITH_EDITOR
void UNeuralNetworkLegacy::ReimportAssetFromEditorData()
{
	//Get the re-import filename
	const FString ImportedFilename = AssetImportData->GetFirstFilename();
	if (ImportedFilename.Len() > 0)
	{
		// Ensure that the file provided by the path exists
		if (IFileManager::Get().FileSize(*ImportedFilename) != INDEX_NONE)
		{
			UE_LOG(LogNeuralNetworkInference, Display, TEXT("Performing atomic reimport of [%s]"), *ImportedFilename);
			Load(ImportedFilename);
		}
	}
}

#if WITH_EDITOR
UAssetImportData* UNeuralNetworkLegacy::GetAssetImportData() const
{
	return AssetImportData;
}
#endif // WITH_EDITOR

UAssetImportData* UNeuralNetworkLegacy::GetAndMaybeCreateAssetImportData()
{
	// An existing import data object was not found, so make one here.
	if (!AssetImportData)
	{
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}
	return AssetImportData;
}
#endif // WITH_EDITOR

#if WITH_EDITOR
bool UNeuralNetworkLegacy::Load(const FString& InFilePath)
{
#if PLATFORM_WINDOWS

	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("UNeuralNetworkLegacy_Load_File"), STAT_UNeuralNetworkLegacy_Load_File, STATGROUP_MachineLearning);

	// Clean previous networks
	if (bIsLoaded)
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetworkLegacy::Load(): A model was previously loaded, removing it and reloading the new model."));
		Operators.Empty();
		ModelProto = FModelProto();
		bIsLoaded = false;
		bAreTensorsInGpu = false;
	}

	// Read ModelProto
	if (!FModelProtoFileReader::ReadModelProtoFromFile(ModelProto, InFilePath) || !ModelProto.IsLoaded() || !FNeuralNetworkInferenceVersion::CheckVersion(ModelProto.GetVersion()))
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetworkLegacy::Load(): Model could not be loaded from %s or is outdated. IsLoaded() = %d."),
			*InFilePath, ModelProto.IsLoaded());
		return false;
	}
	// Turn ModelProto into Operators
	bIsLoaded = FNeuralNetworkFromONNXTranslator::Translate(Operators, TensorManager, ModelProto.GetGraph(), InFilePath);
	if (!TensorManager.IsLoaded() || !FNeuralNetworkInferenceVersion::CheckVersion(TensorManager.GetVersion()))
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetworkLegacy::Load(): TensorManager could not be loaded from %s or is outdated. IsLoaded() = %d."),
			*InFilePath, TensorManager.IsLoaded());
		return false;
	}
	// Update Version
	if (bIsLoaded)
	{
		Version = FNeuralNetworkInferenceVersion::GetVersion();
	}
	return bIsLoaded;

#else //PLATFORM_WINDOWS
	UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetworkLegacy::Load(): Only implemented for Windows."),
		*InFilePath, ModelProto.IsLoaded());
	return false;
#endif //PLATFORM_WINDOWS
}
#endif //WITH_EDITOR

bool UNeuralNetworkLegacy::Load(FNeuralTensorManager& InTensorManager, const TArray<TSharedPtr<FNeuralOperator>>& InOperators)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("UNeuralNetworkLegacy_Load"), STAT_UNeuralNetworkLegacy_Load, STATGROUP_MachineLearning);
	if (!InTensorManager.IsLoaded() || !FNeuralNetworkInferenceVersion::CheckVersion(InTensorManager.GetVersion()))
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetworkLegacy::Load(): TensorManager could not be loaded or is outdated. IsLoaded() = %d."),
			InTensorManager.IsLoaded());
	}
	Swap(TensorManager, InTensorManager);
	Operators = InOperators;
	bIsLoaded = (Operators.Num() > 0 && TensorManager.IsLoaded());
	// Update Version
	if (bIsLoaded)
	{
		Version = FNeuralNetworkInferenceVersion::GetVersion();
	}
	return bIsLoaded;
}

bool UNeuralNetworkLegacy::IsLoaded() const
{
	return bIsLoaded;
}

ENeuralDeviceType UNeuralNetworkLegacy::GetDeviceType() const
{
	return DeviceType;
}

void UNeuralNetworkLegacy::SetDeviceType(const ENeuralDeviceType InDeviceType)
{
	DeviceType = InDeviceType;
}

UNeuralNetworkLegacy::FOnAsyncRunCompletedInAnyThread& UNeuralNetworkLegacy::GetOnAsyncRunCompletedInAnyThreadDelegate()
{
	return OnAsyncRunCompletedInAnyThreadDelegate;
}

const TArray<FNeuralTensor>& UNeuralNetworkLegacy::GetTensors() const
{
	return TensorManager.GetTensors();
}

void UNeuralNetworkLegacy::SetInputFromTensorCopy(const FNeuralTensor& InTensorMap)
{
	return TensorManager.SetInputFromTensorCopy(InTensorMap);
}

void UNeuralNetworkLegacy::SetInputFromTensorMapCopy(const TMap<FString, FNeuralTensor>& InTensorMap)
{
	return TensorManager.SetInputFromTensorMapCopy(InTensorMap);
}

TMap<FString, void*> UNeuralNetworkLegacy::CreateInputDataPointersMutable()
{
	return TensorManager.CreateInputDataPointersMutable();
}

FRDGBufferUAVRef UNeuralNetworkLegacy::GetInputBufferUAVRef()
{
	return TensorManager.GetInputBufferUAVRef();
}

TMap<FString, FRDGBufferUAVRef> UNeuralNetworkLegacy::CreateInputBufferUAVRefs()
{
	return TensorManager.CreateInputBufferUAVRefs();
}

const FNeuralTensor& UNeuralNetworkLegacy::GetInputTensor() const
{
	return TensorManager.GetInputTensor();
}

const TMap<FString, int32>& UNeuralNetworkLegacy::GetInputNameIndexMap() const
{
	return TensorManager.GetInputNameIndexMap();
}

const FNeuralTensor& UNeuralNetworkLegacy::GetOutputTensor() const
{
	return TensorManager.GetOutputTensor();
}

const TMap<FString, int32>& UNeuralNetworkLegacy::GetOutputNameIndexMap() const
{
	return TensorManager.GetOutputNameIndexMap();
}

const FRDGBufferSRVRef UNeuralNetworkLegacy::GetOutputBufferSRVRef() const
{
	return TensorManager.GetOutputBufferSRVRef();
}

TMap<FString, const FRDGBufferSRVRef> UNeuralNetworkLegacy::CreateOutputBufferSRVRefs() const
{
	return TensorManager.CreateOutputBufferSRVRefs();
}

TMap<FString, FNeuralTensor> UNeuralNetworkLegacy::CreateInputTensorMap() const
{
	return TensorManager.CreateInputTensorMap();
}

TMap<FString, FNeuralTensor> UNeuralNetworkLegacy::CreateOutputTensorMap() const
{
	return TensorManager.CreateOutputTensorMap();
}

void UNeuralNetworkLegacy::Run(const ENeuralNetworkSynchronousMode InSynchronousMode, const ENeuralDeviceType InInputDeviceType, const ENeuralDeviceType InOutputDeviceType, const bool bRunGPUEmptyOnlyForProfiling)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("UNeuralNetworkLegacy_Run"), STAT_UNeuralNetworkLegacy_Run, STATGROUP_MachineLearning);
	// Sanity checks
	if (!bIsLoaded)
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetworkLegacy::Run(): No architecture has been loaded yet. Run() will not work until IsLoaded() returns true."));
		return;
	}
	// Run UNeuralNetworkLegacy
	if (Operators.Num() > 0)
	{
		// Run graph
		if (DeviceType == ENeuralDeviceType::CPU)
		{
			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("UNeuralNetworkLegacy_Run::Forward_CPU"), STAT_UNeuralNetworkLegacy_Run_Forward_CPU, STATGROUP_MachineLearning);
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
		else if (DeviceType == ENeuralDeviceType::GPU)
		{
			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("UNeuralNetworkLegacy_Run::Forward_GPU"), STAT_UNeuralNetworkLegacy_Run_Forward_GPU, STATGROUP_MachineLearning);
			// Sanity check
			if (TensorManager.GetTensorsMutable().Num() < 1)
			{
				UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetworkLegacy::Run(): Tensors.Num() = %d (should be > 0)."), TensorManager.GetTensorsMutable().Num());
				return;
			}

			// On RHI thread
			ENQUEUE_RENDER_COMMAND(UNeuralNetworkLegacy_Run_RenderThread)(
				[this, bRunGPUEmptyOnlyForProfiling, InSynchronousMode, InInputDeviceType, InOutputDeviceType](FRHICommandListImmediate& RHICmdList)
				{
					FMemMark Mark(FMemStack::Get());
					FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("UNeuralNetworkLegacy::Run()"));

					// Move memory from CPU to GPU
					TArray<FNeuralTensor>& Tensors = TensorManager.GetTensorsMutable();
					const bool bIsInputInCPU = (InInputDeviceType == ENeuralDeviceType::CPU);
					// Move only input tensors to GPU (once per UNeuralNetworkLegacy::Run())
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

					if (!bRunGPUEmptyOnlyForProfiling)
					{
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
							[this](FRHICommandListImmediate& RHICmdList)
						{
							OnAsyncRunCompletedInAnyThreadDelegate.ExecuteIfBound();
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
			UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetworkLegacy::Run(): Unknown DeviceType = %d."), (int32)DeviceType);
		}
	}
	else
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetworkLegacy::Run() called with an empty model."));
	}
}

FString UNeuralNetworkLegacy::ToString() const
{
	// Add GraphProto
	FString String = ModelProto.GetGraph().ToString();
	// Add FNeuralTensor(s)
	String += TEXT("TensorManager:\n");
	const TMap<FString, int32>& NameIndexMap = TensorManager.GetNameIndexMap();
	const TArray<FNeuralTensor>& Tensors = TensorManager.GetTensors();
	if (NameIndexMap.Num() > 0)
	{
		for (const auto& NameIndexPair : NameIndexMap)
		{
			String += FString::Format(TEXT(" -{0}: {1}\n"), { NameIndexPair.Key, Tensors[NameIndexPair.Value].ToString(20) });
		}
	}
	else
	{
		for (const FNeuralTensor& Tensor : Tensors)
		{
			String += FString::Format(TEXT(" -{0}\n"), { Tensor.ToString(20) });
		}
	}
	String += TEXT("InputTensorMap:\n");
	for (const auto& NameIndexPair : TensorManager.GetInputNameIndexMap())
	{
		String += FString::Format(TEXT(" -{0}: {1}\n"), { NameIndexPair.Key, Tensors[NameIndexPair.Value].ToString(20) });
	}
	String += TEXT("OutputTensorMap:\n");
	for (const auto& NameIndexPair : TensorManager.GetOutputNameIndexMap())
	{
		String += FString::Format(TEXT(" -{0}: {1}\n"), { NameIndexPair.Key, Tensors[NameIndexPair.Value].ToString(20) });
	}
	// Add FNeuralOperator(s)
	String += TEXT("Operators:\n");
	for (const TSharedPtr<FNeuralOperator>& Operator : Operators)
	{
		String += FString::Format(TEXT(" -{0}\n"), { Operator->ToString() });
	}
	// Return result
	return String;
}
