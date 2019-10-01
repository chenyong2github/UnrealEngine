// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
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
	/** Privileges not met. Add 'PwFoundObjRead' privilege to app manifest and request it at runtime. */
	PrivilegeDenied,
	/** Invalid function parameter. */
	InvalidParam,
	/** Unspecified error. */
	UnspecifiedFailure,
	/** Privilege has been requested but not yet granted by the user. */
	PrivilegeRequestPending
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
