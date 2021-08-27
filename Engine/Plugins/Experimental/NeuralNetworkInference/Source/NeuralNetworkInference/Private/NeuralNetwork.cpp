// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralNetwork.h"
#include "NeuralNetworkImplBackEndUEAndORT.h"
#include "NeuralNetworkImplBackEndUEOnly.h"
#include "NeuralNetworkInferenceUtils.h"
#include "EditorFramework/AssetImportData.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"



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
#ifdef WITH_FULL_NNI_SUPPORT
		return ENeuralBackEnd::UEAndORT;
#else //WITH_FULL_NNI_SUPPORT
		return ENeuralBackEnd::UEOnly;
#endif //WITH_FULL_NNI_SUPPORT
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

#if WITH_EDITOR
bool UNeuralNetwork::Load(const FString& InModelFilePath)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("UNeuralNetwork_Load_EditorOnly"), STAT_UNeuralNetwork_Load, STATGROUP_MachineLearning);

	// Clean previous networks
	bIsLoaded = false;

	// Fill ModelFullFilePath & ModelReadFromDiskInBytes
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
		// Read file into ModelReadFromDiskInBytes
		// Source: https://github.com/microsoft/onnxruntime/blob/894fc828587c919d815918c4da6cde314e5d54ed/onnxruntime/test/shared_lib/test_model_loading.cc#L21-L31
		if (!FFileHelper::LoadFileToArray(ModelReadFromDiskInBytes, *ModelFullFilePath))
		{
			UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Load(): Error reading model \"%s\"."), *ModelFullFilePath);
			return false;
		}
	}

	// UEAndORT
	if (BackEndForCurrentPlatform == ENeuralBackEnd::UEAndORT)
	{
		return Load();
	}
	// UEOnly
	else if (BackEndForCurrentPlatform == ENeuralBackEnd::UEOnly)
	{
		bIsLoaded = UNeuralNetwork::FImplBackEndUEOnly::Load(ImplBackEndUEOnly, /*ModelReadFromDiskInBytes*/ ModelFullFilePath);
	}
	// Unknown
	else
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Load(): Unknown [BackEnd,BackEndForCurrentPlatform] = [%d,%d]."), (int32)BackEnd, (int32)BackEndForCurrentPlatform);
	}

	return bIsLoaded;
}
#endif //WITH_EDITOR


bool UNeuralNetwork::Load()
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("UNeuralNetwork_Load"), STAT_UNeuralNetwork_Load, STATGROUP_MachineLearning);

	// Clean previous networks
	bIsLoaded = false;

	// UEAndORT
	if (BackEndForCurrentPlatform == ENeuralBackEnd::UEAndORT)
	{
		bIsLoaded = UNeuralNetwork::FImplBackEndUEAndORT::Load(ImplBackEndUEAndORT, InputTensors, OutputTensors, AreInputTensorSizesVariable, ModelReadFromDiskInBytes, ModelFullFilePath, GetDeviceType());
	}
	// UEOnly
	else if (BackEndForCurrentPlatform == ENeuralBackEnd::UEOnly)
	{
		bIsLoaded = UNeuralNetwork::FImplBackEndUEOnly::Load(ImplBackEndUEOnly, /*ModelReadFromDiskInBytes*/ ModelFullFilePath);
	}
	// Unknown
	else
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Load(): Unknown [BackEnd,BackEndForCurrentPlatform] = [%d,%d]."), (int32)BackEnd, (int32)BackEndForCurrentPlatform);
	}

	return bIsLoaded;
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
	if (DeviceType != InDeviceType)
	{
		DeviceType = InDeviceType;
		if (BackEndForCurrentPlatform == ENeuralBackEnd::UEAndORT)
		{
			Load();
		}
	}
}

ENeuralDeviceType UNeuralNetwork::GetInputDeviceType() const
{
	return InputDeviceType;
}

void UNeuralNetwork::SetInputDeviceType(const ENeuralDeviceType InInputDeviceType)
{
	InputDeviceType = InInputDeviceType;
}

ENeuralDeviceType UNeuralNetwork::GetOutputDeviceType() const
{
	return OutputDeviceType;
}

void UNeuralNetwork::SetOutputDeviceType(const ENeuralDeviceType InOutputDeviceType)
{
	OutputDeviceType = InOutputDeviceType;
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
	// UEAndORT
	if (BackEndForCurrentPlatform == ENeuralBackEnd::UEAndORT)
	{
		return InputTensors[InTensorIndex];
	}

	// UEOnly
	else if (BackEndForCurrentPlatform == ENeuralBackEnd::UEOnly)
	{
		const int32 TensorIndex = ImplBackEndUEOnly->TensorManager.GetInputIndexes()[InTensorIndex];
		return ImplBackEndUEOnly->TensorManager.GetTensors()[TensorIndex];
	}

	// Unknown
	checkf(false, TEXT("UNeuralNetwork::GetInputTensor(): Unknown [BackEnd,BackEndForCurrentPlatform] = [%d,%d]."), (int32)BackEnd, (int32)BackEndForCurrentPlatform);
	return InputTensors[InTensorIndex];
}

const TArray<FNeuralTensor>& UNeuralNetwork::GetInputTensors() const
{
	return InputTensors;
}

void UNeuralNetwork::SetInputFromArrayCopy(const TArray<float>& InArray, const int32 InTensorIndex)
{
	// UEAndORT
	if (BackEndForCurrentPlatform == ENeuralBackEnd::UEAndORT)
	{
		InputTensors[InTensorIndex].SetFromArrayCopy(InArray);
	}

	// UEOnly
	else if (BackEndForCurrentPlatform == ENeuralBackEnd::UEOnly)
	{
UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::SetInputFromArrayCopy(): UEOnly not implemented yet."));
	}

	// Unknown
	else
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::SetInputFromArrayCopy(): Unknown [BackEnd,BackEndForCurrentPlatform] = [%d,%d]."), (int32)BackEnd, (int32)BackEndForCurrentPlatform);
	}
}

void* UNeuralNetwork::GetInputDataPointerMutable(const int32 InTensorIndex)
{
	// UEAndORT
	if (BackEndForCurrentPlatform == ENeuralBackEnd::UEAndORT)
	{
		return InputTensors[InTensorIndex].GetData(); // Or ImplBackEndUEAndORT->InputOrtTensors[InTensorIndex].GetTensorMutableData<float>();
	}

	// UEOnly
	else if (BackEndForCurrentPlatform == ENeuralBackEnd::UEOnly)
	{
UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::GetInputDataPointerMutable(): UEOnly not implemented yet."));
	}

	// Unknown
	UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::SetInputFromArrayCopy(): Unknown [BackEnd,BackEndForCurrentPlatform] = [%d,%d]."), (int32)BackEnd, (int32)BackEndForCurrentPlatform);
	return nullptr;
}

const FNeuralTensor& UNeuralNetwork::GetOutputTensor(const int32 InTensorIndex) const
{
	// UEAndORT
	if (BackEndForCurrentPlatform == ENeuralBackEnd::UEAndORT)
	{
		return OutputTensors[InTensorIndex];
	}

	// UEOnly
	else if (BackEndForCurrentPlatform == ENeuralBackEnd::UEOnly)
	{
UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::GetOutputTensor(): UEOnly not implemented yet."));
	}

	// Unknown
	checkf(false, TEXT("UNeuralNetwork::GetOutputTensor(): Unknown [BackEnd,BackEndForCurrentPlatform] = [%d,%d]."), (int32)BackEnd, (int32)BackEndForCurrentPlatform);
	return OutputTensors[InTensorIndex];
}

const TArray<FNeuralTensor>& UNeuralNetwork::GetOutputTensors() const
{
	return OutputTensors;
}

void UNeuralNetwork::Run()
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("UNeuralNetwork_Run"), STAT_UNeuralNetwork_Run, STATGROUP_MachineLearning);

	// Sanity checks
	if (!bIsLoaded)
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Run(): No architecture has been loaded yet. Run() will not work until IsLoaded() returns true."));
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
	// If ModelReadFromDiskInBytes is not empty, call Load() 
	if (ModelReadFromDiskInBytes.Num() > 0)
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
