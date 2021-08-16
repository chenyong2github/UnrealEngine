// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NeuralEnumClasses.h"
#include "NeuralTensors.h"
#include "NeuralNetwork.generated.h"

class UAssetImportData;

/**
 * Whether Run() will block the thread until completed, or whether it will run on a background thread, not blocking the calling thread.
 */
UENUM()
enum class ENeuralNetworkSynchronousMode : uint8
{
	Synchronous, /* UNeuralNetwork::Run() will block the thread until the network evaluation (i.e., forward pass) has finished. */
	Asynchronous /* UNeuralNetwork::Run() will initialize a forward pass request on a background thread, not blocking the thread that called it. The user should register to UNeuralNetwork's delegate to know when the forward pass has finished. */
};

/**
 * UNeuralNetwork is UE's representation for deep learning and neural network models. It supports the industry standard ONNX model format.
 * All major frameworks (PyTorch, TensorFlow, MXNet, Caffe2, etc.) provide converters to ONNX.
 *
 * See the following examples to learn how to read any ONNX model and run inference (i.e., a forward pass) on it.
 * 1. Constructing a UNeuralNetwork from an ONNX file (Editor-only):
 *	#if WITH_EDITOR
 *		// Create the UNeuralNetwork object
 *		UNeuralNetwork* Network = NewObject<UNeuralNetwork>((UObject*)GetTransientPackage(), UNeuralNetwork::StaticClass());
 *		// Try to load the network and set the device (CPU/GPU)
 *		const FString ONNXModelFilePath = TEXT("SOME_PARENT_FOLDER/SOME_ONNX_FILE_NAME.onnx");
 *		if (Network->Load(ONNXModelFilePath)) 
 *		{
 *			Network->SetDeviceType(ENeuralDeviceType::CPU); // Set to CPU/GPU mode
 *		}
 *	#endif
 *
 * 2. Loading a UNeuralNetwork from a previously-created UAsset (in Editor or in Game):
 *		// Create and load the UNeuralNetwork object from a UAsset
 *		const FString NetworkUAssetFilePath = TEXT("ExampleNetwork'/Game/Models/ExampleNetwork/ExampleNetwork.ExampleNetwork'");
 *		UNeuralNetwork* Network = LoadObject<UNeuralNetwork>((UObject*)GetTransientPackage(), *NetworkUAssetFilePath);
 *		// Check that the network was successfully loaded
 *		check(Network->IsLoaded());
 *
 * 3.1. Running inference (i.e., a forward pass):
 *		// Fill input neural tensor
 *		TArray<float> InArray;
 *		Network->SetInputFromArrayCopy(InArray);
 *		UE_LOG(LogNeuralNetworkInference, Display, TEXT("Input tensor: %s."), *Network->GetInputTensor().ToString());
 *		// Run UNeuralNetwork
 *		Network->Run();
 *		// Read and print OutputTensor
 *		const FNeuralTensor& OutputTensor = Network->GetOutputTensor();
 *		UE_LOG(LogNeuralNetworkInference, Display, TEXT("Output tensor: %s."), *OutputTensor.ToString());
 *
 * 3.2. Alternative - Filling the input tensor without a TArray-to-FNeuralTensor copy:
 *		// Obtain input tensor pointer
 *		float* InputDataPointer = Network->GetInputDataPointerMutable<float>();
 *		// Fill InputDataPointer
 *		for (int64 Index = 0; Index < Network->GetInputTensor().Num(); ++Index)
 *			InputDataPointer[Index] = ...;
 *
 * 3.3. Alternative - Networks with multiple input/output tensors:
 * - Multiple inputs: Add InTensorIndex to GetInputTensor(InTensorIndex) or GetInputDataPointerMutable(InTensorIndex) in the examples above, or use GetInputTensors() instead.
 * - Multiple outputs: Add InTensorIndex to GetOutputTensor(InTensorIndex) in the examples above or use GetOutputTensors() instead.
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
	 * SetInputFromArrayCopy() or GetInputDataPointerMutable().
	 * Its output results can be retrieved with GetOutputTensor() or GetOutputTensors().
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
