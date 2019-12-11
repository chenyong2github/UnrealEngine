// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "EditorUtilityWidget.h"

#include "VPUtilitiesEditorSettings.generated.h"

/**
 * Virtual Production utilities settings for editor
 */
UCLASS(config=VirtualProductionUtilities)
class VPUTILITIESEDITOR_API UVPUtilitiesEditorSettings : public UObject
{
	GENERATED_BODY()

public:

	/** The default user interface that we'll use for virtual scouting */
	UPROPERTY(EditAnywhere, config, Category = "Virtual Production", meta = (DisplayName = "Virtual Scouting User Interface"))
	TSoftClassPtr<UEditorUtilityWidget> VirtualScoutingUI;
	
	/** Speed when flying in VR*/
	UPROPERTY(EditAnywhere, config, Category = "Virtual Production", meta = (ClampMin = "0.0", ClampMax = "1.0", DisplayName = "Virtual Scouting Flight Speed"))
	float FlightSpeed = 0.5f;
	
	/** Speed when using grip nav in VR */
	UPROPERTY(EditAnywhere, config, Category = "Virtual Production", meta = (ClampMin = "0.0", ClampMax = "1.0", DisplayName = "Virtual Scouting Grip Nav Speed"))
	float GripNavSpeed = 0.25f;
	
	/** Whether to use the metric system or imperial for measurements */
	UPROPERTY(EditAnywhere, config, Category = "Virtual Production", meta = (DisplayName = "Show Measurements In Metric Units"))
	bool bUseMetric = false;
	
	/** Whether to enable or disable the transform gizmo */
	UPROPERTY(EditAnywhere, config, Category = "Virtual Production", meta = (DisplayName = "Enable Transform Gizmo In VR"))
	bool bUseTransformGizmo = false;
	
	/** If true, the user will use inertia damping to stop after grip nav. Otherwise the user will just stop immediately */
	UPROPERTY(EditAnywhere, config, Category = "Virtual Production", meta = (DisplayName = "Use Grip Inertia Damping"))
	bool bUseGripInertiaDamping = true;
	
	/** Damping applied to inertia */
	UPROPERTY(EditAnywhere, config, Category = "Virtual Production", meta = (ClampMin = "0.0", ClampMax = "1.0", DisplayName = "Inertia Damping"))
	float InertiaDamping = 0.95f;

	/** Whether the helper system on the controllers is enabled */
	UPROPERTY(EditAnywhere, config, Category = "Virtual Production", meta = (DisplayName = "Helper System Enabled"))
	bool bIsHelperSystemEnabled = true;
	
	/** ScoutingSubsystem class to use for Blueprint helpers */
	UPROPERTY(config, meta = (MetaClass = "VPScoutingSubsystemHelpersBase"))
	FSoftClassPath ScoutingSubsystemEditorUtilityClassPath;

	/** GestureManager class to use by the ScoutingSubsystem */
	UPROPERTY(config, meta = (MetaClass = "VPScoutingSubsystemGestureManagerBase"))
	FSoftClassPath GestureManagerEditorUtilityClassPath;

	/** GestureManager class to use by the ScoutingSubsystem */
	UPROPERTY(config)
	TArray<FSoftClassPath> AdditionnalClassToLoad;

public:

	//~ UObject interface
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObject interface

};