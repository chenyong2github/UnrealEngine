// Copyright Epic Games, Inc. All Rights Reserved.
#include "AudioModulation.h"

#include "AudioModulationSystem.h"
#include "AudioModulationLogging.h"
#include "CanvasTypes.h"
#include "Features/IModularFeatures.h"
#include "IAudioModulation.h"
#include "Modules/ModuleManager.h"
#include "SoundControlBusMix.h"
#include "SoundModulationPatch.h"
#include "UnrealClient.h"


DEFINE_STAT(STAT_AudioModulationProcessControls);
DEFINE_STAT(STAT_AudioModulationProcessModulators);

namespace AudioModulation
{
	FAudioModulation::FAudioModulation()
	{
		ModSystem = MakeUnique<FAudioModulationSystem>();
	}

	FAudioModulationSystem* FAudioModulation::GetModulationSystem()
	{
		return ModSystem.Get();
	}

	Audio::FModulationParameter FAudioModulation::GetParameter(FName InParamName)
	{
		return ModSystem->GetParameter(InParamName);
	}

	void FAudioModulation::Initialize(const FAudioPluginInitializationParams& InitializationParams)
	{
		ModSystem->Initialize(InitializationParams);
	}

#if WITH_EDITOR
	void FAudioModulation::OnEditPluginSettings(const USoundModulationPluginSourceSettingsBase& Settings)
	{
		ModSystem->OnEditPluginSettings(Settings);
	}
#endif // WITH_EDITOR

	void FAudioModulation::OnInitSound(ISoundModulatable& Sound, const USoundModulationPluginSourceSettingsBase& Settings)
	{
		ModSystem->OnInitSound(Sound, Settings);
	}

	void FAudioModulation::OnInitSource(const uint32 SourceId, const uint32 NumChannels, const USoundModulationPluginSourceSettingsBase& Settings)
	{
		ModSystem->OnInitSource(SourceId, NumChannels, Settings);
	}

	void FAudioModulation::OnReleaseSound(ISoundModulatable& Sound)
	{
		ModSystem->OnReleaseSound(Sound);
	}

	void FAudioModulation::OnReleaseSource(const uint32 SourceId)
	{
		ModSystem->OnReleaseSource(SourceId);
	}

#if !UE_BUILD_SHIPPING
	bool FAudioModulation::OnPostHelp(FCommonViewportClient* ViewportClient, const TCHAR* Stream)
	{
		return ModSystem->OnPostHelp(ViewportClient, Stream);
	}

	int32 FAudioModulation::OnRenderStat(FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const UFont& Font, const FVector* ViewLocation, const FRotator* ViewRotation)
	{
		return ModSystem->OnRenderStat(Viewport, Canvas, X, Y, Font, ViewLocation, ViewRotation);
	}

	bool FAudioModulation::OnToggleStat(FCommonViewportClient* ViewportClient, const TCHAR* Stream)
	{
		return ModSystem->OnToggleStat(ViewportClient, Stream);
	}
#endif // !UE_BUILD_SHIPPING

	bool FAudioModulation::ProcessControls(const uint32 InSourceId, FSoundModulationControls& Controls)
	{
		SCOPE_CYCLE_COUNTER(STAT_AudioModulationProcessControls);
		return ModSystem->ProcessControls(InSourceId, Controls);
	}

	void FAudioModulation::ProcessModulators(const double InElapsed)
	{
		SCOPE_CYCLE_COUNTER(STAT_AudioModulationProcessModulators);
		ModSystem->ProcessModulators(InElapsed);
	}

	Audio::FModulatorTypeId FAudioModulation::RegisterModulator(uint32 InParentId, const USoundModulatorBase& InModulatorBase, Audio::FModulationParameter& OutParameter)
	{
		return ModSystem->RegisterModulator(InParentId, InModulatorBase, OutParameter);
	}

	void FAudioModulation::RegisterModulator(uint32 InParentId, Audio::FModulatorId InModulatorId)
	{
		ModSystem->RegisterModulator(InParentId, InModulatorId);
	}

	bool FAudioModulation::GetModulatorValue(const Audio::FModulatorHandle& ModulatorHandle, float& OutValue) const
	{
		return ModSystem->GetModulatorValue(ModulatorHandle, OutValue);
	}

	void FAudioModulation::UnregisterModulator(const Audio::FModulatorHandle& InHandle)
	{
		ModSystem->UnregisterModulator(InHandle);
	}

	void FAudioModulation::UpdateModulator(const USoundModulatorBase& InModulator)
	{
		ModSystem->UpdateModulator(InModulator);
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