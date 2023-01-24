// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AudioDeviceHandle.h"
#include "MediaIOCoreAudioOutput.h"
#include "Subsystems/EngineSubsystem.h"

#include "MediaIOCoreSubsystem.generated.h"

UCLASS()
class MEDIAIOCORE_API UMediaIOCoreSubsystem : public UEngineSubsystem
{
public:
	struct FCreateAudioOutputArgs
	{
		uint32 NumOutputChannels = 0;
		FFrameRate TargetFrameRate; 
		uint32 MaxSampleLatency = 0;
		uint32 OutputSampleRate = 0;
		FAudioDeviceHandle AudioDeviceHandle;
	};

public:
	GENERATED_BODY()

	//~ Begin UEngineSubsystem Interface
	virtual void Initialize(FSubsystemCollectionBase& InCollection) override;
	virtual void Deinitialize() override;
	//~ End UEngineSubsystem Interface

	/**
	 * Create an audio output that allows getting audio that was accumulated during the last frame. 
	 */
	TSharedPtr<FMediaIOAudioOutput> CreateAudioOutput(const FCreateAudioOutputArgs& InArgs);

private:
	void OnAudioDeviceDestroyed(Audio::FDeviceId InAudioDeviceId);

private:
	TUniquePtr<FMediaIOAudioCapture> MainMediaIOAudioCapture;
	
	TMap<Audio::FDeviceId, TUniquePtr<FMediaIOAudioCapture>> MediaIOAudioCaptures;

	FDelegateHandle DeviceDestroyedHandle;
};
