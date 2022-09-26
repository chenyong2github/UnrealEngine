// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NNXCore.h"
#include "NNXRuntime.h"
#include "NNXInferenceModel.h"
#include "NeuralStatistics.h"

#include "NNXThirdPartyWarningDisabler.h"
NNX_THIRD_PARTY_INCLUDES_START
#undef check
#undef TEXT
#include "core/session/onnxruntime_cxx_api.h"
NNX_THIRD_PARTY_INCLUDES_END

#define NNX_RUNTIME_CPU_NAME TEXT("NNXRuntimeCPU")

namespace NNX
{
	struct NNXRUNTIMECPU_API FMLInferenceNNXCPUConf
	{
		uint32 NumberOfThreads = 2;
		GraphOptimizationLevel OptimizationLevel = GraphOptimizationLevel::ORT_ENABLE_ALL;
		EThreadPriority ThreadPriority = EThreadPriority::TPri_Normal;
	};

	class NNXRUNTIMECPU_API FRuntimeCPU : public IRuntime
	{
	public:
		Ort::Env NNXEnvironmentCPU;
		FRuntimeCPU() = default;
		bool Init();
		virtual ~FRuntimeCPU();
		virtual FString GetRuntimeName() const;
		virtual EMLRuntimeSupportFlags GetSupportFlags() const;
		virtual FMLInferenceModel* CreateInferenceModel(UMLInferenceModel* Model);
		FMLInferenceModel* CreateInferenceModel(UMLInferenceModel* InModel, const FMLInferenceNNXCPUConf& InConf);
	};

	static TUniquePtr<FRuntimeCPU> GCPURuntime;

	inline TUniquePtr<FRuntimeCPU> FRuntimeCPUCreate()
	{
		TUniquePtr<FRuntimeCPU> Runtime = MakeUnique<FRuntimeCPU>();

		if (!Runtime->Init())
		{
			UE_LOG(LogNNX, Warning, TEXT("Failed to create NNX CPU runtime"));
			Runtime.Release();
		}

		return Runtime;
	};

	inline IRuntime* FRuntimeCPUStartup()
	{
		if (!GCPURuntime)
		{
			GCPURuntime = FRuntimeCPUCreate();
		}
		return GCPURuntime.Get();
	};

	inline void FRuntimeCPUShutdown()
	{
		if (GCPURuntime)
		{
			GCPURuntime.Release();
		}
	};

	class NNXRUNTIMECPU_API FMLInferenceModelCPU : public NNX::FMLInferenceModel
	{

	public:
		FMLInferenceModelCPU();
		FMLInferenceModelCPU(Ort::Env* InORTEnvironment, const FMLInferenceNNXCPUConf& InNNXCPUConfiguration);
		virtual ~FMLInferenceModelCPU() {};

		bool Init(const UMLInferenceModel* InferenceModel);
		bool IsLoaded() const;

		virtual int Run(TArrayView<const NNX::FMLTensorBinding> InInputBindingTensors, TArrayView<const NNX::FMLTensorBinding> OutOutputBindingTensors);

		float GetLastRunTimeMSec() const;
		FNeuralStatistics GetRunStatistics() const;
		FNeuralStatistics GetInputMemoryTransferStats() const;
		void ResetStats();

	protected:

		bool bIsLoaded = false;
		bool bHasRun = false;

		const NNX::EMLInferenceModelType Type = NNX::EMLInferenceModelType::CPU;
		FMLInferenceNNXCPUConf NNXCPUConf;

		/** ORT-related variables */
		Ort::Env* ORTEnvironment;
		TUniquePtr<Ort::Session> Session;
		TUniquePtr<Ort::AllocatorWithDefaultOptions> Allocator;
		TUniquePtr<Ort::SessionOptions> SessionOptions;
		TUniquePtr<Ort::MemoryInfo> AllocatorInfo;

		/** IO ORT-related variables */
		TArray<ONNXTensorElementDataType> InputTensorsORTType;
		TArray<ONNXTensorElementDataType> OutputTensorsORTType;

		TArray<const char*> InputTensorNames;
		TArray<const char*> OutputTensorNames;

		bool InitializedAndConfigureMembers();
		bool ConfigureTensors(const bool InIsInput);

		/**
		 * Statistics-related members used for GetLastRunTimeMSec(), GetRunStatistics(), GetInputMemoryTransferStats(), ResetStats().
		 */
		FNeuralStatisticsEstimator RunStatisticsEstimator;
		FNeuralStatisticsEstimator InputTransferStatisticsEstimator;

	};
}