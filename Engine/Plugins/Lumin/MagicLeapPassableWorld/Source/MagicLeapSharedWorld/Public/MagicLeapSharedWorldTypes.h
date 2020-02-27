// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "MagicLeapARPinTypes.h"
#include "MagicLeapSharedWorldTypes.generated.h"

/**
	Transforms to be used by all clients to align their coordinate spaces. 
	These transforms are sent by either an authoritative or pseudo-authoritative ("chosen one")
	client to the server via AMagicLeapSharedWorldPlayerController::ServerSetAlignmentTransforms()
	which redirects it to the AMagicLeapSharedWorldGameState instance from where these transforms
	are replicated to all clients to use locally for alignment.
	These transforms can also be set by the server, if they are saved from a previous session.
*/
USTRUCT(BlueprintType)
struct MAGICLEAPSHAREDWORLD_API FMagicLeapSharedWorldAlignmentTransforms
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category="AR Shared World|Magic Leap")
	TArray<FTransform> AlignmentTransforms;
};

USTRUCT(BlueprintType)
struct MAGICLEAPSHAREDWORLD_API FMagicLeapSharedWorldPinData
{
	GENERATED_BODY()

public:
    UPROPERTY(BlueprintReadWrite, Category = "ContentPersistence|MagicLeap")
    FGuid PinID;

    UPROPERTY(BlueprintReadWrite, Category = "ContentPersistence|MagicLeap")
    FMagicLeapARPinState PinState;
};

USTRUCT(BlueprintType)
struct MAGICLEAPSHAREDWORLD_API FMagicLeapSharedWorldLocalData
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category="AR Shared World|Magic Leap")
	TArray<FMagicLeapSharedWorldPinData> LocalPins;
};

USTRUCT(BlueprintType)
struct MAGICLEAPSHAREDWORLD_API FMagicLeapSharedWorldSharedData
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category="AR Shared World|Magic Leap")
	TArray<FGuid> PinIDs;
};

DECLARE_LOG_CATEGORY_EXTERN(LogMagicLeapSharedWorld, Verbose, All);
