// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MLPytorchDataSetInterface.generated.h"

class FMLDeformerEditorData;
class FMLDeformerFrameCache;

/**
 * 
 */
UCLASS(BlueprintType, hidecategories=Object)
class MLDEFORMEREDITOR_API UMLPytorchDataSetInterface : public UObject
{
	GENERATED_BODY()
public:
	UMLPytorchDataSetInterface(); 
	~UMLPytorchDataSetInterface();

	void Clear();
	void SetEditorData(TSharedPtr<FMLDeformerEditorData> InEditorData);
	void SetFrameCache(TSharedPtr<FMLDeformerFrameCache> InFrameCache);

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

	/** The delta values per vertex for this sample. This is updated after SetCurrentSampleIndex is called. Contains an xyz (3 floats) for each vertex. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training Data")
	TArray<float> SampleDeltas;

	/** The curve weights. This is updated after SetCurrentSampleIndex is called. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training Data")
	TArray<float> SampleCurveValues;

	/** The bone rotations in bone (local) space for this sample. This is updated after SetCurrentSampleIndex is called. Contains an xyzw (4 floats) for each bone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training Data")
	TArray<float> SampleBoneRotations;

	/** Mean delta computed over the entire dataset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training Data")
	FVector VertexDeltaMean = FVector::ZeroVector;

	/** Vertex delta scale computed over the entire dataset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training Data")
	FVector VertexDeltaScale = FVector::OneVector;

	/** The current sample index. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Training Data")
	int32 CurrentSampleIndex = -1;

private:
	bool IsValid() const;

private:
	TSharedPtr<FMLDeformerEditorData> EditorData;
	TSharedPtr<FMLDeformerFrameCache> FrameCache;
};
