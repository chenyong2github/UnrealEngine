// Copyright Epic Games, Inc. All Rights Reserved.

#include "TimeSynthSoundWaveAssetActionExtender.h"

#include "ToolMenus.h"
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
	if (!UToolMenus::IsToolMenuUIEnabled())
	{
		return;
	}

	FToolMenuOwnerScoped MenuOwner("TimeSynth");
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.SoundWave");
	FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");

	Section.AddDynamicEntry("TimeSynthSoundWaveAsset", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
	{
		UContentBrowserAssetContextMenuContext* Context = InSection.FindContext<UContentBrowserAssetContextMenuContext>();
		if (!Context || Context->SelectedObjects.Num() == 0)
		{
			return;
		}

		const TAttribute<FText> Label = LOCTEXT("SoundWave_CreateTimeSynthClip", "Create Time Synth Clip(s)");
		const TAttribute<FText> ToolTip = LOCTEXT("SoundWave_CreateTimeSynthClipToolTip", "Creates time synth clip per sound wave selected");
		const FSlateIcon Icon = FSlateIcon(FEditorStyle::GetStyleSetName(), "ClassIcon.SoundCue");

		const FToolMenuExecuteAction UIExecuteAction = FToolMenuExecuteAction::CreateStatic(&FTimeSynthSoundWaveAssetActionExtender::ExecuteCreateTimeSyncClip);

		InSection.AddMenuEntry("SoundWave_CreateTimeSynthClip", Label, ToolTip, Icon, UIExecuteAction);

		const TAttribute<FText> LabelSet = LOCTEXT("SoundWave_CreateTimeSynthClipSet", "Create Time Synth Clip Set");
		const TAttribute<FText> ToolTipSet = LOCTEXT("SoundWave_CreateTimeSynthClipSetToolTip", "Creates time synth clip adding all selected sound waves to single clip as set.");
		const FToolMenuExecuteAction UIExecuteActionSet = FToolMenuExecuteAction::CreateStatic(&FTimeSynthSoundWaveAssetActionExtender::ExecuteCreateTimeSyncClipSet);

		InSection.AddMenuEntry("SoundWave_CreateTimeSynthClipSet", LabelSet, ToolTipSet, Icon, UIExecuteActionSet);
	}));
}

void FTimeSynthSoundWaveAssetActionExtender::ExecuteCreateTimeSyncClip(const FToolMenuContext& MenuContext)
{
	UContentBrowserAssetContextMenuContext* Context = MenuContext.FindContext<UContentBrowserAssetContextMenuContext>();
	if (!Context || Context->SelectedObjects.Num() == 0)
	{
		return;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools");

	UTimeSynthClipFactory* Factory = NewObject<UTimeSynthClipFactory>();
	for (const TWeakObjectPtr<UObject>& Object : Context->SelectedObjects)
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

void FTimeSynthSoundWaveAssetActionExtender::ExecuteCreateTimeSyncClipSet(const FToolMenuContext& MenuContext)
{
	UContentBrowserAssetContextMenuContext* Context = MenuContext.FindContext<UContentBrowserAssetContextMenuContext>();
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
	Factory->SoundWaves = FObjectEditorUtils::GetTypedWeakObjectPtrs<USoundWave>(Context->GetSelectedObjects());

	AssetToolsModule.Get().CreateAsset(Name, FPackageName::GetLongPackagePath(PackagePath), UTimeSynthClip::StaticClass(), Factory);
}
#undef LOCTEXT_NAMESPACE