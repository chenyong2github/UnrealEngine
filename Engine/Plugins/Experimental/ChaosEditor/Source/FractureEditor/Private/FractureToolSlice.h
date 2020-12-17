// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FractureToolCutter.h"

#include "FractureToolSlice.generated.h"


class FFractureToolContext;


UCLASS(config = EditorPerProjectUserSettings)
class UFractureSliceSettings : public UFractureToolSettings
{
public:
	GENERATED_BODY()

	UFractureSliceSettings(const FObjectInitializer& ObjInit)
		: Super(ObjInit)
		, SlicesX(3)
		, SlicesY(3)
		, SlicesZ(1)
		, SliceAngleVariation(0.0f)
		, SliceOffsetVariation(0.0f)
	{}

	/** Num Slices X axis - Slicing Method */
	UPROPERTY(EditAnywhere, Category = Slicing, meta = (UIMin = "0"))
	int32 SlicesX;

	/** Num Slices Y axis - Slicing Method */
	UPROPERTY(EditAnywhere, Category = Slicing, meta = (UIMin = "0"))
	int32 SlicesY;

	/** Num Slices Z axis - Slicing Method */
	UPROPERTY(EditAnywhere, Category = Slicing, meta = (UIMin = "0"))
	int32 SlicesZ;

	/** Slicing Angle Variation - Slicing Method [0..1] */
	UPROPERTY(EditAnywhere, Category = Slicing, meta = (DisplayName = "Random Angle Variation", UIMin = "0.0", UIMax = "90.0"))
	float SliceAngleVariation;

	/** Slicing Offset Variation - Slicing Method [0..1] */
	UPROPERTY(EditAnywhere, Category = Slicing, meta = (DisplayName = "Random Offset Variation", UIMin = "0.0"))
	float SliceOffsetVariation;
};

UCLASS(DisplayName="Slice Tool", Category="FractureTools")
class UFractureToolSlice : public UFractureToolCutterBase
{
public:
	GENERATED_BODY()

	UFractureToolSlice(const FObjectInitializer& ObjInit);

	// UFractureTool Interface
	virtual FText GetDisplayText() const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetToolIcon() const override;

	virtual void RegisterUICommand( FFractureEditorCommands* BindingContext ) override;
	
	virtual TArray<UObject*> GetSettingsObjects() const override;
	void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;

	virtual void FractureContextChanged() override;
	virtual int32 ExecuteFracture(const FFractureToolContext& Context) override;

	void GenerateSliceTransforms(const FFractureToolContext& Context, TArray<FTransform>& CuttingPlaneTransforms);

	// Slicing
	UPROPERTY(EditAnywhere, Category = Slicing)
	UFractureSliceSettings* SliceSettings;

private:
	float RenderCuttingPlaneSize;
	TArray<FTransform> RenderCuttingPlanesTransforms;
};