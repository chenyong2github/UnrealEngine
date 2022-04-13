// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MLDeformerVizSettings.h"
#include "LegacyVertexDeltaModelVizSettings.generated.h"

class UGeometryCache;

/**
 * The vizualization settings specific to the the vertex delta model.
 */
UCLASS()
class LEGACYVERTEXDELTAMODEL_API ULegacyVertexDeltaModelVizSettings
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
#endif // WITH_EDITORONLY_DATA
};
