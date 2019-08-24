// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once
#include <openxr/openxr.h>
#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "XRInputSettings.generated.h"

UENUM()
enum EXRControllerPose
{
	GripPose,
	AimPose
};

USTRUCT()
struct FXRSuggestedBinding
{
	GENERATED_BODY()

	UPROPERTY(config, EditAnywhere, Category = Input)
	FString Path;

	UPROPERTY(config, EditAnywhere, Category = Input)
	FKey Key;
};

USTRUCT()
struct FXRInteractionProfileSettings
{
	GENERATED_BODY()

	bool HasControllers;
	bool HasHaptics;

	/** Indicates whether this profile is supported by the application. */
	UPROPERTY(config, EditAnywhere, Category = Input)
	bool Supported;

	/** Indicates whether the controller is held in a grip or aim pose. */
	UPROPERTY(config, EditAnywhere, Category = Input)
	TEnumAsByte<EXRControllerPose> ControllerPose;
};

/**
* Implements the settings for OpenXR input.
*/
UCLASS(config = Input, defaultconfig)
class OPENXRINPUT_API UXRInputSettings : public UObject
{
public:
	GENERATED_BODY()

	UPROPERTY(config, EditAnywhere, Category = "Interaction Profiles")
	FXRInteractionProfileSettings SimpleController;
	
	UPROPERTY(config, EditAnywhere, Category = "Suggested Bindings")
	TArray<FXRSuggestedBinding> SimpleBindings;

	UPROPERTY(config, EditAnywhere, Category = "Interaction Profiles")
	FXRInteractionProfileSettings DaydreamController;
	
	UPROPERTY(config, EditAnywhere, Category = "Suggested Bindings")
	TArray<FXRSuggestedBinding> DaydreamBindings;

	UPROPERTY(config, EditAnywhere, Category = "Interaction Profiles")
	FXRInteractionProfileSettings ViveController;
	
	UPROPERTY(config, EditAnywhere, Category = "Suggested Bindings")
	TArray<FXRSuggestedBinding> ViveBindings;

	UPROPERTY(config, EditAnywhere, Category = "Interaction Profiles")
	FXRInteractionProfileSettings ViveProHeadset;
	
	UPROPERTY(config, EditAnywhere, Category = "Suggested Bindings")
	TArray<FXRSuggestedBinding> ViveProBindings;

	UPROPERTY(config, EditAnywhere, Category = "Interaction Profiles")
	FXRInteractionProfileSettings MixedRealityController;
	
	UPROPERTY(config, EditAnywhere, Category = "Suggested Bindings")
	TArray<FXRSuggestedBinding> MixedRealityBindings;

	UPROPERTY(config, EditAnywhere, Category = "Interaction Profiles")
	FXRInteractionProfileSettings OculusGoController;
	
	UPROPERTY(config, EditAnywhere, Category = "Suggested Bindings")
	TArray<FXRSuggestedBinding> OculusGoBindings;

	UPROPERTY(config, EditAnywhere, Category = "Interaction Profiles")
	FXRInteractionProfileSettings OculusTouchController;
	
	UPROPERTY(config, EditAnywhere, Category = "Suggested Bindings")
	TArray<FXRSuggestedBinding> OculusTouchBindings;

	UPROPERTY(config, EditAnywhere, Category = "Interaction Profiles")
	FXRInteractionProfileSettings ValveIndexController;
	
	UPROPERTY(config, EditAnywhere, Category = "Suggested Bindings")
	TArray<FXRSuggestedBinding> ValveIndexBindings;

#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif

	static FSimpleMulticastDelegate OnSuggestedBindingsChanged;

	UXRInputSettings()
	{
		SimpleController.HasControllers = true;
		DaydreamController.HasControllers = true;
		ViveController.HasControllers = true;
		ViveProHeadset.HasControllers = false;
		MixedRealityController.HasControllers = true;
		OculusGoController.HasControllers = true;
		OculusTouchController.HasControllers = true;
		ValveIndexController.HasControllers = true;

		SimpleController.HasHaptics = false;
		DaydreamController.HasHaptics = false;
		ViveController.HasHaptics = true;
		ViveProHeadset.HasHaptics = false;
		MixedRealityController.HasHaptics = true;
		OculusGoController.HasHaptics = false;
		OculusTouchController.HasHaptics = true;
		ValveIndexController.HasHaptics = true;
	}
};
