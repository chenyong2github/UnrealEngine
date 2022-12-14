// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MLDeformerMorphModelVizSettings.h"
#include "NearestNeighborModelVizSettings.generated.h"

class UGeometryCache;

/**
 * The vizualization settings specific to the the vertex delta model.
 */
UCLASS()
class NEARESTNEIGHBORMODEL_API UNearestNeighborModelVizSettings 
	: public UMLDeformerMorphModelVizSettings
{
	GENERATED_BODY()
public:
#if	WITH_EDITORONLY_DATA
	void SetNearestNeighborActorsOffset(float InOffset) { NearestNeighborActorsOffset = InOffset; }
	float GetNearestNeighborActorsOffset() const { return NearestNeighborActorsOffset; }
	static FName GetNearestNeighborActorsOffsetPropertyName() { return GET_MEMBER_NAME_CHECKED(UNearestNeighborModelVizSettings, NearestNeighborActorsOffset); }

	static FName GetNearestNeighborIdsPropertyName() { return GET_MEMBER_NAME_CHECKED(UNearestNeighborModelVizSettings, NearestNeighborIds); }
	void SetNearestNeighborIds(const TArray<uint32>& InNearestNeighborIds) { NearestNeighborIds = InNearestNeighborIds; }

protected:
	UPROPERTY(EditAnywhere, Category = "Live Settings")
	float NearestNeighborActorsOffset = 2.0f;

	UPROPERTY(VisibleAnywhere, Category = "Live Settings")
	TArray<uint32> NearestNeighborIds;
#endif
};
