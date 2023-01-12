// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NNXCore.h"
#include "NNECoreTensor.h"
#include "NNECoreTypes.h"
#include "NNEProfilingStatistics.h"
#include "NNXRuntime.h"
#include "NNXInferenceModel.h"
#include "NNXModelOptimizerInterface.h"

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
		virtual bool CanCreateModelData(FString FileType, TConstArrayView<uint8> FileData) const;
		virtual TArray<uint8> CreateModelData(FString FileType, TConstArrayView<uint8> FileData);
		virtual bool CanCreateModel(TConstArrayView<uint8> ModelData) const;
		virtual TUniquePtr<FMLInferenceModel> CreateModel(TConstArrayView<uint8> ModelData);
		static FGuid GUID;
		static int32 Version;
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

		bool Init(TConstArrayView<uint8> ModelData);
		bool IsLoaded() const;

		virtual int SetInputTensorShapes(TConstArrayView<FTensorShape> InInputShapes) override;
		virtual int RunSync(TConstArrayView<FMLTensorBinding> InInputBindings, TConstArrayView<FMLTensorBinding> InOutputBindings) override;

		float GetLastRunTimeMSec() const;
		UE::NNEProfiling::Internal::FStatistics GetRunStatistics() const;
		UE::NNEProfiling::Internal::FStatistics GetInputMemoryTransferStats() const;
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

		TArray<UE::NNECore::Internal::FTensor> InputTensors;
		TArray<UE::NNECore::Internal::FTensor> OutputTensors;

		bool InitializedAndConfigureMembers();
		bool ConfigureTensors(const bool InIsInput);

		/**
		 * Statistics-related members used for GetLastRunTimeMSec(), GetRunStatistics(), GetInputMemoryTransferStats(), ResetStats().
		 */
		UE::NNEProfiling::Internal::FStatisticsEstimator RunStatisticsEstimator;
		UE::NNEProfiling::Internal::FStatisticsEstimator InputTransferStatisticsEstimator;

	};
}