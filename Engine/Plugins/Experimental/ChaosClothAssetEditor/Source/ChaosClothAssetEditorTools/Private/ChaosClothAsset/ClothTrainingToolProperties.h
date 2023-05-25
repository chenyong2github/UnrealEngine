// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InteractiveTool.h"

#include "ClothTrainingToolProperties.generated.h"

class UAnimSequence;
class USkinnedAsset;

UCLASS()
class UClothTrainingToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	/* Skeletal mesh that will be used in MLDeformer */
	UPROPERTY(EditAnywhere, Category = Input)
	TObjectPtr<USkinnedAsset> MLDeformerAsset;

	UPROPERTY(EditAnywhere, Category = Input)
	TObjectPtr<UAnimSequence> AnimationSequence;

	/* e.g. "0, 2, 5-10, 12-15". If left empty, all frames will be used */
	UPROPERTY(EditAnywhere, Category = Input)
	FString FramesToSimulate;

	UPROPERTY(VisibleAnywhere, Category = Output, meta = (EditCondition = "!bDebug"))
	FString SimulatedCacheName;

	UFUNCTION(CallInEditor, Category = "Output Actions", meta = (EditCondition = "!bDebug"))
	void SetSimulatedCacheName();

	UPROPERTY(EditAnywhere, Category = Output)
	bool bDebug = false;

	UPROPERTY(EditAnywhere, Category = Output, meta = (Min = 0, EditCondition = "bDebug"))
	uint32 DebugFrame = 0;

	UPROPERTY(VisibleAnywhere, Category = Output, meta = (EditCondition = "bDebug"))
	FString DebugCacheName;

	UFUNCTION(CallInEditor, Category = "Output Actions", meta = (EditCondition = "bDebug"))
	void SetDebugCacheName();

	UPROPERTY(EditAnywhere, Category = "Simulation Settings", meta = (Min = 0))
	float TimeStep = 1.f / 30;

	UPROPERTY(EditAnywhere, Category = "Simulation Settings", meta = (Min = 0))
	int32 NumSteps = 200;

	UPROPERTY(EditAnywhere, Category = "Simulation Settings", meta = (Min = 1, EditCondition = "!bDebug"))
	int32 NumThreads = 1;

private:
	UObject* GetClothAsset();
};

class UClothTrainingTool;

UCLASS()
class UClothTrainingToolActionProperties : public UObject
{
	GENERATED_BODY()
public:

	TWeakObjectPtr<UClothTrainingTool> ParentTool;

	void Initialize(UClothTrainingTool* ParentToolIn);

	UFUNCTION(CallInEditor, Category = Actions, meta = (DisplayName = "Begin Generating", DisplayPriority = 1))
	void StartGenerating();
};