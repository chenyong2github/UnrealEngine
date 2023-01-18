// Copyright Epic Games, Inc. All Rights Reserved.
#include "NNXRuntimeORT.h"

#include "CoreGlobals.h"
#include "Misc/ConfigCacheIni.h"

#include "NNEProfilingTimer.h"
#include "NNXRuntimeORTUtils.h"
#include "NNEUtilsModelOptimizer.h"
#include "NNECoreAttributeMap.h"
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


FGuid FRuntimeORT::GUID = FGuid((int32)'O', (int32)'N', (int32)'N', (int32)'X');
int32 FRuntimeORT::Version	= 0x00000001;

bool FRuntimeORT::CanCreateModelData(FString FileType, TConstArrayView<uint8> FileData) const
{
	return FileType.Compare("onnx", ESearchCase::IgnoreCase) == 0;
}

TArray<uint8> FRuntimeORT::CreateModelData(FString FileType, TConstArrayView<uint8> FileData)
{
	if (!CanCreateModelData(FileType, FileData))
	{
		return {};
	}

	TUniquePtr<IModelOptimizer> Optimizer = UE::NNEUtils::Internal::CreateONNXToONNXModelOptimizer();

	FNNIModelRaw InputModel;
	InputModel.Data = FileData;
	InputModel.Format = ENNXInferenceFormat::ONNX;
	FNNIModelRaw OutputModel;
	FOptimizerOptionsMap Options;
	if (!Optimizer->Optimize(InputModel, OutputModel, Options))
	{
		return {};
	}

	int32 GuidSize = sizeof(FRuntimeORT::GUID);
	int32 VersionSize = sizeof(FRuntimeORT::Version);
	TArray<uint8> Result;
	FMemoryWriter Writer(Result);
	Writer << FRuntimeORT::GUID;
	Writer << FRuntimeORT::Version;
	Writer.Serialize(OutputModel.Data.GetData(), OutputModel.Data.Num());
	return Result;
}


bool FRuntimeORT::CanCreateModel(TConstArrayView<uint8> ModelData) const
{
	int32 GuidSize = sizeof(FRuntimeORT::GUID);
	int32 VersionSize = sizeof(FRuntimeORT::Version);
	if (ModelData.Num() <= GuidSize + VersionSize)
	{
		return false;
	}
	bool bResult = FGenericPlatformMemory::Memcmp(&(ModelData[0]), &(FRuntimeORT::GUID), GuidSize) == 0;
	bResult &= FGenericPlatformMemory::Memcmp(&(ModelData[GuidSize]), &(FRuntimeORT::Version), VersionSize) == 0;
	return bResult;
}

TUniquePtr<FMLInferenceModel> FRuntimeORTCpu::CreateModel(TConstArrayView<uint8> ModelData)
{
	if (!CanCreateModel(ModelData))
	{
		return TUniquePtr<FMLInferenceModel>();
	}
	const FMLInferenceNNXORTConf InConf;
	FMLInferenceModelORTCpu* Model = new FMLInferenceModelORTCpu(&NNXEnvironmentORT, InConf);
	if (!Model->Init(ModelData))
	{
		delete Model;
		return TUniquePtr<FMLInferenceModel>();
	}
	return TUniquePtr<FMLInferenceModel>(Model);
}

#if PLATFORM_WINDOWS
TUniquePtr<FMLInferenceModel> FRuntimeORTCuda::CreateModel(TConstArrayView<uint8> ModelData)
{
	if (!CanCreateModel(ModelData))
	{
		return TUniquePtr<FMLInferenceModel>();
	}
	const FMLInferenceNNXORTConf InConf;
	FMLInferenceModelORTCuda* Model = new FMLInferenceModelORTCuda(&NNXEnvironmentORT, InConf);
	if (!Model->Init(ModelData))
	{
		delete Model;
		return TUniquePtr<FMLInferenceModel>();
	}
	return TUniquePtr<FMLInferenceModel>(Model);
}

TUniquePtr<FMLInferenceModel> FRuntimeORTDml::CreateModel(TConstArrayView<uint8> ModelData)
{
	if (!CanCreateModel(ModelData))
	{
		return TUniquePtr<FMLInferenceModel>();
	}
	const FMLInferenceNNXORTConf InConf;
	FMLInferenceModelORTDml* Model = new FMLInferenceModelORTDml(&NNXEnvironmentORT, InConf);
	if (!Model->Init(ModelData))
	{
		delete Model;
		return TUniquePtr<FMLInferenceModel>();
	}
	return TUniquePtr<FMLInferenceModel>(Model);
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

bool FMLInferenceModelORT::Init(TConstArrayView<uint8> ModelData)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FMLInferenceModelORT_Init"), STAT_FMLInferenceModelORT_Init, STATGROUP_MachineLearning);

	// Get the header size
	int32 GuidSize = sizeof(FRuntimeORT::GUID);
	int32 VersionSize = sizeof(FRuntimeORT::Version);

	// Clean previous networks
	bIsLoaded = false;
	TConstArrayView<uint8> ModelBuffer = TConstArrayView<uint8>(&(ModelData.GetData()[GuidSize + VersionSize]), ModelData.Num() - GuidSize - VersionSize);

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

	TArray<FTensorDesc>& SymbolicTensorDescs = bIsInput ? InputSymbolicTensors : OutputSymbolicTensors;
	TArray<ONNXTensorElementDataType>& TensorsORTType = bIsInput ? InputTensorsORTType : OutputTensorsORTType;
	TArray<const char*>& TensorNames = bIsInput ? InputTensorNames : OutputTensorNames;

	for (uint32 TensorIndex = 0; TensorIndex < NumberTensors; ++TensorIndex)
	{
		// Get Tensor name
		const char* CurTensorName = bIsInput ? Session->GetInputName(TensorIndex, *Allocator) : Session->GetOutputName(TensorIndex, *Allocator);
		TensorNames.Emplace(CurTensorName);

		// Get node type
		Ort::TypeInfo CurrentTypeInfo = bIsInput ? Session->GetInputTypeInfo(TensorIndex) : Session->GetOutputTypeInfo(TensorIndex);
		Ort::TensorTypeAndShapeInfo CurrentTensorInfo = CurrentTypeInfo.GetTensorTypeAndShapeInfo();
		const ONNXTensorElementDataType ONNXTensorElementDataTypeEnum = CurrentTensorInfo.GetElementType();
		CurrentTypeInfo.release();

		TensorsORTType.Emplace(ONNXTensorElementDataTypeEnum);

		std::pair<ENNETensorDataType, uint64> TypeAndSize = TranslateTensorTypeORTToNNI(ONNXTensorElementDataTypeEnum);
		
		
		TArray<int32> ShapeData;
		ShapeData.Reserve(CurrentTensorInfo.GetShape().size());
		for (const int64_t CurrentTensorSize : CurrentTensorInfo.GetShape())
		{
			ShapeData.Add((int32)CurrentTensorSize);
		}

		UE::NNECore::FSymbolicTensorShape Shape = UE::NNECore::FSymbolicTensorShape::Make(ShapeData);
		UE::NNECore::FTensorDesc SymbolicTensorDesc = UE::NNECore::FTensorDesc::Make(FString(CurTensorName), Shape, TypeAndSize.first);
		
		check(SymbolicTensorDesc.GetElemByteSize() == TypeAndSize.second);
		SymbolicTensorDescs.Emplace(SymbolicTensorDesc);
	}

	return true;
}

int FMLInferenceModelORT::SetInputTensorShapes(TConstArrayView<FTensorShape> InInputShapes)
{
	InputTensors.Empty();
	OutputTensors.Empty();
	OutputTensorShapes.Empty();
	
	// Verify input shape are valid for the model and set InputTensorShapes
	if (FMLInferenceModel::SetInputTensorShapes(InInputShapes) != 0)
	{
		return -1;
	}

	// Setup concrete input tensor
	for (int i = 0; i < InputSymbolicTensors.Num(); ++i)
	{
		UE::NNECore::Internal::FTensor Tensor = UE::NNECore::Internal::FTensor::Make(InputSymbolicTensors[i].GetName(), InInputShapes[i], InputSymbolicTensors[i].GetDataType());
		InputTensors.Emplace(Tensor);
	}

	// Here model optimization could be done now that we know the input shapes, for some models
	// this would allow to resolve output shapes here rather than during inference.

	// Setup concrete output shapes only if all model output shapes are concretes, otherwise it will be set during Run()
	for (FTensorDesc SymbolicTensorDesc : OutputSymbolicTensors)
	{
		if (SymbolicTensorDesc.GetShape().IsConcrete())
		{
			UE::NNECore::Internal::FTensor Tensor = UE::NNECore::Internal::FTensor::MakeFromSymbolicDesc(SymbolicTensorDesc);
			OutputTensors.Emplace(Tensor);
			OutputTensorShapes.Emplace(Tensor.GetShape());
		}
	}
	if (OutputTensors.Num() != OutputSymbolicTensors.Num())
	{
		OutputTensors.Empty();
		OutputTensorShapes.Empty();
	}

	return 0;
}

int FMLInferenceModelORT::RunSync(TConstArrayView<FMLTensorBinding> InInputBindings, TConstArrayView<FMLTensorBinding> InOutputBindings)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FMLInferenceModelORT_Run"), STAT_FMLInferenceModelORT_Run, STATGROUP_MachineLearning);

	// Sanity check
	if (!bIsLoaded)
	{
		UE_LOG(LogNNX, Warning, TEXT("FMLInferenceModelORT::Run(): Call FMLInferenceModelORT::Load() to load a model first."));
		return -1;
	}

	// Verify the model inputs were prepared
	if (InputTensorShapes.Num() == 0)
	{
		UE_LOG(LogNNX, Error, TEXT("Run(): Input shapes are not set, please call SetInputTensorShapes."));
		return -1;
	}

	UE::NNEProfiling::Internal::FTimer RunTimer;
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
		BindTensorsToORT(InInputBindings, InputTensors, InputTensorsORTType, AllocatorInfo.Get(), InputOrtTensors);

		if (!OutputTensors.IsEmpty())
		{
			// If output shapes are known we can directly map preallocated output buffers
			TArray<Ort::Value> OutputOrtTensors;
			BindTensorsToORT(InOutputBindings, OutputTensors, OutputTensorsORTType, AllocatorInfo.Get(), OutputOrtTensors);

			Session->Run(Ort::RunOptions{ nullptr },
				InputTensorNames.GetData(), &InputOrtTensors[0], InputTensorNames.Num(),
				OutputTensorNames.GetData(), &OutputOrtTensors[0], OutputTensorNames.Num());
		}
		else
		{
			TArray<Ort::Value> OutputOrtTensors;
			for (int i = 0; i < InOutputBindings.Num(); ++i)
			{
				OutputOrtTensors.Emplace(nullptr);
			}

			Session->Run(Ort::RunOptions{ nullptr },
				InputTensorNames.GetData(), &InputOrtTensors[0], InputTensorNames.Num(),
				OutputTensorNames.GetData(), &OutputOrtTensors[0], OutputTensorNames.Num());

			// Output shapes were resolved during inference: Copy the data back to bindings and expose output tensor shapes
			CopyFromORTToBindings(OutputOrtTensors, InOutputBindings, OutputSymbolicTensors, OutputTensors);
			check(OutputTensorShapes.IsEmpty());
			for (int i = 0; i < OutputTensors.Num(); ++i)
			{
				OutputTensorShapes.Emplace(OutputTensors[i].GetShape());
			}
		}
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

UE::NNEProfiling::Internal::FStatistics FMLInferenceModelORT::GetRunStatistics() const
{
	return RunStatisticsEstimator.GetStats();
}

UE::NNEProfiling::Internal::FStatistics FMLInferenceModelORT::GetInputMemoryTransferStats() const
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

IRuntime* NNX::FRuntimeORTDMLStartup()
{
	if (!GORTDMLRuntime)
	{
		// In order to register DirectML we need D3D12
		bool	bHasD3D12Config = false;
		bool	bHasD3D12RHI = false;
		FString DefaultGraphicsRHI;

		if (GConfig->GetString(TEXT("/Script/WindowsTargetPlatform.WindowsTargetSettings"), TEXT("DefaultGraphicsRHI"), DefaultGraphicsRHI, GEngineIni))
		{
			bHasD3D12Config = DefaultGraphicsRHI == TEXT("DefaultGraphicsRHI_DX12");
		}

		// We need to check if RHI is forced to D3D12
		if (GDynamicRHI && GDynamicRHI->GetInterfaceType() == ERHIInterfaceType::D3D12)
		{
			bHasD3D12RHI = true;
		}

		if (bHasD3D12Config && bHasD3D12RHI)
		{
			GORTDMLRuntime = FRuntimeORTDMLCreate();
		}
		else
		{
			return nullptr;
		}
	}

	return GORTDMLRuntime.Get();
};

#endif
