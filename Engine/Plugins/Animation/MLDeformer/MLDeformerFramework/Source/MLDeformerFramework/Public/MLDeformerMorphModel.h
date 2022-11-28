// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MLDeformerGeomCacheModel.h"
#include "MLDeformerMorphModel.generated.h"

struct FExternalMorphSet;
struct FExternalMorphSetWeights;
class USkinnedMeshComponent;

UCLASS()
class MLDEFORMERFRAMEWORK_API UMLDeformerMorphModel
	: public UMLDeformerGeomCacheModel
{
	GENERATED_BODY()

public:
	UMLDeformerMorphModel(const FObjectInitializer& ObjectInitializer);

	// UMLDeformerModel overrides.
	virtual bool IsNeuralNetworkOnGPU() const override		{ return false; }	// CPU based neural network.
	virtual void Serialize(FArchive& Archive) override;
	virtual UMLDeformerModelInstance* CreateModelInstance(UMLDeformerComponent* Component) override;
	// ~END UMLDeformerModel overrides.

	// UObject overrides.
	void BeginDestroy() override;
	// ~END UObject overrides.

	float GetMorphTargetDeltaThreshold() const				{ return MorphTargetDeltaThreshold; }
	float GetMorphTargetErrorTolerance() const				{ return MorphTargetErrorTolerance; }
	bool GetIncludeMorphTargetNormals() const				{ return bIncludeNormals; }
	EMLDeformerMaskChannel GetMaskChannel() const			{ return MaskChannel; }
	bool GetInvertMaskChannel() const						{ return bInvertMaskChannel; }

	void SetMorphTargetDeltaThreshold(float Threshold)		{ MorphTargetDeltaThreshold = Threshold; }
	void SetMorphTargetErrorTolerance(float Tolerance)		{ MorphTargetErrorTolerance = Tolerance; }
	void SetIncludeMorphTargetNormals(bool bInclude)		{ bIncludeNormals = bInclude; }
	void SetWeightMask(EMLDeformerMaskChannel Channel)		{ MaskChannel = Channel; }
	void SetInvertMaskChannel(bool bInvert)					{ bInvertMaskChannel = bInvert; }

	// Get property names.
	static FName GetMorphTargetDeltaThresholdPropertyName() { return GET_MEMBER_NAME_CHECKED(UMLDeformerMorphModel, MorphTargetDeltaThreshold); }
	static FName GetMorphTargetErrorTolerancePropertyName() { return GET_MEMBER_NAME_CHECKED(UMLDeformerMorphModel, MorphTargetErrorTolerance); }
	static FName GetIncludeMorphTargetNormalsPropertyName() { return GET_MEMBER_NAME_CHECKED(UMLDeformerMorphModel, bIncludeNormals); }
	static FName GetMaskChannelPropertyName()				{ return GET_MEMBER_NAME_CHECKED(UMLDeformerMorphModel, MaskChannel); }
	static FName GetInvertMaskChannelPropertyName()			{ return GET_MEMBER_NAME_CHECKED(UMLDeformerMorphModel, bInvertMaskChannel); }

	/**
	 * Set the per vertex deltas, as a set of floats. Each vertex delta must have 3 floats.
	 * These deltas are used to generate compressed morph targets internally. You typically call this method from inside
	 * the python training script once your morph target deltas have been generated there.
	 * Concatenate all deltas into one buffer, so like this [morphdeltas_target0, morphdeltas_target1, ..., morphdeltas_targetN].
	 * The vertex ordering should be: [(x, y, z), (x, y, z), (x, y, z)].
	 * This is the same as SetMorphTargetDeltas, except that this takes an array of floats instead of vectors.
	 * @param Deltas The array of floats that contains the deltas. The number of items in the array must be equal to (NumMorphs * NumBaseMeshVerts * 3).
	 */
	UFUNCTION(BlueprintCallable, Category = "MLDeformerMorphModel")
	void SetMorphTargetDeltaFloats(const TArray<float>& Deltas);

	/**
	 * Set the morph target model deltas as an array of 3D vectors.
	 * These deltas are used to generate compressed morph targets internally. You typically call this method from inside
	 * the python training script once your morph target deltas have been generated there.
	 * Concatenate all deltas into one buffer, so like this [morphdeltas_target0, morphdeltas_target1, ..., morphdeltas_targetN].
	 * This is the same as SetMorphTargetDeltaFloats, except that it takes vectors instead of floats.
	 * @param Deltas The array of 3D vectors that contains the vertex deltas. The number of items in the array must be equal to (NumMorphs * NumBaseMeshVerts).
	 */
	UFUNCTION(BlueprintCallable, Category = "MLDeformerMorphModel")
	void SetMorphTargetDeltas(const TArray<FVector3f>& Deltas);

	/**
	 * Get the morph target delta vectors array.
	 * The layout of this array is [morphdeltas_target0, morphdeltas_target1, ..., morphdeltas_targetN].
	 * So the total number of items in the array returned equals (NumMorphTargets * NumBaseMeshVerts).
	 */
	const TArray<FVector3f>& GetMorphTargetDeltas() const	{ return MorphTargetDeltas; }

	/**
	 * Get the morph target set.
	 */
	TSharedPtr<FExternalMorphSet> GetMorphTargetSet() const { return MorphTargetSet; }

	/**
	 * Get the start index into the array of deltas (vectors3's), for a given morph target.
	 * This does not perform a bounds check to see if MorphTargetIndex is in a valid range, so be aware.
	 * @param MorphTargetIndex The morph target index.
	 * @return The start index, or INDEX_NONE in case there are no deltas.
	 */
	int32 GetMorphTargetDeltaStartIndex(int32 MorphTargetIndex) const;

private:
	/** The compressed morph target data, ready for the GPU. */
	TSharedPtr<FExternalMorphSet> MorphTargetSet;

	/**
	 * The entire set of morph target deltas, 3 per vertex, for each morph target, as one flattened buffer.
	 * So the size of this buffer is: (NumVertsPerMorphTarget * 3 * NumMorphTargets).
	 */
	TArray<FVector3f> MorphTargetDeltas;

	/**
	 * Include vertex normals in the morph targets?
	 * The advantage of this can be that it is higher performance than recomputing the normals.
	 * The disadvantage is it can result in lower quality and uses more memory for the stored morph targets.
	 */
	UPROPERTY(EditAnywhere, Category = "Morph Targets")
	bool bIncludeNormals = false;

	/**
	 * Morph target delta values that are smaller than or equal to this threshold will be zeroed out.
	 * This essentially removes small deltas from morph targets, which will lower the memory usage at runtime, however when set too high it can also introduce visual artifacts.
	 * A value of 0 will result in the highest quality morph targets, at the cost of higher runtime memory usage.
	 */
	UPROPERTY(EditAnywhere, Category = "Morph Targets", meta = (ClampMin = "0.0", ClampMax = "1.0", ForceUnits="cm"))
	float MorphTargetDeltaThreshold = 0.0025f;

	/** The morph target error tolerance. Higher values result in larger compression, but could result in visual artifacts. */
	UPROPERTY(EditAnywhere, Category = "Morph Targets", meta = (ClampMin = "0.01", ClampMax = "500"))
	float MorphTargetErrorTolerance = 20.0f;

	/**
	 * The channel data that represents the delta mask multipliers.
	 * You can use this feather out influence of the ML Deformer in specific areas, such as neck line seams, where the head mesh connects with the body.
	 * The painted vertex color values will be like a weight multiplier on the ML deformer deltas applied to that vertex. You can invert the mask as well.
	 */
	UPROPERTY(EditAnywhere, Category = "Morph Targets")
	EMLDeformerMaskChannel MaskChannel = EMLDeformerMaskChannel::Disabled;

	/** 
	 * Enable this if you want to invert the mask channel values. For example if you painted the neck seam vertices in red, and you wish the vertices that got painted to NOT move, you have to invert the mask.
	 * On default you paint areas where the deformer should be active. If you enable the invert option, you paint areas where the deformer will not be active.
	 */
	UPROPERTY(EditAnywhere, Category = "Morph Targets", meta = (EditCondition = "MaskChannel != EMLDeformerMaskChannel::Disabled"))
	bool bInvertMaskChannel = false;
};
