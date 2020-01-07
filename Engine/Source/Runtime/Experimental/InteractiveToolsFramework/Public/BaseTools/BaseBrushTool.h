// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseTools/MeshSurfacePointTool.h"
#include "InteractiveTool.h"
#include "BaseBrushTool.generated.h"

class UBrushStampIndicator;
/**
 * Standard properties for a Brush-type Tool
 */
UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UBrushBaseProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UBrushBaseProperties();

	/** Relative size of brush */
	UPROPERTY(EditAnywhere, Category = Brush, meta = (DisplayName = "Size", UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "10.0"))
	float BrushSize;

	/** If true, ignore relative Brush Size and use explicit World Radius */
	UPROPERTY(EditAnywhere, Category = Brush, AdvancedDisplay)
	bool bSpecifyRadius;

	/** Radius of brush */
	UPROPERTY(EditAnywhere, Category = Brush, AdvancedDisplay, meta = (DisplayName = "Radius", UIMin = "1.0", UIMax = "1000.0", ClampMin = "0.1", ClampMax = "50000.0"))
	float BrushRadius;

	/** Strength of the brush (0.0 - 1.0) */
	UPROPERTY(EditAnywhere, Category = Brush, meta = (DisplayName = "Strength", UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0", EditConditionHides, EditCondition = "bShowFullSettings"))
	float BrushStrength;

	/** Amount of falloff to apply (0.0 - 1.0) */
	UPROPERTY(EditAnywhere, Category = Brush, meta = (DisplayName = "Falloff", UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0", EditConditionHides, EditCondition="bShowFullSettings"))
	float BrushFalloffAmount;

	/** If false, then BrushStrength and BrushFalloffAmount will not be shown in DetailsView panels (otherwise no effect)*/
	UPROPERTY()
	bool bShowFullSettings = true;

	//
	// save/restore support
	//
	virtual void SaveProperties(UInteractiveTool* SaveFromTool) override;
	virtual void RestoreProperties(UInteractiveTool* RestoreToTool) override;
};


/**
 * Generic Brush Stamp data
 */
USTRUCT()
struct INTERACTIVETOOLSFRAMEWORK_API FBrushStampData
{
	GENERATED_BODY();
	/** Radius of brush stamp */
	float Radius;
	/** World Position of brush stamp */
	FVector WorldPosition;
	/** World Normal of brush stamp */
	FVector WorldNormal;

	/** Hit Result provided by implementations - may not be fully populated */
	FHitResult HitResult;

	/** Falloff of brush stamp */
	float Falloff;
};



/**
 * UBaseBrushTool implements standard brush-style functionality for an InteractiveTool.
 * This includes:
 *   1) brush radius property set with dimension-relative brush sizing and default brush radius hotkeys
 *   2) brush indicator visualization
 *   3) tracking of last brush stamp location via .LastBrushStamp FProperty
 *   4) status of brush stroke via .bInBrushStroke FProperty
 */
UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UBaseBrushTool : public UMeshSurfacePointTool
{
	GENERATED_BODY()

public:
	UBaseBrushTool();

	virtual void RegisterActions(FInteractiveToolActionSet& ActionSet) override;

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void Tick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;


	//
	// UMeshSurfacePointTool implementation
	//
	virtual void OnBeginDrag(const FRay& Ray) override;
	virtual void OnUpdateDrag(const FRay& Ray) override;
	virtual void OnEndDrag(const FRay& Ray) override;
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;

public:

	/** Properties that control brush size/etc*/
	UPROPERTY()
	UBrushBaseProperties* BrushProperties;

	/** Set to true by Tool if user is currently in an active brush stroke*/
	UPROPERTY()
	bool bInBrushStroke = false;

	/** Position of brush at last update (both during stroke and during Hover) */
	UPROPERTY()
	FBrushStampData LastBrushStamp;

public:

	virtual void IncreaseBrushSizeAction();
	virtual void DecreaseBrushSizeAction();
	virtual void IncreaseBrushStrengthAction();
	virtual void DecreaseBrushStrengthAction();
	virtual void IncreaseBrushFalloffAction();
	virtual void DecreaseBrushFalloffAction();

	virtual bool IsInBrushStroke() const { return bInBrushStroke; }

protected:

	/**
	 * Subclasses should implement this to give an estimate of target dimension for brush size scaling
	 */
	virtual double EstimateMaximumTargetDimension() { return 100.0; }

protected:
	TInterval<float> BrushRelativeSizeRange;
	double CurrentBrushRadius;
	void RecalculateBrushRadius();

	UPROPERTY()
	TSoftClassPtr<UBrushBaseProperties> PropertyClass;

	//
	// Brush Indicator support
	//
protected:

	UPROPERTY()
	UBrushStampIndicator* BrushStampIndicator;

	virtual void SetupBrushStampIndicator();
	virtual void UpdateBrushStampIndicator();
	virtual void ShutdownBrushStampIndicator();
};
