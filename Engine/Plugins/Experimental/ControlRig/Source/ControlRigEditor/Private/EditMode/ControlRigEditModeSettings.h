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
		, bHideManipulators(false)
		, bDisplayAxesOnSelection(false)
		, AxisScale(10.f)
		, bCoordSystemPerWidgetMode(true)
		, bOnlySelectRigControls(false)
	{}

	// UObject interface
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;

public:

	/** Whether to show all nodes in the hierarchy being animated */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Animation")
	bool bDisplayHierarchy;

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
	
};