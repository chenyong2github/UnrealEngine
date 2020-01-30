// Copyright Epic Games, Inc. All Rights Reserved.
#include "AssetTypeActions_SoundCueTemplate.h"

#include "SoundCueTemplate.h"
#include "SoundCueTemplateFactory.h"

#include "ToolMenuSection.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

UClass* FAssetTypeActions_SoundCueTemplate::GetSupportedClass() const
{
	return USoundCueTemplate::StaticClass();
}

void FAssetTypeActions_SoundCueTemplate::GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section)
{
	TArray<TWeakObjectPtr<USoundCueTemplate>> Cues = GetTypedWeakObjectPtrs<USoundCueTemplate>(InObjects);

	Section.AddMenuEntry(
		"SoundCueTemplate_CopyToSoundCue",
		LOCTEXT("SoundCueTemplate_CopyToSoundCue", "Copy To Sound Cue"),
		LOCTEXT("SoundCueTemplate_CopyToSoundCueTooltip", "Exports a Sound Cue Template to a Sound Cue."),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "ClassIcon.SoundCue"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FAssetTypeActions_SoundCueTemplate::ExecuteCopyToSoundCue, Cues),
			FCanExecuteAction()
		)
	);
}

void FAssetTypeActions_SoundCueTemplate::ExecuteCopyToSoundCue(TArray<TWeakObjectPtr<USoundCueTemplate>> Objects)
{
	for (const TWeakObjectPtr<USoundCueTemplate>& Object : Objects)
	{
		if (!Object.IsValid())
		{
			continue;
		}

		FString Name;
		FString PackagePath;
		CreateUniqueAssetName(Object->GetOutermost()->GetName(), TEXT(""), PackagePath, Name);

		if (USoundCueTemplateCopyFactory* Factory = NewObject<USoundCueTemplateCopyFactory>())
		{
			Factory->SoundCueTemplate = Object;
			FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
			ContentBrowserModule.Get().CreateNewAsset(Name, FPackageName::GetLongPackagePath(PackagePath), USoundCue::StaticClass(), Factory);
		}
	}
}
#undef LOCTEXT_NAMESPACE
