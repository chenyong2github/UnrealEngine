// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Engine/Engine.h"
#include "UObject/NoExportTypes.h"
#include "GameFramework/SaveGame.h"
#include "MagicLeapARPinTypes.generated.h"

/** List of possible error values for MagicLeapARPin functions. */
UENUM(BlueprintType)
enum class EMagicLeapPassableWorldError : uint8
{
	/** No error. */
	None,
	/** Map quality too low for content persistence. Continue building the map. */
	LowMapQuality,
	/** Currently unable to localize into any map. Continue building the map. */
	UnableToLocalize,
	/** AR Pin is not available at this time. */
	Unavailable,
	/** Privileges not met. Add 'PcfRead' privilege to app manifest and request it at runtime. */
	PrivilegeDenied,
	/** Invalid function parameter. */
	InvalidParam,
	/** Unspecified error. */
	UnspecifiedFailure,
	/** Privilege has been requested but not yet granted by the user. */
	PrivilegeRequestPending,
	/** The MagicLeapARPin module is waiting for the startup of other services. */
	StartupPending,
	/** The MagicLeapARPin module or this particular function is not implemented in the current platform. */
	NotImplemented,
	/** Pin ID not found in environment */
	PinNotFound
};

/** Modes for automatically pinning content to real-world. */
UENUM(BlueprintType)
enum class EMagicLeapAutoPinType : uint8
{
	/** 
	 * Pin this component / owner actor automatically only if it was pinned in a previous run of the app or replicated over network.
	 * App needs to call PinSceneComponent() or PinActor() to pin for the very first time.
	 */
	OnlyOnDataRestoration,
	/** Always pin this component / owner actor automatically, without having to call PinSceneComponent() or PinActor() explicitely. */
	Always,
	/** Never pin this component / owner actor automatically. App will control pinning and unpinning itself. */
	Never
};

/** Base class to save game data associated with a given pin. Inherit from this class and set it to be the PinDataClass in the MagicLeapARPinComponent to save and restore related data. */
UCLASS(ClassGroup = MagicLeap, BlueprintType, Blueprintable)
class MAGICLEAPARPIN_API UMagicLeapARPinSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UMagicLeapARPinSaveGame();

	UPROPERTY(VisibleAnywhere, Category = "ContentPersistence|MagicLeap")
	FGuid PinnedID;

	UPROPERTY(VisibleAnywhere, Category = "ContentPersistence|MagicLeap")
	FTransform ComponentWorldTransform;

	UPROPERTY(VisibleAnywhere, Category = "ContentPersistence|MagicLeap")
	FTransform PinTransform;
};

/** Current state of a MagicLeapARPin */
USTRUCT(BlueprintType)
struct MAGICLEAPARPIN_API FMagicLeapARPinState
{
	GENERATED_USTRUCT_BODY()

public:
	/** A confidence value [0,1] representing the confidence in the error levels given below (within the valid radius). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ContentPersistence|MagicLeap")
	float Confidence;

	/** The radius (in centimeters) in which the confidence value is valid. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ContentPersistence|MagicLeap")
	float ValidRadius;

	/** Rotational error (in degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ContentPersistence|MagicLeap")
	float RotationError;

	/** Translation error (in centimeters). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ContentPersistence|MagicLeap")
	float TranslationError;

public:	
	FMagicLeapARPinState();
	FMagicLeapARPinState(float InConfidence, float InValidRadius, float InRotationError, float InTranslationError);

	FString ToString() const;
};

/**
 * Delegate to report updates in ARPins
 * @param Added List of ARPin IDs that were added
 * @param Updated List of ARPin IDs that were updated. Whether a pin is considered updated is determined by whehter any of its state parameters changed a specified delta.
 * 				  The delta thresholds can be set in Project Settings > MagicLeapARPin Plugin
 * @param Deleted List of ARPin IDs deleted
 */
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FMagicLeapARPinUpdatedDelegate, const TArray<FGuid>&, Added, const TArray<FGuid>&, Updated, const TArray<FGuid>&, Deleted);

/**
 * Delegate multicast delegate to report updates in ARPins
 * @param Added List of ARPin IDs that were added
 * @param Updated List of ARPin IDs that were updated. Whether a pin is considered updated is determined by whehter any of its state parameters changed a specified delta.
 * 				  The delta thresholds can be set in Project Settings > MagicLeapARPin Plugin
 * @param Deleted List of ARPin IDs deleted
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMagicLeapARPinUpdatedMultiDelegate, const TArray<FGuid>&, Added, const TArray<FGuid>&, Updated, const TArray<FGuid>&, Deleted);
