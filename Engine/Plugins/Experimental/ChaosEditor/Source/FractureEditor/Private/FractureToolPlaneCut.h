// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FractureTool.h"

#include "FractureToolPlaneCut.generated.h"

UCLASS(config = EditorPerProjectUserSettings)
class UFracturePlaneCutSettings
	: public UObject
{
	GENERATED_BODY()
public:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif

	UFracturePlaneCutSettings()
	: NumberPlanarCuts(3) {}

	/** Number of Clusters - Cluster Voronoi Method */
	UPROPERTY(EditAnywhere, Category = PlaneCut, meta = (DisplayName = "Number of Cuts", UIMin = "1", UIMax = "2000", ClampMin = "1"))
	int32 NumberPlanarCuts;

	/** Actor to be used for voronoi bounds or plane cutting  */
	UPROPERTY(EditAnywhere, Category = PlaneCut, meta = (DisplayName = "Reference Actor"))
	TLazyObjectPtr<AActor> ReferenceActor;

	UPROPERTY()
	UFractureTool *OwnerTool;

};

UCLASS(DisplayName="Plane Cut Tool", Category="FractureTools")
class UFractureToolPlaneCut : public UFractureTool
{
public:
	GENERATED_BODY()

	UFractureToolPlaneCut(const FObjectInitializer& ObjInit);//  : Super(ObjInit) {}

	// UFractureTool Interface
	virtual FText GetDisplayText() const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetToolIcon() const override;

	void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;

	virtual void RegisterUICommand( FFractureEditorCommands* BindingContext );
	
	virtual TArray<UObject*> GetSettingsObjects() const;// { return TArray<UObject*>(); }

	virtual void FractureContextChanged() override;
	virtual void ExecuteFracture(const FFractureContext& FractureContext) override;
	virtual bool CanExecuteFracture() const override;

#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif

private:
	// Slicing
	UPROPERTY(EditAnywhere, Category = Slicing)
	UFracturePlaneCutSettings* PlaneCutSettings;

	void GenerateSliceTransforms(const FFractureContext& Context, TArray<FTransform>& CuttingPlaneTransforms);

	float RenderCuttingPlaneSize;
	TArray<FTransform> RenderCuttingPlanesTransforms;
};