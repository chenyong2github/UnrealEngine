// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralNetwork.h"
#include "NeuralNetworkInferenceUtils.h"
#include "NeuralNetworkInferenceVersion.h"
#include "EditorFramework/AssetImportData.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include <numeric> // std::accumulate

//#define WITH_NNI_CPU_NOT_RECOMMENDED // Only for debugging purposes

#include "ThirdPartyWarningDisabler.h"
NNI_THIRD_PARTY_INCLUDES_START
#undef check
#undef TEXT
#ifdef WITH_FULL_NNI_SUPPORT
	#include "RedirectCoutAndCerrToUeLog.h"

	#include "onnxruntime/core/session/onnxruntime_cxx_api.h"
	#ifdef PLATFORM_WIN64
	#include "onnxruntime/core/providers/dml/dml_provider_factory.h"
	#endif
	#ifdef WITH_NNI_CPU_NOT_RECOMMENDED
	#include "onnxruntime/core/providers/nni_cpu/nni_cpu_provider_factory.h"
	#endif //WITH_NNI_CPU_NOT_RECOMMENDED
#endif //WITH_FULL_NNI_SUPPORT
NNI_THIRD_PARTY_INCLUDES_END



/* FImpl
 *****************************************************************************/

struct UNeuralNetwork::FImpl
{
#ifdef WITH_FULL_NNI_SUPPORT
	TUniquePtr<Ort::Env> Environment;
	TUniquePtr<Ort::Session> Session;
	TUniquePtr<Ort::AllocatorWithDefaultOptions> Allocator;
	TUniquePtr<Ort::SessionOptions> SessionOptions;

	static bool InitializedAndConfigureMembers(TSharedPtr<FImpl>& InOutImpl, const FString& InModelFullFilePath, const ENeuralDeviceType InDeviceType);

	bool ConfigureMembers(const ENeuralDeviceType InDeviceType);

	void ConfigureTensors(FNeuralTensors& OutTensors, TArray<bool>* InAreInputTensorSizesVariable = nullptr);
#endif //WITH_FULL_NNI_SUPPORT
};

#ifdef WITH_FULL_NNI_SUPPORT
bool UNeuralNetwork::FImpl::InitializedAndConfigureMembers(TSharedPtr<FImpl>& InOutImpl, const FString& InModelFullFilePath, const ENeuralDeviceType InDeviceType)
{
	// Initialize InOutImpl
	if (!InOutImpl.IsValid())
	{
		InOutImpl = MakeShared<FImpl>();

		// Set up ORT and create an environment
		Ort::InitApi();
		const char* const ModelFullFilePathCharPtr = TCHAR_TO_ANSI(*InModelFullFilePath);
		InOutImpl->Environment = MakeUnique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, ModelFullFilePathCharPtr); // Any unique string would work, it does not need to be the file path

		InOutImpl->Allocator = MakeUnique<Ort::AllocatorWithDefaultOptions>();
	}

	// Configure InOutImpl
	if (!InOutImpl->ConfigureMembers(InDeviceType))
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Load(): ConfigureMembers failed."));
		return false;
	}

	return true;
}

bool UNeuralNetwork::FImpl::ConfigureMembers(const ENeuralDeviceType InDeviceType)
{
	// Configure Session
	SessionOptions = MakeUnique<Ort::SessionOptions>();

	// Configure number threads
	SessionOptions->SetIntraOpNumThreads(2);
	// Uncomment if you want to change the priority of the threads, by default is TPri_Normal
	//SessionOptions->SetPriorityOpThreads(EThreadPriority::TPri_Normal);

	// Configure Provider
	// GPU
	if (InDeviceType == ENeuralDeviceType::GPU)
	{
#ifdef PLATFORM_WIN64
		// ORT GPU (Direct ML)
		SessionOptions->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL); // ORT_ENABLE_ALL, ORT_ENABLE_EXTENDED, ORT_ENABLE_BASIC, ORT_DISABLE_ALL
		if (OrtSessionOptionsAppendExecutionProvider_DML(*SessionOptions, 0))
		{
			UE_LOG(LogNeuralNetworkInference, Warning, TEXT("Some error occurred."));
			return false;
		}
#else
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork: GPU mode only supported in Windows for now. Please, switch to CPU or to Windows."));
		return false;

		//SessionOptions->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_DISABLE_ALL); // ORT_ENABLE_ALL, ORT_ENABLE_EXTENDED, ORT_ENABLE_BASIC, ORT_DISABLE_ALL
		//if (OrtSessionOptionsAppendExecutionProvider_NNI_HLSL(*SessionOptions, 0))
		//{
		//	UE_LOG(LogNeuralNetworkInference, Warning, TEXT("Some error occurred."));
		//	return false;
		//}
#endif //PLATFORM_WIN64
	}
	// CPU
	else
	{
#ifdef WITH_NNI_CPU_NOT_RECOMMENDED
		// NNI CPU (Deprecated)
		SessionOptions->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_DISABLE_ALL); // ORT_ENABLE_ALL, ORT_ENABLE_EXTENDED, ORT_ENABLE_BASIC, ORT_DISABLE_ALL
		if (OrtSessionOptionsAppendExecutionProvider_NNI_CPU(*SessionOptions))
		{
			UE_LOG(LogNeuralNetworkInference, Warning, TEXT("Some error occurred."));
			return false;
		}
#else
		// ORT CPU
		SessionOptions->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL); // ORT_ENABLE_ALL, ORT_ENABLE_EXTENDED, ORT_ENABLE_BASIC, ORT_DISABLE_ALL
#endif //ORT_CPU
	}

	return true;
}

void UNeuralNetwork::FImpl::ConfigureTensors(FNeuralTensors& OutTensors, TArray<bool>* InAreInputTensorSizesVariable)
{
	const bool bIsInput = (InAreInputTensorSizesVariable != nullptr);
	TArray<const char*> TensorNames;
	TArray<ENeuralDataType> TensorDataTypes;
	TArray<TArray<int64>> TensorSizes;

	const uint32 NumberTensors = bIsInput ? Session->GetInputCount() : Session->GetOutputCount();
	if (InAreInputTensorSizesVariable)
	{
		InAreInputTensorSizesVariable->SetNum(NumberTensors);
	}
	for (uint32 TensorIndex = 0; TensorIndex < NumberTensors; ++TensorIndex)
	{
		// Get node name
		{
			const char* TensorName = bIsInput ? Session->GetInputName(TensorIndex, *Allocator) : Session->GetOutputName(TensorIndex, *Allocator);
			TensorNames.Emplace(TensorName);
		}

		// Get node type
		Ort::TypeInfo CurrentTypeInfo = bIsInput ? Session->GetInputTypeInfo(TensorIndex) : Session->GetOutputTypeInfo(TensorIndex);

		Ort::TensorTypeAndShapeInfo CurrentTensorInfo = CurrentTypeInfo.GetTensorTypeAndShapeInfo();

		{
			ENeuralDataType TensorDataType;
			{
				const ONNXTensorElementDataType ONNXTensorElementDataTypeEnum = CurrentTensorInfo.GetElementType();
				if (ONNXTensorElementDataTypeEnum == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
				{
					TensorDataType = ENeuralDataType::Float;
				}
				//else if (ONNXTensorElementDataTypeEnum == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32)
				//{
				//	TensorDataType = ENeuralDataType::Int32;
				//}
				//else if (ONNXTensorElementDataTypeEnum == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64)
				//{
				//	TensorDataType = ENeuralDataType::Int64;
				//}
				//else if (ONNXTensorElementDataTypeEnum == ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT32)
				//{
				//	TensorDataType = ENeuralDataType::UInt32;
				//}
				//else if (ONNXTensorElementDataTypeEnum == ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT64)
				//{
				//	TensorDataType = ENeuralDataType::UInt64;
				//}
				else
				{
					TensorDataType = ENeuralDataType::None;
					UE_LOG(LogNeuralNetworkInference, Warning, TEXT("ONNXTensorElementDataTypeEnum = %d not implemented yet."), (int32)ONNXTensorElementDataTypeEnum);
					return;
				}
			}
			TensorDataTypes.Push(TensorDataType);
		}

		// Get input shapes/dims
		{
			TArray<int64> CurrentTensorSizes;
			{
				for (const int64_t CurrentTensorSize : CurrentTensorInfo.GetShape())
				{
					if (InAreInputTensorSizesVariable)
					{
						(*InAreInputTensorSizesVariable)[TensorIndex] |= (CurrentTensorSize < 0);
					}
					// Negative (variable) dimensions not implemented yet
					if (CurrentTensorSize < 0)
					{
						CurrentTensorSizes.Push(1);
						UE_LOG(LogNeuralNetworkInference, Display,
							TEXT("Negative (i.e., variable) dimensions not allowed yet, hard-coded to 1. Let us know if you really need variable dimensions."
								" Keep in mind that fixed sizes might allow additional optimizations and speedup of the network during Run()."));
					}
					else
					{
						CurrentTensorSizes.Push(CurrentTensorSize);
					}
				}
			}
			TensorSizes.Push(CurrentTensorSizes);
		}

		CurrentTypeInfo.release();

	}

	OutTensors.SetFromNetwork(TensorNames, TensorDataTypes, TensorSizes);
}
#endif //WITH_FULL_NNI_SUPPORT



/* UNeuralNetwork structors
 *****************************************************************************/

UNeuralNetwork::UNeuralNetwork()
	: DeviceType(ENeuralDeviceType::CPU)
	, InputDeviceType(ENeuralDeviceType::CPU)
	, OutputDeviceType(ENeuralDeviceType::CPU)
	, SynchronousMode(ENeuralNetworkSynchronousMode::Synchronous)
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

	// Initialize and configure Impl
	if (!UNeuralNetwork::FImpl::InitializedAndConfigureMembers(Impl, ModelFullFilePath, GetDeviceType()))
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
//		Impl->SessionOptions->SetOptimizedModelFilePath(*OutputORTOptimizedModelPath);
//#else
//		Impl->SessionOptions->SetOptimizedModelFilePath(TCHAR_TO_ANSI(*OutputORTOptimizedModelPath));
//#endif //_WIN32
//		// ONNX --> ORT conversion
//		// This session is just temporarily opened so the ORT model file can be generated
//		Impl->Session = MakeUnique<Ort::Session>(*Impl->Environment,
//#ifdef _WIN32
//			*ModelFullFilePath,
//#else
//			FilePathCharPtr,
//#endif
//			*Impl->SessionOptions);
//
//		// Read model from OutputORTOptimizedModelPath
//		return Load(OutputORTOptimizedModelPath);
//	}
//	// Create session (it should be ORT file at this point), and read ModelReadFromDiskInBytes if not empty
//	else if (FileExtension.Equals(TEXT("ort"), ESearchCase::IgnoreCase))
//	{
//		// Read model from ModelFullFilePath
//		std::vector<uint8_t> OutputModelReadFromDiskInBytesVector;
//		Impl->Session = MakeUnique<Ort::Session>(*Impl->Environment,
//#ifdef _WIN32
//			*ModelFullFilePath,
//#else
//			FilePathCharPtr,
//#endif
//			*Impl->SessionOptions, &OutputModelReadFromDiskInBytesVector);
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

	// Initialize and configure Impl
	if (!UNeuralNetwork::FImpl::InitializedAndConfigureMembers(Impl, ModelFullFilePath, GetDeviceType()))
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Load(): InitializedAndConfigureMembers failed."));
		return false;
	}

	// Create session from model saved in ModelReadFromDiskInBytes (if not empty)
	if (ModelReadFromDiskInBytes.Num() > 0)
	{
		// Read model from ModelReadFromDiskInBytesVector
		Impl->Session = MakeUnique<Ort::Session>(*Impl->Environment, ModelReadFromDiskInBytes.GetData(), ModelReadFromDiskInBytes.Num(), *Impl->SessionOptions);
	}
	// Else
	else
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("ModelReadFromDiskInBytes was empty."));
		return false;
	}

	Impl->ConfigureTensors(InputTensors, &AreInputTensorSizesVariable);
	Impl->ConfigureTensors(OutputTensors);

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
	DeviceType = InDeviceType;
	Load();
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
		Impl->Session->Run(Ort::RunOptions{ nullptr },
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



/* UNeuralNetwork public functions that should only be used internally (not by the user)
 *****************************************************************************/

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

#if WITH_EDITOR
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
#endif // WITH_EDITOR
