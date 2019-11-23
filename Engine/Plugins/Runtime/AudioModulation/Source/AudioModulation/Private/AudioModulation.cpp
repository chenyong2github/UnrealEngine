// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "AudioModulation.h"

#include "AudioModulationInternal.h"
#include "AudioModulationLogging.h"
#include "CanvasTypes.h"
#include "Features/IModularFeatures.h"
#include "IAudioExtensionPlugin.h"
#include "Modules/ModuleManager.h"
#include "SoundModulationPatch.h"
#include "UnrealClient.h"


DEFINE_STAT(STAT_AudioModulationProcessControls);
DEFINE_STAT(STAT_AudioModulationProcessModulators);

namespace AudioModulation
{
	FAudioModulation::FAudioModulation()
	{
		Impl = TUniquePtr<FAudioModulationImpl>(new FAudioModulationImpl());
	}

	FAudioModulationImpl* FAudioModulation::GetImpl()
	{
		return Impl.Get();
	}

	float FAudioModulation::CalculateInitialVolume(const USoundModulationPluginSourceSettingsBase& Settings)
	{
		return Impl->CalculateInitialVolume(Settings);
	}

	void FAudioModulation::Initialize(const FAudioPluginInitializationParams& InitializationParams)
	{
		Impl->Initialize(InitializationParams);
	}

#if WITH_EDITOR
	void FAudioModulation::OnEditPluginSettings(const USoundModulationPluginSourceSettingsBase& Settings)
	{
		Impl->OnEditPluginSettings(Settings);
	}
#endif // WITH_EDITOR

	void FAudioModulation::OnInitSound(ISoundModulatable& Sound, const USoundModulationPluginSourceSettingsBase& Settings)
	{
		Impl->OnInitSound(Sound, Settings);
	}

	void FAudioModulation::OnInitSource(const uint32 SourceId, const FName& AudioComponentUserId, const uint32 NumChannels, const USoundModulationPluginSourceSettingsBase& Settings)
	{
		Impl->OnInitSource(SourceId, AudioComponentUserId, NumChannels, Settings);
	}

	void FAudioModulation::OnReleaseSound(ISoundModulatable& Sound)
	{
		Impl->OnReleaseSound(Sound);
	}

	void FAudioModulation::OnReleaseSource(const uint32 SourceId)
	{
		Impl->OnReleaseSource(SourceId);
	}

#if !UE_BUILD_SHIPPING
	bool FAudioModulation::OnPostHelp(FCommonViewportClient* ViewportClient, const TCHAR* Stream)
	{
		return Impl->OnPostHelp(ViewportClient, Stream);
	}

	int32 FAudioModulation::OnRenderStat(FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const UFont& Font, const FVector* ViewLocation, const FRotator* ViewRotation)
	{
		return Impl->OnRenderStat(Viewport, Canvas, X, Y, Font, ViewLocation, ViewRotation);
	}

	bool FAudioModulation::OnToggleStat(FCommonViewportClient* ViewportClient, const TCHAR* Stream)
	{
		return Impl->OnToggleStat(ViewportClient, Stream);
	}
#endif // !UE_BUILD_SHIPPING

	bool FAudioModulation::ProcessControls(const uint32 InSourceId, FSoundModulationControls& OutControls)
	{
		SCOPE_CYCLE_COUNTER(STAT_AudioModulationProcessControls);
		return Impl->ProcessControls(InSourceId, OutControls);
	}

	void FAudioModulation::ProcessModulators(const float Elapsed)
	{
		SCOPE_CYCLE_COUNTER(STAT_AudioModulationProcessModulators);
		Impl->ProcessModulators(Elapsed);
	}
} // namespace AudioModulation

TAudioModulationPtr FAudioModulationPluginFactory::CreateNewModulationPlugin(FAudioDevice* OwningDevice)
{
	return TAudioModulationPtr(new AudioModulation::FAudioModulation());
}

void FAudioModulationModule::StartupModule()
{
	UE_LOG(LogAudioModulation, Log, TEXT("Starting Audio Modulation Module"));

	IModularFeatures::Get().RegisterModularFeature(FAudioModulationPluginFactory::GetModularFeatureName(), &ModulationPluginFactory);
}

void FAudioModulationModule::ShutdownModule()
{
	UE_LOG(LogAudioModulation, Log, TEXT("Shutting Down Audio Modulation Module"));

	IModularFeatures::Get().UnregisterModularFeature(FAudioModulationPluginFactory::GetModularFeatureName(), &ModulationPluginFactory);
}

IMPLEMENT_MODULE(FAudioModulationModule, AudioModulation);