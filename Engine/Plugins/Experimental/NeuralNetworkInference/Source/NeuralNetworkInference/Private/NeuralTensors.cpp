// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralTensors.h"
#include "NeuralNetworkInferenceUtils.h"
#include "RedirectCoutAndCerrToUeLog.h"

#ifdef ONNXRUNTIME_USING_DLL_VERSION
#include "ThirdPartyWarningDisabler.h"
NNI_THIRD_PARTY_INCLUDES_START
#undef check
#undef TEXT
#endif
#include <numeric> // std::accumulate
#include "onnxruntime/core/session/onnxruntime_cxx_api.h"
#ifdef ONNXRUNTIME_USING_DLL_VERSION
NNI_THIRD_PARTY_INCLUDES_END
#endif



/* FImpl
 *****************************************************************************/

struct FNeuralTensors::FImpl
{
	/** Memory allocator information */
	Ort::MemoryInfo AllocatorInfo;
	/** Actual ONNXRuntime tensors */
	TArray<Ort::Value> OrtTensors;
	/** Tensor names */
	TArray<const char*> TensorNames;
	/** Constructor */
	FImpl()
		: AllocatorInfo(Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU))
	{
	}
};



/* FNeuralTensors structors
 *****************************************************************************/

FNeuralTensors::FNeuralTensors()
	: bIsLoaded(false)
{
}



/* FNeuralTensors public functions
 *****************************************************************************/

bool FNeuralTensors::IsLoaded() const
{
	return bIsLoaded;
}

const FNeuralTensor& FNeuralTensors::GetTensor(const int32 InTensorIndex) const
{
	return TensorArray[InTensorIndex];
}

bool FNeuralTensors::Load()
{
#if WITH_EDITOR
	try
#endif //WITH_EDITOR
	{
		const FRedirectCoutAndCerrToUeLog RedirectCoutAndCerrToUeLog;
    	Impl = MakeShared<FImpl>();
		ensureMsgf(Impl, TEXT("Impl was not initialized."));
		bIsLoaded = (Impl != nullptr);
	}
#if WITH_EDITOR
	catch (const std::exception& Exception)
	{
		UE_LOG(LogNeuralNetworkInference, Error, TEXT("%s"), UTF8_TO_TCHAR(Exception.what()));
		bIsLoaded = false;
	}
#endif //WITH_EDITOR
	return bIsLoaded;
}

void* FNeuralTensors::GetData(const int32 InTensorIndex)
{
	if (!bIsLoaded && !Load())
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("bIsLoaded is false."));
		return nullptr;
	}

	const ENeuralDataType NeuralDataType = TensorArray[InTensorIndex].GetDataType();
#if WITH_EDITOR
	try
#endif //WITH_EDITOR
	{
		const FRedirectCoutAndCerrToUeLog RedirectCoutAndCerrToUeLog;
		if (NeuralDataType == ENeuralDataType::Float)
		{
			return Impl->OrtTensors[InTensorIndex].GetTensorMutableData<float>();
		}
		//else if (NeuralDataType == ENeuralDataType::Double)
		//{
		//	return Impl->OrtTensors[InTensorIndex].GetTensorMutableData<double>();
		//}
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("Not implemented (yet) for ENeuralDataType = %d."), (int32)NeuralDataType);
		return nullptr;
	}
#if WITH_EDITOR
	catch (const std::exception& Exception)
	{
		UE_LOG(LogNeuralNetworkInference, Error, TEXT("%s"), UTF8_TO_TCHAR(Exception.what()));
		return nullptr;
	}
#endif //WITH_EDITOR
}

int64 FNeuralTensors::GetNumberTensors() const
{
	return TensorArray.Num();
}

const void* const FNeuralTensors::GetData(const int32 InTensorIndex) const
{
	if (!bIsLoaded)
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("bIsLoaded is false."));
		return nullptr;
	}

	const ENeuralDataType NeuralDataType = TensorArray[InTensorIndex].GetDataType();
#if WITH_EDITOR
	try
#endif //WITH_EDITOR
	{
		const FRedirectCoutAndCerrToUeLog RedirectCoutAndCerrToUeLog;
		if (NeuralDataType == ENeuralDataType::Float)
		{
			return Impl->OrtTensors[InTensorIndex].GetTensorMutableData<float>();
		}
		//else if (NeuralDataType == ENeuralDataType::Double)
		//{
		//	return Impl->OrtTensors[InTensorIndex].GetTensorMutableData<double>();
		//}
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("Not implemented (yet) for ENeuralDataType = %d."), (int32)NeuralDataType);
		return nullptr;
	}
#if WITH_EDITOR
	catch (const std::exception& Exception)
	{
		UE_LOG(LogNeuralNetworkInference, Error, TEXT("%s"), UTF8_TO_TCHAR(Exception.what()));
		return nullptr;
	}
#endif //WITH_EDITOR
}

FString FNeuralTensors::GetTensorName(const int32 InTensorIndex) const
{
	return FString(ANSI_TO_TCHAR(Impl->TensorNames[InTensorIndex]));
}

const TArray<int64>& FNeuralTensors::GetSizes(const int32 InTensorIndex) const
{
	return TensorArray[InTensorIndex].GetSizes();
}

ENeuralDataType FNeuralTensors::GetDataType(const int32 InTensorIndex) const
{
	return TensorArray[InTensorIndex].GetDataType();
}

void FNeuralTensors::SetNumUninitialized(const TArray<int64>& InSizes, const ENeuralDataType InDataType, const int32 InTensorIndex)
{
	// Pre-allocate TArray
	TensorArray[InTensorIndex].SetNumUninitialized(InSizes, InDataType);
	// Link tensor with ORT blobl
	LinkTensorToONNXRuntime(InTensorIndex);
}

void FNeuralTensors::SetFromArrayCopy(const TArray<float>& InArray, const int32 InTensorIndex)
{
	// Sanity check: Current size = InArray size
	checkf(TensorArray[InTensorIndex].Num() == InArray.Num(), TEXT("TensorArray[%d].Num() == InArray.Num() failed, %d != %d."), InTensorIndex, TensorArray[InTensorIndex].Num(), InArray.Num());
	// Copy
	TensorArray[InTensorIndex].SetFromArrayCopy<float>(InArray);
}

void* FNeuralTensors::GetDataPointerMutable(const int32 InTensorIndex)
{
	return TensorArray[InTensorIndex].GetData(); // Or Impl->OrtTensors[0].GetTensorMutableData<float>();
}

const char* const* FNeuralTensors::GetTensorNames() const
{
	if (!bIsLoaded)
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("bIsLoaded is false."));
		return nullptr;
	}

	return Impl->TensorNames.GetData();
}

Ort::Value* FNeuralTensors::GetONNXRuntimeTensors()
{
	if (!bIsLoaded && !Load())
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("bIsLoaded is false."));
		return nullptr;
	}

	return &Impl->OrtTensors[0];
}

const Ort::Value* const FNeuralTensors::GetONNXRuntimeTensors() const
{
	if (!bIsLoaded)
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("bIsLoaded is false."));
		return nullptr;
	}

	return &Impl->OrtTensors[0];
}



/* FNeuralTensors private functions
 *****************************************************************************/

void FNeuralTensors::LinkTensorToONNXRuntime(const int32 InTensorIndex)
{
	if (!bIsLoaded && !Load())
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("bIsLoaded is false."));
	}

	const TArray<int64>& Sizes = TensorArray[InTensorIndex].GetSizes();
	if (Sizes.Num() > 0 && TensorArray[InTensorIndex].Num() > 0)
	{
		FNeuralTensor& Tensor = TensorArray[InTensorIndex];
		const int64 Volume = Tensor.Num();
		const int32 ArrayDimensions = Sizes.Num();

		const FRedirectCoutAndCerrToUeLog RedirectCoutAndCerrToUeLog;
#if WITH_EDITOR
		try
#endif //WITH_EDITOR
		{
			const ENeuralDataType NeuralDataType = Tensor.GetDataType();
			if (NeuralDataType == ENeuralDataType::Float)
			{
#ifdef _WIN32
				const TArray<int64_t>& SizesInt64t = Sizes;
#else
				checkf(sizeof(int64) == sizeof(int64_t), TEXT("int64 and int64_t should both have the same size."));
				TArray<int64_t> SizesInt64t;
				SizesInt64t.SetNumUninitialized(ArrayDimensions);
				FMemory::Memcpy(SizesInt64t.GetData(), (int64_t*)Sizes.GetData(), sizeof(int64_t)*ArrayDimensions);
#endif //_WIN32
				Impl->OrtTensors[InTensorIndex] = Ort::Value::CreateTensor<float>(Impl->AllocatorInfo, Tensor.GetDataCasted<float>(), Volume, SizesInt64t.GetData(), ArrayDimensions);
			}
			//else if (NeuralDataType == ENeuralDataType::Double)
			//{
			//	Impl->OrtTensors[InTensorIndex] = Ort::Value::CreateTensor<double>(Impl->AllocatorInfo, Tensor.GetDataCasted<double>(), Volume, Sizes.GetData(), ArrayDimensions);
			//}
			else
			{
				UE_LOG(LogNeuralNetworkInference, Warning, TEXT("Not implemented (yet) for ENeuralDataType = %d."), (int32)NeuralDataType);
			}
		}
#if WITH_EDITOR
		catch (const std::exception& Exception)
		{
			checkf(false, TEXT("Exception on ONNX Runtime: \"%s\"."), UTF8_TO_TCHAR(Exception.what()));
		}
#endif //WITH_EDITOR
	}
}

void FNeuralTensors::SetFromNetwork(TArray<const char*>& InTensorNames, TArray<ENeuralDataType>& InTensorDataTypes,
		TArray<TArray<int64>>& InSizes)
{
	if (!bIsLoaded && !Load())
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("bIsLoaded is false."));
		return;
	}

	const int32 TensorNumber = InTensorNames.Num();
	if (InTensorDataTypes.Num() != TensorNumber || InSizes.Num() != TensorNumber)
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("InTensorNames.Num() == InTensorDataTypes.Num() == InSizes.Num() failed, %d vs. %d vs. %d."),
			InTensorNames.Num(), InTensorDataTypes.Num(), InSizes.Num());
		return;
	}

	// Swap variables
	Swap(Impl->TensorNames, InTensorNames);

	// Pre-allocate each variable
	if (TensorArray.Num() != TensorNumber)
	{
		TensorArray.SetNum(TensorNumber);
	}

	// Config each TensorIndex
	for (int32 TensorIndex = 0; TensorIndex < TensorNumber; ++TensorIndex)
	{
		if (Impl->OrtTensors.Num() <= TensorIndex)
		{
			Impl->OrtTensors.Emplace(Ort::Value(nullptr));
		}
		SetNumUninitialized(InSizes[TensorIndex], InTensorDataTypes[TensorIndex], TensorIndex);
	}
}
