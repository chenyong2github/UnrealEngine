// Copyright Epic Games, Inc. All Rights Reserved.
#include "NNXRuntimeCPU.h"
#include "NNXRuntimeCPUUtils.h"
#include "NNXModelOptimizer.h"
#include "NNEProfilingTimer.h"
#include "RedirectCoutAndCerrToUeLog.h"

using namespace NNX;

FString FRuntimeCPU::GetRuntimeName() const
{
	return NNX_RUNTIME_CPU_NAME;
}

TUniquePtr<IModelOptimizer> FRuntimeCPU::CreateModelOptimizer() const
{
	return CreateONNXToONNXModelOptimizer();
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
	const TArray<uint8>& ModelBuffer{ InferenceModel->GetFormatDesc().Data };

	// Checking Inference Model 
	{
		if (ModelBuffer.Num() == 0) 
		{
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

		std::pair<EMLTensorDataType, uint64> TypeAndSize = TranslateTensorTypeORTToNNI(ONNXTensorElementDataTypeEnum);

		FSymbolicTensorShape Shape;
		Shape.Data.Reserve(CurrentTensorInfo.GetShape().size());
		for (const int64_t CurrentTensorSize : CurrentTensorInfo.GetShape())
		{
			Shape.Data.Add((int32)CurrentTensorSize);
		}

		FTensorDesc SymbolicTensorDesc = FTensorDesc::Make(FString(CurTensorName), Shape, TypeAndSize.first);
		
		check(SymbolicTensorDesc.GetElemByteSize() == TypeAndSize.second);
		SymbolicTensorDescs.Emplace(SymbolicTensorDesc);
	}

	return true;
}

int FMLInferenceModelCPU::SetInputTensorShapes(TConstArrayView<FTensorShape> InInputShapes)
{
	// Verify input shape are valid for the model and set InputTensorShapes
	if (FMLInferenceModel::SetInputTensorShapes(InInputShapes) != 0)
	{
		return -1;
	}

	// Setup concrete input tensor
	InputTensors.Empty();
	for (int i = 0; i < InputSymbolicTensors.Num(); ++i)
	{
		FTensor Tensor = FTensor::Make(InputSymbolicTensors[i].GetName(), InInputShapes[i], InputSymbolicTensors[i].GetDataType());
		InputTensors.Emplace(Tensor);
	}

	// Here model optimization could be done now that we know the input shapes, for some models
	// this would allow to resolve output shapes here rather than during inference.

	// Setup concrete output shapes only if all model output shapes are concretes, otherwise it will be set during Run()
	OutputTensors.Empty();
	for (FTensorDesc SymbolicTensorDesc : OutputSymbolicTensors)
	{
		if (SymbolicTensorDesc.IsConcrete())
		{
			FTensor Tensor = FTensor::MakeFromSymbolicDesc(SymbolicTensorDesc);
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

int FMLInferenceModelCPU::Run(TConstArrayView<FMLTensorBinding> InInputBindings, TConstArrayView<FMLTensorBinding> InOutputBindings)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FMLInferenceModelCPU_Run"), STAT_FMLInferenceModelCPU_Run, STATGROUP_MachineLearning);

	// Sanity check
	if (!bIsLoaded)
	{
		UE_LOG(LogNNX, Warning, TEXT("FMLInferenceModelCPU::Run(): Call FMLInferenceModelCPU::Load() to load a model first."));
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

float FMLInferenceModelCPU::GetLastRunTimeMSec() const
{
	return RunStatisticsEstimator.GetLastSample();
}

UE::NNEProfiling::Internal::FStatistics FMLInferenceModelCPU::GetRunStatistics() const
{
	return RunStatisticsEstimator.GetStats();
}

UE::NNEProfiling::Internal::FStatistics FMLInferenceModelCPU::GetInputMemoryTransferStats() const
{
	return InputTransferStatisticsEstimator.GetStats();
}

void FMLInferenceModelCPU::ResetStats()
{
	RunStatisticsEstimator.ResetStats();
	InputTransferStatisticsEstimator.ResetStats();
}
