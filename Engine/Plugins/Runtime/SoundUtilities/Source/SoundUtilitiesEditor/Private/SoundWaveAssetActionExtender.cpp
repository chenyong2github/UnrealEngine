// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SoundWaveAssetActionExtender.h"
#include "EditorMenuSubsystem.h"
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
	if (!UEditorMenuSubsystem::IsRunningEditorUI())
	{
		return;
	}

	FEditorMenuOwnerScoped MenuOwner("SoundUtilities");
	UEditorMenu* Menu = UEditorMenuSubsystem::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.SoundWave");
	FEditorMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");
	
	Section.AddDynamicEntry("SoundWaveAsset", FNewEditorMenuSectionDelegate::CreateLambda([](FEditorMenuSection& InSection)
	{
		const TAttribute<FText> Label = LOCTEXT("SoundWave_CreateSimpleSound", "Create Simple Sound");
		const TAttribute<FText> ToolTip = LOCTEXT("SoundWave_CreateSimpleSoundTooltip", "Creates a simple sound asset using the selected sound waves.");
		const FSlateIcon Icon = FSlateIcon(FEditorStyle::GetStyleSetName(), "ClassIcon.SoundSimple");
		const FEditorMenuExecuteAction UIAction = FEditorMenuExecuteAction::CreateStatic(&FSoundWaveAssetActionExtender::ExecuteCreateSimpleSound);

		InSection.AddMenuEntry("SoundWave_CreateSimpleSound", Label, ToolTip, Icon, UIAction);
	}));
}

void FSoundWaveAssetActionExtender::ExecuteCreateSimpleSound(const FEditorMenuContext& MenuContext)
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
		for (UObject* Object : Context->SelectedObjects)
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