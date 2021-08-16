// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ModelProto.h"
#include "NeuralEnumClasses.h"
#include "NeuralNetwork.h"
#include "NeuralOperator.h"
#include "NeuralTensor.h"
#include "NeuralTensorManager.h"
#include "NeuralNetworkLegacy.generated.h"

class UAssetImportData;

/**
 * First version of NNI, meant as a proof of concept.
 */
UCLASS(BlueprintType)
class NEURALNETWORKINFERENCE_API UNeuralNetworkLegacy : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Callbacks when the asyn Run() is finished. It will happen from any thread.
	 */
	DECLARE_DELEGATE(FOnAsyncRunCompletedInAnyThread);

	UNeuralNetworkLegacy();

	virtual ~UNeuralNetworkLegacy();

	//~UObject interface
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
	virtual void Serialize(FArchive& Archive) override;
	//~End of UObject interface
#if WITH_EDITOR
	/** Editor-only function: Re-import asset with editor data (imported file). */
	void ReimportAssetFromEditorData();
	/** Editor-only function: Importing data and options used for loading the neural network. */
	UAssetImportData* GetAssetImportData() const;
	UAssetImportData* GetAndMaybeCreateAssetImportData();
#endif // WITH_EDITOR

	/**
	 * Editor-only function
	 * It loads the desired UNeuralNetworkLegacy definition and its weights from an ONNX file.
	 * @return Whether it loaded the UNeuralNetworkLegacy successfully.
	 */
#if WITH_EDITOR
	bool Load(const FString& InFilePath);
#endif // WITH_EDITOR

	/**
	 * It loads the desired UNeuralNetworkLegacy definition and its weights from an deserialized UNeuralNetworkLegacy uasset.
	 * @param InTensorManager It will be moved for performance reasons. Do not use InTensorManager after calling this function.
	 * @return Whether it loaded the UNeuralNetworkLegacy successfully.
	 */
	bool Load(FNeuralTensorManager& InTensorManager, const TArray<TSharedPtr<FNeuralOperator>>& InOperators);

	/**
	 * It returns whether it loaded the UNeuralNetworkLegacy successfully.
	 */
	bool IsLoaded() const;

	/**
	 * Whether Run() will occur on the CPU or GPU.
	 * If SetDeviceType() is never called, the default device (EDeviceType::CPU) will be used.
	 */
	ENeuralDeviceType GetDeviceType() const;
	void SetDeviceType(const ENeuralDeviceType InDeviceType);

	/**
	 * Returns the delegate called when async Run() is over (on any thread)
	 */
	FOnAsyncRunCompletedInAnyThread& GetOnAsyncRunCompletedInAnyThreadDelegate();

	/**
	 * It returns the TArray of FNeuralTensors as a read-only object.
	 */
	const TArray<FNeuralTensor>& GetTensors() const;

	/**
	 * There are 6 alternative functions to fill the input FNeuralTensor(s) data:
	 * If exactly 1 input FNeuralTensor: SetInputFromArrayCopy(), SetInputFromTensorCopy(), GetInputDataPointerMutable().
	 *     - They ensure there is exactly 1 input FNeuralTensor or will log a warning if more than 1 input tensor exists.
	 * If more than 1 input FNeuralTensors: SetInputFromTensorMapCopy(), CreateInputDataPointersMutable().
	 *
	 * - SetInputFromArrayCopy()/SetInputFromTensorCopy()/SetInputFromTensorMapCopy() deeply copy the input FNeuralTensor(s) (slower but safer and less error
	 *   prone). See FNeuralTensor::SetFromArrayCopy() for more details.
	 * - GetInputDataPointerMutable() returns a pointer to the input FNeuralTensor raw data, so it can be filled before calling Run().
	 * - CreateInputDataPointersMutable() returns a TMap of pointers to the input FNeuralTensors raw data, so it can be filled before calling Run().
	 *
	 * @return GetInputDataPointerMutable() returns a T* corresponding to the mutable data in the single input FNeuralTensor, CreateInputDataPointersMutable() returns a
	 * TMap<FString, void*> for each one of the input FNeuralTensors.
	 *
	 * For read-only access to the input FNeuralTensor(s), see GetInputTensor() or GetInputNameIndexMap() (e.g., to extract properties
	 * from the input FNeuralTensor(s) such as volume, dimensions).
	 */
	template<typename T>
	void SetInputFromArrayCopy(const TArray<T>& InArray);
	void SetInputFromTensorCopy(const FNeuralTensor& InTensor);
	void SetInputFromTensorMapCopy(const TMap<FString, FNeuralTensor>& InTensorMap);
	template<typename T>
	T* GetInputDataPointerMutable();
	TMap<FString, void*> CreateInputDataPointersMutable();
	FRDGBufferUAVRef GetInputBufferUAVRef();
	TMap<FString, FRDGBufferUAVRef> CreateInputBufferUAVRefs();

	/**
	 * It returns the input FNeuralTensor (map) as a read-only object.
	 * GetInputTensor() ensures there is only 1 input and returns a single TSharedPtr<FNeuralTensor>, while GetInputNameIndexMap() returns the whole TMap.
	 * In order to modify their input data of the FNeuralTensor (map), use GetInputDataPointerMutable() or CreateInputDataPointersMutable().
	 *
	 * These 2 functions are only meant to extract the input properties (e.g., volume, dimensions), but the actual FNeuralTensor(map) cannot be modified
	 * to avoid undefined behavior in the next Run() of the UNeuralNetworkLegacy due to non-controlled functions (e.g., resizes, memory re-allocation).
	 *
	 * @return GetInputTensor() returns a read-only const FNeuralTensor& corresponding to the single input FNeuralTensor, GetInputNameIndexMap() returns a
	 * TMap<FString, int32> for each one of the input FNeuralTensors. The TMap will go out of scope if UNeuralNetworkLegacy does.
	 */
	const FNeuralTensor& GetInputTensor() const;
	const TMap<FString, int32>& GetInputNameIndexMap() const;

	/**
	 * It returns the output FNeuralTensor (map) as a read-only object.
	 * GetOutputTensor() ensures there is only 1 output and returns a single TSharedPtr<FNeuralTensor>, while GetOutputNameIndexMap() returns the whole TMap.
	 *
	 * These 2 functions are only meant to extract the output properties (e.g., output result, volume, dimensions), but the actual FNeuralTensor(map) cannot
	 * be modified to avoid undefined behavior in the next Run() of the UNeuralNetworkLegacy due to non-controlled functions (e.g., resizes, memory re-allocation).
	 *
	 * @return GetOutputTensor() returns a read-only const FNeuralTensor& corresponding to the single output FNeuralTensor, GetOutputNameIndexMap() returns a
	 * TMap<FString, int32> for each one of the input FNeuralTensors. The TMap will go out of scope if UNeuralNetworkLegacy does.
	 */
	const FNeuralTensor& GetOutputTensor() const;
	const TMap<FString, int32>& GetOutputNameIndexMap() const;
	const FRDGBufferSRVRef GetOutputBufferSRVRef() const;
	TMap<FString, const FRDGBufferSRVRef> CreateOutputBufferSRVRefs() const;

	/**
	 * These functions are slower than the Get ones, because they deep copy each one of the TArrays in the final TMap.
	 */
	TMap<FString, FNeuralTensor> CreateInputTensorMap() const;
	TMap<FString, FNeuralTensor> CreateOutputTensorMap() const;

	/**
	 * Run() executes the forward pass on the current UNeuralNetworkLegacy given the current input FNeuralTensor(s), which were previously filled with
	 * SetInputFromTensorCopy() or GetInputDataPointerMutable(), or their multi-input analogs.
	 * Its output results can be retrieved with GetOutputTensor() or GetOutputNameIndexMap().
	 * @param GPUSynchronousMode Whether it should block the thread until the UNeuralNetworkLegacy has fully run.
	 */
	void Run(const ENeuralNetworkSynchronousMode InSynchronousMode = ENeuralNetworkSynchronousMode::Synchronous, const ENeuralDeviceType InInputDeviceType = ENeuralDeviceType::CPU, const ENeuralDeviceType InOutputDeviceType = ENeuralDeviceType::CPU,
		const bool bRunGPUEmptyOnlyForProfiling = false);

	/**
	 * Auxiliary function that returns the network architecture, weights, tensors, operators, etc.
	 */
	FString ToString() const;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	TArray<int32> Version;
	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	bool bIsLoaded;
	UPROPERTY(EditAnywhere, Category = "Neural Network Inference")
	ENeuralDeviceType DeviceType;
	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	FNeuralTensorManager TensorManager; /* It contains a few TArray and TMaps for all FNeuralTensors (Input, Output, Intermediate(Not)Initialized, Weight) */
	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	FModelProto ModelProto;
#if WITH_EDITORONLY_DATA
	/** Importing data and options used for loading the neural network. */
	UPROPERTY(VisibleAnywhere, Instanced, Category = "ImportSettings")
	UAssetImportData* AssetImportData;
#endif // WITH_EDITORONLY_DATA

private:
	// Non-uproperty members
	bool bAreTensorsInGpu; /* It should always be false when loaded from uasset (FNeuralTensors are not auto-loaded to GPU) */
	/**
	 * Operators represents the set of operators of the network that need to run on the Forward pass and that might need to run on the PostForward.
	 */
	TArray<TSharedPtr<FNeuralOperator>> Operators;
	FOnAsyncRunCompletedInAnyThread OnAsyncRunCompletedInAnyThreadDelegate;
};



/* UNeuralNetworkLegacy template functions
 *****************************************************************************/

template<typename T>
void UNeuralNetworkLegacy::SetInputFromArrayCopy(const TArray<T>& InArray)
{
	return TensorManager.SetInputFromArrayCopy(InArray);
}

template<typename T>
T* UNeuralNetworkLegacy::GetInputDataPointerMutable()
{
	return TensorManager.GetInputDataPointerMutable<T>();
}
