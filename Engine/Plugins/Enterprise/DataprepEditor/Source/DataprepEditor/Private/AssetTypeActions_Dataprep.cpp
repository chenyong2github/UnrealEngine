// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_Dataprep.h"

#include "DataprepEditorModule.h"
#include "DataprepRecipe.h"

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
