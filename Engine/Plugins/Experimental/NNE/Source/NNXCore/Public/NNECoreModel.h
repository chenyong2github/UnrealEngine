// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "NNECoreRuntimeCPU.h"
#include "NNECoreModelData.h"

#include "NNECoreModel.generated.h"

UENUM(BlueprintType, Category = "NNE - Neural Network Engine")
enum FNNETaskPriority
{
	Low     UMETA(DisplayName = "Low"),
	Normal  UMETA(DisplayName = "Normal"),
	High	UMETA(DisplayName = "High"),
};

USTRUCT(BlueprintType, Category = "NNE - Neural Network Engine")
struct FNNETensor
{
	GENERATED_BODY()

public:

	UPROPERTY(BlueprintReadWrite, Category = "NNE - Neural Network Engine")
	TArray<int32> Shape = TArray<int32>();

	UPROPERTY(BlueprintReadWrite, Category = "NNE - Neural Network Engine")
	TArray<float> Data = TArray<float>();
};

DECLARE_DYNAMIC_DELEGATE_TwoParams(FNNEModelOnAsyncResult, const TArray<FNNETensor>&, Output, bool, Success);

UCLASS(BlueprintType, Category = "NNE - Neural Network Engine")
class NNXCORE_API UNNEModel : public UObject
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "NNE - Neural Network Engine")
	static TArray<FString> GetRuntimeNames();

	/**
	 * Creates a new model.
	 */
	UFUNCTION(BlueprintCallable, Category = "NNE - Neural Network Engine")
	static UNNEModel* Create(UObject* Parent, FString RuntimeName, UNNEModelData* ModelData);

	/**
	 * Loads the model from model data for a given runtime.
	 */
	UFUNCTION(BlueprintCallable, Category = "NNE - Neural Network Engine")
	bool Load(FString RuntimeName, UNNEModelData* ModelData);

public:

	UFUNCTION(BlueprintCallable, Category = "NNE - Neural Network Engine")
	int32 NumInputs();

	UFUNCTION(BlueprintCallable, Category = "NNE - Neural Network Engine")
	int32 NumOutputs();

	UFUNCTION(BlueprintCallable, Category = "NNE - Neural Network Engine")
	TArray<int32> GetInputShapes(int32 Index);

	UFUNCTION(BlueprintCallable, Category = "NNE - Neural Network Engine")
	TArray<int32> GetOutputShapes(int32 Index);

public:

	/**
	 * Sets the input and output tensors.
	 * If changes are made to the shape or memory inside the passed tensors, this function must be called again. 
	 * Calls shape inference internally, avoid multiple calls if the input memory and shape does not change.
	 */
	UFUNCTION(BlueprintCallable, Category = "NNE - Neural Network Engine")
	bool SetInput(const TArray<FNNETensor>& Input);
	
public:

	/**
	 * Synchroneous call to run the model.
	 * Requires the runtime to be selected with SetRuntime(...) first.
	 * If an RDG runtime is selected, data will be up and downloaded to and from the GPU.
	 * Upon successful return of this function, the data inside the output tensors will contain the resulting data.
	 */
	UFUNCTION(BlueprintCallable, Category = "NNE - Neural Network Engine")
	bool RunSync(UPARAM(ref) TArray<FNNETensor>& Output);

	/**
	 * Asynchroneous call to run the model.
	 * Requires the runtime to be selected with SetRuntime(...) first.
	 * If an RDG runtime is selected, data will be up and downloaded to and from the GPU.
	 * Upon successful return of this function, the inference is run on a background task 
	 */
	UFUNCTION(BlueprintCallable, Category = "NNE - Neural Network Engine")
	bool RunAsync(FNNETaskPriority TaskPriority, FNNEModelOnAsyncResult OnAsyncResult);

public:

	/**
	 * Returns true while an async call is running in the background.
	 */
	UFUNCTION(BlueprintCallable, Category = "NNE - Neural Network Engine")
	bool IsRunning();
	
private:

	TSharedPtr<UE::NNECore::IModelCPU> Model;

	TArray<UE::NNECore::FTensorBindingCPU> InputBindings;
	TArray<UE::NNECore::FTensorShape> InputShapes;

	TSharedPtr<bool> IsAsyncRunning;
};
