// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TimeSynthSoundWaveAssetActionExtender.h"

#include "EditorMenuSubsystem.h"
#include "AssetTypeActions_Base.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "ContentBrowserMenuContexts.h"
#include "ObjectEditorUtils.h"
#include "EditorStyleSet.h"
#include "Sound/SoundWave.h"
#include "TimeSynthClip.h"
#include "TimeSynthComponent.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

namespace
{
	const FString DefaultSuffix = TEXT("_TSC");
} // namespace <>

void FTimeSynthSoundWaveAssetActionExtender::RegisterMenus()
{
	if (!UEditorMenuSubsystem::IsRunningEditorUI())
	{
		return;
	}

	FEditorMenuOwnerScoped MenuOwner("TimeSynth");
	UEditorMenu* Menu = UEditorMenuSubsystem::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.SoundWave");
	FEditorMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");

	Section.AddDynamicEntry("TimeSynthSoundWaveAsset", FNewEditorMenuSectionDelegate::CreateLambda([](FEditorMenuSection& InSection)
	{
		UContentBrowserAssetContextMenuContext* Context = InSection.FindContext<UContentBrowserAssetContextMenuContext>();
		if (!Context || Context->SelectedObjects.Num() == 0)
		{
			return;
		}

		const TAttribute<FText> Label = LOCTEXT("SoundWave_CreateTimeSynthClip", "Create Time Synth Clip(s)");
		const TAttribute<FText> ToolTip = LOCTEXT("SoundWave_CreateTimeSynthClipToolTip", "Creates time synth clip per sound wave selected");
		const FSlateIcon Icon = FSlateIcon(FEditorStyle::GetStyleSetName(), "ClassIcon.SoundCue");

		const FEditorMenuExecuteAction UIExecuteAction = FEditorMenuExecuteAction::CreateStatic(&FTimeSynthSoundWaveAssetActionExtender::ExecuteCreateTimeSyncClip);

		InSection.AddMenuEntry("SoundWave_CreateTimeSynthClip", Label, ToolTip, Icon, UIExecuteAction);

		const TAttribute<FText> LabelSet = LOCTEXT("SoundWave_CreateTimeSynthClipSet", "Create Time Synth Clip Set");
		const TAttribute<FText> ToolTipSet = LOCTEXT("SoundWave_CreateTimeSynthClipSetToolTip", "Creates time synth clip adding all selected sound waves to single clip as set.");
		const FEditorMenuExecuteAction UIExecuteActionSet = FEditorMenuExecuteAction::CreateStatic(&FTimeSynthSoundWaveAssetActionExtender::ExecuteCreateTimeSyncClipSet);

		InSection.AddMenuEntry("SoundWave_CreateTimeSynthClipSet", LabelSet, ToolTipSet, Icon, UIExecuteActionSet);
	}));
}

void FTimeSynthSoundWaveAssetActionExtender::ExecuteCreateTimeSyncClip(const FEditorMenuContext& MenuContext)
{
	UContentBrowserAssetContextMenuContext* Context = MenuContext.Find<UContentBrowserAssetContextMenuContext>();
	if (!Context || Context->SelectedObjects.Num() == 0)
	{
		return;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools");

	UTimeSynthClipFactory* Factory = NewObject<UTimeSynthClipFactory>();
	for (UObject* Object : Context->SelectedObjects)
	{
		USoundWave* SoundWave = Cast<USoundWave>(Object);

		FString Name;
		FString PackagePath;

		AssetToolsModule.Get().CreateUniqueAssetName(SoundWave->GetOutermost()->GetName(), DefaultSuffix, PackagePath, Name);

		Factory->SoundWaves.Reset();
		Factory->SoundWaves.Add(SoundWave);

		AssetToolsModule.Get().CreateAsset(Name, FPackageName::GetLongPackagePath(PackagePath), UTimeSynthClip::StaticClass(), Factory);
	}
}

void FTimeSynthSoundWaveAssetActionExtender::ExecuteCreateTimeSyncClipSet(const FEditorMenuContext& MenuContext)
{
	UContentBrowserAssetContextMenuContext* Context = MenuContext.Find<UContentBrowserAssetContextMenuContext>();
	if (!Context || Context->SelectedObjects.Num() == 0)
	{
		return;
	}

	USoundWave* SoundWave = Cast<USoundWave>(Context->SelectedObjects[0]);
	if (SoundWave == nullptr)
	{
		return;
	}

	FString PackagePath;
	FString Name;

	FAssetToolsModule& AssetToolsModule = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools");
	AssetToolsModule.Get().CreateUniqueAssetName(SoundWave->GetOutermost()->GetName(), DefaultSuffix, PackagePath, Name);

	UTimeSynthClipFactory* Factory = NewObject<UTimeSynthClipFactory>();
	Factory->SoundWaves = FObjectEditorUtils::GetTypedWeakObjectPtrs<USoundWave>(Context->SelectedObjects);

	AssetToolsModule.Get().CreateAsset(Name, FPackageName::GetLongPackagePath(PackagePath), UTimeSynthClip::StaticClass(), Factory);
}
#undef LOCTEXT_NAMESPACE