// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "LiveLinkSourceFactory.h"
#include "LiveLinkTypes.h"
#include "Misc/FrameNumber.h"
#include "Misc/FrameRate.h"
#include "Templates/SubclassOf.h"

#include "LiveLinkSourceSettings.generated.h"

UENUM()
enum class ELiveLinkSourceMode : uint8
{
	//The source will the latest frame available to evaluate its subjects.
	//This mode will not attempt any type of interpolation or time synchronization.
	Latest,

	//The source will use the engine's time to evaluate its subjects.
	//This mode is most useful when smooth animation is desired.
	EngineTime,

	//The source will use the engine's timecode to evaluate its subjects.
	//This mode is most useful when sources need to be synchronized with 
	//multiple other external inputs
	//(such as video or other time synchronized sources).
	//Should not be used when the engine isn't setup with a Timecode provider.
	Timecode,
};

USTRUCT()
struct FLiveLinkSourceBufferManagementSettings
{
	GENERATED_BODY()

	/** If the frame is older than ValidTime, remove it from the buffer list (in seconds). */
	UPROPERTY(EditAnywhere, Category = "Settings", meta=(ForceUnits=s))
	float ValidEngineTime = 1.0f;

	/** When evaluating with time: how far back from current time should we read the buffer (in seconds) */
	UPROPERTY(EditAnywhere, Category = "Settings", meta=(ForceUnits=s))
	float EngineTimeOffset;

	/** When evaluating with timecode: what is the expected frame rate of the timecode */
	UPROPERTY(EditAnywhere, Category = "Settings")
	FFrameRate TimecodeFrameRate = {24, 1};

	/** If the frame timecode is older than ValidTimecodeFrame, remove it from the buffer list (in TimecodeFrameRate). */
	UPROPERTY(EditAnywhere, Category = "Settings")
	int32 ValidTimecodeFrame = 30;

	/** When evaluating with timecode: how far back from current timecode should we read the buffer (in TimecodeFrameRate). */
	UPROPERTY(EditAnywhere, Category = "Settings")
	int32 TimecodeFrameOffset;

	/** Maximum number of frame to keep in memory. */
	UPROPERTY(EditAnywhere, Category = "Settings")
	int32 MaxNumberOfFrameToBuffered = 10;
};

USTRUCT()
struct FLiveLinkSourceDebugInfo
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Debug")
	FLiveLinkSubjectName SubjectName;

	UPROPERTY(VisibleAnywhere, Category = "Debug")
	int32 SnapshotIndex;

	UPROPERTY(VisibleAnywhere, Category = "Debug")
	int32 NumberOfBufferAtSnapshot;
};

/** Base class for live link source settings (can be replaced by sources themselves) */
UCLASS()
class LIVELINKINTERFACE_API ULiveLinkSourceSettings : public UObject
{
public:
	GENERATED_BODY()

	/**
	 * The the subject how to create the frame snapshot.
	 * @note A client may evaluate manually the subject in a different mode by using EvaluateFrameAtWorldTime or EvaluateFrameAtSceneTime.
	 */
	UPROPERTY(EditAnywhere, Category = "Settings", meta=(DisplayName="Evaluation Mode"))
	ELiveLinkSourceMode Mode = ELiveLinkSourceMode::EngineTime;

	/** How the frame buffers are managed. */
	UPROPERTY(EditAnywhere, Category = "Settings", meta=(ShowOnlyInnerProperties))
	FLiveLinkSourceBufferManagementSettings BufferSettings;

	/** Connection information that is needed by the factory to recreate the source from a preset. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Settings")
	FString ConnectionString;

	/** Factory used to create the source. */
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "Settings")
	TSubclassOf<ULiveLinkSourceFactory> Factory;

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Category = "Debug", meta=(ShowOnlyInnerProperties))
	TArray<FLiveLinkSourceDebugInfo> SourceDebugInfos;
#endif

	virtual void Serialize(FArchive& Ar) override;

#if WITH_EDITOR
	virtual bool CanEditChange(const UProperty* InProperty) const override;
#endif
};

USTRUCT()
struct
	UE_DEPRECATED(4.23, "FLiveLinkTimeSynchronizationSettings is now unused.")
	FLiveLinkTimeSynchronizationSettings
{
	GENERATED_BODY()

	FLiveLinkTimeSynchronizationSettings() : FrameRate(60, 1) {}

	/**
	 * The frame rate of the source.
	 * This should be the frame rate the source is "stamped" at, not necessarily the frame rate the source is sending.
	 * The source should supply this whenever possible.
	 */
	UPROPERTY(EditAnywhere, Category = Settings)
	FFrameRate FrameRate;

	/** When evaluating: how far back from current timecode should we read the buffer (in frame number) */
	UPROPERTY(EditAnywhere, Category = Settings)
	FFrameNumber FrameOffset;
};
 
USTRUCT()
struct 
	UE_DEPRECATED(4.23, "FLiveLinkInterpolationSettings is now unused.")
	FLiveLinkInterpolationSettings
{
	GENERATED_BODY()

	FLiveLinkInterpolationSettings() 
		: bUseInterpolation_DEPRECATED(false)
		, InterpolationOffset(0.5f) 
	{}

	UPROPERTY()
	bool bUseInterpolation_DEPRECATED;

	/** When interpolating: how far back from current time should we read the buffer (in seconds) */
	UPROPERTY(EditAnywhere, Category = Settings)
	float InterpolationOffset;
};
