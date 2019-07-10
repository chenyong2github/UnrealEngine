// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_DataPrepAsset.h"

#include "DataPrepAsset.h"
#include "DataPrepEditor.h"
#include "DataPrepEditorModule.h"


#define LOCTEXT_NAMESPACE "AssetTypeActions_DataprepAsset"

uint32 FAssetTypeActions_DataprepAsset::GetCategories()
{
	return IDataprepEditorModule::DataprepCategoryBit;
}

FText FAssetTypeActions_DataprepAsset::GetName() const
{
	return NSLOCTEXT("AssetTypeActions_DataprepAsset", "AssetTypeActions_DataprepAsset_Name", "Dataprep");
}

UClass* FAssetTypeActions_DataprepAsset::GetSupportedClass() const
{
	return UDataprepAsset::StaticClass();
}

void FAssetTypeActions_DataprepAsset::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<class IToolkitHost> EditWithinLevelEditor)
{
	if (InObjects.Num() == 0)
	{
		return;
	}

	for (UObject* Object : InObjects)
	{
		if (UDataprepAsset* DataprepAsset = Cast<UDataprepAsset>(Object))
		{
			TSharedRef<FDataprepEditor> NewDataprepEditor(new FDataprepEditor());
			NewDataprepEditor->InitDataprepEditor( EToolkitMode::Standalone, EditWithinLevelEditor, DataprepAsset );
		}
	}
}

#undef LOCTEXT_NAMESPACE
