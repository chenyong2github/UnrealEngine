// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_DataPrep.h"

#include "DataPrepRecipe.h"
#include "DataPrepEditorModule.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions_Dataprep"


/* FAssetTypeActions_Base interface
*****************************************************************************/

uint32 FAssetTypeActions_Dataprep::GetCategories()
{
	return IDataprepEditorModule::DataprepCategoryBit;
}


FText FAssetTypeActions_Dataprep::GetName() const
{
	return NSLOCTEXT("AssetTypeActions_Dataprep", "AssetTypeActions_Dataprep_Name", "Datasmith Dataprep");
}


UClass* FAssetTypeActions_Dataprep::GetSupportedClass() const
{
	return UDataprepRecipe::StaticClass();
}


FColor FAssetTypeActions_Dataprep::GetTypeColor() const
{
	return FColor::White;
}


#undef LOCTEXT_NAMESPACE
