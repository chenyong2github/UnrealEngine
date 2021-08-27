// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NeuralNetwork.h"

//#define WITH_NNI_CPU_NOT_RECOMMENDED // Only for debugging purposes

#include "ThirdPartyWarningDisabler.h"
NNI_THIRD_PARTY_INCLUDES_START
#undef check
#undef TEXT
#ifdef WITH_FULL_NNI_SUPPORT
	#include "RedirectCoutAndCerrToUeLog.h"

	#include "onnxruntime/core/session/onnxruntime_cxx_api.h"
	#ifdef PLATFORM_WIN64
	#include "onnxruntime/core/providers/dml/dml_provider_factory.h"
	#endif
	#ifdef WITH_NNI_CPU_NOT_RECOMMENDED
	#include "onnxruntime/core/providers/nni_cpu/nni_cpu_provider_factory.h"
	#endif //WITH_NNI_CPU_NOT_RECOMMENDED
#endif //WITH_FULL_NNI_SUPPORT
NNI_THIRD_PARTY_INCLUDES_END



struct UNeuralNetwork::FImplBackEndUEAndORT
{
public:
#ifdef WITH_FULL_NNI_SUPPORT
	// Network-related variables
	TUniquePtr<Ort::Env> Environment;
	TUniquePtr<Ort::Session> Session;
	TUniquePtr<Ort::AllocatorWithDefaultOptions> Allocator;
	TUniquePtr<Ort::SessionOptions> SessionOptions;
	// Tensor-related variables
	TUniquePtr<Ort::MemoryInfo> AllocatorInfo; /* Memory allocator information */
	TArray<Ort::Value> InputOrtTensors; /* Actual ONNXRuntime tensors */
	TArray<const char*> InputTensorNames; /* Tensor names */
	TArray<Ort::Value> OutputOrtTensors; /* Actual ONNXRuntime tensors */
	TArray<const char*> OutputTensorNames; /* Tensor names */
#endif //WITH_FULL_NNI_SUPPORT

//#if WITH_EDITOR
//	static bool LoadFile(TSharedPtr<FImplBackEndUEAndORT>& InOutImplBackEndUEAndORT, TArray<FNeuralTensor>& OutInputTensors, TArray<FNeuralTensor>& OutOutputTensors,
//		TArray<bool>& OutAreInputTensorSizesVariable, const TArray<uint8>& InModelReadFromDiskInBytes, const FString& InModelFullFilePath, const ENeuralDeviceType InDeviceType);
//#endif //WITH_EDITOR

	static bool Load(TSharedPtr<FImplBackEndUEAndORT>& InOutImplBackEndUEAndORT, TArray<FNeuralTensor>& OutInputTensors, TArray<FNeuralTensor>& OutOutputTensors,
		TArray<bool>& OutAreInputTensorSizesVariable, const TArray<uint8>& InModelReadFromDiskInBytes, const FString& InModelFullFilePath, const ENeuralDeviceType InDeviceType);
	
	void Run(const ENeuralNetworkSynchronousMode InSynchronousMode, const ENeuralDeviceType InInputDeviceType, const ENeuralDeviceType InOutputDeviceType);

#ifdef WITH_FULL_NNI_SUPPORT
private:
	static bool InitializedAndConfigureMembers(TSharedPtr<FImplBackEndUEAndORT>& InOutImplBackEndUEAndORT, const FString& InModelFullFilePath, const ENeuralDeviceType InDeviceType);

	bool ConfigureMembers(const ENeuralDeviceType InDeviceType);

	void ConfigureTensors(TArray<FNeuralTensor>& OutTensors, TArray<bool>* OutAreInputTensorSizesVariable = nullptr);

	void SetTensorsFromNetwork(TArray<FNeuralTensor>& OutTensors, TArray<const char*>& InTensorNames, TArray<ENeuralDataType>& InTensorDataTypes, TArray<TArray<int64>>& InSizes, const bool bIsInput);

	static void LinkTensorToONNXRuntime(TArray<FNeuralTensor>& InOutTensors, TArray<Ort::Value>& InOutOrtTensors, Ort::MemoryInfo& InOutAllocatorInfo, const int32 InTensorIndex);
#endif //WITH_FULL_NNI_SUPPORT
};
