// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectPtr.h"
#include "MLDeformerTrainingModel.generated.h"

class UMLDeformerModel;

namespace UE::MLDeformer
{
	class FMLDeformerEditorModel;
}

UCLASS(Blueprintable)
class MLDEFORMERFRAMEWORKEDITOR_API UMLDeformerTrainingModel
	: public UObject
{
	GENERATED_BODY()

public:
	void Init(UE::MLDeformer::FMLDeformerEditorModel* InEditorModel);

	UE::MLDeformer::FMLDeformerEditorModel* GetEditorModel() const { return EditorModel; }
	void SetEditorModel(UE::MLDeformer::FMLDeformerEditorModel* InModel) { EditorModel = InModel; }

	UFUNCTION(BlueprintPure, Category = "Training Data")
	UMLDeformerModel* GetModel() const;

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

	/** Set the current sample frame. This will internally call the SampleFrame method, which will update the deltas, curve values and bone rotations. */
	UFUNCTION(BlueprintCallable, Category = "Training Data")
	bool SetCurrentSampleIndex(int32 Index);

	UFUNCTION(BlueprintCallable, Category = "Training Data")
	bool GetNeedsResampling() const;

	UFUNCTION(BlueprintCallable, Category = "Training Data")
	void SetNeedsResampling(bool bNeedsResampling);

protected:
	/** Sample a given frame. This updates the sample deltas, curves, and bone rotations. */
    virtual bool SampleFrame(int32 Index);

public:
	// The delta values per vertex for this sample. This is updated after SetCurrentSampleIndex is called. Contains an xyz (3 floats) for each vertex.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training Data")
	TArray<float> SampleDeltas;

	// The curve weights. This is updated after SetCurrentSampleIndex is called.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training Data")
	TArray<float> SampleCurveValues;

	// The bone rotations in bone (local) space for this sample. This is updated after SetCurrentSampleIndex is called and is 6 floats per bone (2 columns of 3x3 rotation matrix).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training Data")
	TArray<float> SampleBoneRotations;

protected:
	UE::MLDeformer::FMLDeformerEditorModel* EditorModel = nullptr;
};
