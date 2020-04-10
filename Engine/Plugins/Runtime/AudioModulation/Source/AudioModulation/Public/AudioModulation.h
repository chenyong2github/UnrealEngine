// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeBool.h"
#include "IAudioExtensionPlugin.h"
#include "IAudioModulation.h"
#include "Modules/ModuleInterface.h"
#include "SoundModulationPatch.h"
#include "SoundModulationSettings.h"
#include "Stats/Stats.h"


// Cycle stats for audio mixer
DECLARE_STATS_GROUP(TEXT("AudioModulation"), STATGROUP_AudioModulation, STATCAT_Advanced);

// Tracks the time for the full render block 
DECLARE_CYCLE_STAT_EXTERN(TEXT("Process Modulators"), STAT_AudioModulationProcessModulators, STATGROUP_AudioModulation, AUDIOMODULATION_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Process Controls"), STAT_AudioModulationProcessControls, STATGROUP_AudioModulation, AUDIOMODULATION_API);

namespace AudioModulation
{
	class FAudioModulationSystem;

	class AUDIOMODULATION_API FAudioModulation : public IAudioModulation
	{
	public:
		FAudioModulation();
		virtual ~FAudioModulation() = default;

		//~ Begin IAudioModulation implementation
		virtual float CalculateInitialVolume(const USoundModulationPluginSourceSettingsBase& InSettingsBase) override;

		virtual void Initialize(const FAudioPluginInitializationParams& InitializationParams) override;
		virtual void OnBeginAudioRenderThreadUpdate() override;
		virtual void OnInitSound(ISoundModulatable& Sound, const USoundModulationPluginSourceSettingsBase& Settings) override;
		virtual void OnInitSource(const uint32 SourceId, const FName& AudioComponentUserId, const uint32 NumChannels, const USoundModulationPluginSourceSettingsBase& Settings) override;

#if !UE_BUILD_SHIPPING
		virtual bool OnPostHelp(FCommonViewportClient* ViewportClient, const TCHAR* Stream) override;
		virtual int32 OnRenderStat(FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const UFont& Font, const FVector* ViewLocation, const FRotator* ViewRotation) override;
		virtual bool OnToggleStat(FCommonViewportClient* ViewportClient, const TCHAR* Stream) override;
#endif // !UE_BUILD_SHIPPING

		virtual void OnReleaseSound(ISoundModulatable& Sound) override;
		virtual void OnReleaseSource(const uint32 SourceId) override;
		virtual bool ProcessControls(const uint32 SourceId, FSoundModulationControls& Controls) override;
		virtual void ProcessModulators(const float Elapsed) override;

		virtual void UpdateModulator(const USoundModulatorBase& InModulator) override;
		//~ End IAudioModulation implementation

#if WITH_EDITOR
		void OnEditPluginSettings(const USoundModulationPluginSourceSettingsBase& Settings);
#endif // WITH_EDITOR

		FAudioModulationSystem* GetModulationSystem();

	protected:
		virtual bool RegisterModulator(uint32 InParentId, const USoundModulatorBase& InModulatorBase) override;
		virtual bool RegisterModulator(uint32 InParentId, Audio::FModulatorId InModulatorId) override;
		virtual bool GetModulatorValue(const Audio::FModulatorHandle& ModulatorHandle, float& OutValue) override;
		virtual void UnregisterModulator(const Audio::FModulatorHandle& InHandle) override;

	private:
		TUniquePtr<FAudioModulationSystem> ModSystem;
	};
} // namespace AudioModulation

class FAudioModulationPluginFactory : public IAudioModulationFactory
{
public:
	virtual const FName& GetDisplayName() const override
	{
		static FName DisplayName = FName(TEXT("DefaultModulationPlugin"));
		return DisplayName;
	}

	virtual TAudioModulationPtr CreateNewModulationPlugin(FAudioDevice* OwningDevice) override;

	virtual UClass* GetCustomModulationSettingsClass() const override
	{
		return USoundModulationSettings::StaticClass();
	}
};

class FAudioModulationModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	FAudioModulationPluginFactory ModulationPluginFactory;
};