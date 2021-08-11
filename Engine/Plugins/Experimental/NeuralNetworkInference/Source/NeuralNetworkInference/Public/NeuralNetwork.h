// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NeuralEnumClasses.h"
#include "NeuralTensors.h"
#include "NeuralNetwork.generated.h"

class UAssetImportData;

/**
 * First version of NNI, meant to work with any ONNX/ORT model, but only in the Editor in Windows
 */
UCLASS(BlueprintType)
class NEURALNETWORKINFERENCE_API UNeuralNetwork : public UObject
{
	GENERATED_BODY()

public:
	UNeuralNetwork();

	virtual ~UNeuralNetwork();

	/**
	 * Editor-only function
	 * It loads the desired UNeuralNetwork definition and its weights from an ONNX/ORT file.
	 * @param InModelFilePath can either be a full path or a relative path with respect to the Game project.
	 * @return Whether it loaded the UNeuralNetwork successfully.
	 */
#if WITH_EDITOR
	bool Load(const FString& InModelFilePath);
#endif // WITH_EDITOR

	/**
	 * It loads the UNeuralNetwork definition and its weights into ORT.
	 * @return Whether it loaded the UNeuralNetwork successfully.
	 */
	bool Load();

	/**
	 * It returns whether it loaded the UNeuralNetwork successfully.
	 */
	bool IsLoaded() const;

	/**
	 * Getter and setter functions for DeviceType, InputDeviceType, and OutputDeviceType.
	 * See DeviceType, InputDeviceType, and OutputDeviceType for more details.
	 */
	ENeuralDeviceType GetDeviceType() const;
	void SetDeviceType(const ENeuralDeviceType InDeviceType);
	ENeuralDeviceType GetInputDeviceType() const;
	void SetInputDeviceType(const ENeuralDeviceType InInputDeviceType);
	ENeuralDeviceType GetOutputDeviceType() const;
	void SetOutputDeviceType(const ENeuralDeviceType InOutputDeviceType);

	/**
	 * Functions to get/fill input.
	 */
	const FNeuralTensor& GetInputTensor(const int32 InTensorIndex = 0) const;
	const FNeuralTensors& GetInputTensors() const;
	void SetInputFromArrayCopy(const TArray<float>& InArray, const int32 InTensorIndex = 0);
	void* GetInputDataPointerMutable(const int32 InTensorIndex = 0);

	/**
	 * Functions to get output.
	 */
	const FNeuralTensor& GetOutputTensor(const int32 InTensorIndex = 0) const;
	const FNeuralTensors& GetOutputTensors() const;

	/**
	 * Run() executes the forward pass on the current UNeuralNetwork given the current input FDeprecatedNeuralTensor(s), which were previously filled with
	 * SetInputFromTensorCopy() or GetInputDataPointerMutable(), or their multi-input analogs.
	 * Its output results can be retrieved with GetOutputTensor() or GetOutputNameIndexMap().
	 * @param GPUSynchronousMode Whether it should block the thread until the UNeuralNetwork has fully run.
	 */
	void Run();

protected:
	/**
	 * Whether Run() will use CPU or GPU acceleration hardware.
	 * If SetDeviceType() is never called, the default device (EDeviceType::CPU) will be used.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	ENeuralDeviceType DeviceType;
	
	/**
	 * If DeviceType == CPU, InputDeviceType and OutputDeviceType must also be set to CPU.
	 * If DeviceType == GPU:
	 *  - InputDeviceType defines whether Run() will expect the input data in CPU (Run() will upload the memory to the GPU first) or GPU (no upload copy needed) format.
	 *  - OutputDeviceType defines whether Run() will return output data in CPU (Run() will download the memory to the CPU first) or GPU (no download copy needed) format.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	ENeuralDeviceType InputDeviceType;
	
	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	ENeuralDeviceType OutputDeviceType;

	/**
	 * Original model file path from which this UNeuralNetwork was loaded from.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	FString ModelFullFilePath;

	/**
	 * InputTensors and OutputTensors represent the input and output TArray<FNeuralTensors> of the network, respectively.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	FNeuralTensors InputTensors;

	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	FNeuralTensors OutputTensors;

	/** Whether some of the FNeuralTensor of InputTensor have flexible/variable dimensions. */
	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	TArray<bool> AreInputTensorSizesVariable;

#if WITH_EDITORONLY_DATA
	/** Importing data and options used for loading the neural network. */
	UPROPERTY(VisibleAnywhere, Instanced, Category = "ImportSettings")
	UAssetImportData* AssetImportData;
#endif // WITH_EDITORONLY_DATA

private:
	UPROPERTY()
	TArray<uint8> ModelReadFromDiskInBytes;

	bool bIsLoaded;
	/**
	 * PIMPL idiom to hide 3rd party dependencies
	 * http://www.cppsamples.com/common-tasks/pimpl.html
	 */
	struct FImpl;
	TSharedPtr<FImpl> Impl;

public:
	// Internal functions not needed by user
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
};
