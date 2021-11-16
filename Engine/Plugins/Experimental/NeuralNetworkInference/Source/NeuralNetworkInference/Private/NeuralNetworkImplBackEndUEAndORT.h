// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NeuralNetwork.h"
#include "Async/AsyncWork.h"

// RHI includes must happen before onnxruntime_cxx_api.h (both files include Windows.h)
#include "HAL/CriticalSection.h"
#include "RHI.h"
#include "DynamicRHI.h"

#if defined(WITH_UE_AND_ORT_SUPPORT) && defined(PLATFORM_WIN64)
	// Disable NOMINMAX & WIN32_LEAN_AND_MEAN defines to avoid compiler warnings
#pragma push_macro("NOMINMAX")
#pragma push_macro("WIN32_LEAN_AND_MEAN")
#undef NOMINMAX
#undef WIN32_LEAN_AND_MEAN
#include "D3D12RHIPrivate.h"
#pragma pop_macro("WIN32_LEAN_AND_MEAN")
#pragma pop_macro("NOMINMAX")
#endif

#include "NeuralNetworkAsyncTask.h"

#include "ThirdPartyWarningDisabler.h"
NNI_THIRD_PARTY_INCLUDES_START
#undef check
#undef TEXT
#ifdef WITH_UE_AND_ORT_SUPPORT
#include "core/session/onnxruntime_cxx_api.h"
#ifdef PLATFORM_WIN64
struct OrtDMLProviderOptions;
namespace Ort
{
	class DMLGPUResourceAllocator;
}
#endif
#endif //WITH_UE_AND_ORT_SUPPORT
NNI_THIRD_PARTY_INCLUDES_END




struct UNeuralNetwork::FImplBackEndUEAndORT
{
public:
	/**
	 * InputTensors and OutputTensors represent the input and output TArray<FNeuralTensor> of the network, respectively.
	 */
	TArray<FNeuralTensor> InputTensors;
	TArray<FNeuralTensor> OutputTensors;

	static void WarnAndSetDeviceToCPUIfDX12NotEnabled(ENeuralDeviceType& InOutDeviceType);

	static bool IsGPUConfigCompatible();

	static bool Load(TSharedPtr<FImplBackEndUEAndORT>& InOutImplBackEndUEAndORT, FOnAsyncRunCompleted& InOutOnAsyncRunCompletedDelegate, std::atomic<bool>& bInOutIsBackgroundThreadRunning,
		FCriticalSection& InOutResoucesCriticalSection, TArray<bool>& OutAreInputTensorSizesVariable, const TArray<uint8>& InModelReadFromFileInBytes, const FString& InModelFullFilePath,
		const ENeuralDeviceType InDeviceType, const ENeuralDeviceType InInputDeviceType, const ENeuralDeviceType InOutputDeviceType);
	
	void Run(FOnAsyncRunCompleted& InOutOnAsyncRunCompletedDelegate, std::atomic<bool>& bInIsBackgroundThreadRunning , FCriticalSection& InResoucesCriticalSection,
		const ENeuralNetworkSynchronousMode InSynchronousMode, const ENeuralDeviceType InDeviceType, const ENeuralDeviceType InInputDeviceType, const ENeuralDeviceType InOutputDeviceType);

#ifdef WITH_UE_AND_ORT_SUPPORT
private:
	/** Network-related variables */
	TUniquePtr<Ort::Env> Environment;
	TUniquePtr<Ort::Session> Session;
	TUniquePtr<Ort::AllocatorWithDefaultOptions> Allocator;
	TUniquePtr<Ort::SessionOptions> SessionOptions;
	/** Tensor-related variables */
	TUniquePtr<Ort::MemoryInfo> AllocatorInfo; /* Memory allocator information */
#ifdef PLATFORM_WIN64
	TUniquePtr<OrtDMLProviderOptions> DmlProviderOptions; /* DirectML execution provider options */
	TUniquePtr<Ort::DMLGPUResourceAllocator> DmlGPUAllocator; /* DirectML GPU allocator */
	TArray<void*> DmlGPUResources; /* Shared D3D12 resources with DirectML GPU execution provider */
#endif
	TArray<Ort::Value> InputOrtTensors; /* Actual ONNXRuntime tensors */
	TArray<const char*> InputTensorNames; /* Tensor names */
	TArray<Ort::Value> OutputOrtTensors; /* Actual ONNXRuntime tensors */
	TArray<const char*> OutputTensorNames; /* Tensor names */

	TUniquePtr<FAsyncTask<FNeuralNetworkAsyncTask>> NeuralNetworkAsyncTask;

	void IsAsyncTaskDone() const;

	static bool InitializedAndConfigureMembers(TSharedPtr<FImplBackEndUEAndORT>& InOutImplBackEndUEAndORT, const FString& InModelFullFilePath, const ENeuralDeviceType InDeviceType);

	bool ConfigureMembers(const ENeuralDeviceType InDeviceType);

	bool ConfigureTensors(TArray<FNeuralTensor>& OutTensors, TArray<bool>* OutAreInputTensorSizesVariable, const ENeuralDeviceType InInputDeviceType, const ENeuralDeviceType InOutputDeviceType);

	bool SetTensorsFromNetwork(TArray<FNeuralTensor>& OutTensors, TArray<const char*>& InTensorNames, TArray<ENeuralDataType>& InTensorDataTypes, TArray<TArray<int64>>& InSizes, TArray<ENeuralTensorTypeGPU>& InTensorGPUTypes, const bool bIsInput);

	static void LinkTensorToONNXRuntime(TArray<FNeuralTensor>& InOutTensors, TArray<Ort::Value>& InOutOrtTensors, Ort::MemoryInfo& InOutAllocatorInfo, const int32 InTensorIndex);

#ifdef PLATFORM_WIN64
	bool LinkTensorResourceToONNXRuntime(FNeuralTensor& InOutTensor, Ort::Value& InOutOrtTensor, void* D3DResource);
#endif

	void ClearResources();

#endif //WITH_UE_AND_ORT_SUPPORT
};
