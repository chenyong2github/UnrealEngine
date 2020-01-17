// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "UObject/ScriptInterface.h"

#include "IVirtualCameraController.generated.h"

class IVirtualCameraOptions;
class IVirtualCameraPresetContainer;
class UCineCameraComponent;
class ULevelSequencePlaybackController;

UENUM(BlueprintType)
enum class ETouchInputState : uint8
{
	/* Allows user to select an actor to always be in focus */
	ActorFocusTargeting,

	/* Allows user to select a point on the screen to auto-focus through */
	AutoFocusTargeting,

	/* Allows the touch input to be handled in the blueprint event. This should be the default */
	BlueprintDefined,

	/* Allows for the user to focus on target on touch without exiting manual focus */
	ManualTouchFocus,

	/* Touch support for scrubbing through a sequence */
	Scrubbing,

	/* Touch and hold for attach targeting */
	TouchAndHold,
};

USTRUCT(BlueprintType)
struct FTrackingOffset
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Settings")
	FVector Translation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Settings")
	FRotator Rotation;

	FTransform AsTransform() const
	{
		return FTransform(Rotation, Translation);
	}

	FTrackingOffset()
		: Translation(EForceInit::ForceInitToZero), Rotation(EForceInit::ForceInitToZero)
	{

	};
};

UINTERFACE(Blueprintable)
class VIRTUALCAMERA_API UVirtualCameraController: public UInterface
{
	GENERATED_BODY()
};

class VIRTUALCAMERA_API IVirtualCameraController
{
	GENERATED_BODY()
	
public:

	/** Returns the target camera that is used to create the streamed view. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VirtualCamera | Component")
	UCineCameraComponent* GetStreamedCameraComponent() const;

	/** Returns the recorded camera. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VirtualCamera | Component")
	UCineCameraComponent* GetRecordingCameraComponent() const;

	/** Returns the VirtualCamera's Sequence Controller. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VirtualCamera | Sequencer")
	ULevelSequencePlaybackController* GetSequenceController() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VirtualCamera | Presets")
	TScriptInterface<IVirtualCameraPresetContainer> GetPresetContainer() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VirtualCamera | Options")
	TScriptInterface<IVirtualCameraOptions> GetOptions() const;
};
