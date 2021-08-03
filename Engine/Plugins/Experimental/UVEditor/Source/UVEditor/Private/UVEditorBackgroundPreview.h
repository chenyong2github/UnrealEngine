// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Drawing/PreviewGeometryActor.h"
#include "Drawing/TriangleSetComponent.h"
#include "InteractiveTool.h"
#include "GeometryBase.h"
#include "UVEditorBackgroundPreview.generated.h"

/**
 * Visualization settings for the UUVEditorBackgroundPreview
 */
UCLASS()
class UVEDITOR_API UUVEditorBackgroundPreviewProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	/** Should the background be shown */
	UPROPERTY(EditAnywhere, Category = Background)
	bool bVisible = false;

	/** Should the background show textures or materials? */
	UPROPERTY(EditAnywhere, Category = Background, meta = (EditCondition = "bVisible", EditConditionHides = true))
	bool bUseMaterials = false;


	UPROPERTY(EditAnywhere, Category = Background, meta = (EditCondition = "!bUseMaterials && bVisible", EditConditionHides = true))
	TObjectPtr<UTexture2D> SourceTexture;

	UPROPERTY(EditAnywhere, Category = Background, meta = (EditCondition = "bUseMaterials && bVisible", EditConditionHides=true))
	TObjectPtr<UMaterial> SourceMaterial;
};

/**
  Serves as a container for the background texture/material display in the UVEditor. This class is responsible for managing the quad
  drawn behind the grid, as well as maintaining the texture and material choices from the user to display.
 */
UCLASS(Transient)
class UVEDITOR_API UUVEditorBackgroundPreview : public UPreviewGeometry
{
	GENERATED_BODY()

public:

	/**
	 * Client must call this every frame for changes to .Settings to be reflected in rendered result.
	 */
	void OnTick(float DeltaTime);


public:
	/** Visualization settings */
	UPROPERTY()
		TObjectPtr<UUVEditorBackgroundPreviewProperties> Settings;

	/** The component containing the quad visualization */
	UPROPERTY()
		TObjectPtr<UTriangleSetComponent> BackgroundComponent;

	/** The active material being displayed for the background */
	UPROPERTY()
		TObjectPtr<UMaterialInstanceDynamic> BackgroundMaterial;

protected:
	virtual void OnCreated() override;

	bool bSettingsModified = false;

	void UpdateVisibility();
	void UpdateBackground();
};

