// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanProjectUtilities.h"
#include "MetaHumanImport.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, MetaHumanProjectUtilities)

// External APIs
void METAHUMANPROJECTUTILITIES_API FMetaHumanProjectUtilities::EnableAutomation(IMetaHumanProjectUtilitiesAutomationHandler* Handler)
{
	FMetaHumanImport::Get()->SetAutomationHandler(Handler);
}

void METAHUMANPROJECTUTILITIES_API FMetaHumanProjectUtilities::SetBulkImportHandler(IMetaHumanBulkImportHandler* Handler)
{
	FMetaHumanImport::Get()->SetBulkImportHandler(Handler);
}

void METAHUMANPROJECTUTILITIES_API FMetaHumanProjectUtilities::ImportAsset(const FMetaHumanAssetImportDescription& AssetImportDescription)
{
	FMetaHumanImport::Get()->ImportAsset(AssetImportDescription);
}
