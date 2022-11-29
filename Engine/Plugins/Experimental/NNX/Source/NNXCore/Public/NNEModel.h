// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "NNXRuntime.h"
#include "NNEModelData.h"

#include "NNEModel.generated.h"

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
	 * If changes are made to the shape or data sizes inside the passed tensors, this function must be called again. 
	 */
	UFUNCTION(BlueprintCallable, Category = "NNE - Neural Network Engine")
	bool SetInputOutput(const TArray<FNNETensor>& Input, UPARAM(ref) TArray<FNNETensor>& Output);
	
public:

	/**
	 * Synchroneous call to run the model.
	 * Requires the runtime to be selected with SetRuntime(...) first.
	 * If an RDG runtime is selected, data will be up and downloaded to and from the GPU.
	 * Upon successful return of this function, the data inside the output tensors passed to SetInputOutput will contain the resulting data.
	 */
	UFUNCTION(BlueprintCallable, Category = "NNE - Neural Network Engine")
	bool Run();
	
private:

	TSharedPtr<NNX::FMLInferenceModel> Model;

	TArray<NNX::FMLTensorBinding> InputBindings;
	TArray<NNX::FMLTensorBinding> OutputBindings;
};
