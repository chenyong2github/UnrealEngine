// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"

#include "NNECoreModelData.h"
#include "NNXInferenceModel.h"

#include "NNERuntimeRDGNeuralPostProcessing.generated.h"

class NNERUNTIMERDG_API FNNENeuralPostProcessing : public FSceneViewExtensionBase
{
public:

	FNNENeuralPostProcessing(const FAutoRegister& AutoRegister);

public:

	int32 Add(FString RuntimeName, UNNEModelData* ModelData);
	bool Remove(int32 ModelId);
	bool SetWeight(int32 ModelId, float Weight);
	bool SetRangeScale(int32 ModelId, float RangeScale);
	bool SetInputSize(int32 ModelId, FIntPoint InputSize);
	void Enable(int32 ModelId);
	void Disable(int32 ModelId);

public:

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void PreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily) override {}
	virtual void PreRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView) override {}
	virtual void PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessingInputs& Inputs) override;

private:

	FCriticalSection CriticalSection;
	int32 NumEnabled;
	int32 LastId;
	TMap<int32, TSharedPtr<NNX::FMLInferenceModel, ESPMode::ThreadSafe>> Models;
	TMap<int32, float> Weights;
	TMap<int32, float> RangeScales;
	TMap<int32, FIntPoint> InputSizes;
	TSet<int32> Enabled;

};

UCLASS(BlueprintType, Category = "NNE - Neural Network Engine")
class NNERUNTIMERDG_API UNNENeuralPostProcessing : public UObject
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "NNE - Neural Network Engine")
	int32 Add(FString RuntimeName, UNNEModelData* ModelData);

	UFUNCTION(BlueprintCallable, Category = "NNE - Neural Network Engine")
	bool Remove(int32 ModelId);

	UFUNCTION(BlueprintCallable, Category = "NNE - Neural Network Engine")
	bool SetWeight(int32 ModelId, float Weight);

	UFUNCTION(BlueprintCallable, Category = "NNE - Neural Network Engine")
	bool SetRangeScale(int32 ModelId, float RangeScale);
	
	UFUNCTION(BlueprintCallable, Category = "NNE - Neural Network Engine")
	bool SetInputSize(int32 ModelId, FIntPoint InputSize);

	UFUNCTION(BlueprintCallable, Category = "NNE - Neural Network Engine")
	void Enable(int32 ModelId);

	UFUNCTION(BlueprintCallable, Category = "NNE - Neural Network Engine")
	void Disable(int32 ModelId);

private:

	TSharedPtr<FNNENeuralPostProcessing> NeuralPostProcessing;

};