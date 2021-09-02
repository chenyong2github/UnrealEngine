// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralNetworkImplBackEndUEAndORT.h"
#include "NeuralNetworkInferenceUtils.h"
#include "RedirectCoutAndCerrToUeLog.h"

#if defined(WITH_UE_AND_ORT_SUPPORT) && defined(PLATFORM_WIN64)
	#include "RHI.h"
	#include "DynamicRHI.h"
	
	// Disable NOMINMAX & WIN32_LEAN_AND_MEAN defines to avoid compiler warnings
	#pragma push_macro("NOMINMAX")
	#pragma push_macro("WIN32_LEAN_AND_MEAN")
	#undef NOMINMAX
	#undef WIN32_LEAN_AND_MEAN
	#include "D3D12RHIPrivate.h"
	#pragma pop_macro("WIN32_LEAN_AND_MEAN")
	#pragma pop_macro("NOMINMAX")

#endif

//#define WITH_NNI_CPU_NOT_RECOMMENDED // Only for debugging purposes

NNI_THIRD_PARTY_INCLUDES_START
#undef check
#undef TEXT
#ifdef WITH_UE_AND_ORT_SUPPORT
	#ifdef PLATFORM_WIN64
	#include "onnxruntime/core/providers/dml/dml_provider_factory.h"
	#endif
	#ifdef WITH_NNI_CPU_NOT_RECOMMENDED
	#include "onnxruntime/core/providers/nni_cpu/nni_cpu_provider_factory.h"
	#endif //WITH_NNI_CPU_NOT_RECOMMENDED
#endif //WITH_UE_AND_ORT_SUPPORT
NNI_THIRD_PARTY_INCLUDES_END

#if defined(WITH_UE_AND_ORT_SUPPORT) && defined(PLATFORM_WIN64)



/* DMLDeviceList auxiliary class
 *****************************************************************************/
class FPrivateNeuralNetwork
{
public:
	/**
	 * Helper class that maintains a list of created DML Devices for given ID3D12Device
	 */
	class DMLDeviceList
	{
	public:
		IDMLDevice* GetDMLDevice(ID3D12Device* Device);

	private:
		IDMLDevice* Add(ID3D12Device* Device);

		struct DMLDeviceEntry
		{
			ID3D12Device* Device;
			IDMLDevice* DmlDevice;
		};

		TArray<DMLDeviceEntry>		Entries;
	};

	static DMLDeviceList GDMLDevices;
};

IDMLDevice* FPrivateNeuralNetwork::DMLDeviceList::GetDMLDevice(ID3D12Device* Device)
{
	for (size_t c = 0; c < Entries.Num(); ++c)
	{
		if (Entries[c].Device == Device)
		{
			return Entries[c].DmlDevice;
		}
	}

	return Add(Device);
}

IDMLDevice* FPrivateNeuralNetwork::DMLDeviceList::Add(ID3D12Device* Device)
{
	// Create new DML Device
	IDMLDevice* DmlDevice = nullptr;

	DML_CREATE_DEVICE_FLAGS DmlCreateFlags = DML_CREATE_DEVICE_FLAG_NONE;

	DMLCreateDevice1(Device,
		DmlCreateFlags,
		DML_FEATURE_LEVEL_2_0,
		IID_PPV_ARGS(&DmlDevice));

	if (!DmlDevice)
	{
		UE_LOG(LogNeuralNetworkInference, Error, TEXT("Failed to create DML device."));
		return nullptr;
	}

	Entries.Push(DMLDeviceEntry{ Device, DmlDevice });

	return DmlDevice;
}

FPrivateNeuralNetwork::DMLDeviceList FPrivateNeuralNetwork::GDMLDevices;

#endif



/* UNeuralNetwork public functions
 *****************************************************************************/

//#if WITH_EDITOR
//bool UNeuralNetwork::FImplBackEndUEAndORT::LoadFile(TSharedPtr<FImplBackEndUEAndORT>& InOutImplBackEndUEAndORT, TArray<bool>& OutAreInputTensorSizesVariable, const TArray<uint8>& InModelReadFromFileInBytes, const FString& InModelFullFilePath, const ENeuralDeviceType InDeviceType)
//{
//	// Initialize and configure InOutImplBackEndUEAndORT
//	const FRedirectCoutAndCerrToUeLog RedirectCoutAndCerrToUeLog;
//	if (!InitializedAndConfigureMembers(InOutImplBackEndUEAndORT, InModelFullFilePath, InDeviceType))
//	{
//		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Load(): InitializedAndConfigureMembers failed."));
//		return false;
//	}
//
//	// Note: This code uses the changes in the ONNX Runtime API, which are not needed for desktop platforms (where ONNX/ProtoBuf is supported)
//	// Fill InModelReadFromFileInBytes from ONNX/ORT file
//	const FString FileExtension = FPaths::GetExtension(InModelFullFilePath, /*bIncludeDot*/ false);
//	const char* const FilePathCharPtr = TCHAR_TO_ANSI(*InModelFullFilePath);
//	// If ONNX file, turn into ORT format first
//	if (FileExtension.Equals(TEXT("onnx"), ESearchCase::IgnoreCase))
//	{
//		FString ONNXPathPart, ONNXFilenamePart, ONNXExtensionPart;
//		FPaths::Split(InModelFullFilePath, ONNXPathPart, ONNXFilenamePart, ONNXExtensionPart);
//		const FString OutputORTOptimizedModelPath = ONNXPathPart / ONNXFilenamePart + TEXT(".ort");
//#ifdef _WIN32
//		InOutImplBackEndUEAndORT->SessionOptions->SetOptimizedModelFilePath(*OutputORTOptimizedModelPath);
//#else
//		InOutImplBackEndUEAndORT->SessionOptions->SetOptimizedModelFilePath(TCHAR_TO_ANSI(*OutputORTOptimizedModelPath));
//#endif //_WIN32
//		// ONNX --> ORT conversion
//		// This session is just temporarily opened so the ORT model file can be generated
//		InOutImplBackEndUEAndORT->Session = MakeUnique<Ort::Session>(*InOutImplBackEndUEAndORT->Environment,
//#ifdef _WIN32
//			*InModelFullFilePath,
//#else
//			FilePathCharPtr,
//#endif
//			*InOutImplBackEndUEAndORT->SessionOptions);
//	
//		// Read model from OutputORTOptimizedModelPath
//		return Load(OutputORTOptimizedModelPath);
//	}
//	// Create session (it should be ORT file at this point), and read InModelReadFromFileInBytes if not empty
//	else if (FileExtension.Equals(TEXT("ort"), ESearchCase::IgnoreCase))
//	{
//		// Read model from InModelFullFilePath
//		std::vector<uint8_t> OutputModelReadFromFileInBytesVector;
//		InOutImplBackEndUEAndORT->Session = MakeUnique<Ort::Session>(*InOutImplBackEndUEAndORT->Environment,
//#ifdef _WIN32
//			*InModelFullFilePath,
//#else
//			FilePathCharPtr,
//#endif
//			*InOutImplBackEndUEAndORT->SessionOptions, &OutputModelReadFromFileInBytesVector);
//	
//		// Fill InModelReadFromFileInBytes
//		const int32 ArraySize = OutputModelReadFromFileInBytesVector.size();
//		if (ArraySize > 0)
//		{
//			InModelReadFromFileInBytes.SetNumUninitialized(ArraySize);
//			FMemory::Memcpy(InModelReadFromFileInBytes.GetData(), &OutputModelReadFromFileInBytesVector[0], ArraySize);
//		}
//	
//		return Load();
//	}
//	// Else
//	UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Load(): Unknown file format \"%s\" used."), *FileExtension);
//	return false;
//}
//#endif //WITH_EDITOR

bool UNeuralNetwork::FImplBackEndUEAndORT::Load(TSharedPtr<FImplBackEndUEAndORT>& InOutImplBackEndUEAndORT, TArray<bool>& OutAreInputTensorSizesVariable, const TArray<uint8>& InModelReadFromFileInBytes, const FString& InModelFullFilePath, const ENeuralDeviceType InDeviceType)
{
#ifdef WITH_UE_AND_ORT_SUPPORT
#if WITH_EDITOR
	try
#endif //WITH_EDITOR
	{
		const FRedirectCoutAndCerrToUeLog RedirectCoutAndCerrToUeLog;

		// Initialize and configure InOutImplBackEndUEAndORT
		if (!UNeuralNetwork::FImplBackEndUEAndORT::InitializedAndConfigureMembers(InOutImplBackEndUEAndORT, InModelFullFilePath, InDeviceType))
		{
			UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Load(): InitializedAndConfigureMembers failed."));
			return false;
		}

		// Create session from model saved in InModelReadFromFileInBytes (if not empty)
		if (InModelReadFromFileInBytes.Num() > 0)
		{
			// Read model from ModelReadFromFileInBytesVector
			InOutImplBackEndUEAndORT->Session = MakeUnique<Ort::Session>(*InOutImplBackEndUEAndORT->Environment, InModelReadFromFileInBytes.GetData(), InModelReadFromFileInBytes.Num(), *InOutImplBackEndUEAndORT->SessionOptions);
		}
		// Else
		else
		{
			UE_LOG(LogNeuralNetworkInference, Warning, TEXT("InModelReadFromFileInBytes was empty."));
			return false;
		}

		InOutImplBackEndUEAndORT->ConfigureTensors(InOutImplBackEndUEAndORT->InputTensors, &OutAreInputTensorSizesVariable);
		InOutImplBackEndUEAndORT->ConfigureTensors(InOutImplBackEndUEAndORT->OutputTensors);

		return true;
	}
#if WITH_EDITOR
	catch (const std::exception& Exception)
	{
		UE_LOG(LogNeuralNetworkInference, Error, TEXT("%s"), UTF8_TO_TCHAR(Exception.what()));
		return false;
	}
#endif //WITH_EDITOR

#else //WITH_UE_AND_ORT_SUPPORT
	UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Load(): Platform or Operating System not suported yet for UEAndORT BackEnd. Set BackEnd to ENeuralBackEnd::Auto (recommended) or ENeuralBackEnd::UEOnly for this platform."));
	return false;
#endif //WITH_UE_AND_ORT_SUPPORT
}

void UNeuralNetwork::FImplBackEndUEAndORT::Run(const ENeuralNetworkSynchronousMode InSynchronousMode, const ENeuralDeviceType InInputDeviceType, const ENeuralDeviceType InOutputDeviceType)
{
#ifdef WITH_UE_AND_ORT_SUPPORT
#if WITH_EDITOR
	try
#endif //WITH_EDITOR
	{
		const FRedirectCoutAndCerrToUeLog RedirectCoutAndCerrToUeLog;

		// @todo: Temporarily disabled until we connect GPU input/output between UE and ORT
		if (InInputDeviceType == ENeuralDeviceType::GPU || InOutputDeviceType == ENeuralDeviceType::GPU)
		{
			UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Run(): InputDeviceType & OutputDeviceType must be set to CPU for now."));
			return;
		}

		// Run UNeuralNetwork
		if (InSynchronousMode == ENeuralNetworkSynchronousMode::Synchronous)
		{
			Session->Run(Ort::RunOptions{ nullptr },
				InputTensorNames.GetData(), &InputOrtTensors[0], InputTensorNames.Num(),
				OutputTensorNames.GetData(), &OutputOrtTensors[0], OutputTensorNames.Num());
		}
		else if (InSynchronousMode == ENeuralNetworkSynchronousMode::Asynchronous)
		{
			UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Run(): SynchronousMode = %d not implemented yet. Use SynchronousMode = Synchronous."), (int32)InSynchronousMode);
		}
		else
		{
			UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Run(): Unknown SynchronousMode = %d."), (int32)InSynchronousMode);
		}
	}
#if WITH_EDITOR
	catch (const std::exception& Exception)
	{
		UE_LOG(LogNeuralNetworkInference, Error, TEXT("%s"), UTF8_TO_TCHAR(Exception.what()));
	}
#endif //WITH_EDITOR

#else //WITH_UE_AND_ORT_SUPPORT
	UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::Run(): Platform or Operating System not suported yet for UEAndORT BackEnd. Set BackEnd to ENeuralBackEnd::Auto or ENeuralBackEnd::UEOnly for this platform."));
#endif //WITH_UE_AND_ORT_SUPPORT
}



/* UNeuralNetwork private functions
 *****************************************************************************/

#ifdef WITH_UE_AND_ORT_SUPPORT

bool UNeuralNetwork::FImplBackEndUEAndORT::InitializedAndConfigureMembers(TSharedPtr<FImplBackEndUEAndORT>& InOutImplBackEndUEAndORT, const FString& InModelFullFilePath, const ENeuralDeviceType InDeviceType)
{
	// Initialize InOutImplBackEndUEAndORT
	if (!InOutImplBackEndUEAndORT.IsValid())
	{
		InOutImplBackEndUEAndORT = MakeShared<FImplBackEndUEAndORT>();

		// Set up ORT and create an environment
		Ort::InitApi();
		const char* const ModelFullFilePathCharPtr = TCHAR_TO_ANSI(*InModelFullFilePath);
		InOutImplBackEndUEAndORT->Environment = MakeUnique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, ModelFullFilePathCharPtr); // Any unique string would work, it does not need to be the file path

		InOutImplBackEndUEAndORT->Allocator = MakeUnique<Ort::AllocatorWithDefaultOptions>();

		InOutImplBackEndUEAndORT->AllocatorInfo = MakeUnique<Ort::MemoryInfo>(Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU));
	}

	// Configure InOutImplBackEndUEAndORT
	if (!InOutImplBackEndUEAndORT->ConfigureMembers(InDeviceType))
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::InitializedAndConfigureMembers(): ConfigureMembers failed."));
		return false;
	}

	return true;
}

bool UNeuralNetwork::FImplBackEndUEAndORT::ConfigureMembers(const ENeuralDeviceType InDeviceType)
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
		// To create a DirectML device we need to check that we're using DX12 first
		const FString RHIName = GDynamicRHI->GetName();

		// @todo for Nebojša.Dragosavac & Ginés.Hidalgo: Handle the default DX11 nicely
		if (RHIName != TEXT("D3D12"))
		{
			//UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::ConfigureMembers(): Only DX12 (\"D3D12\")) rendering is compatible with UEAndORT-based NNI. Instead, \"%s\" was used."), *RHIName);
			//return false;

			// Temporary workaround to avoid breaking NNI
			if (OrtSessionOptionsAppendExecutionProvider_DML(*SessionOptions, 0))
			{
				UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::ConfigureMembers(): Some error occurred when using OrtSessionOptionsAppendExecutionProvider_DML()."));
				return false;
			}
			return true;
		}

		// Get adapter's D3D12 device that we would like to share with DirectML execution provider
		// NOTE: For now we're only using first device that has Dadapter 0 and device 0
		FD3D12DynamicRHI* Rhi = static_cast<FD3D12DynamicRHI*>(GDynamicRHI);

		if (Rhi->GetNumAdapters() > 1 || Rhi->GetAdapter(0).GetDesc().NumDeviceNodes > 1)
		{
			UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::ConfigureMembers(): There are multiple (%d) adapters and/or multiple (%d) devices, using device at index 0."),
				Rhi->GetNumAdapters(), Rhi->GetAdapter(0).GetDesc().NumDeviceNodes);
			return false;
		}

		ID3D12Device* NativeDevice = Rhi->GetAdapter(0).GetD3DDevice();

		// Make sure that we have one DMLDevice per D3D12 device
		IDMLDevice* DmlDevice = FPrivateNeuralNetwork::GDMLDevices.GetDMLDevice(NativeDevice);

		if (!DmlDevice)
		{
			UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::ConfigureMembers(): Invalid DML device found."));
			return false;
		}

		// Get a ID3D12CommandQueue as well
		// TODO: Should we create our own queue?
		ID3D12CommandQueue* NativeCmdQ = Rhi->RHIGetD3DCommandQueue();

		// ORT GPU (Direct ML)
		SessionOptions->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL); // ORT_ENABLE_ALL, ORT_ENABLE_EXTENDED, ORT_ENABLE_BASIC, ORT_DISABLE_ALL
		if (OrtSessionOptionsAppendExecutionProviderEx_DML(*SessionOptions, DmlDevice, NativeCmdQ))
		{
			UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork::ConfigureMembers(): Some error occurred when using OrtSessionOptionsAppendExecutionProviderEx_DML()."));
			return false;
		}
		return true; // @todo: Remove this line when NNI_HLSL is working
#else
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("UNeuralNetwork: GPU mode only supported in Windows for now. Please, switch to CPU or to Windows."));

		//SessionOptions->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_DISABLE_ALL); // ORT_ENABLE_ALL, ORT_ENABLE_EXTENDED, ORT_ENABLE_BASIC, ORT_DISABLE_ALL
		//if (OrtSessionOptionsAppendExecutionProvider_NNI_HLSL(*SessionOptions, 0))
		//{
		//	UE_LOG(LogNeuralNetworkInference, Warning, TEXT("Some error occurred."));
		//	return false;
		//}
#endif //PLATFORM_WIN64
	}
	// CPU
	//else // @todo: Uncomment this line when NNI_HLSL is working
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

void UNeuralNetwork::FImplBackEndUEAndORT::ConfigureTensors(TArray<FNeuralTensor>& OutTensors, TArray<bool>* OutAreInputTensorSizesVariable)
{
	const bool bIsInput = (OutAreInputTensorSizesVariable != nullptr);
	TArray<const char*> TensorNames;
	TArray<ENeuralDataType> TensorDataTypes;
	TArray<TArray<int64>> TensorSizes;

	const uint32 NumberTensors = bIsInput ? Session->GetInputCount() : Session->GetOutputCount();
	if (OutAreInputTensorSizesVariable)
	{
		OutAreInputTensorSizesVariable->SetNum(NumberTensors);
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
					if (OutAreInputTensorSizesVariable)
					{
						(*OutAreInputTensorSizesVariable)[TensorIndex] |= (CurrentTensorSize < 0);
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

	SetTensorsFromNetwork(OutTensors, TensorNames, TensorDataTypes, TensorSizes, bIsInput);
}

void UNeuralNetwork::FImplBackEndUEAndORT::SetTensorsFromNetwork(TArray<FNeuralTensor>& OutTensors, TArray<const char*>& InTensorNames, TArray<ENeuralDataType>& InTensorDataTypes,
	TArray<TArray<int64>>& InSizes, const bool bIsInput)
{
	const int32 TensorNumber = InTensorNames.Num();
	if (InTensorDataTypes.Num() != TensorNumber || InSizes.Num() != TensorNumber)
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("InTensorNames.Num() == InTensorDataTypes.Num() == InSizes.Num() failed, %d vs. %d vs. %d."),
			InTensorNames.Num(), InTensorDataTypes.Num(), InSizes.Num());
		return;
	}

	// Swap variables
	TArray<const char*>& TensorNames = (bIsInput ? InputTensorNames : OutputTensorNames);
	Swap(TensorNames, InTensorNames);

	// Note: Switching from/to CPU to/from GPU would cause the FNeuralTensors to be re-initialized. We need to avoid that. For that, we will only re-allocate the tensors...
	// - If bAreTensorsAlreadyCreatedWithRightNames == false, meaning tensors had not been created until now for this network.
	// - And if the existing tensors have the right size, given that SetNumUninitialized() only re-allocates them if their size has changed.

	// Fill bAreTensorsAlreadyCreatedWithRightNames - Check if tensors already created with the right names
	bool bAreTensorsAlreadyCreatedWithRightNames = (OutTensors.Num() == TensorNames.Num());
	if (bAreTensorsAlreadyCreatedWithRightNames)
	{
		for (int32 TensorIndex = 0; TensorIndex < TensorNumber; ++TensorIndex)
		{
			if (OutTensors[TensorIndex].GetName() != ANSI_TO_TCHAR(TensorNames[TensorIndex]))
			{
				bAreTensorsAlreadyCreatedWithRightNames = false;
				break;
			}
		}
	}

	// Assign name to each input/output tensor
	if (!bAreTensorsAlreadyCreatedWithRightNames)
	{
		OutTensors.Empty();
		for (const char* const TensorName : TensorNames)
		{
			OutTensors.Emplace(FNeuralTensor(ANSI_TO_TCHAR(TensorName)));
		}
	}
	ensureMsgf(OutTensors.Num() == TensorNumber, TEXT("OutTensors.Num() == TensorNumber failed, %d != %d."), OutTensors.Num(), TensorNumber);

	// Config each TensorIndex
	TArray<Ort::Value>& OrtTensors = (bIsInput ? InputOrtTensors : OutputOrtTensors);
	for (int32 TensorIndex = 0; TensorIndex < TensorNumber; ++TensorIndex)
	{
		if (OrtTensors.Num() <= TensorIndex)
		{
			OrtTensors.Emplace(Ort::Value(nullptr));
		}
		// Pre-allocate TArray (if size is different)
		OutTensors[TensorIndex].SetNumUninitialized(InSizes[TensorIndex], InTensorDataTypes[TensorIndex]);
		// Link tensor with ORT blob
		LinkTensorToONNXRuntime(OutTensors, OrtTensors, *AllocatorInfo, TensorIndex);
	}
}

void UNeuralNetwork::FImplBackEndUEAndORT::LinkTensorToONNXRuntime(TArray<FNeuralTensor>& InOutTensors, TArray<Ort::Value>& InOutOrtTensors, Ort::MemoryInfo& InOutAllocatorInfo, const int32 InTensorIndex)
{
	const TArray<int64>& Sizes = InOutTensors[InTensorIndex].GetSizes();
	if (Sizes.Num() > 0 && InOutTensors[InTensorIndex].Num() > 0)
	{
		FNeuralTensor& Tensor = InOutTensors[InTensorIndex];
		const int64 Volume = Tensor.Num();
		const int32 ArrayDimensions = Sizes.Num();

		const ENeuralDataType NeuralDataType = Tensor.GetDataType();
		if (NeuralDataType == ENeuralDataType::Float)
		{
#ifdef _WIN32
			const TArray<int64_t>& SizesInt64t = Sizes;
#else
			checkf(sizeof(int64) == sizeof(int64_t), TEXT("int64 and int64_t should both have the same size."));
			TArray<int64_t> SizesInt64t;
			SizesInt64t.SetNumUninitialized(ArrayDimensions);
			FMemory::Memcpy(SizesInt64t.GetData(), (int64_t*)Sizes.GetData(), sizeof(int64_t) * ArrayDimensions);
#endif //_WIN32
			InOutOrtTensors[InTensorIndex] = Ort::Value::CreateTensor<float>(InOutAllocatorInfo, Tensor.GetDataCasted<float>(), Volume, SizesInt64t.GetData(), ArrayDimensions);
		}
		//else if (NeuralDataType == ENeuralDataType::Double)
		//{
		//	InOutOrtTensors[InTensorIndex] = Ort::Value::CreateTensor<double>(InOutAllocatorInfo, Tensor.GetDataCasted<double>(), Volume, Sizes.GetData(), ArrayDimensions);
		//}
		else
		{
			UE_LOG(LogNeuralNetworkInference, Warning, TEXT("Not implemented (yet) for ENeuralDataType = %d."), (int32)NeuralDataType);
		}
	}
}

#endif //WITH_UE_AND_ORT_SUPPORT
