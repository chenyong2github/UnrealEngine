// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "InteractiveToolActivity.h"

#include "PolyEditInsetOutsetActivity.generated.h"

class UPolyEditActivityContext;
class UPolyEditPreviewMesh;
class USpatialCurveDistanceMechanic;

UCLASS()
class MESHMODELINGTOOLS_API UPolyEditInsetOutsetProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	//~ TODO: We should not have to have the user tell us when it's an inset/offset, we should just do the proper
	//~ thing based on where the user clicks.
	UPROPERTY(EditAnywhere, Category = "Inset/Outset")
	bool bOutset = false;

	/** Amount of smoothing applied to outset boundary */
	UPROPERTY(EditAnywhere, Category = "Inset/Outset", 
		meta = (UIMin = "0.0", UIMax = "1.0", EditCondition = "bBoundaryOnly == false"))
	float Softness = 0.5;

	/** Controls whether outset operation will move interior vertices as well as border vertices */
	UPROPERTY(EditAnywhere, Category = "Inset/Outset", AdvancedDisplay)
	bool bBoundaryOnly = false;

	/** Tweak area scaling when solving for interior vertices */
	UPROPERTY(EditAnywhere, Category = "Inset/Outset", AdvancedDisplay, 
		meta = (UIMin = "0.0", UIMax = "1.0", EditCondition = "bBoundaryOnly == false"))
	float AreaScale = true;

	/** When insetting, determines whether vertices in inset region should be projected back onto input surface */
	UPROPERTY(EditAnywhere, Category = "Inset/Outset")
	bool bReproject = true;


};


/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UPolyEditInsetOutsetActivity : public UInteractiveToolActivity,
	public IClickBehaviorTarget, public IHoverBehaviorTarget
{
	GENERATED_BODY()

public:
	// IInteractiveToolActivity
	virtual void Setup(UInteractiveTool* ParentTool) override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;
	virtual bool CanStart() const override;
	virtual EToolActivityStartResult Start() override;
	virtual bool IsRunning() const override { return bIsRunning; }
	virtual bool CanAccept() const override;
	virtual EToolActivityEndResult End(EToolShutdownType) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual void Tick(float DeltaTime) override;

	// IClickBehaviorTarget API
	virtual FInputRayHit IsHitByClick(const FInputDeviceRay& ClickPos) override;
	virtual void OnClicked(const FInputDeviceRay& ClickPos) override;

	// IHoverBehaviorTarget implementation
	virtual FInputRayHit BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos) override;
	virtual void OnBeginHover(const FInputDeviceRay& DevicePos) override {}
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;
	virtual void OnEndHover() override {}

	UPROPERTY()
	UPolyEditInsetOutsetProperties* InsetProperties;

protected:
	void Clear();
	void BeginInset();
	void ApplyInset();

	bool bIsRunning = false;
	bool bPreviewUpdatePending = false;

	UPROPERTY()
	UPolyEditPreviewMesh* EditPreview;

	UPROPERTY()
	USpatialCurveDistanceMechanic* CurveDistMechanic = nullptr;

	UPROPERTY()
	TObjectPtr<UPolyEditActivityContext> ActivityContext;

	float UVScaleFactor = 1.0f;
};
