// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MLDeformerVizSettings.h"
#include "NeuralMorphModelVizSettings.generated.h"

class UGeometryCache;

/**
 * The vizualization settings specific to this model.
 */
UCLASS()
class NEURALMORPHMODEL_API UNeuralMorphModelVizSettings
	: public UMLDeformerVizSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITORONLY_DATA
	virtual bool HasTestGroundTruth() const override { return (GroundTruth.Get() != nullptr); }
	UGeometryCache* GetTestGroundTruth() const { return GroundTruth; }

public:
	/** The geometry cache that represents the ground truth of the test anim sequence. */
	UPROPERTY(EditAnywhere, Category = "Test Assets")
	TObjectPtr<UGeometryCache> GroundTruth = nullptr;

	/** 
	 * The morph target to visualize. The first one always being the means, so not a sparse target. 
	 * This only can be used after you trained, in the same editor session directly after training.
	 */
	UPROPERTY(EditAnywhere, Category = "Live Settings", meta = (ClampMin = "0"))
	int32 MorphTargetNumber = 0;

	/**
	 * The morph target delta threshold. This is a preview of what deltas would be included in the selected morph target
	 * when using a delta threshold during training that is equal to this value.
	 * This only can be used after you trained, in the same editor session directly after training.
	 */
	UPROPERTY(EditAnywhere, Category = "Live Settings", meta = (ClampMin = "0.001", ClampMax = "1.0"))
	float MorphTargetDeltaThreshold = 0.01f;

	/** 
	 * Draw the morph targets as debug data? 
	 * This only can be used after you trained, in the same editor session directly after training.
	 */
	UPROPERTY(EditAnywhere, Category = "Live Settings")
	bool bDrawMorphTargets = false;
#endif // WITH_EDITORONLY_DATA
};
