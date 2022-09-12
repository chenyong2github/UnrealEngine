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
	virtual FString GetDisplayName() const override			{ return "Morph Base Model"; }
	virtual bool IsNeuralNetworkOnGPU() const override		{ return false; }	// CPU based neural network.
	virtual void Serialize(FArchive& Archive) override;
	virtual void PostMLDeformerComponentInit(UMLDeformerModelInstance* ModelInstance) override;
	virtual UMLDeformerModelInstance* CreateModelInstance(UMLDeformerComponent* Component) override;
	// ~END UMLDeformerModel overrides.

	// UObject overrides.
	void BeginDestroy() override;
	// ~END UObject overrides.

#if WITH_EDITORONLY_DATA
	float GetMorphTargetDeltaThreshold() const				{ return MorphTargetDeltaThreshold; }
	float GetMorphTargetErrorTolerance() const				{ return MorphTargetErrorTolerance; }

	// Get property names.
	static FName GetMorphTargetDeltaThresholdPropertyName() { return GET_MEMBER_NAME_CHECKED(UMLDeformerMorphModel, MorphTargetDeltaThreshold); }
	static FName GetMorphTargetErrorTolerancePropertyName() { return GET_MEMBER_NAME_CHECKED(UMLDeformerMorphModel, MorphTargetErrorTolerance); }
#endif

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
	 * Get the external morph target set ID for this model.
	 * This basically identifies the set of morph targets that belong to this model.
	 * Different models on the same skeletal mesh gives each model its own unique ID.
	 * You can use this ID to find the weight values for a specific model instance, inside the USkinnedMeshComponent class.
	 * @return The unique ID of the morph target set for this model.
	 * @see USkinnedMeshComponent::GetExternalMorphWeights.
	 * @see FindExternalMorphWeights.
	 */
	int32 GetExternalMorphSetID() const						{ return ExternalMorphSetID; }

	/**
	 * Get the weights for the external morph target set that belongs to this model.
	 * @param LOD The LOD level to get the weights for.
	 * @param SkinnedMeshComponent The skinned mesh component that we have to search in.
	 * @return A pointer to the weight data, or nullptr in case we cannot find the weight data.
	 */
	FExternalMorphSetWeights* FindExternalMorphWeights(int32 LOD, USkinnedMeshComponent* SkinnedMeshComponent) const;

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

protected:
	/** The next free morph target set ID. This is used to generate unique ID's for each morph model. */
	static TAtomic<int32> NextFreeMorphSetID;

	/** The compressed morph target data, ready for the GPU. */
	TSharedPtr<FExternalMorphSet> MorphTargetSet;

	/**
	 * The entire set of morph target deltas, 3 per vertex, for each morph target, as one flattened buffer.
	 * So the size of this buffer is: (NumVertsPerMorphTarget * 3 * NumMorphTargets).
	 */
	TArray<FVector3f> MorphTargetDeltas;

	/** 
	 * The external morph set data type ID, specific to this model.
	 * If you inherit your model from this base class, you should set this to some unique value, that represents your model.
	 */
	int32 ExternalMorphSetID = -1;

#if WITH_EDITORONLY_DATA
	/**
	 * Morph target delta values that are smaller than or equal to this threshold will be zeroed out.
	 * This essentially removes small deltas from morph targets, which will lower the memory usage at runtime, however when set too high it can also introduce visual artifacts.
	 * A value of 0 will result in the highest quality morph targets, at the cost of higher runtime memory usage.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Targets", meta = (ClampMin = "0.0", ClampMax = "1.0", ForceUnits="cm"))
	float MorphTargetDeltaThreshold = 0.0025f;

	/** The morph target error tolerance. Higher values result in larger compression, but could result in visual artifacts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Targets", meta = (ClampMin = "0.01", ClampMax = "500"))
	float MorphTargetErrorTolerance = 50.0f;
#endif // WITH_EDITORONLY_DATA
};
