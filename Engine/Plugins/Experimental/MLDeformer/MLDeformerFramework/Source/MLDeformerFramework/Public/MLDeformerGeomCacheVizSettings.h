// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MLDeformerVizSettings.h"
#include "MLDeformerGeomCacheVizSettings.generated.h"

class UGeometryCache;

/**
 * The vizualization settings for a model that has a geometry cache.
 * This can be used in combination with a UMLDeformerGeomCacheModel.
 */
UCLASS()
class MLDEFORMERFRAMEWORK_API UMLDeformerGeomCacheVizSettings
	: public UMLDeformerVizSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITORONLY_DATA
	// UMLDeformerModel overrides.
	virtual bool HasTestGroundTruth() const override { return !GroundTruth.IsNull(); }
	// ~END UMLDeformerModel overrides.

	/**
	 * Get the test ground truth geometry cache, which represents the ground truth version of the test animation sequence.
	 * @return A pointer to the geometry cache object.
	 */
	UGeometryCache* GetTestGroundTruth() const { return GroundTruth; }

public:
	/** The geometry cache that represents the ground truth of the test anim sequence. */
	UPROPERTY(EditAnywhere, Category = "Test Assets")
	TObjectPtr<UGeometryCache> GroundTruth = nullptr;
#endif // WITH_EDITORONLY_DATA
};
