// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralNetwork.h"
#include "NeuralNetworkInferenceUtils.h"
#include "EditorFramework/AssetImportData.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
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

#ifdef WITH_FULL_NNI_SUPPORT
	if (InModelFilePath.IsEmpty())
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Load(): Input model path was empty."));
		return false;
	}

	ModelFullFilePath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*InModelFilePath);

	// Sanity check
	if (!FPaths::FileExists(ModelFullFilePath))
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Load(): Model not found \"%s\"."), *ModelFullFilePath);
		return false;
	}

	const FRedirectCoutAndCerrToUeLog RedirectCoutAndCerrToUeLog;

	// Initialize and configure ImplBackEndUEAndORT
	if (!UNeuralNetwork::FImplBackEndUEAndORT::InitializedAndConfigureMembers(ImplBackEndUEAndORT, ModelFullFilePath, GetDeviceType()))
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Load(): InitializedAndConfigureMembers failed."));
		return false;
	}

	// Read file into ModelReadFromDiskInBytes
	// Source: https://github.com/microsoft/onnxruntime/blob/894fc828587c919d815918c4da6cde314e5d54ed/onnxruntime/test/shared_lib/test_model_loading.cc#L21-L31
	if (!FFileHelper::LoadFileToArray(ModelReadFromDiskInBytes, *ModelFullFilePath))
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Load(): Error reading model \"%s\"."), *ModelFullFilePath);
		return false;
	}
	return Load();



//	// Note: This code uses the changes in the ONNX Runtime API, which are not needed for desktop platforms (where ONNX/ProtoBuf is supported)
//	// Fill ModelReadFromDiskInBytes from ONNX/ORT file
//	const FString FileExtension = FPaths::GetExtension(ModelFullFilePath, /*bIncludeDot*/ false);
//	const char* const FilePathCharPtr = TCHAR_TO_ANSI(*ModelFullFilePath);
//	// If ONNX file, turn into ORT format first
//	if (FileExtension.Equals(TEXT("onnx"), ESearchCase::IgnoreCase))
//	{
//		FString ONNXPathPart, ONNXFilenamePart, ONNXExtensionPart;
//		FPaths::Split(ModelFullFilePath, ONNXPathPart, ONNXFilenamePart, ONNXExtensionPart);
//		const FString OutputORTOptimizedModelPath = ONNXPathPart / ONNXFilenamePart + TEXT(".ort");
//#ifdef _WIN32
//		ImplBackEndUEAndORT->SessionOptions->SetOptimizedModelFilePath(*OutputORTOptimizedModelPath);
//#else
//		ImplBackEndUEAndORT->SessionOptions->SetOptimizedModelFilePath(TCHAR_TO_ANSI(*OutputORTOptimizedModelPath));
//#endif //_WIN32
//		// ONNX --> ORT conversion
//		// This session is just temporarily opened so the ORT model file can be generated
//		ImplBackEndUEAndORT->Session = MakeUnique<Ort::Session>(*ImplBackEndUEAndORT->Environment,
//#ifdef _WIN32
//			*ModelFullFilePath,
//#else
//			FilePathCharPtr,
//#endif
//			*ImplBackEndUEAndORT->SessionOptions);
//
//		// Read model from OutputORTOptimizedModelPath
//		return Load(OutputORTOptimizedModelPath);
//	}
//	// Create session (it should be ORT file at this point), and read ModelReadFromDiskInBytes if not empty
//	else if (FileExtension.Equals(TEXT("ort"), ESearchCase::IgnoreCase))
//	{
//		// Read model from ModelFullFilePath
//		std::vector<uint8_t> OutputModelReadFromDiskInBytesVector;
//		ImplBackEndUEAndORT->Session = MakeUnique<Ort::Session>(*ImplBackEndUEAndORT->Environment,
//#ifdef _WIN32
//			*ModelFullFilePath,
//#else
//			FilePathCharPtr,
//#endif
//			*ImplBackEndUEAndORT->SessionOptions, &OutputModelReadFromDiskInBytesVector);
//
//		// Fill ModelReadFromDiskInBytes
//		const int32 ArraySize = OutputModelReadFromDiskInBytesVector.size();
//		if (ArraySize > 0)
//		{
//			ModelReadFromDiskInBytes.SetNumUninitialized(ArraySize);
//			FMemory::Memcpy(ModelReadFromDiskInBytes.GetData(), &OutputModelReadFromDiskInBytesVector[0], ArraySize);
//		}
//
//		return Load();
//	}
//	// Else
//	UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Load(): Unknown file format \"%s\" used."), *FileExtension);
//	return false;

#else
	UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Load(): Platform or Operating System not suported yet."));
	return false;
#endif //WITH_FULL_NNI_SUPPORT
}
#endif //WITH_EDITOR


bool UNeuralNetwork::Load()
{
#ifdef WITH_FULL_NNI_SUPPORT
	// Clean previous networks
	bIsLoaded = false;

	const FRedirectCoutAndCerrToUeLog RedirectCoutAndCerrToUeLog;

	// Initialize and configure ImplBackEndUEAndORT
	if (!UNeuralNetwork::FImplBackEndUEAndORT::InitializedAndConfigureMembers(ImplBackEndUEAndORT, ModelFullFilePath, GetDeviceType()))
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Load(): InitializedAndConfigureMembers failed."));
		return false;
	}

	// Create session from model saved in ModelReadFromDiskInBytes (if not empty)
	if (ModelReadFromDiskInBytes.Num() > 0)
	{
		// Read model from ModelReadFromDiskInBytesVector
		ImplBackEndUEAndORT->Session = MakeUnique<Ort::Session>(*ImplBackEndUEAndORT->Environment, ModelReadFromDiskInBytes.GetData(), ModelReadFromDiskInBytes.Num(), *ImplBackEndUEAndORT->SessionOptions);
	}
	// Else
	else
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("ModelReadFromDiskInBytes was empty."));
		return false;
	}

	ImplBackEndUEAndORT->ConfigureTensors(InputTensors, &AreInputTensorSizesVariable);
	ImplBackEndUEAndORT->ConfigureTensors(OutputTensors);

	bIsLoaded = true;
	return bIsLoaded;

#else
	UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Load(): Platform or Operating System not suported yet."));
	return false;
#endif //WITH_FULL_NNI_SUPPORT
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
		Load();
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
#ifdef WITH_FULL_NNI_SUPPORT
	// Sanity checks
	if (!bIsLoaded)
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Run(): No architecture has been loaded yet. Run() will not work until IsLoaded() returns true."));
		return;
	}
	// @todo: Temporarily disabled until we connect GPU input/output between UE and ORT
	if (InputDeviceType == ENeuralDeviceType::GPU || OutputDeviceType == ENeuralDeviceType::GPU)
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Run(): InputDeviceType & OutputDeviceType must be set to CPU for now."));
		return;
	}

	// Run UNeuralNetwork
	if (SynchronousMode == ENeuralNetworkSynchronousMode::Synchronous)
	{
		const FRedirectCoutAndCerrToUeLog RedirectCoutAndCerrToUeLog;
		ImplBackEndUEAndORT->Session->Run(Ort::RunOptions{ nullptr },
			InputTensors.GetTensorNames(), InputTensors.GetONNXRuntimeTensors(), InputTensors.GetNumberTensors(),
			OutputTensors.GetTensorNames(), OutputTensors.GetONNXRuntimeTensors(), OutputTensors.GetNumberTensors());
	}
	else if (SynchronousMode == ENeuralNetworkSynchronousMode::Asynchronous)
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Run(): SynchronousMode = %d not implemented yet. Use SynchronousMode = Synchronous."), (int32)SynchronousMode);
	}
	else
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Run(): Unknown SynchronousMode = %d."), (int32)SynchronousMode);
	}

#else
	UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Run(): Platform or Operating System not suported yet."));
#endif //WITH_FULL_NNI_SUPPORT
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
