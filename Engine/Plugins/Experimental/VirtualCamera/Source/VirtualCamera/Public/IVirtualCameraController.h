// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/DelegateCombinations.h"
#include "LiveLinkRole.h"
#include "Misc/CoreMiscDefines.h"
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

USTRUCT(BlueprintType)
struct FVirtualCameraTransform
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VirtualCamera")
	FTransform Transform;
};

DECLARE_DYNAMIC_DELEGATE_RetVal_OneParam(FVirtualCameraTransform, FPreSetVirtualCameraTransform, FVirtualCameraTransform, CameraTransform);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVirtualCameraTickDelegateGroup, float, DeltaTime);
DECLARE_DYNAMIC_DELEGATE_OneParam(FVirtualCameraTickDelegate, float, DeltaTime);

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
	TScriptInterface<IVirtualCameraPresetContainer> GetPresetContainer();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VirtualCamera | Options")
	TScriptInterface<IVirtualCameraOptions> GetOptions();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VirtualCamera | Movement")
	FLiveLinkSubjectRepresentation GetLiveLinkRepresentation() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VirtualCamera | Movement")
	void SetLiveLinkRepresentation(const FLiveLinkSubjectRepresentation& InLiveLinkRepresenation);

	virtual bool StartStreaming() PURE_VIRTUAL(IVirtualCameraController::StartStreaming, return false;);

	virtual bool StopStreaming() PURE_VIRTUAL(IVirtualCameraController::StopStreaming, return false;);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VirtualCamera | Streaming")
	bool IsStreaming() const;

	/** Check whether settings should save when stream is stopped. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VirtualCamera | Settings")
	bool ShouldSaveSettingsOnStopStreaming() const;

	/** Sets whether settings should be saved when stream is stopped. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VirtualCamera | Settings")
	void SetSaveSettingsOnStopStreaming(bool bShouldSettingsSave);

	/** Delegate will be executed before transform is set onto VirtualCamera. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VirtualCamera | Movement")
	void SetBeforeSetVirtualCameraTransformDelegate(const FPreSetVirtualCameraTransform& InDelegate);

	/** Adds a delegate that will be executed every tick while streaming. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VirtualCamera")
	void AddOnVirtualCameraUpdatedDelegate(const FVirtualCameraTickDelegate& InDelegate);

	/** Remove delegate that is executed every tick while streaming. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VirtualCamera")
	void RemoveOnVirtualCameraUpdatedDelegate(const FVirtualCameraTickDelegate& InDelegate);
};
