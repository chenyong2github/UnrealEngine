// Copyright Epic Games, Inc. All Rights Reserved.

#include "SoundWaveAssetActionExtenderMotoSynth.h"
#include "ToolMenus.h"
#include "AssetTypeActions_Base.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "ContentBrowserMenuContexts.h"
#include "ObjectEditorUtils.h"
#include "EditorStyleSet.h"
#include "Sound/SoundWave.h"
#include "MotoSynthSourceFactory.h"
#include "MotoSynthSourceAsset.h"
//#include "ConvolutionReverbComponent.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

void FMotoSynthExtension::RegisterMenus()
{
	if (!UToolMenus::IsToolMenuUIEnabled())
	{
		return;
	}

	FToolMenuOwnerScoped MenuOwner("MotoSynth");
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.SoundWave");
	FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");

	Section.AddDynamicEntry("SoundWaveAssetConversion", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
	{
		const TAttribute<FText> Label = LOCTEXT("SoundWave_CreateMotoSource", "Create MotoSynth Source");
		const TAttribute<FText> ToolTip = LOCTEXT("SoundWave_CreateMotoSynthSourceTooltip", "Creates a MotoSynth Source asset using the selected sound wave.");
		const FSlateIcon Icon = FSlateIcon(FEditorStyle::GetStyleSetName(), "ClassIcon.MotoSynthSource");
		const FToolMenuExecuteAction UIAction = FToolMenuExecuteAction::CreateStatic(&FMotoSynthExtension::ExecuteCreateMotoSynthSource);

		InSection.AddMenuEntry("SoundWave_CreateMotoSynthSource", Label, ToolTip, Icon, UIAction);
	}));
}

void FMotoSynthExtension::ExecuteCreateMotoSynthSource(const FToolMenuContext& MenuContext)
{
	UContentBrowserAssetContextMenuContext* Context = MenuContext.FindContext<UContentBrowserAssetContextMenuContext>();
	if (!Context || Context->SelectedObjects.Num() == 0)
	{
		return;
	}

	const FString DefaultSuffix = TEXT("_MotoSynthSource");
	FAssetToolsModule& AssetToolsModule = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools");

	// Create the factory used to generate the asset
	UMotoSynthSourceFactory* Factory = NewObject<UMotoSynthSourceFactory>();
	
	for (const TWeakObjectPtr<UObject>& Object : Context->SelectedObjects)
	{
		// stage the soundwave on the factory to be used during asset creation
		USoundWave* Wave = Cast<USoundWave>(Object);
		check(Wave);
		Factory->StagedSoundWave = Wave; // WeakPtr gets reset by the Factory after it is consumed

		// Determine an appropriate name
		FString Name;
		FString PackagePath;
		AssetToolsModule.Get().CreateUniqueAssetName(Wave->GetOutermost()->GetName(), DefaultSuffix, PackagePath, Name);

		// create new asset
		AssetToolsModule.Get().CreateAsset(Name, FPackageName::GetLongPackagePath(PackagePath), UMotoSynthSource::StaticClass(), Factory);
	}
}

#undef LOCTEXT_NAMESPACE