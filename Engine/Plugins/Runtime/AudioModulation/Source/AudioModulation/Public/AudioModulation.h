// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeBool.h"
#include "IAudioExtensionPlugin.h"
#include "IAudioModulation.h"
#include "Modules/ModuleInterface.h"
#include "SoundModulationPatch.h"
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
		virtual Audio::FModulationParameter GetParameter(FName InParamName);
		virtual void Initialize(const FAudioPluginInitializationParams& InitializationParams) override;

		virtual void OnAuditionEnd() override;

#if !UE_BUILD_SHIPPING
		virtual bool OnPostHelp(FCommonViewportClient* ViewportClient, const TCHAR* Stream) override;
		virtual int32 OnRenderStat(FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const UFont& Font, const FVector* ViewLocation, const FRotator* ViewRotation) override;
		virtual bool OnToggleStat(FCommonViewportClient* ViewportClient, const TCHAR* Stream) override;
#endif // !UE_BUILD_SHIPPING

		virtual void ProcessModulators(const double InElapsed) override;

		virtual void UpdateModulator(const USoundModulatorBase& InModulator) override;
		//~ End IAudioModulation implementation

		FAudioModulationSystem* GetModulationSystem();

	protected:
		virtual Audio::FModulatorTypeId RegisterModulator(Audio::FModulatorHandleId InHandleId, const USoundModulatorBase* InModulatorBase, Audio::FModulationParameter& OutParameter) override;
		virtual void RegisterModulator(Audio::FModulatorHandleId InHandleId, Audio::FModulatorId InModulatorId) override;
		virtual bool GetModulatorValue(const Audio::FModulatorHandle& ModulatorHandle, float& OutValue) const override;
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
};

class FAudioModulationModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	FAudioModulationPluginFactory ModulationPluginFactory;
};