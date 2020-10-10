// Copyright Epic Games, Inc. All Rights Reserved.
#include "AudioModulation.h"

#include "AudioModulationLogging.h"
#include "AudioModulationSystem.h"
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
		: ModSystem(new FAudioModulationSystem())
	{
	}

	FAudioModulation::~FAudioModulation()
	{
		delete ModSystem;
	}

	Audio::FModulationParameter FAudioModulation::GetParameter(FName InParamName)
	{
		return ModSystem->GetParameter(InParamName);
	}

	void FAudioModulation::Initialize(const FAudioPluginInitializationParams& InitializationParams)
	{
		ModSystem->Initialize(InitializationParams);
	}

	void FAudioModulation::OnAuditionEnd()
	{
		ModSystem->OnAuditionEnd();
	}

	void FAudioModulation::ActivateBus(const USoundControlBus& InBus)
	{
		ModSystem->ActivateBus(InBus);
	}

	void FAudioModulation::ActivateBusMix(const USoundControlBusMix& InBusMix)
	{
		ModSystem->ActivateBusMix(InBusMix);
	}

	void FAudioModulation::ActivateGenerator(const USoundModulationGenerator& InGenerator)
	{
		ModSystem->ActivateGenerator(InGenerator);
	}

	void FAudioModulation::DeactivateBus(const USoundControlBus& InBus)
	{
		ModSystem->DeactivateBus(InBus);
	}

	void FAudioModulation::DeactivateBusMix(const USoundControlBusMix& InBusMix)
	{
		ModSystem->DeactivateBusMix(InBusMix);
	}

	void FAudioModulation::DeactivateAllBusMixes()
	{
		ModSystem->DeactivateAllBusMixes();
	}

	void FAudioModulation::DeactivateGenerator(const USoundModulationGenerator& InGenerator)
	{
		ModSystem->DeactivateGenerator(InGenerator);
	}

#if !UE_BUILD_SHIPPING
	void FAudioModulation::SetDebugBusFilter(const FString* InNameFilter)
	{
		ModSystem->SetDebugBusFilter(InNameFilter);
	}

	void FAudioModulation::SetDebugGeneratorFilter(const FString* InFilter)
	{
		ModSystem->SetDebugGeneratorFilter(InFilter);
	}

	void FAudioModulation::SetDebugGeneratorTypeFilter(const FString* InFilter, bool bInIsEnabled)
	{
		ModSystem->SetDebugGeneratorTypeFilter(InFilter, bInIsEnabled);
	}

	void FAudioModulation::SetDebugGeneratorsEnabled(bool bInIsEnabled)
	{
		ModSystem->SetDebugGeneratorsEnabled(bInIsEnabled);
	}

	void FAudioModulation::SetDebugMatrixEnabled(bool bInIsEnabled)
	{
		ModSystem->SetDebugMatrixEnabled(bInIsEnabled);
	}

	void FAudioModulation::SetDebugMixFilter(const FString* InNameFilter)
	{
		ModSystem->SetDebugMixFilter(InNameFilter);
	}

#endif // !UE_BUILD_SHIPPING

	void FAudioModulation::SaveMixToProfile(const USoundControlBusMix& InBusMix, const int32 InProfileIndex)
	{
		ModSystem->SaveMixToProfile(InBusMix, InProfileIndex);
	}

	TArray<FSoundControlBusMixStage> FAudioModulation::LoadMixFromProfile(const int32 InProfileIndex, USoundControlBusMix& OutBusMix)
	{
		return ModSystem->LoadMixFromProfile(InProfileIndex, OutBusMix);
	}

	void FAudioModulation::UpdateMix(const TArray<FSoundControlBusMixStage>& InStages, USoundControlBusMix& InOutMix, bool bInUpdateObject, float InFadeTime)
	{
		ModSystem->UpdateMix(InStages, InOutMix, bInUpdateObject, InFadeTime);
	}

	void FAudioModulation::UpdateMix(const USoundControlBusMix& InMix, float InFadeTime)
	{
		ModSystem->UpdateMix(InMix, InFadeTime);
	}

	void FAudioModulation::UpdateMixByFilter(const FString& InAddressFilter, const TSubclassOf<USoundModulationParameter>& InParamClassFilter, USoundModulationParameter* InParamFilter, float Value, float FadeTime, USoundControlBusMix& InOutMix, bool bInUpdateObject)
	{
		ModSystem->UpdateMixByFilter(InAddressFilter, InParamClassFilter, InParamFilter, Value, FadeTime, InOutMix, bInUpdateObject);
	}

	void FAudioModulation::SoloBusMix(const USoundControlBusMix& InBusMix)
	{
		ModSystem->SoloBusMix(InBusMix);
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

	void FAudioModulation::ProcessModulators(const double InElapsed)
	{
		SCOPE_CYCLE_COUNTER(STAT_AudioModulationProcessModulators);
		ModSystem->ProcessModulators(InElapsed);
	}

	Audio::FModulatorTypeId FAudioModulation::RegisterModulator(Audio::FModulatorHandleId InHandleId, const USoundModulatorBase* InModulatorBase, Audio::FModulationParameter& OutParameter)
	{
		return ModSystem->RegisterModulator(InHandleId, InModulatorBase, OutParameter);
	}

	void FAudioModulation::RegisterModulator(Audio::FModulatorHandleId InHandleId, Audio::FModulatorId InModulatorId)
	{
		ModSystem->RegisterModulator(InHandleId, InModulatorId);
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