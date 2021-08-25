// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralNetwork.h"
#include "NeuralNetworkInferenceUtils.h"
#include "EditorFramework/AssetImportData.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

// Other files with UNeuralNetwork implementation (to be included only once and after the includes above)
#include "NeuralNetworkImplBackEndUEAndORT.imp"
#include "NeuralNetworkImplBackEndUEOnly.imp"



/* UNeuralNetwork structors
 *****************************************************************************/

UNeuralNetwork::UNeuralNetwork()
	: DeviceType(ENeuralDeviceType::CPU)
	, InputDeviceType(ENeuralDeviceType::CPU)
	, OutputDeviceType(ENeuralDeviceType::CPU)
	, SynchronousMode(ENeuralNetworkSynchronousMode::Synchronous)
#ifdef WITH_FULL_NNI_SUPPORT
	, BackEnd(ENeuralBackEnd::UEAndORT)
#else //WITH_FULL_NNI_SUPPORT
	, BackEnd(ENeuralBackEnd::UEOnly)
#endif //WITH_FULL_NNI_SUPPORT
	, bIsLoaded(false)
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
	if (BackEnd == ENeuralBackEnd::UEAndORT)
	{
		return Load();
	}
	// UEOnly
	else if (BackEnd == ENeuralBackEnd::UEOnly)
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Load(): Not implemented for BackEnd = %d yet."), (int32)BackEnd);
	}
	// Unknown
	else
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Load(): Unknown BackEnd = %d."), (int32)BackEnd);
	}

	return bIsLoaded;
}
#endif //WITH_EDITOR


bool UNeuralNetwork::Load()
{
	// Clean previous networks
	bIsLoaded = false;

	// UEAndORT
	if (BackEnd == ENeuralBackEnd::UEAndORT)
	{
#ifdef WITH_FULL_NNI_SUPPORT
		bIsLoaded = UNeuralNetwork::FImplBackEndUEAndORT::Load(ImplBackEndUEAndORT, InputTensors, OutputTensors, AreInputTensorSizesVariable, ModelReadFromDiskInBytes, ModelFullFilePath, GetDeviceType());
#else
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Load(): Platform or Operating System not suported yet for UEAndORT BackEnd. Set BackEnd to ENeuralBackEnd::Auto or ENeuralBackEnd::UEOnly for this platform."));
#endif //WITH_FULL_NNI_SUPPORT
	}
	// UEOnly
	else if (BackEnd == ENeuralBackEnd::UEOnly)
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Load(): Not implemented for BackEnd = %d yet."), (int32)BackEnd);
	}
	// Unknown
	else
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Load(): Unknown BackEnd = %d."), (int32)BackEnd);
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
		if (BackEnd == ENeuralBackEnd::UEAndORT)
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

void UNeuralNetwork::SetBackEnd(const ENeuralBackEnd InBackEnd)
{
	if (BackEnd != InBackEnd)
	{
		BackEnd = InBackEnd;
		Load();
	}
}

const FNeuralTensor& UNeuralNetwork::GetInputTensor(const int32 InTensorIndex) const
{
	return InputTensors.GetTensor(InTensorIndex);
}

const FNeuralTensors& UNeuralNetwork::GetInputTensors() const
{
	return InputTensors;
}

void UNeuralNetwork::SetInputFromArrayCopy(const TArray<float>& InArray, const int32 InTensorIndex)
{
	InputTensors.SetFromArrayCopy(InArray, InTensorIndex);
}

void* UNeuralNetwork::GetInputDataPointerMutable(const int32 InTensorIndex)
{
	return InputTensors.GetDataPointerMutable(InTensorIndex);
}

const FNeuralTensor& UNeuralNetwork::GetOutputTensor(const int32 InTensorIndex) const
{
	return OutputTensors.GetTensor(InTensorIndex);
}

const FNeuralTensors& UNeuralNetwork::GetOutputTensors() const
{
	return OutputTensors;
}

void UNeuralNetwork::Run()
{
	// Sanity checks
	if (!bIsLoaded)
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Run(): No architecture has been loaded yet. Run() will not work until IsLoaded() returns true."));
		return;
	}

	// UEAndORT
	if (BackEnd == ENeuralBackEnd::UEAndORT)
	{
#ifdef WITH_FULL_NNI_SUPPORT
		ImplBackEndUEAndORT->Run(OutputTensors, InputTensors, SynchronousMode, InputDeviceType, OutputDeviceType);
#else
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Run(): Platform or Operating System not suported yet for UEAndORT BackEnd. Set BackEnd to ENeuralBackEnd::Auto or ENeuralBackEnd::UEOnly for this platform."));
#endif //WITH_FULL_NNI_SUPPORT
	}

	// UEOnly
	else if (BackEnd == ENeuralBackEnd::UEOnly)
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Run(): Not implemented for BackEnd = %d yet."), (int32)BackEnd);
	}

	// Unknown
	else
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Run(): Unknown BackEnd = %d."), (int32)BackEnd);
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
