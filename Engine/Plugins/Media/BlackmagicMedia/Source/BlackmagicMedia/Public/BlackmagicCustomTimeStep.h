// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FixedFrameRateCustomTimeStep.h"

#include "MediaIOCoreDefinitions.h"

#include "BlackmagicCustomTimeStep.generated.h"

namespace BlackmagicCustomTimeStepHelpers
{
	class FInputEventCallback;
	class FOutputEventCallback;
}

/**
 * Control the Engine TimeStep via the Blackmagic Design card.
 */
UCLASS(Blueprintable, editinlinenew, meta=(DisplayName="Blackmagic SDI Input", MediaIOCustomLayout="Blackmagic"))
class BLACKMAGICMEDIA_API UBlackmagicCustomTimeStep : public UFixedFrameRateCustomTimeStep
{
	GENERATED_UCLASS_BODY()

public:
	//~ UEngineCustomTimeStep interface
	virtual bool Initialize(class UEngine* InEngine) override;
	virtual void Shutdown(class UEngine* InEngine) override;
	virtual bool UpdateTimeStep(class UEngine* InEngine) override;
	virtual ECustomTimeStepSynchronizationState GetSynchronizationState() const override;
	virtual FFrameRate GetFixedFrameRate() const override;

	//~ UObject interface
	virtual void BeginDestroy() override;

private:
	void WaitForSync() const;
	void ReleaseResources();

public:
	/** The device, port and video settings that correspond to where the Genlock signal will be coming from */
	UPROPERTY(EditAnywhere, Category="Genlock", meta=(DisplayName="Configuration"))
	FMediaIOConfiguration MediaConfiguration;

	/** Enable mechanism to detect Engine loop overrunning the source */
	UPROPERTY(EditAnywhere, Category="Genlock options", meta=(DisplayName="Display Dropped Frames Warning"))
	bool bEnableOverrunDetection;

private:
	friend BlackmagicCustomTimeStepHelpers::FInputEventCallback;
	BlackmagicCustomTimeStepHelpers::FInputEventCallback* InputEventCallback;

	bool bWarnedAboutVSync;
};
