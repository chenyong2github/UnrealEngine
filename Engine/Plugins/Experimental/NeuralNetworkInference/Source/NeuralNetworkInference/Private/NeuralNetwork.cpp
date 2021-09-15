// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralNetwork.h"
#include "NeuralNetworkImplBackEndUEAndORT.h"
#include "NeuralNetworkImplBackEndUEOnly.h"
#include "NeuralNetworkInferenceUtils.h"
#include "EditorFramework/AssetImportData.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderGraphBuilder.h"
#include "RenderingThread.h"
#include "RHI.h"



/* FPrivateNeuralNetwork functions
 *****************************************************************************/

struct FPrivateNeuralNetwork
{
public:
	static ENeuralBackEnd SetBackEndForCurrentPlatform(const ENeuralBackEnd InBackEnd);
};

ENeuralBackEnd FPrivateNeuralNetwork::SetBackEndForCurrentPlatform(const ENeuralBackEnd InBackEnd)
{
	// Auto
	if (InBackEnd == ENeuralBackEnd::Auto)
	{
#ifdef WITH_UE_AND_ORT_SUPPORT
		return ENeuralBackEnd::UEAndORT;
#else //WITH_UE_AND_ORT_SUPPORT
		return ENeuralBackEnd::UEOnly;
#endif //WITH_UE_AND_ORT_SUPPORT
	}
	// Otherwise
	return InBackEnd;
}



/* UNeuralNetwork structors
 *****************************************************************************/

UNeuralNetwork::UNeuralNetwork()
	: DeviceType(ENeuralDeviceType::GPU)
	, InputDeviceType(ENeuralDeviceType::CPU)
	, OutputDeviceType(ENeuralDeviceType::CPU)
	, SynchronousMode(ENeuralNetworkSynchronousMode::Synchronous)
	, BackEnd(ENeuralBackEnd::Auto)
	, bIsLoaded(false)
	, BackEndForCurrentPlatform(FPrivateNeuralNetwork::SetBackEndForCurrentPlatform(BackEnd))
{
}

UNeuralNetwork::~UNeuralNetwork()
{
}



/* UNeuralNetwork public functions
 *****************************************************************************/

bool UNeuralNetwork::Load(const FString& InModelFilePath)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("UNeuralNetwork_Load_FromFString"), STAT_UNeuralNetwork_Load, STATGROUP_MachineLearning);

	// Clean previous networks
	bIsLoaded = false;

	// Fill ModelFullFilePath & ModelReadFromFileInBytes
	{
		// Sanity check
		if (InModelFilePath.IsEmpty())
		{
			UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Load(): Input model path was empty."));
			return false;
		}
		// Fill ModelFullFilePath
		ModelFullFilePath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*InModelFilePath);
		// Sanity check
		if (!FPaths::FileExists(ModelFullFilePath))
		{
			UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Load(): Model not found \"%s\"."), *ModelFullFilePath);
			return false;
		}
		// Read file into ModelReadFromFileInBytes
		// Source: https://github.com/microsoft/onnxruntime/blob/894fc828587c919d815918c4da6cde314e5d54ed/onnxruntime/test/shared_lib/test_model_loading.cc#L21-L31
		if (!FFileHelper::LoadFileToArray(ModelReadFromFileInBytes, *ModelFullFilePath))
		{
			UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Load(): Error reading model \"%s\"."), *ModelFullFilePath);
			return false;
		}
	}

	return Load();
}


bool UNeuralNetwork::Load(TArray<uint8>& InModelReadFromFileInBytes)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("UNeuralNetwork_Load_FromTArrayUInt8"), STAT_UNeuralNetwork_Load, STATGROUP_MachineLearning);

	// Clean previous networks
	bIsLoaded = false;

	if (InModelReadFromFileInBytes.IsEmpty())
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Load(): InModelReadFromFileInBytes is empty."));
		return false;
	}

	// Read file into ModelReadFromFileInBytes
	Swap(ModelReadFromFileInBytes, InModelReadFromFileInBytes);

	return Load();
}

bool UNeuralNetwork::IsLoaded() const
{
	return bIsLoaded;
}

ENeuralDeviceType UNeuralNetwork::GetDeviceType() const
{
	return DeviceType;
}

void UNeuralNetwork::SetDeviceType(const ENeuralDeviceType InDeviceType)
{
	SetXDeviceType(DeviceType, InDeviceType);
}

ENeuralDeviceType UNeuralNetwork::GetInputDeviceType() const
{
	return InputDeviceType;
}

void UNeuralNetwork::SetInputDeviceType(const ENeuralDeviceType InInputDeviceType)
{
	SetXDeviceType(InputDeviceType, InInputDeviceType);
}

ENeuralDeviceType UNeuralNetwork::GetOutputDeviceType() const
{
	return OutputDeviceType;
}

void UNeuralNetwork::SetOutputDeviceType(const ENeuralDeviceType InOutputDeviceType)
{
	SetXDeviceType(OutputDeviceType, InOutputDeviceType);
}

ENeuralNetworkSynchronousMode UNeuralNetwork::GetSynchronousMode() const
{
	return SynchronousMode;
}

void UNeuralNetwork::SetSynchronousMode(const ENeuralNetworkSynchronousMode InSynchronousMode)
{
	SynchronousMode = InSynchronousMode;
}

UNeuralNetwork::FOnAsyncRunCompleted& UNeuralNetwork::GetOnAsyncRunCompletedDelegate()
{
	return OnAsyncRunCompletedDelegate;
}

ENeuralBackEnd UNeuralNetwork::GetBackEnd() const
{
	return BackEnd;
}

ENeuralBackEnd UNeuralNetwork::GetBackEndForCurrentPlatform() const
{
	return BackEndForCurrentPlatform;
}

void UNeuralNetwork::SetBackEnd(const ENeuralBackEnd InBackEnd)
{
	BackEnd = InBackEnd;
	const ENeuralBackEnd NewBackEndForCurrentPlatform = FPrivateNeuralNetwork::SetBackEndForCurrentPlatform(BackEnd);
	// Reload only required if BackEndForCurrentPlatform changes (regardless of whether BackEnd changed).
	// BackEndForCurrentPlatform does not necesarily change if BackEnd changes. E.g., changing from UEAndORT into Auto in Windows will result in BackEndForCurrentPlatform = UEAndORT in both cases.
	if (BackEndForCurrentPlatform != NewBackEndForCurrentPlatform)
	{
		BackEndForCurrentPlatform = NewBackEndForCurrentPlatform;
		Load();
	}
}

const FNeuralTensor& UNeuralNetwork::GetInputTensor(const int32 InTensorIndex) const
{
	// Sanity check
	checkf(bIsLoaded, TEXT("Call UNeuralNetwork::Load() to load a model first."));

	// UEAndORT
	if (BackEndForCurrentPlatform == ENeuralBackEnd::UEAndORT)
	{
		return ImplBackEndUEAndORT->InputTensors[InTensorIndex];
	}

	// UEOnly
	else if (BackEndForCurrentPlatform == ENeuralBackEnd::UEOnly)
	{
		FNeuralTensorManager& TensorManager = ImplBackEndUEOnly->TensorManager;
		return TensorManager.GetTensors()[TensorManager.GetInputIndexes()[InTensorIndex]];
	}

	// Unknown
	checkf(false, TEXT("UNeuralNetwork::GetInputTensor(): Unknown [BackEnd,BackEndForCurrentPlatform] = [%d,%d]."), (int32)BackEnd, (int32)BackEndForCurrentPlatform);
	return ImplBackEndUEAndORT->InputTensors[InTensorIndex];
}

void UNeuralNetwork::SetInputFromArrayCopy(const TArray<float>& InArray, const int32 InTensorIndex)
{
	// Sanity check
	if (!bIsLoaded)
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::SetInputFromArrayCopy(): Call UNeuralNetwork::Load() to load a model first."));
	}

	// UEAndORT
	if (BackEndForCurrentPlatform == ENeuralBackEnd::UEAndORT)
	{
		ImplBackEndUEAndORT->InputTensors[InTensorIndex].SetFromArrayCopy(InArray);
	}

	// UEOnly
	else if (BackEndForCurrentPlatform == ENeuralBackEnd::UEOnly)
	{
		FNeuralTensorManager& TensorManager = ImplBackEndUEOnly->TensorManager;
		TensorManager.GetTensorsMutable()[TensorManager.GetInputIndexes()[InTensorIndex]].SetFromArrayCopy(InArray);
	}

	// Unknown
	else
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::SetInputFromArrayCopy(): Unknown [BackEnd,BackEndForCurrentPlatform] = [%d,%d]."), (int32)BackEnd, (int32)BackEndForCurrentPlatform);
	}
}

void* UNeuralNetwork::GetInputDataPointerMutable(const int32 InTensorIndex)
{
	// Sanity check
	if (!bIsLoaded)
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::GetInputDataPointerMutable(): Call UNeuralNetwork::Load() to load a model first."));
		return nullptr;
	}

	// UEAndORT
	if (BackEndForCurrentPlatform == ENeuralBackEnd::UEAndORT)
	{
		return ImplBackEndUEAndORT->InputTensors[InTensorIndex].GetData(); // Or ImplBackEndUEAndORT->InputOrtTensors[InTensorIndex].GetTensorMutableData<float>();
	}

	// UEOnly
	else if (BackEndForCurrentPlatform == ENeuralBackEnd::UEOnly)
	{
		FNeuralTensorManager& TensorManager = ImplBackEndUEOnly->TensorManager;
		return TensorManager.GetTensorsMutable()[TensorManager.GetInputIndexes()[InTensorIndex]].GetData();
	}

	// Unknown
	UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::SetInputFromArrayCopy(): Unknown [BackEnd,BackEndForCurrentPlatform] = [%d,%d]."), (int32)BackEnd, (int32)BackEndForCurrentPlatform);
	return nullptr;
}

int64 UNeuralNetwork::GetInputTensorNumber() const
{
	// Sanity check
	if (!bIsLoaded)
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::GetInputTensorNumber(): Call UNeuralNetwork::Load() to load a model first."));
		return -1;
	}

	// UEAndORT
	if (BackEndForCurrentPlatform == ENeuralBackEnd::UEAndORT)
	{
		return ImplBackEndUEAndORT->InputTensors.Num();
	}

	// UEOnly
	else if (BackEndForCurrentPlatform == ENeuralBackEnd::UEOnly)
	{
		return ImplBackEndUEOnly->TensorManager.GetInputIndexes().Num();
	}

	// Unknown
	checkf(false, TEXT("UNeuralNetwork::GetInputTensorNumber(): Unknown [BackEnd,BackEndForCurrentPlatform] = [%d,%d]."), (int32)BackEnd, (int32)BackEndForCurrentPlatform);
	return -1;
}

#define GET_OUTPUT_TENSOR_CODE(InTensorIndex, UEOnly_GetTensorsFunction) \
	/* Sanity check */ \
	checkf(bIsLoaded, TEXT("Call UNeuralNetwork::Load() to load a model first.")); \
	/* UEAndORT */ \
	if (BackEndForCurrentPlatform == ENeuralBackEnd::UEAndORT) \
	{ \
		return ImplBackEndUEAndORT->OutputTensors[InTensorIndex]; \
	} \
	/* UEOnly */ \
	else if (BackEndForCurrentPlatform == ENeuralBackEnd::UEOnly) \
	{ \
		FNeuralTensorManager& TensorManager = ImplBackEndUEOnly->TensorManager; \
		return TensorManager.UEOnly_GetTensorsFunction()[TensorManager.GetOutputIndexes()[InTensorIndex]]; \
	} \
	/* Unknown */ \
	checkf(false, TEXT("UNeuralNetwork::GetOutputTensor(): Unknown [BackEnd,BackEndForCurrentPlatform] = [%d,%d]."), (int32)BackEnd, (int32)BackEndForCurrentPlatform); \
	return ImplBackEndUEAndORT->OutputTensors[InTensorIndex]

const FNeuralTensor& UNeuralNetwork::GetOutputTensor(const int32 InTensorIndex) const
{
	GET_OUTPUT_TENSOR_CODE(InTensorIndex, GetTensors);
}

FNeuralTensor& UNeuralNetwork::GetOutputTensorMutable(const int32 InTensorIndex)
{
	GET_OUTPUT_TENSOR_CODE(InTensorIndex, GetTensorsMutable);
}

int64 UNeuralNetwork::GetOutputTensorNumber() const
{
	// Sanity check
	if (!bIsLoaded)
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::GetOutputTensorNumber(): Call UNeuralNetwork::Load() to load a model first."));
		return -1;
	}

	// UEAndORT
	if (BackEndForCurrentPlatform == ENeuralBackEnd::UEAndORT)
	{
		return ImplBackEndUEAndORT->OutputTensors.Num();
	}

	// UEOnly
	else if (BackEndForCurrentPlatform == ENeuralBackEnd::UEOnly)
	{
		return ImplBackEndUEOnly->TensorManager.GetOutputIndexes().Num();
	}

	// Unknown
	UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::GetOutputTensorNumber(): Unknown [BackEnd,BackEndForCurrentPlatform] = [%d,%d]."), (int32)BackEnd, (int32)BackEndForCurrentPlatform);
	return -1;
}

void UNeuralNetwork::OutputTensorsToCPU(const TArray<int32>& InOutputTensorIndexes)
{
	// Sanity check
	if (!bIsLoaded)
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::OutputTensorToCPU(): Call UNeuralNetwork::Load() to load a model first."));
		return;
	}

	// In RHI
	ENQUEUE_RENDER_COMMAND(UNeuralNetwork_OutputTensorToCPU_RenderThread)(
		[this, &InOutputTensorIndexes](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("UNeuralNetwork::OutputTensorToCPU()"));
			// Run for each output tensor
			if (InOutputTensorIndexes.IsEmpty())
			{
				// Refresh tensor(s) w.r.t. GraphBuilder + Move memory from GPU to CPU
				for (uint32 OutputTensorIndex = 0; OutputTensorIndex < GetOutputTensorNumber(); ++OutputTensorIndex)
				{
					FNeuralTensor& OutputTensor = GetOutputTensorMutable(OutputTensorIndex);
					OutputTensor.UpdateSRVAndOrUAV_RenderThread(&GraphBuilder);
					OutputTensor.ToCPU_RenderThread(&GraphBuilder);
				}
			}
			// Run for the desired output tensors
			else
			{
				for (const int32 OutputTensorIndex : InOutputTensorIndexes)
				{
					FNeuralTensor& OutputTensor = GetOutputTensorMutable(OutputTensorIndex);
					OutputTensor.UpdateSRVAndOrUAV_RenderThread(&GraphBuilder);
					OutputTensor.ToCPU_RenderThread(&GraphBuilder);
				}
			}
			// Execute Render Graph
			GraphBuilder.Execute();
		}
	);

	FNeuralNetworkInferenceUtils::WaitUntilRHIFinished();
}

void UNeuralNetwork::Run()
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("UNeuralNetwork_Run"), STAT_UNeuralNetwork_Run, STATGROUP_MachineLearning);

	// Sanity check
	if (!bIsLoaded)
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Run(): Call UNeuralNetwork::Load() to load a model first."));
		return;
	}

	// UEAndORT
	if (BackEndForCurrentPlatform == ENeuralBackEnd::UEAndORT)
	{
		ImplBackEndUEAndORT->Run(SynchronousMode, InputDeviceType, OutputDeviceType);
	}

	// UEOnly
	else if (BackEndForCurrentPlatform == ENeuralBackEnd::UEOnly)
	{
		ImplBackEndUEOnly->Run(OnAsyncRunCompletedDelegate, SynchronousMode, DeviceType, InputDeviceType, OutputDeviceType);
	}

	// Unknown
	else
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Run(): Unknown [BackEnd,BackEndForCurrentPlatform] = [%d,%d]."), (int32)BackEnd, (int32)BackEndForCurrentPlatform);
	}
}



/* UNeuralNetwork private functions
 *****************************************************************************/

void UNeuralNetwork::SetXDeviceType(ENeuralDeviceType& OutDeviceType, const ENeuralDeviceType InDeviceType)
{
	if (OutDeviceType != InDeviceType)
	{
		OutDeviceType = InDeviceType;
		if (BackEndForCurrentPlatform == ENeuralBackEnd::UEAndORT)
		{
			Load();
		}
	}
}

bool UNeuralNetwork::Load()
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("UNeuralNetwork_Load"), STAT_UNeuralNetwork_Load, STATGROUP_MachineLearning);

	// Clean previous networks
	bIsLoaded = false;

	// UEAndORT
	if (BackEndForCurrentPlatform == ENeuralBackEnd::UEAndORT)
	{
		UNeuralNetwork::FImplBackEndUEAndORT::SetDeviceToCPUIfD3D12NotSupported(DeviceType);
		bIsLoaded = UNeuralNetwork::FImplBackEndUEAndORT::Load(ImplBackEndUEAndORT, AreInputTensorSizesVariable, ModelReadFromFileInBytes, ModelFullFilePath, GetDeviceType(), GetInputDeviceType(), GetOutputDeviceType());
	}
	// UEOnly
	else if (BackEndForCurrentPlatform == ENeuralBackEnd::UEOnly)
	{
		bIsLoaded = UNeuralNetwork::FImplBackEndUEOnly::Load(ImplBackEndUEOnly, ModelReadFromFileInBytes);
	}
	// Unknown
	else
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Load(): Unknown [BackEnd,BackEndForCurrentPlatform] = [%d,%d]."), (int32)BackEnd, (int32)BackEndForCurrentPlatform);
	}

	return bIsLoaded;
}

#if WITH_EDITOR
UAssetImportData* UNeuralNetwork::GetAssetImportData() const
{
	return AssetImportData;
}

UAssetImportData* UNeuralNetwork::GetAndMaybeCreateAssetImportData()
{
	// An existing import data object was not found, so make one here.
	if (!AssetImportData)
	{
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}
	return AssetImportData;
}

void UNeuralNetwork::ReimportAssetFromEditorData()
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
#endif // WITH_EDITOR

void UNeuralNetwork::PostInitProperties()
{
#if WITH_EDITORONLY_DATA
	if (!HasAnyFlags(RF_ClassDefaultObject) && !AssetImportData)
	{
		GetAndMaybeCreateAssetImportData();
	}
#endif
	Super::PostInitProperties();
}

void UNeuralNetwork::PostLoad()
{
	Super::PostLoad();
	// If ModelReadFromFileInBytes is not empty, call Load() 
	if (ModelReadFromFileInBytes.Num() > 0)
	{
		if (!Load())
		{
			UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::PostLoad(): UNeuralNetwork could not be loaded."));
		}
	}
}

void UNeuralNetwork::Serialize(FArchive& Archive)
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
