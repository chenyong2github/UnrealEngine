// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_SoundCueTemplate.h"

#include "SoundCueTemplate.h"
#include "SoundCueTemplateFactory.h"

#include "Components/AudioComponent.h"
#include "ToolMenus.h"
#include "ToolMenuSection.h"
#include "ContentBrowserModule.h"
#include "ContentBrowserMenuContexts.h"
#include "IContentBrowserSingleton.h"
#include "ObjectEditorUtils.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

UClass* FAssetTypeActions_SoundCueTemplate::GetSupportedClass() const
{
	return USoundCueTemplate::StaticClass();
}

void FAssetTypeActions_SoundCueTemplate::GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section)
{
	TArray<TWeakObjectPtr<USoundCueTemplate>> Cues = GetTypedWeakObjectPtrs<USoundCueTemplate>(InObjects);
	FAssetTypeActions_SoundBase::GetActions(InObjects, Section);

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

void FAssetActionExtender_SoundCueTemplate::RegisterMenus()
{
	if (!UToolMenus::IsToolMenuUIEnabled())
	{
		return;
	}

	FToolMenuOwnerScoped MenuOwner("SoundCueTemplate");
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.SoundWave");
	FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");

	Section.AddDynamicEntry("SoundCueTemplateSoundWaveAsset", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
	{
		UContentBrowserAssetContextMenuContext* Context = InSection.FindContext<UContentBrowserAssetContextMenuContext>();
		if (!Context || Context->SelectedObjects.Num() == 0)
		{
			return;
		}

		const TAttribute<FText> Label = LOCTEXT("SoundWave_CreateSoundCueTemplate", "Create SoundCueTemplate");
		const TAttribute<FText> ToolTip = LOCTEXT("SoundWave_CreateSoundCueTemplateToolTip", "Creates a SoundCueTemplate from the selected sound waves.");
		const FSlateIcon Icon = FSlateIcon(FEditorStyle::GetStyleSetName(), "ClassIcon.SoundCue");
		const FToolMenuExecuteAction UIExecuteAction = FToolMenuExecuteAction::CreateStatic(&FAssetActionExtender_SoundCueTemplate::ExecuteCreateSoundCueTemplate);

		InSection.AddMenuEntry("SoundWave_CreateSoundCueTemplate", Label, ToolTip, Icon, UIExecuteAction);
	}));
}

void FAssetActionExtender_SoundCueTemplate::ExecuteCreateSoundCueTemplate(const struct FToolMenuContext& MenuContext)
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
	AssetToolsModule.Get().CreateUniqueAssetName(SoundWave->GetOutermost()->GetName(), TEXT(""), PackagePath, Name);

	USoundCueTemplateFactory* Factory = NewObject<USoundCueTemplateFactory>();
	Factory->ConfigureProperties();

	Name = Factory->GetDefaultNewAssetName();
	Factory->SoundWaves = FObjectEditorUtils::GetTypedWeakObjectPtrs<USoundWave>(Context->GetSelectedObjects());

	FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	ContentBrowserModule.Get().CreateNewAsset(Name, FPackageName::GetLongPackagePath(PackagePath), USoundCueTemplate::StaticClass(), Factory);
}
#undef LOCTEXT_NAMESPACE
