// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NNXCore.h"
#include "NNXRuntime.h"
#include "NNXInferenceModel.h"
#include "NNXRuntimeORTProviders.h"
#include "NeuralStatistics.h"

#include "ThirdPartyWarningDisabler.h"
NNI_THIRD_PARTY_INCLUDES_START
#undef check
#undef TEXT
#include "core/session/onnxruntime_cxx_api.h"
NNI_THIRD_PARTY_INCLUDES_END

#define NNX_RUNTIME_ORT_NAME_CPU L"NNXRuntimeORTCpu"
#define NNX_RUNTIME_ORT_NAME_DML L"NNXRuntimeORTDml"
#define NNX_RUNTIME_ORT_NAME_CUDA L"NNXRuntimeORTCuda"

namespace NNX
{
	struct NNXRUNTIMEORT_API FMLInferenceNNXORTConf
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

	static TUniquePtr<FRuntimeORTCpu> GORTCPURuntime;
	static TUniquePtr<FRuntimeORTCuda> GORTCUDARuntime;
	static TUniquePtr<FRuntimeORTDml> GORTDMLRuntime;

	inline TUniquePtr<FRuntimeORTCpu> FRuntimeORTCPUCreate()
	{
		Ort::InitApi();
		TUniquePtr<FRuntimeORTCpu> Runtime = MakeUnique<FRuntimeORTCpu>();

		return Runtime;
	};
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

	inline IRuntime* FRuntimeORTCPUStartup()
	{
		if (!GORTCPURuntime)
		{
			GORTCPURuntime = FRuntimeORTCPUCreate();
		}
		return GORTCPURuntime.Get();
	};
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

	inline void FRuntimeORTCPUShutdown()
	{
		if (GORTCPURuntime)
		{
			GORTCPURuntime.Release();
		}
	};
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

	class NNXRUNTIMEORT_API FMLInferenceModelORT : public FMLInferenceModel
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

	class NNXRUNTIMEORT_API FMLInferenceModelORTCpu : public NNX::FMLInferenceModelORT
	{
	public:
		FMLInferenceModelORTCpu(Ort::Env* InORTEnvironment, const FMLInferenceNNXORTConf& InORTConfiguration);

		virtual bool InitializedAndConfigureMembers() override;
	};

	class NNXRUNTIMEORT_API FMLInferenceModelORTDml : public NNX::FMLInferenceModelORT
	{
	public:
		FMLInferenceModelORTDml(Ort::Env* InORTEnvironment, const FMLInferenceNNXORTConf& InORTConfiguration);

		virtual bool InitializedAndConfigureMembers() override;
	};

	class NNXRUNTIMEORT_API FMLInferenceModelORTCuda : public NNX::FMLInferenceModelORT
	{
	public:
		FMLInferenceModelORTCuda(Ort::Env* InORTEnvironment, const FMLInferenceNNXORTConf& InORTConfiguration);

		virtual bool InitializedAndConfigureMembers() override;
	};

}