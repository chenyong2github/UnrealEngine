// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/LazyObjectPtr.h"
#include "ControlRigEditModeSettings.generated.h"

/** Settings object used to show useful information in the details panel */
UCLASS()
class UControlRigEditModeSettings : public UObject
{
	GENERATED_BODY()

	UControlRigEditModeSettings()
		: bDisplayHierarchy(false)
		, bDisplaySpaces(false)
		, bHideManipulators(false)
		, bDisplayAxesOnSelection(false)
		, AxisScale(10.f)
		, bCoordSystemPerWidgetMode(true)
		, bOnlySelectRigControls(false)
		, bLocalTransformsInEachLocalSpace(true)
		, GizmoScale(1.0f)
	{}

	// UObject interface
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#if WITH_EDITOR
	virtual void PostEditUndo() override;
#endif
public:

	/** Whether to show all bones in the hierarchy */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Animation")
	bool bDisplayHierarchy;

	/** Whether to show all spaces in the hierarchy */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Animation")
	bool bDisplaySpaces;

	/** Should we always hide manipulators in viewport */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Animation")
	bool bHideManipulators;

	/** Should we show axes for the selected elements */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Animation")
	bool bDisplayAxesOnSelection;

	/** The scale for axes to draw on the selection */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Animation")
	float AxisScale;

	/** If true we restore the coordinate space when changing Widget Modes in the Viewport*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Animation")
	bool bCoordSystemPerWidgetMode;

	/** If true we can only select Rig Controls in the scene not other Actors. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Animation")
	bool bOnlySelectRigControls;

	/** If true when we transform multiple selected objects in the viewport they each transforms along their own local transform space */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Animation")
	bool bLocalTransformsInEachLocalSpace;
	
	/** The scale for Gizmos */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Animation")
	float GizmoScale;

	
};