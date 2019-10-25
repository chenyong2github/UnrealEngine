// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_DatasmithScene.h"

#include "DatasmithAssetImportData.h"
#include "DatasmithContentEditorModule.h"
#include "DataprepAssetUserData.h"
#include "DataprepCoreLibrary.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions_DatasmithScene"

uint32 FAssetTypeActions_DatasmithScene::GetCategories()
{
	return IDatasmithContentEditorModule::DatasmithAssetCategoryBit;
}

FText FAssetTypeActions_DatasmithScene::GetName() const
{
	return NSLOCTEXT("AssetTypeActions_DatasmithScene", "AssetTypeActions_DatasmithScene_Name", "Datasmith Scene");
}

UClass* FAssetTypeActions_DatasmithScene::GetSupportedClass() const
{
	return UDatasmithScene::StaticClass();
}

void FAssetTypeActions_DatasmithScene::GetResolvedSourceFilePaths(const TArray<UObject*>& TypeAssets, TArray<FString>& OutSourceFilePaths) const
{
	for ( const UObject* Asset : TypeAssets )
	{
		const UDatasmithScene* DatasmithScene = CastChecked< UDatasmithScene >( Asset );

		if ( DatasmithScene && DatasmithScene->AssetImportData )
		{
			DatasmithScene->AssetImportData->ExtractFilenames( OutSourceFilePaths );
		}
	}
}

void FAssetTypeActions_DatasmithScene::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<class IToolkitHost> EditWithinLevelEditor)
{
	if (InObjects.Num() == 0)
	{
		return;
	}

	FOnCreateDatasmithSceneEditor DatasmithSceneEditorHandler = IDatasmithContentEditorModule::Get().GetDatasmithSceneEditorHandler();

	if (DatasmithSceneEditorHandler.IsBound() == false)
	{
		FSimpleAssetEditor::CreateEditor(EToolkitMode::Standalone, EditWithinLevelEditor, InObjects);
		return;
	}

	for (UObject* Object : InObjects)
	{
		UDatasmithScene* DatasmithScene = Cast<UDatasmithScene>(Object);
		if (DatasmithScene != nullptr)
		{
			DatasmithSceneEditorHandler.ExecuteIfBound(EToolkitMode::Standalone, EditWithinLevelEditor, DatasmithScene);
		}
	}
}

void FAssetTypeActions_DatasmithScene::ExecuteDataprepAssets( TArray< TSoftObjectPtr< UDataprepAssetInterface > > DataprepAssets )
{
	for ( TSoftObjectPtr< UDataprepAssetInterface > DataprepAssetInterface : DataprepAssets ) 
	{
		UDataprepCoreLibrary::ExecuteWithReporting( DataprepAssetInterface.Get() );
	}
}

void FAssetTypeActions_DatasmithScene::GetActions( const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder )
{
	auto DatasmithScenes = GetTypedWeakObjectPtrs<UDatasmithScene>( InObjects );

	if ( DatasmithScenes.Num() == 0 )
	{
		return;
	}

	// Allow execute only if we have at least one valid dataprep asset pointer
	TArray< TSoftObjectPtr< UDataprepAssetInterface > > DataprepAssets;
	for ( TWeakObjectPtr< UDatasmithScene > Scene : DatasmithScenes )
	{
		UDataprepAssetUserData* DataprepAssetUserData = CastChecked< UDataprepAssetUserData >( Scene->GetAssetUserDataOfClass( UDataprepAssetUserData::StaticClass() ), ECastCheckedType::NullAllowed );
		if (DataprepAssetUserData && DataprepAssetUserData->DataprepAssetPtr)
		{
			DataprepAssets.Add( DataprepAssetUserData->DataprepAssetPtr );
		}
	}

	if ( DataprepAssets.Num() > 0 )
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("RunAsset", "Execute"),
			LOCTEXT("RunAssetTooltip", "Runs the Dataprep asset's producers, execute its recipe, finally runs the consumer"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &FAssetTypeActions_DatasmithScene::ExecuteDataprepAssets, DataprepAssets ),
				FCanExecuteAction()
			)
		);
	}
}

#undef LOCTEXT_NAMESPACE
