// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MLDeformerGeomCacheModel.h"
#include "UObject/Object.h"
#include "MLDeformerMorphModel.generated.h"

class USkeletalMesh;
class UMLDeformerAsset;
class UMLDeformerModelInstance;
struct FExternalMorphSet;

UCLASS()
class MLDEFORMERFRAMEWORK_API UMLDeformerMorphModel
	: public UMLDeformerGeomCacheModel
{
	GENERATED_BODY()

public:
	UMLDeformerMorphModel(const FObjectInitializer& ObjectInitializer);

	// UMLDeformerModel overrides.
	virtual FString GetDisplayName() const override { return "Morph Base Model"; }
	virtual void Serialize(FArchive& Archive) override;
	virtual void PostMLDeformerComponentInit(UMLDeformerModelInstance* ModelInstance) override;
	virtual bool IsNeuralNetworkOnGPU() const override { return false; }	// CPU neural network.
	virtual UMLDeformerModelInstance* CreateModelInstance(UMLDeformerComponent* Component) override;
	// ~END UMLDeformerModel overrides.

	// UObject overrides.
	void BeginDestroy() override;
	// ~END UObject overrides.

#if WITH_EDITORONLY_DATA
	float GetMorphTargetDeltaThreshold() const { return MorphTargetDeltaThreshold; }
#endif

	UFUNCTION(BlueprintCallable, Category = "NeuralMorphModel")
	void SetMorphTargetDeltas(const TArray<float>& Deltas);

	const TArray<FVector3f>& GetMorphTargetDeltas() const { return MorphTargetDeltas; }
	int32 GetMorphTargetDeltaStartIndex(int32 MorphTargetIndex) const;

public:
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
