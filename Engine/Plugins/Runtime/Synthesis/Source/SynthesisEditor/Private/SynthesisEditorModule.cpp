// Copyright Epic Games, Inc. All Rights Reserved.

#include "SynthesisEditorModule.h"
#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "AssetToolsModule.h"
#include "SynthComponents/EpicSynth1Component.h"
#include "Factories/Factory.h"
#include "AssetTypeActions_Base.h"
#include "AudioEditorModule.h"
#include "EpicSynth1PresetBank.h"
#include "MonoWaveTablePresetBank.h"
#include "AudioImpulseResponseAsset.h"
#include "ToolMenus.h"
#include "Misc/AssertionMacros.h"
#include "SourceEffects/SourceEffectBitCrusher.h"
#include "SourceEffects/SourceEffectChorus.h"
#include "SourceEffects/SourceEffectDynamicsProcessor.h"
#include "SourceEffects/SourceEffectEnvelopeFollower.h"
#include "SourceEffects/SourceEffectEQ.h"
#include "SourceEffects/SourceEffectFilter.h"
#include "SourceEffects/SourceEffectFoldbackDistortion.h"
#include "SourceEffects/SourceEffectMidSideSpreader.h"
#include "SourceEffects/SourceEffectPanner.h"
#include "SourceEffects/SourceEffectPhaser.h"
#include "SourceEffects/SourceEffectRingModulation.h"
#include "SourceEffects/SourceEffectSimpleDelay.h"
#include "SourceEffects/SourceEffectStereoDelay.h"
#include "SourceEffects/SourceEffectWaveShaper.h"
#include "SubmixEffects/SubmixEffectConvolutionReverb.h"
#include "SubmixEffects/SubmixEffectDelay.h"
#include "SubmixEffects/SubmixEffectFilter.h"
#include "SubmixEffects/SubmixEffectFlexiverb.h"
#include "SubmixEffects/SubmixEffectStereoDelay.h"
#include "SubmixEffects/SubmixEffectTapDelay.h"
#include "SynthesisEditorSettings.h"


DEFINE_LOG_CATEGORY(LogSynthesisEditor);

IMPLEMENT_MODULE(FSynthesisEditorModule, SynthesisEditor)

namespace SynthesisEditorUtils
{
	void RegisterSoundEffectPresets(const FName* InPropertyChanged = nullptr)
	{
		IAudioEditorModule* AudioEditorModule = &FModuleManager::LoadModuleChecked<IAudioEditorModule>("AudioEditor");
		const USynthesisEditorSettings* Settings = GetDefault<USynthesisEditorSettings>();
		check(Settings);

		auto RegisterWidget = [AudioEditorModule, InPropertyChanged](FName InName, FSoftObjectPath InPath, TSubclassOf<USoundEffectPreset> InClass)
		{
			if (!InPropertyChanged || *InPropertyChanged == InName)
			{
				if (UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(InPath.TryLoad()))
				{
					AudioEditorModule->RegisterSoundEffectPresetWidget(InClass, WidgetBP);
				}
			}
		};

		// Source Effects
		RegisterWidget(GET_MEMBER_NAME_CHECKED(USynthesisEditorSettings, BitCrusherWidget), Settings->BitCrusherWidget, USourceEffectBitCrusherPreset::StaticClass());
		RegisterWidget(GET_MEMBER_NAME_CHECKED(USynthesisEditorSettings, ChorusWidget), Settings->ChorusWidget, USourceEffectChorusPreset::StaticClass());
		RegisterWidget(GET_MEMBER_NAME_CHECKED(USynthesisEditorSettings, DynamicsProcessorWidget), Settings->DynamicsProcessorWidget, USourceEffectDynamicsProcessorPreset::StaticClass());
		RegisterWidget(GET_MEMBER_NAME_CHECKED(USynthesisEditorSettings, EnvelopeFollowerWidget), Settings->EnvelopeFollowerWidget, USourceEffectEnvelopeFollowerPreset::StaticClass());
		RegisterWidget(GET_MEMBER_NAME_CHECKED(USynthesisEditorSettings, EQWidget), Settings->EQWidget, USourceEffectEQPreset::StaticClass());
		RegisterWidget(GET_MEMBER_NAME_CHECKED(USynthesisEditorSettings, FilterWidget), Settings->FilterWidget, USourceEffectFilterPreset::StaticClass());
		RegisterWidget(GET_MEMBER_NAME_CHECKED(USynthesisEditorSettings, FoldbackDistortionWidget), Settings->FoldbackDistortionWidget, USourceEffectFoldbackDistortionPreset::StaticClass());
		RegisterWidget(GET_MEMBER_NAME_CHECKED(USynthesisEditorSettings, MidSideSpreaderWidget), Settings->MidSideSpreaderWidget, USourceEffectMidSideSpreaderPreset::StaticClass());
		RegisterWidget(GET_MEMBER_NAME_CHECKED(USynthesisEditorSettings, PannerWidget), Settings->PannerWidget, USourceEffectPannerPreset::StaticClass());
		RegisterWidget(GET_MEMBER_NAME_CHECKED(USynthesisEditorSettings, PhaserWidget), Settings->PhaserWidget, USourceEffectPhaserPreset::StaticClass());
		RegisterWidget(GET_MEMBER_NAME_CHECKED(USynthesisEditorSettings, RingModulationWidget), Settings->RingModulationWidget, USourceEffectRingModulationPreset::StaticClass());
		RegisterWidget(GET_MEMBER_NAME_CHECKED(USynthesisEditorSettings, SimpleDelayWidget), Settings->SimpleDelayWidget, USourceEffectSimpleDelayPreset::StaticClass());
		RegisterWidget(GET_MEMBER_NAME_CHECKED(USynthesisEditorSettings, StereoDelayWidget), Settings->StereoDelayWidget, USourceEffectStereoDelayPreset::StaticClass());
		RegisterWidget(GET_MEMBER_NAME_CHECKED(USynthesisEditorSettings, WaveShaperWidget), Settings->WaveShaperWidget, USourceEffectWaveShaperPreset::StaticClass());

		// Submix Effects
		RegisterWidget(GET_MEMBER_NAME_CHECKED(USynthesisEditorSettings, SubmixConvolutionReverbWidget), Settings->SubmixConvolutionReverbWidget, USubmixEffectConvolutionReverbPreset::StaticClass());
		RegisterWidget(GET_MEMBER_NAME_CHECKED(USynthesisEditorSettings, SubmixDelayWidget), Settings->SubmixDelayWidget, USubmixEffectDelayPreset::StaticClass());
		RegisterWidget(GET_MEMBER_NAME_CHECKED(USynthesisEditorSettings, SubmixFilterWidget), Settings->SubmixFilterWidget, USubmixEffectFilterPreset::StaticClass());
		RegisterWidget(GET_MEMBER_NAME_CHECKED(USynthesisEditorSettings, SubmixFlexiverbWidget), Settings->SubmixFlexiverbWidget, USubmixEffectFlexiverbPreset::StaticClass());
		RegisterWidget(GET_MEMBER_NAME_CHECKED(USynthesisEditorSettings, SubmixStereoDelayWidget), Settings->SubmixStereoDelayWidget, USubmixEffectStereoDelayPreset::StaticClass());
		RegisterWidget(GET_MEMBER_NAME_CHECKED(USynthesisEditorSettings, SubmixTapDelayWidget), Settings->SubmixTapDelayWidget, USubmixEffectTapDelayPreset::StaticClass());
	}
} // namespace SynthesisEditorUtils

void USynthesisEditorSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	SynthesisEditorUtils::RegisterSoundEffectPresets(&PropertyName);
}

void FSynthesisEditorModule::StartupModule()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	AssetTools.RegisterAssetTypeActions(MakeShared<FAssetTypeActions_ModularSynthPresetBank>());
	AssetTools.RegisterAssetTypeActions(MakeShared<FAssetTypeActions_MonoWaveTableSynthPreset>());
	AssetTools.RegisterAssetTypeActions(MakeShared<FAssetTypeActions_AudioImpulseResponse>());

	// Now that we've loaded this module, we need to register our effect preset actions
	IAudioEditorModule* AudioEditorModule = &FModuleManager::LoadModuleChecked<IAudioEditorModule>("AudioEditor");
	AudioEditorModule->RegisterEffectPresetAssetActions();

	SynthesisEditorUtils::RegisterSoundEffectPresets();

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FSynthesisEditorModule::RegisterMenus));
}

void FSynthesisEditorModule::ShutdownModule()
{
}

void FSynthesisEditorModule::RegisterMenus()
{
	FAudioImpulseResponseExtension::RegisterMenus();
}
