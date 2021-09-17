// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MLPytorchDataSetInterface.generated.h"

class FMLDeformerEditorData;
/**
 * 
 */
UCLASS(BlueprintType, hidecategories=Object)
class MLDEFORMEREDITOR_API UMLPytorchDataSetInterface : public UObject
{
	GENERATED_BODY()
public:
	UMLPytorchDataSetInterface(); 
	void SetEditorData(TSharedPtr<FMLDeformerEditorData> InEditorData);

	/** Get the number of input transforms. */
	UFUNCTION(BlueprintPure, Category = "Training Data")
	int32 GetNumberSampleTransforms() const;

	/** Get number of input curves. */
	UFUNCTION(BlueprintPure, Category = "Training Data")
	int32 GetNumberSampleCurves() const;

	/** Get the number of output deltas. */
	UFUNCTION(BlueprintPure, Category = "Training Data")
	int32 GetNumberSampleDeltas() const;

	/** Get the number of samples in this data set. */
	UFUNCTION(BlueprintPure, Category = "Training Data")
    int32 NumSamples() const;

	/** Set the current sample index. This must be in range of [0..GetNumSamples()-1]. */
	UFUNCTION(BlueprintCallable, Category = "Training Data")
    bool SetCurrentSampleIndex(int32 Index);

	/** Compute delta statistics for the whole dataset. */
	UFUNCTION(BlueprintCallable, Category = "Training Data")
	bool ComputeDeltasStatistics();

	/** The delta values per vertex for this sample. This is updated after SetCurrentSampleIndex is called. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training Data")
	TArray<float> SampleDeltas;

	/** The curve weights. This is updated after SetCurrentSampleIndex is called. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training Data")
	TArray<float> SampleCurveWeights;

	/** Mean delta computed over the entire dataset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training Data")
	FVector VertexDeltaMean = FVector::ZeroVector;

	/** Vertex delta scale computed over the entire dataset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training Data")
	FVector VertexDeltaScale = FVector::OneVector;

	/** The bone transformations in component space for this sample. This is updated after SetCurrentSampleIndex is called. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training Data")
	TArray<FTransform> SampleBoneTransforms;

	/** The current sample index. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Training Data")
	int32 CurrentSampleIndex = -1;

private:
	bool IsValid() const;

private:
	TSharedPtr<FMLDeformerEditorData> EditorData;
};
