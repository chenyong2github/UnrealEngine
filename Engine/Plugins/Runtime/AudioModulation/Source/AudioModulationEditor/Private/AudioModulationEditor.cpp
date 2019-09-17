// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "AudioModulationEditor.h"

#include "AssetTypeActions/AssetTypeActions_SoundModulationSettings.h"
#include "AssetTypeActions/AssetTypeActions_SoundControlBus.h"
#include "AssetTypeActions/AssetTypeActions_SoundControlBusMix.h"
#include "AssetTypeActions/AssetTypeActions_SoundModulatorLFO.h"
#include "Layouts/SoundModulationTransformLayout.h"
#include "SoundModulationTransform.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"

namespace
{
	static const FName AssetToolsName = TEXT("AssetTools");

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
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(AssetToolsName).Get();

	AssetActions.Add(MakeShareable(new FAssetTypeActions_SoundVolumeControlBus));
	AssetActions.Add(MakeShareable(new FAssetTypeActions_SoundPitchControlBus));
	AssetActions.Add(MakeShareable(new FAssetTypeActions_SoundHPFControlBus));
	AssetActions.Add(MakeShareable(new FAssetTypeActions_SoundLPFControlBus));
	AssetActions.Add(MakeShareable(new FAssetTypeActions_SoundControlBusMix));
	AssetActions.Add(MakeShareable(new FAssetTypeActions_SoundModulatorLFO));
	AssetActions.Add(MakeShareable(new FAssetTypeActions_SoundModulationSettings));

	for (TSharedPtr<FAssetTypeActions_Base>& AssetAction : AssetActions)
	{
		AssetTools.RegisterAssetTypeActions(AssetAction.ToSharedRef());
	}

	SetIcon(TEXT("SoundVolumeControlBus"));
	SetIcon(TEXT("SoundPitchControlBus"));
	SetIcon(TEXT("SoundHPFControlBus"));
	SetIcon(TEXT("SoundLPFControlBus"));
	SetIcon(TEXT("SoundControlBusMix"));
	SetIcon(TEXT("SoundBusModulatorLFO"));
	SetIcon(TEXT("SoundModulationSettings"));

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	PropertyModule.RegisterCustomPropertyTypeLayout("SoundModulationOutputTransform",
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(
			&FSoundModulationOutputTransformLayoutCustomization::MakeInstance));

	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());
}

void FAudioModulationEditorModule::ShutdownModule()
{
	if (FModuleManager::Get().IsModuleLoaded(AssetToolsName))
	{
		IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>(AssetToolsName).Get();
		for (TSharedPtr<FAssetTypeActions_Base>& AssetAction : AssetActions)
		{
			AssetTools.UnregisterAssetTypeActions(AssetAction.ToSharedRef());
		}
	}
	AssetActions.Reset();

	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet.Get());
}


IMPLEMENT_MODULE(FAudioModulationEditorModule, AudioModulationEditor);
