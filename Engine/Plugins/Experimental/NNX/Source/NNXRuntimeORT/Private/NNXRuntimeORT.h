// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NNXCore.h"
#include "NNXRuntime.h"
#include "NNXInferenceModel.h"
#include "NeuralStatistics.h"
#include "ThirdPartyWarningDisabler.h"

NNI_THIRD_PARTY_INCLUDES_START
#undef check
#undef TEXT

#include "core/session/onnxruntime_cxx_api.h"

#include "core/providers/cpu/cpu_provider_factory.h"

#if PLATFORM_WINDOWS
#include "Windows/MinWindows.h"

#ifdef USE_DML
#include "core/providers/dml/dml_provider_factory.h"
#endif
#endif

NNI_THIRD_PARTY_INCLUDES_END

#define NNX_RUNTIME_ORT_NAME_CPU TEXT("NNXRuntimeORTCpu")
#define NNX_RUNTIME_ORT_NAME_DML TEXT("NNXRuntimeORTDml")
#define NNX_RUNTIME_ORT_NAME_CUDA TEXT("NNXRuntimeORTCuda")

namespace NNX
{
	struct FMLInferenceNNXORTConf
	{
		FMLInferenceNNXORTConf() {};
		FMLInferenceNNXORTConf(
			const uint32 InDeviceId, 
			uint32 InNumberOfThreads = 2, 
			GraphOptimizationLevel InOptimizationLevel = GraphOptimizationLevel::ORT_ENABLE_ALL
		) : DeviceId(InDeviceId), NumberOfThreads(InNumberOfThreads), OptimizationLevel(InOptimizationLevel)
		{
		};

		uint32 DeviceId = 0;
		uint32 NumberOfThreads = 2;
		GraphOptimizationLevel OptimizationLevel = GraphOptimizationLevel::ORT_ENABLE_ALL;
	};

	class FRuntimeORTCpu : public IRuntime
	{
	public:
		Ort::Env NNXEnvironmentORT;
		FRuntimeORTCpu() = default;
		virtual ~FRuntimeORTCpu() = default;
		virtual FString GetRuntimeName() const override;
		virtual EMLRuntimeSupportFlags GetSupportFlags() const override;
		virtual FMLInferenceModel* CreateInferenceModel(UMLInferenceModel* InModel);
		FMLInferenceModel* CreateInferenceModel(UMLInferenceModel* InModel, const FMLInferenceNNXORTConf& InConf);
	};

#if PLATFORM_WINDOWS
	class FRuntimeORTCuda : public IRuntime
	{
	public:
		Ort::Env NNXEnvironmentORT;
		FRuntimeORTCuda() = default;
		virtual ~FRuntimeORTCuda() = default;
		virtual FString GetRuntimeName() const override;
		virtual EMLRuntimeSupportFlags GetSupportFlags() const override;
		virtual FMLInferenceModel* CreateInferenceModel(UMLInferenceModel* InModel);
		FMLInferenceModel* CreateInferenceModel(UMLInferenceModel* InModel, const FMLInferenceNNXORTConf& InConf);
	};

	class FRuntimeORTDml : public IRuntime
	{
	public:
		Ort::Env NNXEnvironmentORT;
		FRuntimeORTDml() = default;
		virtual ~FRuntimeORTDml() = default;
		virtual FString GetRuntimeName() const override;
		virtual EMLRuntimeSupportFlags GetSupportFlags() const override;
		virtual FMLInferenceModel* CreateInferenceModel(UMLInferenceModel* InModel);
		FMLInferenceModel* CreateInferenceModel(UMLInferenceModel* InModel, const FMLInferenceNNXORTConf& InConf);
	};
#endif

	static TUniquePtr<FRuntimeORTCpu> GORTCPURuntime;

#if PLATFORM_WINDOWS
	static TUniquePtr<FRuntimeORTCuda> GORTCUDARuntime;
	static TUniquePtr<FRuntimeORTDml> GORTDMLRuntime;
#endif

	inline TUniquePtr<FRuntimeORTCpu> FRuntimeORTCPUCreate()
	{
		Ort::InitApi();
		TUniquePtr<FRuntimeORTCpu> Runtime = MakeUnique<FRuntimeORTCpu>();

		return Runtime;
	};

#if PLATFORM_WINDOWS
	inline TUniquePtr<FRuntimeORTCuda> FRuntimeORTCUDACreate()
	{
		Ort::InitApi();
		TUniquePtr<FRuntimeORTCuda> Runtime = MakeUnique<FRuntimeORTCuda>();

		return Runtime;
	};

	inline TUniquePtr<FRuntimeORTDml> FRuntimeORTDMLCreate()
	{
		Ort::InitApi();
		TUniquePtr<FRuntimeORTDml> Runtime = MakeUnique<FRuntimeORTDml>();

		return Runtime;
	};
#endif

	inline IRuntime* FRuntimeORTCPUStartup()
	{
		if (!GORTCPURuntime)
		{
			GORTCPURuntime = FRuntimeORTCPUCreate();
		}
		return GORTCPURuntime.Get();
	};

#if PLATFORM_WINDOWS

	inline IRuntime* FRuntimeORTCUDAStartup()
	{
		if (!GORTCUDARuntime)
		{
			GORTCUDARuntime = FRuntimeORTCUDACreate();
		}
		return GORTCUDARuntime.Get();
	};

	inline IRuntime* FRuntimeORTDMLStartup()
	{
		if (!GORTDMLRuntime)
		{
			GORTDMLRuntime = FRuntimeORTDMLCreate();
		}
		return GORTDMLRuntime.Get();
	};
#endif

	inline void FRuntimeORTCPUShutdown()
	{
		if (GORTCPURuntime)
		{
			GORTCPURuntime.Release();
		}
	};

#if PLATFORM_WINDOWS

	inline void FRuntimeORTCUDAShutdown()
	{
		if (GORTCUDARuntime)
		{
			GORTCUDARuntime.Release();
		}
	};

	inline void FRuntimeORTDMLShutdown()
	{
		if (GORTDMLRuntime)
		{
			GORTDMLRuntime.Release();
		}
	};
#endif

	class FMLInferenceModelORT : public FMLInferenceModel
	{

	public:
		virtual ~FMLInferenceModelORT() = default;

		bool Init(const UMLInferenceModel* InferenceModel);
		bool IsLoaded() const;

		virtual int Run(TArrayView<const FMLTensorBinding> InInputBindingTensors, TArrayView<const FMLTensorBinding> OutOutputBindingTensors);

		float GetLastRunTimeMSec() const;
		FNeuralStatistics GetRunStatistics() const;
		FNeuralStatistics GetInputMemoryTransferStats() const;
		void ResetStats();

	protected:
		FMLInferenceModelORT(Ort::Env* InORTEnvironment, EMLInferenceModelType InType, const FMLInferenceNNXORTConf& InORTConfiguration);

		bool bIsLoaded = false;
		bool bHasRun = false;

		/** ORT-related variables */
		Ort::Env* ORTEnvironment;
		FMLInferenceNNXORTConf ORTConfiguration;
		TUniquePtr<Ort::Session> Session;
		TUniquePtr<Ort::AllocatorWithDefaultOptions> Allocator;
		TUniquePtr<Ort::SessionOptions> SessionOptions;
		TUniquePtr<Ort::MemoryInfo> AllocatorInfo;

		/** IO ORT-related variables */
		TArray<ONNXTensorElementDataType> InputTensorsORTType;
		TArray<ONNXTensorElementDataType> OutputTensorsORTType;

		TArray<const char*> InputTensorNames;
		TArray<const char*> OutputTensorNames;

		virtual bool InitializedAndConfigureMembers();
		bool ConfigureTensors(const bool InIsInput);

		/**
		 * Statistics-related members used for GetLastRunTimeMSec(), GetRunStatistics(), GetInputMemoryTransferStats(), ResetStats().
		 */
		FNeuralStatisticsEstimator RunStatisticsEstimator;
		FNeuralStatisticsEstimator InputTransferStatisticsEstimator;
	};

	class FMLInferenceModelORTCpu : public NNX::FMLInferenceModelORT
	{
	public:
		FMLInferenceModelORTCpu(Ort::Env* InORTEnvironment, const FMLInferenceNNXORTConf& InORTConfiguration);

		virtual bool InitializedAndConfigureMembers() override;
	};

#if PLATFORM_WINDOWS
	class FMLInferenceModelORTDml : public NNX::FMLInferenceModelORT
	{
	public:
		FMLInferenceModelORTDml(Ort::Env* InORTEnvironment, const FMLInferenceNNXORTConf& InORTConfiguration);

		virtual bool InitializedAndConfigureMembers() override;
	};

	class FMLInferenceModelORTCuda : public NNX::FMLInferenceModelORT
	{
	public:
		FMLInferenceModelORTCuda(Ort::Env* InORTEnvironment, const FMLInferenceNNXORTConf& InORTConfiguration);

		virtual bool InitializedAndConfigureMembers() override;
	};
#endif


}