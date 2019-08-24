// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "AudioModulationEditor.h"

#include "AssetTypeActions/AssetTypeActions_SoundModulationSettings.h"
#include "AssetTypeActions/AssetTypeActions_SoundModulatorBus.h"
#include "AssetTypeActions/AssetTypeActions_SoundModulatorBusMix.h"
#include "AssetTypeActions/AssetTypeActions_SoundModulatorLFO.h"
#include "Layouts/SoundModulationTransformLayout.h"
#include "SoundModulationTransform.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"

namespace
{
	TSharedPtr<FSlateStyleSet> StyleSet;
} // namespace <>

FAudioModulationEditorModule::FAudioModulationEditorModule()
{
	StyleSet = MakeShareable(new FSlateStyleSet("AudioModulationStyleSet"));
}

void FAudioModulationEditorModule::SetIcon(const FString& ClassName)
{
	static const FVector2D Icon16 = FVector2D(16.0f, 16.0f);
	static const FVector2D Icon64 = FVector2D(64.0f, 64.0f);

	static const FString IconDir = FPaths::EngineDir() / FString::Printf(TEXT("Plugins/Runtime/AudioModulation/Icons"));

	const FString IconFileName16 = FString::Printf(TEXT("%s_16x.png"), *ClassName);
	const FString IconFileName64 = FString::Printf(TEXT("%s_64x.png"), *ClassName);

	StyleSet->Set(*FString::Printf(TEXT("ClassIcon.%s"), *ClassName), new FSlateImageBrush(IconDir / IconFileName16, Icon16));
	StyleSet->Set(*FString::Printf(TEXT("ClassThumbnail.%s"), *ClassName), new FSlateImageBrush(IconDir / IconFileName64, Icon64));
}

void FAudioModulationEditorModule::StartupModule()
{
	// Register the audio editor asset type actions
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	AssetTools.RegisterAssetTypeActions(MakeShareable(new FAssetTypeActions_SoundVolumeModulatorBus));
	AssetTools.RegisterAssetTypeActions(MakeShareable(new FAssetTypeActions_SoundPitchModulatorBus));
	AssetTools.RegisterAssetTypeActions(MakeShareable(new FAssetTypeActions_SoundLPFModulatorBus));
	AssetTools.RegisterAssetTypeActions(MakeShareable(new FAssetTypeActions_SoundHPFModulatorBus));
	AssetTools.RegisterAssetTypeActions(MakeShareable(new FAssetTypeActions_SoundModulatorBusMix));
	AssetTools.RegisterAssetTypeActions(MakeShareable(new FAssetTypeActions_SoundModulatorLFO));
	AssetTools.RegisterAssetTypeActions(MakeShareable(new FAssetTypeActions_SoundModulationSettings));

	SetIcon(TEXT("SoundVolumeModulatorBus"));
	SetIcon(TEXT("SoundPitchModulatorBus"));
	SetIcon(TEXT("SoundLPFModulatorBus"));
	SetIcon(TEXT("SoundHPFModulatorBus"));
	SetIcon(TEXT("SoundModulatorBusMix"));
	SetIcon(TEXT("SoundModulatorLFO"));
	SetIcon(TEXT("SoundModulationSettings"));

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomPropertyTypeLayout("SoundModulationOutputTransform", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FSoundModulationOutputTransformLayoutCustomization::MakeInstance));

	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());
}

void FAudioModulationEditorModule::ShutdownModule()
{
}


IMPLEMENT_MODULE(FAudioModulationEditorModule, AudioModulationEditor);
