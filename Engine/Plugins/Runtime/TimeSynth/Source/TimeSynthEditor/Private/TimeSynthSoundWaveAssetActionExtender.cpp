// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TimeSynthSoundWaveAssetActionExtender.h"

#include "Framework/Commands/UIAction.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "AssetTypeActions_Base.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "EditorStyleSet.h"
#include "Sound/SoundWave.h"
#include "TimeSynthClip.h"
#include "TimeSynthComponent.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

namespace
{
	const FString DefaultSuffix = TEXT("_TSC");
} // namespace <>

void FTimeSynthSoundWaveAssetActionExtender::GetExtendedActions(const TArray<TWeakObjectPtr<USoundWave>>& InSounds, FMenuBuilder& MenuBuilder)
{
	const TAttribute<FText> Label = LOCTEXT("SoundWave_CreateTimeSynthClip", "Create Time Synth Clip(s)");
	const TAttribute<FText> ToolTip = LOCTEXT("SoundWave_CreateTimeSynthClipToolTip", "Creates time synth clip per sound wave selected");
	const FSlateIcon Icon = FSlateIcon(FEditorStyle::GetStyleSetName(), "ClassIcon.SoundCue");
	const FUIAction UIAction = FUIAction(FExecuteAction::CreateSP(this, &FTimeSynthSoundWaveAssetActionExtender::ExecuteCreateTimeSyncClip, InSounds), FCanExecuteAction());

	MenuBuilder.AddMenuEntry(Label, ToolTip, Icon, UIAction);

	const TAttribute<FText> LabelSet = LOCTEXT("SoundWave_CreateTimeSynthClipSet", "Create Time Synth Clip Set");
	const TAttribute<FText> ToolTipSet = LOCTEXT("SoundWave_CreateTimeSynthClipSetToolTip", "Creates time synth clip adding all selected sound waves to single clip as set.");
	const FUIAction UIActionSet = FUIAction(FExecuteAction::CreateSP(this, &FTimeSynthSoundWaveAssetActionExtender::ExecuteCreateTimeSyncClipSet, InSounds), FCanExecuteAction());

	MenuBuilder.AddMenuEntry(LabelSet, ToolTipSet, Icon, UIActionSet);
}

void FTimeSynthSoundWaveAssetActionExtender::ExecuteCreateTimeSyncClip(TArray<TWeakObjectPtr<USoundWave>> SoundWaves)
{
	FAssetToolsModule& AssetToolsModule = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools");

	UTimeSynthClipFactory* Factory = NewObject<UTimeSynthClipFactory>();
	for (TWeakObjectPtr<USoundWave>& SoundWave : SoundWaves)
	{
		FString Name;
		FString PackagePath;

		AssetToolsModule.Get().CreateUniqueAssetName(SoundWave->GetOutermost()->GetName(), DefaultSuffix, PackagePath, Name);

		Factory->SoundWaves.Reset();
		Factory->SoundWaves.Add(SoundWave);

		AssetToolsModule.Get().CreateAsset(Name, FPackageName::GetLongPackagePath(PackagePath), UTimeSynthClip::StaticClass(), Factory);
	}
}

void FTimeSynthSoundWaveAssetActionExtender::ExecuteCreateTimeSyncClipSet(TArray<TWeakObjectPtr<USoundWave>> SoundWaves)
{
	FString PackagePath;
	FString Name;

	if (SoundWaves.Num() == 0 || !SoundWaves[0].IsValid())
	{
		return;
	}

	USoundWave& SoundWave = *SoundWaves[0].Get();
	FAssetToolsModule& AssetToolsModule = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools");
	AssetToolsModule.Get().CreateUniqueAssetName(SoundWave.GetOutermost()->GetName(), DefaultSuffix, PackagePath, Name);

	UTimeSynthClipFactory* Factory = NewObject<UTimeSynthClipFactory>();
	Factory->SoundWaves = SoundWaves;

	AssetToolsModule.Get().CreateAsset(Name, FPackageName::GetLongPackagePath(PackagePath), UTimeSynthClip::StaticClass(), Factory);
}
#undef LOCTEXT_NAMESPACE