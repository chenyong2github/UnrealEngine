// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SoundWaveAssetActionExtender.h"
#include "ToolMenus.h"
#include "AssetTypeActions_Base.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "ContentBrowserMenuContexts.h"
#include "ObjectEditorUtils.h"
#include "EditorStyleSet.h"
#include "Sound/SoundWave.h"
#include "SoundSimple.h"
#include "SoundSimpleFactory.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

void FSoundWaveAssetActionExtender::RegisterMenus()
{
	if (!UToolMenus::IsToolMenuUIEnabled())
	{
		return;
	}

	FToolMenuOwnerScoped MenuOwner("SoundUtilities");
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.SoundWave");
	FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");
	
	Section.AddDynamicEntry("SoundWaveAsset", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
	{
		const TAttribute<FText> Label = LOCTEXT("SoundWave_CreateSimpleSound", "Create Simple Sound");
		const TAttribute<FText> ToolTip = LOCTEXT("SoundWave_CreateSimpleSoundTooltip", "Creates a simple sound asset using the selected sound waves.");
		const FSlateIcon Icon = FSlateIcon(FEditorStyle::GetStyleSetName(), "ClassIcon.SoundSimple");
		const FToolMenuExecuteAction UIAction = FToolMenuExecuteAction::CreateStatic(&FSoundWaveAssetActionExtender::ExecuteCreateSimpleSound);

		InSection.AddMenuEntry("SoundWave_CreateSimpleSound", Label, ToolTip, Icon, UIAction);
	}));
}

void FSoundWaveAssetActionExtender::ExecuteCreateSimpleSound(const FToolMenuContext& MenuContext)
{
	UContentBrowserAssetContextMenuContext* Context = MenuContext.Find<UContentBrowserAssetContextMenuContext>();
	if (!Context || Context->SelectedObjects.Num() == 0)
	{
		return;
	}

	const FString DefaultSuffix = TEXT("_SimpleSound");
	
	if (USoundWave* SoundWave = Cast<USoundWave>(Context->SelectedObjects[0]))
	{
		// Determine an appropriate name
		FString Name;
		FString PackagePath;

		FAssetToolsModule& AssetToolsModule = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools");
		AssetToolsModule.Get().CreateUniqueAssetName(SoundWave->GetOutermost()->GetName(), DefaultSuffix, PackagePath, Name);

		// Create the factory used to generate the asset
		USoundSimpleFactory* Factory = NewObject<USoundSimpleFactory>();
		Factory->SoundWaves.Reset();
		for (const TWeakObjectPtr<UObject>& Object : Context->SelectedObjects)
		{
			if (USoundWave* Wave = Cast<USoundWave>(Object))
			{
				Factory->SoundWaves.Add(Wave);
			}
		}

		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		ContentBrowserModule.Get().CreateNewAsset(Name, FPackageName::GetLongPackagePath(PackagePath), USoundSimple::StaticClass(), Factory);

	}
}

#undef LOCTEXT_NAMESPACE