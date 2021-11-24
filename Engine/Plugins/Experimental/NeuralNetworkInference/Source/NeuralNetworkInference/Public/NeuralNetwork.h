// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NeuralEnumClasses.h"
#include "NeuralTensor.h"
#include "NeuralStats.h"
#include "NeuralNetwork.generated.h"

/**
 * Whether UNeuralNetwork::Run() will block the thread until completed (Synchronous), or whether it will run on a background thread,
 * not blocking the calling thread (Asynchronous).
 */
UENUM()
enum class ENeuralNetworkSynchronousMode : uint8
{
	Synchronous, /* UNeuralNetwork::Run() will block the thread until the network evaluation (i.e., forward pass) has finished. */
	/**
	 * UNeuralNetwork::Run() will initialize a forward pass request on a background thread, not blocking the thread that called it.
	 * The user should register to UNeuralNetwork's delegate to know when the forward pass has finished.
	 *
	 * Very important: It takes ~1 millisecond to start the background thread. If your network runs synchronously faster than 1 msec,
	 * using asynchronous running will make the game (main) thread slower than running it synchronously.
	 */
	Asynchronous
};

/**
 * UNeuralNetwork is UE's representation for deep learning and neural network models. It supports the industry standard ONNX model format.
 * All major frameworks (PyTorch, TensorFlow, MXNet, Caffe2, etc.) provide converters to ONNX.
 *
 * See the following examples to learn how to read any ONNX model and run inference (i.e., a forward pass) on it.
 * 1. Constructing a UNeuralNetwork from an ONNX file (Editor-only):
 *		// Create the UNeuralNetwork object
 *		UNeuralNetwork* Network = NewObject<UNeuralNetwork>((UObject*)GetTransientPackage(), UNeuralNetwork::StaticClass());
 *		// Try to load the network and set the device (CPU/GPU)
 *		const FString ONNXModelFilePath = TEXT("SOME_PARENT_FOLDER/SOME_ONNX_FILE_NAME.onnx");
 *		if (Network->Load(ONNXModelFilePath)) 
 *		{
 *			Network->SetDeviceType(ENeuralDeviceType::CPU); // Set to CPU/GPU mode
 *		}
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
	 * It loads the desired network graph definition and weights given an input ONNX file path.
	 * @param InModelFilePath can either be a full path or a relative path with respect to the Game project.
	 * @return Whether the network was successfully loaded.
	 */
	bool Load(const FString& InModelFilePath);

	/**
	 * It loads the desired network graph definition and weights given an input ONNX file path that has been read as a TArray<uint8> buffer.
	 * @see FFileHelper::LoadFileToArray for an example of how to read a file into a TArray<uint8>.
	 * @param InModelReadFromFileInBytes will be moved for performance reasons.
	 * @return Whether the network was successfully loaded.
	 */
	bool Load(TArray<uint8>& InModelReadFromFileInBytes);

	/**
	 * It returns whether a network is currently loaded.
	 */
	bool IsLoaded() const;

	/**
	 * Getter and setter functions for DeviceType, InputDeviceType, and OutputDeviceType.
	 * @see DeviceType, InputDeviceType, and OutputDeviceType for more details.
	 */
	ENeuralDeviceType GetDeviceType() const;
	ENeuralDeviceType GetInputDeviceType() const;
	ENeuralDeviceType GetOutputDeviceType() const;
	void SetDeviceType(const ENeuralDeviceType InDeviceType, const ENeuralDeviceType InInputDeviceType = ENeuralDeviceType::CPU, const ENeuralDeviceType InOutputDeviceType = ENeuralDeviceType::CPU);

	/**
	 * Getter and setter functions for SynchronousMode.
	 * @see SynchronousMode and GetOnAsyncRunCompletedDelegate() for more details.
	 */
	ENeuralNetworkSynchronousMode GetSynchronousMode() const;
	void SetSynchronousMode(const ENeuralNetworkSynchronousMode InSynchronousMode);


	/**
	 * GetOnAsyncRunCompletedDelegate() returns a FOnAsyncRunCompleted delegate that will be called when async UNeuralNetwork::Run() is completed.
	 * This FOnAsyncRunCompleted delegate could be triggered from any thread but will only be triggered if SynchronousMode == ENeuralNetworkSynchronousMode::Asynchronous.
	 * If SynchronousMode == ENeuralNetworkSynchronousMode::Synchronous, UNeuralNetwork::Run() will block the calling thread until completed, so a callback delegate is not required.
	 */
	DECLARE_DELEGATE(FOnAsyncRunCompleted);
	FOnAsyncRunCompleted& GetOnAsyncRunCompletedDelegate();

	/**
	 * Getter and setter functions for BackEnd.
	 * GetBackEnd()/GetBackEndForCurrentPlatform():
	 * - If BackEnd == Auto, GetBackEnd() will return Auto and GetBackEndForCurrentPlatform() will return the actual BackEnd being used for the current platform (UEAndORT or UEOnly).
	 * - If BackEnd != Auto, GetBackEnd() and GetBackEndForCurrentPlatform() will both return the same value (UEAndORT or UEOnly).
	 * SetBackEnd() will modify both BackEnd and BackEndForCurrentPlatform and return IsLoaded(). 
	 * Important! SetBackEnd is NOT THREAD SAFE, please ensure that no other Neural Network function such as Run() is running when SetBackEnd is called. 
	 * @see ENeuralBackEnd for more details.
	 */
	ENeuralBackEnd GetBackEnd() const;
	ENeuralBackEnd GetBackEndForCurrentPlatform() const;
	bool SetBackEnd(const ENeuralBackEnd InBackEnd);
	
	/**
	 * IsGPUCompatible will always return true for ENeuralBackEnd::UEOnly. For ENeuralBackEnd::UEAndORT, it will return:
	 * - True if DX12 is enabled, meaning UEAndORT can run on both the CPU and GPU. Also true if the current platform is not Windows.
	 * - False if DX12 is disabled, meaning UEAndORT can run only run on the CPU. The user will need to enable DX12 to be able to run GPU, switch to CPU, or switch to the UEOnly back end.
	 */
	bool IsGPUSupported() const;

	/**
	 * Functions to get/fill input.
	 * These functions either take a TArray as input, return a modificable void* to fill the data, or return a constant FNeuralTensor(s) to see input properties (e.g., size or dimensions).
	 */
	const FNeuralTensor& GetInputTensor(const int32 InTensorIndex = 0) const;
	void SetInputFromArrayCopy(const TArray<float>& InArray, const int32 InTensorIndex = 0);
	void* GetInputDataPointerMutable(const int32 InTensorIndex = 0);
	int64 GetInputTensorNumber() const;

	/**
	 * Slow functions (as they will copy every input/output FNeuralNetwork) only meant for debugging purposes.
	 */
	TArray<FNeuralTensor> CreateInputArrayCopy() const;
	void SetInputFromArrayCopy(const TArray<FNeuralTensor>& InTensorDataArray);
	TArray<FNeuralTensor> CreateOutputArrayCopy() const;

	/**
	 * Functions to get output. The returned FNeuralTensor(s) are constant to prevent the user from modifying the tensor properties (e.g., size or dimensions).
	 */
	const FNeuralTensor& GetOutputTensor(const int32 InTensorIndex = 0) const;
	int64 GetOutputTensorNumber() const;

	/**
	 * Non-efficient functions meant to be used only for debugging purposes.
	 * - InputTensorsToCPU will send the CPU memory of the desired input tensor(s) to GPU memory. Used to debug InputDeviceType == ENeuralDeviceType::GPU.
	 * - OutputTensorsToCPU will send the GPU memory of the desired output tensor(s) back to CPU memory. Used to debug OutputDeviceType == ENeuralDeviceType::GPU.
	 * @param InTensorIndexes If empty (default value), it will apply to all output tensors.
	 */
	void InputTensorsToGPU(const TArray<int32>& InTensorIndexes = TArray<int32>());
	void OutputTensorsToCPU(const TArray<int32>& InTensorIndexes = TArray<int32>());

	/**
	 * Run() executes the forward pass on the current UNeuralNetwork given the current input FDeprecatedNeuralTensor(s), which were previously filled with
	 * SetInputFromArrayCopy() or GetInputDataPointerMutable().
	 * Its output results can be retrieved with GetOutputTensor() or GetOutputTensors().
	 *
	 * If Run() is called asynchronously, this does not guarantee that calling SetInputFromArrayCopy multiple times will result in each one being applied for a different Run. The user is
	 * responable of not calling SetInputFromArrayCopy until Run() is completed and its delegate (OnAsyncRunCompletedDelegate) called. Otherwise, the wrong results might be returned.
	 */
	void Run();

	/**
	 * Stats functions: 
	 * - GetLastInferenceTime will provide the last inference time measured milliseconds
	 * - GetInferenceStats, returns Inference time statistics. (NumberSamples, Average, StdDev, Min, Max statistics measured in milliseconds)
	 * - GetInputMemoryTransferStats, returns Input Memory Transfer statistics. (NumberSamples, Average, StdDev, Min, Max statistics measured in milliseconds)
	 */
	float GetLastInferenceTime() const;
	FNeuralStatsData GetInferenceStats() const;
	FNeuralStatsData GetInputMemoryTransferStats() const;
	void ResetStats();

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
	 * Whether UNeuralNetwork::Run() will block the thread until completed (Synchronous), or whether it will run on a background thread, not blocking the calling thread (Asynchronous).
	 * @see ENeuralNetworkSynchronousMode for more details.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	ENeuralNetworkSynchronousMode SynchronousMode;
	
	/**
	 * @see ENeuralBackEnd for more details.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	ENeuralBackEnd BackEnd;

	/**
	 * Original model file path from which this UNeuralNetwork was loaded from.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	FString ModelFullFilePath;

private:
	bool bIsLoaded;

	/**
	 * Critical section (mutex) used to avoid issues or crashes due to the asynchronous Run being run at the same time than any other non-const class function.
	 * @see UNeuralNetwork::Run().
	 */
	FCriticalSection ResoucesCriticalSection;

	UPROPERTY()
	TArray<uint8> ModelReadFromFileInBytes;

	/** Whether some of the FNeuralTensor of InputTensor have flexible/variable dimensions. */
	TArray<bool> AreInputTensorSizesVariable;

	/**
	 * If BackEnd != Auto, BackEndForCurrentPlatform will be equal to BackEnd.
	 * Otherwise, BackEndForCurrentPlatform will be set to the optimal BackEnd given the current platform.
	 * @see ENeuralBackEnd for more details.
	 */
	ENeuralBackEnd BackEndForCurrentPlatform;

	/**
	 * @see FOnAsyncRunCompleted and GetOnAsyncRunCompletedDelegate to understand OnAsyncRunCompletedDelegate.
	 */
	FOnAsyncRunCompleted OnAsyncRunCompletedDelegate;

	/**
	 * Struct pointer containing the UE-and-ORT-based back end implementation.
	 * PIMPL idiom to minimize memory when not using this back end and to to hide 3rd party dependencies.
	 * http://www.cppsamples.com/common-tasks/pimpl.html
	 */
	struct FImplBackEndUEAndORT;
	TSharedPtr<FImplBackEndUEAndORT> ImplBackEndUEAndORT;

	/**
	 * Struct pointer containing the only-UE-based back end implementation.
	 * PIMPL idiom to minimize memory when not using this back end.
	 * http://www.cppsamples.com/common-tasks/pimpl.html
	 */
	struct FImplBackEndUEOnly;
	TSharedPtr<FImplBackEndUEOnly> ImplBackEndUEOnly;

	/**
	 * It loads the desired network graph definition and weights internally saved on this UNeuralNetwork instance.
	 * @return Whether the network was successfully loaded.
	 */
	bool Load();

	/**
	 * Private and mutable version of GetInputTensor()/GetOutputTensor().
	 */
	FNeuralTensor& GetInputTensorMutable(const int32 InTensorIndex = 0);
	FNeuralTensor& GetOutputTensorMutable(const int32 InTensorIndex = 0);

	FNeuralStats ComputeStatsModule;
	FNeuralStats InputMemoryTransferStatsModule;

public:
	/**
	 * Internal function not needed by the user.
	 * Used to create custom networks without an ONNX file for QA testing in FOperatorTester::TestOperator().
	 */
	bool Load(TArray<FNeuralTensor>& InTensors, const TArray<FNeuralTensor*>& InInputTensors, const TArray<FNeuralTensor*>& InOutputTensors, const TArray<TSharedPtr<class FNeuralOperator>>& InOperators);

#if WITH_EDITOR
	/**
	 * Internal and Editor-only functions not needed by the user.
	 * Importing data and options used for loading the network.
	 */
	class UAssetImportData* GetAssetImportData() const;
	class UAssetImportData* GetAndMaybeCreateAssetImportData();
#endif // WITH_EDITOR

private:
#if WITH_EDITORONLY_DATA
	/** Importing data and options used for loading the network. */
	UPROPERTY(Instanced)
	class UAssetImportData* AssetImportData;
#endif // WITH_EDITORONLY_DATA

#if WITH_EDITOR
	/** Editor-only function: Re-import asset with editor data (imported file). */
	void ReimportAssetFromEditorData();
#endif // WITH_EDITOR

	//~UObject interface
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
	virtual void Serialize(FArchive& Archive) override;
	//~End of UObject interface
};
