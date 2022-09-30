// Copyright Epic Games, Inc. All Rights Reserved.
#include "NNXRuntimeCPU.h"
#include "NNXRuntimeCPUUtils.h"
#include "NeuralTimer.h"
#include "RedirectCoutAndCerrToUeLog.h"

using namespace NNX;

FString FRuntimeCPU::GetRuntimeName() const
{
	return NNX_RUNTIME_CPU_NAME;
}

EMLRuntimeSupportFlags FRuntimeCPU::GetSupportFlags() const 
{
	return EMLRuntimeSupportFlags::CPU;
}


FMLInferenceModel* FRuntimeCPU::CreateInferenceModel(UMLInferenceModel* InModel, const FMLInferenceNNXCPUConf& InConf)
{
	FMLInferenceModelCPU* CPUModel = new FMLInferenceModelCPU(&NNXEnvironmentCPU, InConf);
	if (!CPUModel->Init(InModel))
	{
		delete CPUModel;
		CPUModel = nullptr;
	}

	return CPUModel;
}


FMLInferenceModel* FRuntimeCPU::CreateInferenceModel(UMLInferenceModel* InModel)
{
	FMLInferenceNNXCPUConf CPUInferenceConf;
	return CreateInferenceModel(InModel, CPUInferenceConf);
}

bool FRuntimeCPU::Init()
{
	return true;
}

FRuntimeCPU::~FRuntimeCPU()
{ }

FMLInferenceModelCPU::FMLInferenceModelCPU() :
	FMLInferenceModel(NNX::EMLInferenceModelType::CPU),
	bIsLoaded(false),
	bHasRun(false) 
{ }

FMLInferenceModelCPU::FMLInferenceModelCPU(
	Ort::Env* InORTEnvironment, 
	const FMLInferenceNNXCPUConf& InNNXCPUConfiguration) :
	FMLInferenceModel(NNX::EMLInferenceModelType::CPU),
	bIsLoaded(false),
	bHasRun(false),
	NNXCPUConf(InNNXCPUConfiguration),
	ORTEnvironment(InORTEnvironment)
{ }

bool FMLInferenceModelCPU::Init(const UMLInferenceModel* InferenceModel)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FMLInferenceModelCPU_Init"), STAT_FMLInferenceModelCPU_Init, STATGROUP_MachineLearning);
	
	// Clean previous networks
	bIsLoaded = false;
	const TArray<uint8>& ModelBuffer{ InferenceModel->GetData() };

	// Checking Inference Model 
	{
		if (ModelBuffer.Num() == 0) {
			UE_LOG(LogNNX, Warning, TEXT("FMLInferenceModelCPU::Load(): Input model path is empty."));
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
			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FMLInferenceModelCPU_Init_CreateORTSession"), STAT_FMLInferenceModelCPU_Init_CreateORTSession, STATGROUP_MachineLearning);
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

bool FMLInferenceModelCPU::IsLoaded() const
{
	return bIsLoaded;
}

bool FMLInferenceModelCPU::InitializedAndConfigureMembers()
{
	// Initialize 
	// Set up ORT and create an environment
	Allocator = MakeUnique<Ort::AllocatorWithDefaultOptions>();
	AllocatorInfo = MakeUnique<Ort::MemoryInfo>(Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU));
	
	// Configure Session
	SessionOptions = MakeUnique<Ort::SessionOptions>();

	// Configure number threads
	//SessionOptions->SetIntraOpNumThreads(NNXCPUConf.NumberOfThreads);
	// Uncomment if you want to change the priority of the threads, by default is TPri_Normal
	//SessionOptions->SetPriorityOpThreads(NNXCPUConf.ThreadPriority);

	// ORT CPU
	SessionOptions->SetGraphOptimizationLevel(NNXCPUConf.OptimizationLevel); // ORT_ENABLE_ALL, ORT_ENABLE_EXTENDED, ORT_ENABLE_BASIC, ORT_DISABLE_ALL
	SessionOptions->EnableCpuMemArena();

	return true;
}


bool FMLInferenceModelCPU::ConfigureTensors(const bool InIsInput)
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


int FMLInferenceModelCPU::Run(TArrayView<const NNX::FMLTensorBinding> InInputBindingTensors, TArrayView<const NNX::FMLTensorBinding> OutOutputBindingTensors)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FMLInferenceModelCPU_Run"), STAT_FMLInferenceModelCPU_Run, STATGROUP_MachineLearning);

	// Sanity check
	if (!bIsLoaded)
	{
		UE_LOG(LogNNX, Warning, TEXT("FMLInferenceModelCPU::Run(): Call FMLInferenceModelCPU::Load() to load a model first."));
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

float FMLInferenceModelCPU::GetLastRunTimeMSec() const
{
	return RunStatisticsEstimator.GetLastSample();
}

FNeuralStatistics FMLInferenceModelCPU::GetRunStatistics() const
{
	return RunStatisticsEstimator.GetStats();
}

FNeuralStatistics FMLInferenceModelCPU::GetInputMemoryTransferStats() const
{
	return InputTransferStatisticsEstimator.GetStats();
}

void FMLInferenceModelCPU::ResetStats()
{
	RunStatisticsEstimator.ResetStats();
	InputTransferStatisticsEstimator.ResetStats();
}
