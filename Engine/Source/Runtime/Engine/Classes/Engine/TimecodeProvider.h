// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "Misc/FrameRate.h"
#include "Misc/Timecode.h"
#include "Misc/QualifiedFrameTime.h"

#include "TimecodeProvider.generated.h"

/**
 * Possible states of TimecodeProvider.
 */
UENUM()
enum class ETimecodeProviderSynchronizationState
{
	/** TimecodeProvider has not been initialized or has been shutdown. */
	Closed,

	/** Unrecoverable error occurred during Synchronization. */
	Error,

	/** TimecodeProvider is currently synchronized with the source. */
	Synchronized,

	/** TimecodeProvider is initialized and being prepared for synchronization. */
	Synchronizing,
};

/**
 * A class responsible of fetching a timecode from a source.
 * Note, FApp::GetTimecode and FApp::GetTimecodeFramerate should be used to retrieve
 * the current system Timecode and Framerate.
 */
UCLASS(abstract)
class ENGINE_API UTimecodeProvider : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Number of frames to subtract from the qualified frame time when GetDelayedQualifiedFrameTime or GetDelayedTimecode is called.
	 * @see GetDelayedQualifiedFrameTime, GetDelayedTimecode
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Settings")
	float FrameDelay = 0.f;
	
	/**
	 * Return the frame number and the frame rate of the frame number at that moment. It may not be in sync with the current frame.
	 * Depending on the implementation, it may or may not be valid only when GetSynchronizationState() is Synchronized.
	*/
	UFUNCTION(BlueprintCallable, Category = "Provider")
	virtual FQualifiedFrameTime GetQualifiedFrameTime() const PURE_VIRTUAL(UTimecodeProvider::GetQualifiedFrameTime, return FQualifiedFrameTime(););

	/**
	 * Return the frame number and the frame rate of the frame number with the Frame Delay applied. It may not be in sync with the current frame.
	 * Depending on the implementation, it may or may not be valid only when GetSynchronizationState() is Synchronized.
	*/
	UFUNCTION(BlueprintCallable, Category = "Provider")
	FQualifiedFrameTime GetDelayedQualifiedFrameTime() const;

	/** Return the frame time converted into a timecode value. */
	UFUNCTION(BlueprintCallable, Category = "Provider")
	FTimecode GetTimecode() const;

	/** Return the delayed frame time converted into a timecode value. */
	UFUNCTION(BlueprintCallable, Category = "Provider")
	FTimecode GetDelayedTimecode() const;
	
	/** Return the frame rate of the frame time. */
	UFUNCTION(BlueprintCallable, Category = "Provider")
	FFrameRate GetFrameRate() const { return GetQualifiedFrameTime().Rate; }

	/** The state of the TimecodeProvider and if it's currently synchronized and the Timecode and FrameRate are valid. */
	UFUNCTION(BlueprintCallable, Category = "Provider")
	virtual ETimecodeProviderSynchronizationState GetSynchronizationState() const PURE_VIRTUAL(UTimecodeProvider::IsSynchronized, return ETimecodeProviderSynchronizationState::Closed;);

public:
	/** This Provider became the Engine's Provider. */
	virtual bool Initialize(class UEngine* InEngine) PURE_VIRTUAL(UTimecodeProvider::Initialize, return false;);

	/** This Provider stopped being the Engine's Provider. */
	virtual void Shutdown(class UEngine* InEngine) PURE_VIRTUAL(UTimecodeProvider::Shutdown, );
};
