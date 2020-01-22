// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"

#include "Delegates/Delegate.h"

#include "Misc/FrameRate.h"
#include "Misc/FrameTime.h"
#include "Misc/QualifiedFrameTime.h"

#include "ITimedDataInput.generated.h"


struct FSlateBrush;
class ITimedDataInput;
class ITimedDataInputGroup;
class SWidget;


UENUM()
enum class ETimedDataInputEvaluationType : uint8
{
	/** There is no special evaluation type for that input. */
	None,
	/** The input is evaluated from the engine's timecode. */
	Timecode,
	/** The input is evaluated from the engine's time. Note that the engine's time is relative to FPlatformTime::Seconds. */
	EngineTime,
};


UENUM()
enum class ETimedDataInputState : uint8
{
	/** The input is connected. */
	Connected,
	/** The input is connected but no data is available. */
	Unresponsive,
	/** The input is not connected. */
	Disconnected,
};


USTRUCT(BlueprintType)
struct TIMEMANAGEMENT_API FTimedDataInputBufferStats
{
	GENERATED_BODY()

	/**
	 * The number of evaluation requests that asked for data that was not available
	 * and the time requested is under the lowest value in the buffer.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Time")
	int32 BufferUnderflow = 0;
	
	/**
	 * The number of evaluation requests that asked for data that was not available
	 * and the time requested is over the biggest value in the buffer.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Time")
	int32 BufferOverflow = 0;

	/** Number of frame drops. */
	UPROPERTY(BlueprintReadWrite, Category = "Time")
	int32 FrameDrop = 0;
};


/**
 * Interface for data sources that can be synchronized with time
 */
class TIMEMANAGEMENT_API ITimedDataInput
{
public:
	struct FDataTime
	{
		FDataTime() = default;
		FDataTime(double InSeconds, const FQualifiedFrameTime& InTimecode)
			: Second(InSeconds), Timecode(InTimecode)
		{ }
		/** The time is relative to FPlatformTime::Seconds.*/
		double Second;
		/** Timecode value of the sample */
		FQualifiedFrameTime Timecode;
	};

	static FFrameRate UnknowedFrameRate;
	
	static double ConvertSecondOffsetInFrameOffset(double Seconds, FFrameRate Rate);
	static double ConvertFrameOffsetInSecondOffset(double Frames, FFrameRate Rate);
	
public:
	/**
	 * Get the group to which this input is attached to.
	 * It can return null when the input doesn't have a group.
	 */
	virtual ITimedDataInputGroup* GetGroup() const = 0;

	/** Get the current state of the input. */
	virtual ETimedDataInputState GetState() const = 0;

	/** Get the name used when displayed. */
	virtual FText GetDisplayName() const = 0;
	
	/** Get the time of all the data samples available. */
	virtual TArray<FDataTime> GetDataTimes() const = 0;

	/** Get how the input is evaluated. */
	virtual ETimedDataInputEvaluationType GetEvaluationType() const = 0;

	/** Set how the input is evaluated. */
	virtual void SetEvaluationType(ETimedDataInputEvaluationType Evaluation) = 0;

	/** Get the offset in seconds used at evaluation. */
	virtual double GetEvaluationOffsetInSeconds() const = 0;

	/** Set the offset in seconds used at evaluation. */
	virtual void SetEvaluationOffsetInSeconds(double Offset) = 0;

	/** Get the frame rate at which the samples is produce. */
	virtual FFrameRate GetFrameRate() const = 0;

	/** Get the size of the buffer used by the input. */
	virtual int32 GetDataBufferSize() const = 0;

	/** Set the size of the buffer used by the input. */
	virtual void SetDataBufferSize(int32 BufferSize) const = 0;

	/** Enable tracking stat */
	virtual bool IsBufferStatsEnabled() const = 0;

	/** */
	virtual void SetBufferStatsEnabled(bool bEnable) = 0;
	
	/** */
	virtual FTimedDataInputBufferStats GetBufferStats() const = 0;
	
	/** */
	virtual void ResetBufferStats() = 0;
};


/**
 * Interface for grouping TimedDataInput
 */
class TIMEMANAGEMENT_API ITimedDataInputGroup
{
public:

	/** Get the name to used when displayed. */
	virtual FText GetDisplayName() const = 0;

	/** Get the a description for this group. */
	virtual FText GetDescription() const = 0;

#if WITH_EDITOR
	/** Get the icon that represent the group. */
	virtual const FSlateBrush* GetDisplayIcon() const = 0;
#endif
};
