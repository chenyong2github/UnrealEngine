// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "ARTrackable.h"

#include "AppleARKitSettings.generated.h"

UENUM(BlueprintType, Category="AR AugmentedReality", meta=(Experimental))
enum class EARFaceTrackingFileWriterType : uint8
{
	/** Disables creation of a file writer */
	None,
	/** Comma delimited file, one row per captured frame */
	CSV,
	/** JSON object array, one frame object per captured frame */
	JSON
};


UCLASS(Config=Engine, defaultconfig)
class APPLEARKIT_API UAppleARKitSettings :
	public UObject
{
	GENERATED_BODY()

public:
	UAppleARKitSettings()
		: bEnableLiveLinkForFaceTracking(false)
		, bFaceTrackingWriteEachFrame(false)
		, FaceTrackingFileWriterType(EARFaceTrackingFileWriterType::None)
		, LiveLinkPublishingPort(11111)
		, DefaultFaceTrackingLiveLinkSubjectName(FName("iPhoneXFaceAR"))
		, DefaultFaceTrackingDirection(EARFaceTrackingDirection::FaceRelative)
		, bAdjustThreadPrioritiesDuringARSession(false)
		, GameThreadPriorityOverride(47)
		, RenderThreadPriorityOverride(45)
	{
	}

	/** Whether to publish face blend shapes to LiveLink or not */
	UPROPERTY(Config, EditAnywhere, Category="AR Settings")
	bool bEnableLiveLinkForFaceTracking;

	/** Whether to publish each frame or when the "FaceAR WriteCurveFile */
	UPROPERTY(Config, EditAnywhere, Category="AR Settings")
	bool bFaceTrackingWriteEachFrame;

	/** The type of face AR publisher that writes to disk to create */
	UPROPERTY(Config, EditAnywhere, Category="AR Settings")
	EARFaceTrackingFileWriterType FaceTrackingFileWriterType;

	/** The port to use when listening/sending LiveLink face blend shapes via the network */
	UPROPERTY(Config, EditAnywhere, Category="AR Settings")
	int32 LiveLinkPublishingPort;

	/** The default name to use when publishing face tracking name */
	UPROPERTY(Config, EditAnywhere, Category="AR Settings")
	FName DefaultFaceTrackingLiveLinkSubjectName;

	/** The default tracking to use when tracking face blend shapes (face relative or mirrored). Defaults to face relative */
	UPROPERTY(Config, EditAnywhere, Category="AR Settings")
	EARFaceTrackingDirection DefaultFaceTrackingDirection;

	/** Whether to adjust thread priorities during an AR session or not */
	UPROPERTY(Config, EditAnywhere, Category="AR Settings")
	bool bAdjustThreadPrioritiesDuringARSession;

	/** The game thread priority to change to when an AR session is running, default is 47 */
	UPROPERTY(Config, EditAnywhere, Category="AR Settings")
	int32 GameThreadPriorityOverride;
	
	/** The render thread priority to change to when an AR session is running, default is 45 */
	UPROPERTY(Config, EditAnywhere, Category="AR Settings")
	int32 RenderThreadPriorityOverride;
};
