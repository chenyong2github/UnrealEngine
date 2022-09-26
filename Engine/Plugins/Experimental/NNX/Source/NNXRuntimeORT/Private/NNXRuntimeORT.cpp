// Copyright Epic Games, Inc. All Rights Reserved.
#include "NNXRuntimeORT.h"

#include "NeuralTimer.h"
#include "NNXRuntimeORTUtils.h"
#include "RedirectCoutAndCerrToUeLog.h"

// NOTE: For now we only have DML on Windows, we should add support for XSX
#if PLATFORM_WINDOWS

#include "Windows/AllowWindowsPlatformTypes.h"
#include <unknwn.h>
#include "Microsoft/COMPointer.h"
#include "Windows/HideWindowsPlatformTypes.h"

#include "ID3D12DynamicRHI.h"
#include "D3D12RHIBridge.h"
#include "DirectML.h"
#endif

using namespace NNX;

FString FRuntimeORTCpu::GetRuntimeName() const
{
	return NNX_RUNTIME_ORT_NAME_CPU;
}

#if PLATFORM_WINDOWS
FString FRuntimeORTCuda::GetRuntimeName() const
{
	return NNX_RUNTIME_ORT_NAME_CUDA;
}

FString FRuntimeORTDml::GetRuntimeName() const
{
	return NNX_RUNTIME_ORT_NAME_DML;
}
#endif

EMLRuntimeSupportFlags FRuntimeORTCpu::GetSupportFlags() const
{
	return EMLRuntimeSupportFlags::CPU;
}

#if PLATFORM_WINDOWS
EMLRuntimeSupportFlags FRuntimeORTCuda::GetSupportFlags() const
{
	return EMLRuntimeSupportFlags::GPU;
}

EMLRuntimeSupportFlags FRuntimeORTDml::GetSupportFlags() const
{
	return EMLRuntimeSupportFlags::GPU;
}
#endif

FMLInferenceModel* FRuntimeORTCpu::CreateInferenceModel(UMLInferenceModel* InModel, const FMLInferenceNNXORTConf& InConf)
{
	FMLInferenceModelORTCpu* ORTModel = new FMLInferenceModelORTCpu(&NNXEnvironmentORT, InConf);
	if (!ORTModel->Init(InModel))
	{
		delete ORTModel;
		ORTModel = nullptr;
	}

	return ORTModel;
}

#if PLATFORM_WINDOWS
FMLInferenceModel* FRuntimeORTCuda::CreateInferenceModel(UMLInferenceModel* InModel, const FMLInferenceNNXORTConf& InConf)
{
	FMLInferenceModelORTCuda* ORTModel = new FMLInferenceModelORTCuda(&NNXEnvironmentORT, InConf);
	if (!ORTModel->Init(InModel))
	{
		delete ORTModel;
		ORTModel = nullptr;
	}

	return ORTModel;
}

FMLInferenceModel* FRuntimeORTDml::CreateInferenceModel(UMLInferenceModel* InModel, const FMLInferenceNNXORTConf& InConf)
{
	FMLInferenceModelORTDml* ORTModel = new FMLInferenceModelORTDml(&NNXEnvironmentORT, InConf);
	if (!ORTModel->Init(InModel))
	{
		delete ORTModel;
		ORTModel = nullptr;
	}

	return ORTModel;
}
#endif

FMLInferenceModel* FRuntimeORTCpu::CreateInferenceModel(UMLInferenceModel* InModel)
{
	FMLInferenceNNXORTConf ORTInferenceConf;
	return CreateInferenceModel(InModel, ORTInferenceConf);
}

#if PLATFORM_WINDOWS
FMLInferenceModel* FRuntimeORTCuda::CreateInferenceModel(UMLInferenceModel* InModel)
{
	FMLInferenceNNXORTConf ORTInferenceConf;
	return CreateInferenceModel(InModel, ORTInferenceConf);
}

FMLInferenceModel* FRuntimeORTDml::CreateInferenceModel(UMLInferenceModel* InModel)
{
	FMLInferenceNNXORTConf ORTInferenceConf;
	return CreateInferenceModel(InModel, ORTInferenceConf);
}
#endif

FMLInferenceModelORT::FMLInferenceModelORT(
	Ort::Env* InORTEnvironment, 
	EMLInferenceModelType InType,
	const FMLInferenceNNXORTConf& InORTConfiguration) :
	FMLInferenceModel(InType),
	bIsLoaded(false),
	bHasRun(false),
	ORTEnvironment(InORTEnvironment),
	ORTConfiguration(InORTConfiguration)
{ }

bool FMLInferenceModelORT::Init(const UMLInferenceModel* InferenceModel)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FMLInferenceModelORT_Init"), STAT_FMLInferenceModelORT_Init, STATGROUP_MachineLearning);
	
	// Clean previous networks
	bIsLoaded = false;
	const TArray<uint8>& ModelBuffer{ InferenceModel->GetData()};

	// Checking Inference Model 
	{
		if (ModelBuffer.Num() == 0) {
			UE_LOG(LogNNX, Warning, TEXT("FMLInferenceModelORT::Load(): Input model path is empty."));
			return false;
		}

	}

#if WITH_EDITOR
	try
#endif //WITH_EDITOR
	{
		const FRedirectCoutAndCerrToUeLog RedirectCoutAndCerrToUeLog;
		if (!InitializedAndConfigureMembers())
		{
			UE_LOG(LogNNX, Warning, TEXT("Load(): InitializedAndConfigureMembers failed."));
			return false;
		}

		{
			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FMLInferenceModelORT_Init_CreateORTSession"), STAT_FMLInferenceModelORT_Init_CreateORTSession, STATGROUP_MachineLearning);
			// Read model from InferenceModel
			Session = MakeUnique<Ort::Session>(*ORTEnvironment, ModelBuffer.GetData(), ModelBuffer.Num(), *SessionOptions);
		}
		
		if (!ConfigureTensors(true))
		{
			UE_LOG(LogNNX, Warning, TEXT("Load(): Failed to configure Inputs tensors."));
			return false;
		}

		if (!ConfigureTensors(false))
		{
			UE_LOG(LogNNX, Warning, TEXT("Load(): Failed to configure Outputs tensors."));
			return false;
		}
	}
#if WITH_EDITOR
	catch (const std::exception& Exception)
	{
		UE_LOG(LogNNX, Error, TEXT("%s"), UTF8_TO_TCHAR(Exception.what()));
		return false;
	}
#endif //WITH_EDITOR

	bIsLoaded = true;

	// Reset Stats
	ResetStats();

	return IsLoaded();
}

bool FMLInferenceModelORT::IsLoaded() const
{
	return bIsLoaded;
}

bool FMLInferenceModelORT::InitializedAndConfigureMembers()
{
	// Initialize 
	// Setting up ORT
	Allocator = MakeUnique<Ort::AllocatorWithDefaultOptions>();
	AllocatorInfo = MakeUnique<Ort::MemoryInfo>(Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU));
	
	// Configure Session
	SessionOptions = MakeUnique<Ort::SessionOptions>();

	// Configure number threads
	SessionOptions->SetIntraOpNumThreads(ORTConfiguration.NumberOfThreads);
	
	// ORT Setting optimizations to the fastest possible
	SessionOptions->SetGraphOptimizationLevel(ORTConfiguration.OptimizationLevel); // ORT_ENABLE_ALL, ORT_ENABLE_EXTENDED, ORT_ENABLE_BASIC, ORT_DISABLE_ALL
	
	return true;
}


bool FMLInferenceModelORT::ConfigureTensors(const bool InIsInput)
{
	const bool bIsInput = InIsInput;

	const uint32 NumberTensors = bIsInput ? Session->GetInputCount() : Session->GetOutputCount();

	TArray<NNX::FMLTensorDesc>& TensorsDescriptors = bIsInput ? InputTensors : OutputTensors;
	TArray<ONNXTensorElementDataType>& TensorsORTType = bIsInput ? InputTensorsORTType : OutputTensorsORTType;
	TArray<const char*>& TensorNames = bIsInput ? InputTensorNames : OutputTensorNames;


	for (uint32 TensorIndex = 0; TensorIndex < NumberTensors; ++TensorIndex)
	{
		NNX::FMLTensorDesc CurrentTensorDescriptor;

		// Get Tensor name
		const char* CurTensorName = bIsInput ? Session->GetInputName(TensorIndex, *Allocator) : Session->GetOutputName(TensorIndex, *Allocator);
		TensorNames.Emplace(CurTensorName);
		CurrentTensorDescriptor.Name = FString(CurTensorName);

		// Get node type
		Ort::TypeInfo CurrentTypeInfo = bIsInput ? Session->GetInputTypeInfo(TensorIndex) : Session->GetOutputTypeInfo(TensorIndex);
		Ort::TensorTypeAndShapeInfo CurrentTensorInfo = CurrentTypeInfo.GetTensorTypeAndShapeInfo();
		const ONNXTensorElementDataType ONNXTensorElementDataTypeEnum = CurrentTensorInfo.GetElementType();
		CurrentTypeInfo.release();

		TensorsORTType.Emplace(ONNXTensorElementDataTypeEnum);

		std::pair<EMLTensorDataType, uint64> TypeAndSize = TranslateTensorTypeORTToNNI(ONNXTensorElementDataTypeEnum);
		CurrentTensorDescriptor.DataType = TypeAndSize.first;
		
		CurrentTensorDescriptor.Dimension = CurrentTensorInfo.GetShape().size();
		uint8 Index = 0;
		uint64 TensorNumberElements = 1;
		for (const int64_t CurrentTensorSize : CurrentTensorInfo.GetShape())
		{
			// Negative (variable) dimensions not implemented yet
			if (CurrentTensorSize < 0)
			{
				CurrentTensorDescriptor.Sizes[Index] = 1;
				UE_LOG(LogNNX, Display,
					TEXT("Negative (i.e., variable) dimensions not allowed yet, hard-coded to 1. Let us know if you really need variable dimensions."
						" Keep in mind that fixed sizes might allow additional optimizations and speedup of the network during Run()."));
			}
			else
			{
				CurrentTensorDescriptor.Sizes[Index] = CurrentTensorSize;
				TensorNumberElements *= CurrentTensorSize;
			}
			++Index;
		}
		
		CurrentTensorDescriptor.DataSize = TensorNumberElements * TypeAndSize.second;
		TensorsDescriptors.Emplace(CurrentTensorDescriptor);
	}

	return true;
}

int FMLInferenceModelORT::Run(TArrayView<const NNX::FMLTensorBinding> InInputBindingTensors, TArrayView<const NNX::FMLTensorBinding> OutOutputBindingTensors)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FMLInferenceModelORT_Run"), STAT_FMLInferenceModelORT_Run, STATGROUP_MachineLearning);

	// Sanity check
	if (!bIsLoaded)
	{
		UE_LOG(LogNNX, Warning, TEXT("FMLInferenceModelORT::Run(): Call FMLInferenceModelORT::Load() to load a model first."));
		return -1;
	}

	FNeuralTimer RunTimer;
	RunTimer.Tic();

	if (!bHasRun)
	{
		bHasRun = true;
	}

#if WITH_EDITOR
	try
#endif //WITH_EDITOR
	{
		TArray<Ort::Value> InputOrtTensors;
		BindTensorsToORT(InInputBindingTensors, InputTensors, InputTensorsORTType, AllocatorInfo.Get(), InputOrtTensors);

		TArray<Ort::Value> OutputOrtTensors;
		BindTensorsToORT(OutOutputBindingTensors, OutputTensors, OutputTensorsORTType, AllocatorInfo.Get(), OutputOrtTensors);

		Session->Run(Ort::RunOptions{ nullptr },
			InputTensorNames.GetData(), &InputOrtTensors[0], InputTensorNames.Num(),
			OutputTensorNames.GetData(), &OutputOrtTensors[0], OutputTensorNames.Num());

	}
#if WITH_EDITOR
	catch (const std::exception& Exception)
	{
		UE_LOG(LogNNX, Error, TEXT("%s"), UTF8_TO_TCHAR(Exception.what()));
	}
#endif //WITH_EDITOR

	RunStatisticsEstimator.StoreSample(RunTimer.Toc());
	return 0;
}

float FMLInferenceModelORT::GetLastRunTimeMSec() const
{
	return RunStatisticsEstimator.GetLastSample();
}

FNeuralStatistics FMLInferenceModelORT::GetRunStatistics() const
{
	return RunStatisticsEstimator.GetStats();
}

FNeuralStatistics FMLInferenceModelORT::GetInputMemoryTransferStats() const
{
	return InputTransferStatisticsEstimator.GetStats();
}

void FMLInferenceModelORT::ResetStats()
{
	RunStatisticsEstimator.ResetStats();
	InputTransferStatisticsEstimator.ResetStats();
}

//-------------
FMLInferenceModelORTCpu::FMLInferenceModelORTCpu(Ort::Env* InORTEnvironment, const FMLInferenceNNXORTConf& InORTConfiguration) : 
	FMLInferenceModelORT(InORTEnvironment, EMLInferenceModelType::CPU, InORTConfiguration)
{}

bool FMLInferenceModelORTCpu::InitializedAndConfigureMembers()
{
	if (!FMLInferenceModelORT::InitializedAndConfigureMembers())
	{
		return false;
	}

	SessionOptions->EnableCpuMemArena();

	return true;
}

#if PLATFORM_WINDOWS
//-------------
FMLInferenceModelORTCuda::FMLInferenceModelORTCuda(Ort::Env* InORTEnvironment, const FMLInferenceNNXORTConf& InORTConfiguration) :
	FMLInferenceModelORT(InORTEnvironment, EMLInferenceModelType::GPU, InORTConfiguration)
{}

bool FMLInferenceModelORTCuda::InitializedAndConfigureMembers()
{
	if (!FMLInferenceModelORT::InitializedAndConfigureMembers())
	{
		return false;
	}

	SessionOptions->EnableCpuMemArena();

	OrtStatusPtr Status = OrtSessionOptionsAppendExecutionProvider_CUDA(*SessionOptions.Get(), ORTConfiguration.DeviceId);
	if (Status)
	{
		UE_LOG(LogNNX, Warning, TEXT("Failed to initialize session options for ORT CUDA EP: %s"), ANSI_TO_TCHAR(Ort::GetApi().GetErrorMessage(Status)));
		return false;
	}

	return true;
}

//-------------

FMLInferenceModelORTDml::FMLInferenceModelORTDml(Ort::Env* InORTEnvironment, const FMLInferenceNNXORTConf& InORTConfiguration) :
	FMLInferenceModelORT(InORTEnvironment, EMLInferenceModelType::GPU, InORTConfiguration)
{}


bool FMLInferenceModelORTDml::InitializedAndConfigureMembers()
{
	if (!FMLInferenceModelORT::InitializedAndConfigureMembers())
	{
		return false;
	}
		
	SessionOptions->DisableCpuMemArena();

	HRESULT res;

	// In order to use DirectML we need D3D12
	ID3D12DynamicRHI* RHI = nullptr;

	if (GDynamicRHI && GDynamicRHI->GetInterfaceType() == ERHIInterfaceType::D3D12)
	{
		RHI = static_cast<ID3D12DynamicRHI*>(GDynamicRHI);

		if (!RHI)
		{
			UE_LOG(LogNNX, Warning, TEXT("Error:%s RHI is not supported by DirectML"), GDynamicRHI->GetName());
			return false;
		}
	}
	else
	{
		if (GDynamicRHI)
		{
			UE_LOG(LogNNX, Warning, TEXT("Error:%s RHI is not supported by DirectML"), GDynamicRHI->GetName());
			return false;
		}
		else
		{
			UE_LOG(LogNNX, Warning, TEXT("Error:No RHI found"));
			return false;
		}
	}

	int DeviceIndex = 0;
	ID3D12Device* D3D12Device = RHI->RHIGetDevice(DeviceIndex);

	DML_CREATE_DEVICE_FLAGS DmlCreateFlags = DML_CREATE_DEVICE_FLAG_NONE;

	// Set debugging flags
	if (RHI->IsD3DDebugEnabled())
	{
		DmlCreateFlags |= DML_CREATE_DEVICE_FLAG_DEBUG;
	}

	IDMLDevice* DmlDevice;

	res = DMLCreateDevice(D3D12Device, DmlCreateFlags, IID_PPV_ARGS(&DmlDevice));
	if (!DmlDevice)
	{
		UE_LOG(LogNNX, Warning, TEXT("Failed to create DML device"));
		return false;
	}

	ID3D12CommandQueue* CmdQ = RHI->RHIGetCommandQueue();

	//OrtSessionOptionsAppendExecutionProvider_DML(*SessionOptions.Get(), ORTConfiguration.DeviceId);
	
	OrtStatusPtr Status = OrtSessionOptionsAppendExecutionProviderEx_DML(*SessionOptions.Get(), DmlDevice, CmdQ);
	if (Status)
	{
		UE_LOG(LogNNX, Warning, TEXT("Failed to initialize session options for ORT Dml EP: %s"), ANSI_TO_TCHAR(Ort::GetApi().GetErrorMessage(Status)));
		return false;
	}

	return true;
}
#endif
