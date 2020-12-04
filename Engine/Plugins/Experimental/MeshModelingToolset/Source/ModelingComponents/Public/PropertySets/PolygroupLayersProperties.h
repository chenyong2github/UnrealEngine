// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InteractiveTool.h"
#include "PolygroupLayersProperties.generated.h"

class FDynamicMesh3;

/**
 * Basic Tool Property Set that allows for selecting from a list of FNames (that we assume are Polygroup Layers)
 */
UCLASS()
class MODELINGCOMPONENTS_API UPolygroupLayersProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:

	/** Select vertex weight map. If configured, the weight map value will be sampled to modulate displacement intensity. */
	UPROPERTY(EditAnywhere, Category = PolygroupLayers, meta = (GetOptions = GetGroupLayersFunc))
	FName ActiveGroupLayer;

	// this function is called provide set of available group layers
	UFUNCTION()
	TArray<FString> GetGroupLayersFunc() { return GroupLayersList; }

	// internal list used to implement above
	UPROPERTY(meta = (TransientToolProperty))
	TArray<FString> GroupLayersList;

	void InitializeGroupLayers(const FDynamicMesh3* Mesh);

	// return true if any option other than "None" is selected
	bool HasSelectedPolygroup() const;

	void SetSelectedFromPolygroupIndex(int32 Index);
};

