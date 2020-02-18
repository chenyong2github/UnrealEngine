// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/TimecodeProvider.h"

#include "MediaIOCoreDefinitions.h"

#include "BlackmagicTimecodeProvider.generated.h"

namespace BlackmagicTimecodeProviderHelpers
{
	class FEventCallback;
	class FEventCallbackWrapper;
}

/**
 * Class to fetch a timecode via a Blackmagic Design card.
 */
UCLASS(Blueprintable, editinlinenew, meta=(DisplayName="Blackmagic SDI Input", MediaIOCustomLayout="Blackmagic"))
class BLACKMAGICMEDIA_API UBlackmagicTimecodeProvider : public UTimecodeProvider
{
	GENERATED_UCLASS_BODY()

public:
	//~ UTimecodeProvider interface
	virtual FQualifiedFrameTime GetQualifiedFrameTime() const override;
	virtual ETimecodeProviderSynchronizationState GetSynchronizationState() const override;
	virtual bool Initialize(class UEngine* InEngine) override;
	virtual void Shutdown(class UEngine* InEngine) override;

	//~ UObject interface
	virtual void BeginDestroy() override;

private:
	void ReleaseResources();

public:
	/** The device, port and video settings that correspond to the input. */
	UPROPERTY(EditAnywhere, Category="Blackmagic")
	FMediaIOConfiguration MediaConfiguration;

	/** Timecode format to read from a video signal. */
	UPROPERTY(EditAnywhere, Category="Blackmagic")
	EMediaIOTimecodeFormat TimecodeFormat;

private:
	friend BlackmagicTimecodeProviderHelpers::FEventCallback;
	BlackmagicTimecodeProviderHelpers::FEventCallback* EventCallback;
};
