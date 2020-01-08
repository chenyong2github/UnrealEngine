// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FractureTool.h"

#include "FractureToolSlice.generated.h"


UCLASS()
class UFractureSliceSettings : public UObject
{
	GENERATED_BODY()
public:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif

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

	UPROPERTY()
	UFractureTool *OwnerTool;

};

UCLASS(DisplayName="Slice Tool", Category="FractureTools")
class UFractureToolSlice : public UFractureTool
{
public:
	GENERATED_BODY()

	UFractureToolSlice(const FObjectInitializer& ObjInit);//  : Super(ObjInit) {}

	// UFractureTool Interface
	virtual FText GetDisplayText() const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetToolIcon() const override;

	virtual void RegisterUICommand( FFractureEditorCommands* BindingContext );
	
	virtual TArray<UObject*> GetSettingsObjects() const;// { return TArray<UObject*>(); }
	void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;

	virtual void FractureContextChanged() override;
	virtual void ExecuteFracture(const FFractureContext& Context) override;
	virtual bool CanExecuteFracture() const override;

	void GenerateSliceTransforms(const FFractureContext& Context, TArray<FTransform>& CuttingPlaneTransforms);

#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif


	// Slicing
	UPROPERTY(EditAnywhere, Category = Slicing)
	UFractureSliceSettings* SliceSettings;
private:
// 	TArray<TTuple<FVector, FVector>> Edges;

	float RenderCuttingPlaneSize;
	TArray<FTransform> RenderCuttingPlanesTransforms;
};