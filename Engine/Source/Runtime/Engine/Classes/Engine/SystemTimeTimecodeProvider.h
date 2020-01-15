// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TimecodeProvider.h"
#include "SystemTimeTimecodeProvider.generated.h"

/**
 * Converts the current system time to timecode, relative to a provided frame rate.
 */
UCLASS(config=Engine, Blueprintable, editinlinenew)
class ENGINE_API USystemTimeTimecodeProvider : public UTimecodeProvider
{
	GENERATED_BODY()

private:

	UPROPERTY(EditAnywhere, Category = Timecode)
	FFrameRate FrameRate;

	/** When generating frame time, should we generate full frame without subframe value.*/
	UPROPERTY(EditAnywhere, Category = Timecode)
	bool bGenerateFullFrame;

	ETimecodeProviderSynchronizationState State;

public:

	USystemTimeTimecodeProvider();

	/** Generate a frame time value, including subframe, using the system clock. */
	static FFrameTime GenerateFrameTimeFromSystemTime(FFrameRate Rate);

	/** Generate a timecode value using the system clock. */
	static FTimecode GenerateTimecodeFromSystemTime(FFrameRate Rate);

	/**
	 * Generate a frame time value, including subframe, using the high performance clock
	 * Using the high performance clock is faster but will make the value drift over time.
	 * This is an optimized version. Prefer GenerateTimecodeFromSystemTime, if the value need to be accurate.
	 **/
	static FFrameTime GenerateFrameTimeFromHighPerformanceClock(FFrameRate Rate);

	/**
	 * Generate a timecode value using the high performance clock
	 * Using the high performance clock is faster but will make the value drift over time.
	 * This is an optimized version. Prefer GenerateTimecodeFromSystemTime, if the value need to be accurate.
	 **/
	static FTimecode GenerateTimecodeFromHighPerformanceClock(FFrameRate Rate);

	//~ Begin UTimecodeProvider Interface
	virtual FQualifiedFrameTime GetQualifiedFrameTime() const override;
	
	virtual ETimecodeProviderSynchronizationState GetSynchronizationState() const override
	{
		return State;
	}

	virtual bool Initialize(class UEngine* InEngine) override
	{
		State = ETimecodeProviderSynchronizationState::Synchronized;
		return true;
	}

	virtual void Shutdown(class UEngine* InEngine) override
	{
		State = ETimecodeProviderSynchronizationState::Closed;
	}
	//~ End UTimecodeProvider Interface

	UFUNCTION()
	void SetFrameRate(const FFrameRate& InFrameRate)
	{
		FrameRate = InFrameRate;
	}
};
