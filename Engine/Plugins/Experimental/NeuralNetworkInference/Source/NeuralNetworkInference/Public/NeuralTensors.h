// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NeuralEnumClasses.h"
#include "NeuralTensor.h"
#include "NeuralTensors.generated.h"

namespace Ort
{
	struct Value;
}

/**
 * FNeuralTensors is a bind of the ONNX runtime Tensor.
 */
USTRUCT()
struct NEURALNETWORKINFERENCE_API FNeuralTensors
{
	GENERATED_BODY()

public:
	FNeuralTensors();

	/**
	 * It initializes the ORT code and auxiliary variables.
	 */
	bool IsLoaded() const;
	bool Load();

	const FNeuralTensor& GetTensor(const int32 InTensorIndex) const;
	// TODO
	// Maybe: Add internal TMap<FString, int32> for direct mapping
	//int32 GetTensorIndex(const FString& InTensorName);
	//FNeuralTensor& GetTensor(const FString& InTensorName);

	/**
	 * Returns the reference to an element at the given index.
	 * @returns Reference to the indexed element.
	 */
	template <typename T, typename TInput>
	T& At(const TInput InIndex, const int32 InTensorIndex = 0);

	/**
	 * Const version of the non-const function At().
	 * @returns Reference to the indexed element.
	 */
	template <typename T, typename TInput>
	const T& At(const TInput InIndex, const int32 InTensorIndex = 0) const;

	void* GetData(const int32 InTensorIndex = 0);

	const void* const GetData(const int32 InTensorIndex = 0) const;

	template<typename T>
	T* GetDataCasted(const int32 InTensorIndex);

	template<typename T>
	const T* const GetDataCasted(const int32 InTensorIndex) const;

	FString GetTensorName(const int32 InTensorIndex = 0) const;

	int64 GetNumberTensors() const;

	const TArray<int64>& GetSizes(const int32 InTensorIndex = 0) const;

	ENeuralDataType GetDataType(const int32 InTensorIndex = 0) const;

	void SetNumUninitialized(const TArray<int64>& InSizes = TArray<int64>(), const ENeuralDataType InDataType = ENeuralDataType::Float, const int32 InTensorIndex = 0);

	/**
	 * 2 different functions to fill the input tensor:
	 * - SetFromArrayCopy (safer): Updates the CPU array with the new data. The size of both (old and new) arrays must match. I.e., GetVolume() must match InArray.Num().
	 * - GetDataPointerMutable (faster): Returns a float* that the user can fill at any moment (more efficient given that it avoids a TArray to TArray copy).
	 */
	void SetFromArrayCopy(const TArray<float>& InArray, const int32 InTensorIndex = 0);
	void* GetDataPointerMutable(const int32 InTensorIndex = 0);

	/**
	 * These functions should only be called by UNeuralNetworkLegacy, not by the user!
	 */
	const char* const* GetTensorNames() const;
	Ort::Value* GetONNXRuntimeTensors();
	const Ort::Value* const GetONNXRuntimeTensors() const;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	TArray<FNeuralTensor> TensorArray;

private:
	bool bIsLoaded;

	// PIMPL idiom
	// http://www.cppsamples.com/common-tasks/pimpl.html
	struct FImpl;
	TSharedPtr<FImpl> Impl;

	/**
	 * Exposes the given FNeuralTensor to ORT.
	 */
	void LinkTensorToONNXRuntime(const int32 InTensorIndex);

public:
	/**
	 * These functions should only be called by UNeuralNetworkLegacy, not by the user!
	 */
	void SetFromNetwork(TArray<const char*>& InTensorNames, TArray<ENeuralDataType>& InTensorDataTypes, TArray<TArray<int64>>& InSizes);
};



/* FNeuralTensor template functions
 *****************************************************************************/

template <typename T, typename TInput>
T& FNeuralTensors::At(const TInput InIndex, const int32 InTensorIndex)
{
	return TensorArray[InTensorIndex].At<T>(InIndex);
}

template <typename T, typename TInput>
const T& FNeuralTensors::At(const TInput InIndex, const int32 InTensorIndex) const
{
	return TensorArray[InTensorIndex].At<T>(InIndex);
}

template<typename T>
T* FNeuralTensors::GetDataCasted(const int32 InTensorIndex)
{
	return TensorArray[InTensorIndex].GetDataCasted<T>();
}

template<typename T>
const T* const FNeuralTensors::GetDataCasted(const int32 InTensorIndex) const
{
	return TensorArray[InTensorIndex].GetDataCasted<T>();
}
