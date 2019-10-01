// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MagicLeapHMDFunctionLibrary.generated.h"

UENUM(BlueprintType)
enum class EMagicLeapHeadTrackingError : uint8
{
	None,
	NotEnoughFeatures,
	LowLight,
	Unknown
};

UENUM(BlueprintType)
enum class EMagicLeapHeadTrackingMode : uint8
{
	PositionAndOrientation,
	Unavailable,
	Unknown
};

/** Different types of map events that can occur that a developer may have to handle. */
UENUM(BlueprintType)
enum class EMagicLeapHeadTrackingMapEvent : uint8
{
	/** Map was lost. It could possibly recover. */
	Lost,
	/** Previous map was recovered. */
	Recovered,
	/** Failed to recover previous map. */
	RecoveryFailed,
	/** New map session created. */
	NewSession
};

USTRUCT(BlueprintType)
struct FMagicLeapHeadTrackingState
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MagicLeap")
	EMagicLeapHeadTrackingMode Mode;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MagicLeap")
	EMagicLeapHeadTrackingError Error;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MagicLeap")
	float Confidence;
};

UCLASS(ClassGroup = MagicLeap)
class MAGICLEAP_API UMagicLeapHMDFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Input|MagicLeap", meta = (DeprecatedFunction, DeprecationMessage = "Use XRPawn setup for coordinate space calibration"))
	static void SetBasePosition(const FVector& InBasePosition);

	UFUNCTION(BlueprintCallable, Category = "Input|MagicLeap", meta = (DeprecatedFunction, DeprecationMessage = "Use XRPawn setup for coordinate space calibration"))
	static void SetBaseOrientation(const FQuat& InBaseOrientation);

	UFUNCTION(BlueprintCallable, Category = "Input|MagicLeap", meta = (DeprecatedFunction, DeprecationMessage = "Use XRPawn setup for coordinate space calibration"))
	static void SetBaseRotation(const FRotator& InBaseRotation);

   /**
	* Set the actor whose location is used as the focus point. The focus distance is the distance from the HMD to the focus point.
	*
	* @param InFocusActor			The actor that will be set as the new focus actor.
	* @param bSetStabilizationActor  True if the function should set the stabilization depth actor to match the passed in focus actor. (RECOMMENDED TO STAY CHECKED)
	*/
	UFUNCTION(BlueprintCallable, Category = "Rendering|MagicLeap")
	static void SetFocusActor(const AActor* InFocusActor, bool bSetStabilizationActor = true);

   /**
	* Set the actor whose location is used as the depth for timewarp stabilization.
	*
	* @param InStabilizationDepthActor  The actor that will be set as the new stabilization depth actor.
	* @param bSetFocusActor				True if the function should set the focus actor to match the passed in stabilization depth actor. (RECOMMENDED TO STAY CHECKED)
	*/
	UFUNCTION(BlueprintCallable, Category = "Rendering|MagicLeap")
	static void SetStabilizationDepthActor(const AActor* InStabilizationDepthActor, bool bSetFocusActor = true);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "MagicLeap")
	static int32 GetMLSDKVersionMajor();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "MagicLeap")
	static int32 GetMLSDKVersionMinor();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "MagicLeap")
	static int32 GetMLSDKVersionRevision();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "MagicLeap")
	static FString GetMLSDKVersion();

	/** Returns true if this code is executing on the ML HMD, false otherwise (e.g. it's executing on PC) */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "MagicLeap")
	static bool IsRunningOnMagicLeapHMD();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "MagicLeap")
	static bool GetHeadTrackingState(FMagicLeapHeadTrackingState& State);

	/**
		Get map events.
		A developer must be aware of certain events that can occur under degenerative conditions
		in order to cleanly handle it. The most important event to be aware of is when a map changes.
		In the case that a new map session begins, or recovery fails, all formerly cached transform
		and world reconstruction data (raycast, planes, mesh) is invalidated and must be updated.
		@param MapEvents Set of map events occured since the last frame. Valid only if function returns true.
		@return true if call get map events from the platform succeeded, false otherwise.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "MagicLeap")
	static bool GetHeadTrackingMapEvents(TSet<EMagicLeapHeadTrackingMapEvent>& MapEvents);

	/**
		Notifies lifecycle that the app is ready to run (dismisses the loading logo).
		@note This MUST be called if you have checked bManualCallToAppReady in LuminRuntimeSettings.  Failure to do so will cause the 
		application to remain in the loading state.
	*/
	UFUNCTION(BlueprintCallable, Category = "MagicLeap")
	static bool SetAppReady();
};
