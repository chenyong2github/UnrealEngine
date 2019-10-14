// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_DataPrep.h"

#include "DataPrepRecipe.h"
#include "DataPrepCoreModule.h"

/* FAssetTypeActions_Base interface
*****************************************************************************/

uint32 FAssetTypeActions_Dataprep::GetCategories()
{
	return IDataprepCoreModule::DataprepCategoryBit;
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
