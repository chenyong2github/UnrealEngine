// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineAudioOutput.h"
#include "MoviePipeline.h"
#include "HAL/IConsoleManager.h"
#include "AudioDeviceManager.h"
#include "MovieRenderPipelineCoreModule.h"
#include "Engine/Engine.h"
#include "AudioMixerBlueprintLibrary.h"
#include "LevelSequence.h"
#include "AudioDevice.h"
#include "AudioMixerDevice.h"
// #include "AudioMixerPlatformNonRealtime.h"

void UMoviePipelineAudioOutput::SetupForPipelineImpl(UMoviePipeline* InPipeline)
{
	/*
	// Swap to the non-real-time audio renderer at runtime. This has to come before
	// we try to retrieve the cvar, otherwise the module won't have been loaded to create the cvar.
	check(GEngine);

	// Ensure that we try to play audio at full volume, even if we're unfocused.
	PrevUnfocusedAudioMultiplier = FApp::GetUnfocusedVolumeMultiplier();
	FApp::SetUnfocusedVolumeMultiplier(1.f);

	if (FAudioDeviceManager* AudioDeviceManager = GEngine->GetAudioDeviceManager())
	{
		PrevAudioDevicePlatform = AudioDeviceManager->GetCurrentAudioDeviceModuleName();
		bool bInitSuccessful = AudioDeviceManager->SwitchAudioDevicePlatform(TEXT("NonRealtimeAudioRenderer"));
		if (!bInitSuccessful)
		{
			UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Failed to initialize non-real-time Audio Device. Only supported on Windows. No audio output will be generated."));
			return;
		}

		if (bInitSuccessful)
		{
			IConsoleVariable* AudioRenderEveryTickCvar = IConsoleManager::Get().FindConsoleVariable(TEXT("au.nrt.RenderEveryTick"));
			check(AudioRenderEveryTickCvar);

			// Cache the value of their current setting
			PrevRenderEveryTickValue = AudioRenderEveryTickCvar->GetInt();

			// Override it to prevent it from automatically ticking, we'll control this below.
			AudioRenderEveryTickCvar->Set(0, ECVF_SetByConstructor);

			int32 PostRenderEveryTickValue = AudioRenderEveryTickCvar->GetInt();

			UE_LOG(LogMovieRenderPipeline, Log, TEXT("Successfully initialized Non-Real Time audio backend."));
		}
	}
	
	// Grab a rough estimate of how long the expected output is to avoid unnecessarily resizing arrays.
	// This may not be perfect (due to handle frames, slow-mo, etc.) but it's better than guessing.
	ULevelSequence* TargetSequence = InPipeline->GetTargetSequence();
	FFrameNumber StartFrame = TargetSequence->GetMovieScene()->GetPlaybackRange().GetLowerBoundValue();
	FFrameNumber EndFrame = TargetSequence->GetMovieScene()->GetPlaybackRange().GetUpperBoundValue();
	FFrameNumber SequenceDuration = (EndFrame - StartFrame);
	CachedExpectedDuration = TargetSequence->GetMovieScene()->GetTickResolution().AsInterval() * SequenceDuration.Value;

	// Start recording, but pause until the first shot starts producing frames.
	UAudioMixerBlueprintLibrary::StartRecordingOutput(GetWorld(), CachedExpectedDuration);
	UAudioMixerBlueprintLibrary::PauseRecordingOutput(GetWorld());*/
}

void UMoviePipelineAudioOutput::OnPipelineFinishedImpl()
{
	/*// Restore our cached CVar value.
	IConsoleVariable* AudioRenderEveryTickCvar = IConsoleManager::Get().FindConsoleVariable(TEXT("au.nrt.RenderEveryTick"));
	
	// This will be null if initialization failed.
	if (AudioRenderEveryTickCvar)
	{
		AudioRenderEveryTickCvar->Set(PrevRenderEveryTickValue, ECVF_SetByConstructor);
		
		// Convert it to absolute as the Audio Recorder will save relative paths to a different
		// directory than we consider to be our relative root.
		FString FormattedFileName = TEXT("Audio.wav");
		FString AbsoluteDirectory = FPaths::ConvertRelativePathToFull(GetPipeline()->GetPipelineConfig()->OutputDirectory.Path);
		UAudioMixerBlueprintLibrary::StopRecordingOutput(GetWorld(), EAudioRecordingExportType::WavFile, FormattedFileName, AbsoluteDirectory);
	}

	// Attempt to restore the previous Audio Platform.
	check(GEngine);
	if (FAudioDeviceManager* AudioDeviceManager = GEngine->GetAudioDeviceManager())
	{
		AudioDeviceManager->SwitchAudioDevicePlatform(PrevAudioDevicePlatform);
	}

	FApp::SetUnfocusedVolumeMultiplier(PrevUnfocusedAudioMultiplier);*/
}

void UMoviePipelineAudioOutput::OnPostTickImpl()
{
	/*check(GEngine);
	
	if (FAudioDevice* AudioDevice = GEngine->GetActiveAudioDevice())
	{
		// Handle any game logic that changed Audio State.
		AudioDevice->Update(true);

		// Now render some samples
		Audio::FMixerDevice* MixerDevice = static_cast<Audio::FMixerDevice*>(AudioDevice);
		Audio::FMixerPlatformNonRealtime* Platform = static_cast<Audio::FMixerPlatformNonRealtime*>(MixerDevice->GetAudioMixerPlatform());
		Platform->RenderAudio(FApp::GetDeltaTime());

	}

	// IAudioDeviceModule* AudioDeviceModule = FModuleManager::LoadModulePtr<IAudioDeviceModule>(TEXT("NonRealtimeAudioRenderer"));
	// Audio::FMixerPlatformNonRealtime NonRealtimePlatform = static_cast<Audio::FMixerPlatformNonRealtime>(AudioDeviceModule);

	// Now process the same amount of time as this frame for processing audio samples. The
	// App DeltaTime should be correct since we're controlling it each frame.
	// Audio::FMixerPlatformNonRealtime::RenderAudio(FApp::GetDeltaTime());*/
}

/* void UMoviePipelineAudioOutput::OnFrameProductionStartImpl()
{
	// When we start producing frames for a shot, unpause our recording.
	UAudioMixerBlueprintLibrary::ResumeRecordingOutput(GetWorld());
}*/

void UMoviePipelineAudioOutput::OnShotFinishedImpl(const FMoviePipelineShotInfo& Shot)
{
	// Pause our recording when the shot finishes. This lets us skip over any audio samples that
	// are generated between shots (ie: warm up frames) where we're not recording the output.
	// UAudioMixerBlueprintLibrary::PauseRecordingOutput(GetWorld());
}