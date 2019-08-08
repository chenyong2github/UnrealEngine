// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "AssetTypeActions/AssetTypeActions_SoundCueTemplate.h"

#include "ContentBrowserModule.h"
#include "EditorStyleSet.h"
#include "Factories/SoundCueFactoryNew.h"
#include "Factories/SoundCueTemplateFactory.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IContentBrowserSingleton.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundCueTemplate.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"


#define LOCTEXT_NAMESPACE "AssetTypeActions"

UClass* FAssetTypeActions_SoundCueTemplate::GetSupportedClass() const
{
	return USoundCueTemplate::StaticClass();
}

void FAssetTypeActions_SoundCueTemplate::GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder)
{
	TArray<TWeakObjectPtr<USoundCueTemplate>> Cues = GetTypedWeakObjectPtrs<USoundCueTemplate>(InObjects);

	MenuBuilder.AddMenuEntry(
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
