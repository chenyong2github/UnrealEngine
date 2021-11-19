// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralNetwork.h"
#include "EditorFramework/AssetImportData.h"
#include "NeuralTimer.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NeuralNetworkImplBackEndUEAndORT.h"
#include "NeuralNetworkImplBackEndUEOnly.h"
#include "NeuralNetworkInferenceUtils.h"
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
	, bIsBackgroundThreadRunning(false)
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
	
	const FScopeLock ResourcesLock(&ResoucesCriticalSection);

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

	const FScopeLock ResourcesLock(&ResoucesCriticalSection);

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

ENeuralDeviceType UNeuralNetwork::GetInputDeviceType() const
{
	return InputDeviceType;
}

ENeuralDeviceType UNeuralNetwork::GetOutputDeviceType() const
{
	return OutputDeviceType;
}

void UNeuralNetwork::SetDeviceType(const ENeuralDeviceType InDeviceType, const ENeuralDeviceType InInputDeviceType, const ENeuralDeviceType InOutputDeviceType)
{
	const FScopeLock ResourcesLock(&ResoucesCriticalSection);
	if (DeviceType != InDeviceType || InputDeviceType != InInputDeviceType || OutputDeviceType != InOutputDeviceType)
	{
		DeviceType = InDeviceType;
		InputDeviceType = InInputDeviceType;
		OutputDeviceType = InOutputDeviceType;
		if (bIsLoaded && BackEndForCurrentPlatform == ENeuralBackEnd::UEAndORT) // No need to re-load if not bIsLoaded
		{
			Load();
		}
	}
}

ENeuralNetworkSynchronousMode UNeuralNetwork::GetSynchronousMode() const
{
	return SynchronousMode;
}

void UNeuralNetwork::SetSynchronousMode(const ENeuralNetworkSynchronousMode InSynchronousMode)
{
	const FScopeLock ResourcesLock(&ResoucesCriticalSection);
	SynchronousMode = InSynchronousMode;
}

UNeuralNetwork::FOnAsyncRunCompleted& UNeuralNetwork::GetOnAsyncRunCompletedDelegate()
{
	const FScopeLock ResourcesLock(&ResoucesCriticalSection);
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

bool UNeuralNetwork::SetBackEnd(const ENeuralBackEnd InBackEnd)
{
	BackEnd = InBackEnd;
	const ENeuralBackEnd NewBackEndForCurrentPlatform = FPrivateNeuralNetwork::SetBackEndForCurrentPlatform(BackEnd);
	// Reload only required if BackEndForCurrentPlatform changes (regardless of whether BackEnd changed).
	// BackEndForCurrentPlatform does not necessarily change if BackEnd changes. E.g., changing from UEAndORT into Auto in Windows will result in BackEndForCurrentPlatform = UEAndORT in both cases.
	if (BackEndForCurrentPlatform != NewBackEndForCurrentPlatform)
	{
		BackEndForCurrentPlatform = NewBackEndForCurrentPlatform;
		if (bIsLoaded) // No need to re-load if not bIsLoaded
		{
			Load();
		}
	}
	return IsLoaded();
}

void UNeuralNetwork::ResetStats()
{
	ComputeStatsModule.ResetStats();
	InputMemoryTransferStatsModule.ResetStats();
}

bool UNeuralNetwork::IsGPUSupported() const
{
	// UEAndORT
	if (BackEndForCurrentPlatform == ENeuralBackEnd::UEAndORT)
	{
		return FImplBackEndUEAndORT::IsGPUSupported();
	}

	// UEOnly
	else if (BackEndForCurrentPlatform == ENeuralBackEnd::UEOnly)
	{
		return true;
	}

	// Unknown
	UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::IsGPUSupported(): Unknown [BackEnd,BackEndForCurrentPlatform] = [%d,%d]."), (int32)BackEnd, (int32)BackEndForCurrentPlatform);
	return false;
}

#define GET_TENSOR_CODE(InTensorIndex, UEAndORTOnly_GetIndexes, UEOnly_GetTensorsFunction, UEOnly_Tensors) \
	/* Sanity check */ \
	checkf(bIsLoaded, TEXT("Call UNeuralNetwork::Load() to load a model first.")); \
	/* UEAndORT */ \
	if (BackEndForCurrentPlatform == ENeuralBackEnd::UEAndORT) \
	{ \
		return ImplBackEndUEAndORT->UEOnly_Tensors[InTensorIndex]; \
	} \
	/* UEOnly */ \
	else if (BackEndForCurrentPlatform == ENeuralBackEnd::UEOnly) \
	{ \
		FNeuralTensorManager& TensorManager = ImplBackEndUEOnly->TensorManager; \
		return TensorManager.UEOnly_GetTensorsFunction()[TensorManager.UEAndORTOnly_GetIndexes()[InTensorIndex]]; \
	} \
	/* Unknown */ \
	checkf(false, TEXT("Unknown [BackEnd,BackEndForCurrentPlatform] = [%d,%d]."), (int32)BackEnd, (int32)BackEndForCurrentPlatform); \
	return ImplBackEndUEAndORT->UEOnly_Tensors[InTensorIndex]

const FNeuralTensor& UNeuralNetwork::GetInputTensor(const int32 InTensorIndex) const
{
	GET_TENSOR_CODE(InTensorIndex, GetInputIndexes, GetTensors, InputTensors);
}

void UNeuralNetwork::SetInputFromArrayCopy(const TArray<float>& InArray, const int32 InTensorIndex)
{
	const FScopeLock ResourcesLock(&ResoucesCriticalSection);
	// Sanity check
	if (!bIsLoaded)
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::SetInputFromArrayCopy(): Call UNeuralNetwork::Load() to load a model first."));
	}
	
	FNeuralTimer RunTimer;
	RunTimer.Tic();
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
	InputMemoryTransferStatsModule.StoreSample(RunTimer.Toc());
}

void* UNeuralNetwork::GetInputDataPointerMutable(const int32 InTensorIndex)
{
	const FScopeLock ResourcesLock(&ResoucesCriticalSection);

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

const FNeuralTensor& UNeuralNetwork::GetOutputTensor(const int32 InTensorIndex) const
{
	GET_TENSOR_CODE(InTensorIndex, GetOutputIndexes, GetTensors, OutputTensors);
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

TArray<FNeuralTensor> UNeuralNetwork::CreateInputArrayCopy() const
{
	TArray<FNeuralTensor> InputTensorArray;
	for (uint32 InputTensorIndex = 0; InputTensorIndex < GetInputTensorNumber(); ++InputTensorIndex)
	{
		const FNeuralTensor& InputTensor = GetInputTensor(InputTensorIndex);
		InputTensorArray.Push(InputTensor);
	}
	return InputTensorArray;
}

void UNeuralNetwork::SetInputFromArrayCopy(const TArray<FNeuralTensor>& InInputTensorArray)
{
	const FScopeLock ResourcesLock(&ResoucesCriticalSection);
	if (GetInputTensorNumber() != InInputTensorArray.Num())
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::SetInputFromArrayCopy(): GetInputTensorNumber() == InInputTensorArray.Num() failed, %d != %d."), GetInputTensorNumber(), InInputTensorArray.Num());
		return;
	}
	FNeuralTimer RunTimer;
	RunTimer.Tic();
	for (uint32 InputTensorIndex = 0; InputTensorIndex < GetInputTensorNumber(); ++InputTensorIndex)
	{
		FNeuralTensor& InputTensor = GetInputTensorMutable(InputTensorIndex);
		InputTensor.SetFromUnderlyingUInt8ArrayCopy(InInputTensorArray[InputTensorIndex].GetUnderlyingUInt8ArrayRef());
	}
	InputMemoryTransferStatsModule.StoreSample(RunTimer.Toc());
}

TArray<FNeuralTensor> UNeuralNetwork::CreateOutputArrayCopy() const
{
	TArray<FNeuralTensor> OutputTensorArray;
	for (uint32 OutputTensorIndex = 0; OutputTensorIndex < GetOutputTensorNumber(); ++OutputTensorIndex)
	{
		const FNeuralTensor& OutputTensor = GetOutputTensor(OutputTensorIndex);
		OutputTensorArray.Push(OutputTensor);
	}
	return OutputTensorArray;
}

void UNeuralNetwork::InputTensorsToGPU(const TArray<int32>& InTensorIndexes)
{
	const FScopeLock ResourcesLock(&ResoucesCriticalSection);
	// Sanity check
	if (!bIsLoaded)
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::InputTensorsToGPU(): Call UNeuralNetwork::Load() to load a model first."));
		return;
	}

	// In RHI
	ENQUEUE_RENDER_COMMAND(UNeuralNetwork_InputTensorToGPU_RenderThread)(
		[this, &InTensorIndexes](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("UNeuralNetwork::InputTensorToGPU()"));
			// Run for each input tensor
			if (InTensorIndexes.IsEmpty())
			{
				// Refresh tensor(s) w.r.t. GraphBuilder + Move memory from CPU to GPU
				for (uint32 InputTensorIndex = 0; InputTensorIndex < GetInputTensorNumber(); ++InputTensorIndex)
				{
					FNeuralTensor& InputTensor = GetInputTensorMutable(InputTensorIndex);
					InputTensor.ToGPU_RenderThread(&GraphBuilder);
				}
			}
			// Run for the desired input tensors
			else
			{
				for (const int32 InputTensorIndex : InTensorIndexes)
				{
					FNeuralTensor& InputTensor = GetInputTensorMutable(InputTensorIndex);
					InputTensor.ToGPU_RenderThread(&GraphBuilder);
				}
			}
			// Execute Render Graph
			GraphBuilder.Execute();
		}
	);

	FNeuralNetworkInferenceUtils::WaitUntilRHIFinished();
}

void UNeuralNetwork::OutputTensorsToCPU(const TArray<int32>& InTensorIndexes)
{
	const FScopeLock ResourcesLock(&ResoucesCriticalSection);
	// Sanity check
	if (!bIsLoaded)
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::OutputTensorToCPU(): Call UNeuralNetwork::Load() to load a model first."));
		return;
	}

	// In RHI
	ENQUEUE_RENDER_COMMAND(UNeuralNetwork_OutputTensorToCPU_RenderThread)(
		[this, &InTensorIndexes](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("UNeuralNetwork::OutputTensorToCPU()"));
			// Run for each output tensor
			if (InTensorIndexes.IsEmpty())
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
				for (const int32 OutputTensorIndex : InTensorIndexes)
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
	FNeuralTimer RunTimer;
	RunTimer.Tic();
	// UEAndORT
	if (BackEndForCurrentPlatform == ENeuralBackEnd::UEAndORT)
	{
		ImplBackEndUEAndORT->Run(SynchronousMode, DeviceType, InputDeviceType, OutputDeviceType);
	}
	// UEOnly
	else if (BackEndForCurrentPlatform == ENeuralBackEnd::UEOnly)
	{
		ImplBackEndUEOnly->Run(OnAsyncRunCompletedDelegate, bIsBackgroundThreadRunning, SynchronousMode, DeviceType, InputDeviceType, OutputDeviceType);
	}
	// Unknown
	else
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Run(): Unknown [BackEnd,BackEndForCurrentPlatform] = [%d,%d]."), (int32)BackEnd, (int32)BackEndForCurrentPlatform);
	}
	ComputeStatsModule.StoreSample(RunTimer.Toc());
}

float UNeuralNetwork::GetLastInferenceTime() const
{
	return ComputeStatsModule.GetLastSample();
}

FNeuralStatsData UNeuralNetwork::GetInferenceStats() const
{
	return ComputeStatsModule.GetStats();
}

FNeuralStatsData UNeuralNetwork::GetInputMemoryTransferStats() const
{
	return InputMemoryTransferStatsModule.GetStats();
}


/* UNeuralNetwork private functions
 *****************************************************************************/

bool UNeuralNetwork::Load()
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("UNeuralNetwork_Load"), STAT_UNeuralNetwork_Load, STATGROUP_MachineLearning);

	const FScopeLock ResourcesLock(&ResoucesCriticalSection);

	// Clean previous networks
	bIsLoaded = false;

	// UEAndORT
	if (BackEndForCurrentPlatform == ENeuralBackEnd::UEAndORT)
	{
		UNeuralNetwork::FImplBackEndUEAndORT::WarnAndSetDeviceToCPUIfDX12NotEnabled(DeviceType, /*bShouldOpenMessageLog*/true);
		bIsLoaded = UNeuralNetwork::FImplBackEndUEAndORT::Load(ImplBackEndUEAndORT, OnAsyncRunCompletedDelegate, bIsBackgroundThreadRunning, ResoucesCriticalSection, AreInputTensorSizesVariable,
			ModelReadFromFileInBytes, ModelFullFilePath, GetDeviceType(), GetInputDeviceType(), GetOutputDeviceType());
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

	// Reset Stats
	ResetStats();

	return IsLoaded();
}

FNeuralTensor& UNeuralNetwork::GetInputTensorMutable(const int32 InTensorIndex)
{
	GET_TENSOR_CODE(InTensorIndex, GetInputIndexes, GetTensorsMutable, InputTensors);
}

FNeuralTensor& UNeuralNetwork::GetOutputTensorMutable(const int32 InTensorIndex)
{
	GET_TENSOR_CODE(InTensorIndex, GetOutputIndexes, GetTensorsMutable, OutputTensors);
}

bool UNeuralNetwork::Load(TArray<FNeuralTensor>& InTensors, const TArray<FNeuralTensor*>& InInputTensors, const TArray<FNeuralTensor*>& InOutputTensors, const TArray<TSharedPtr<class FNeuralOperator>>& InOperators)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("UNeuralNetwork_Load_FromTensorManagerAndOperators"), STAT_UNeuralNetwork_Load, STATGROUP_MachineLearning);

	const FScopeLock ResourcesLock(&ResoucesCriticalSection);

	BackEnd = ENeuralBackEnd::UEOnly;
	BackEndForCurrentPlatform = ENeuralBackEnd::UEOnly;
	// Create and load TensorManager
	FNeuralTensorManager TensorManager(InTensors, InInputTensors, InOutputTensors);
	if (!TensorManager.IsLoaded())
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Load(): TensorManager could not be loaded."));
		return false;
	}
	// Load network
	bIsLoaded = UNeuralNetwork::FImplBackEndUEOnly::Load(ImplBackEndUEOnly, TensorManager, InOperators);
	return IsLoaded();
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
		// If GPU selected but not compatible, set to CPU
		if (BackEnd == ENeuralBackEnd::UEAndORT)
		{
			FImplBackEndUEAndORT::WarnAndSetDeviceToCPUIfDX12NotEnabled(DeviceType, /*bShouldOpenMessageLog*/false);
		}
		// Load
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

bool UNeuralNetwork::IsReadyForFinishDestroy()
{
	return !bIsBackgroundThreadRunning;
}
