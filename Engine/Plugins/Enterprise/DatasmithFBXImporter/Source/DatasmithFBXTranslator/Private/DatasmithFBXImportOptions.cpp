// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithFBXImportOptions.h"

#include "DatasmithAssetImportData.h"

#include "CoreTypes.h"

#define LOCTEXT_NAMESPACE "DatasmithFBXImporter"

UDatasmithFBXImportOptions::UDatasmithFBXImportOptions(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, IntermediateSerialization(EDatasmithFBXIntermediateSerializationType::Disabled)
	, bColorizeMaterials(false)
{
}

void UDatasmithFBXImportOptions::FromSceneImportData(UDatasmithFBXSceneImportData* InImportData)
{
	if (InImportData)
	{
		IntermediateSerialization = (EDatasmithFBXIntermediateSerializationType)InImportData->IntermediateSerialization;
	}
}

void UDatasmithFBXImportOptions::ToSceneImportData(UDatasmithFBXSceneImportData* OutImportData)
{
	if (OutImportData)
	{
		OutImportData->IntermediateSerialization = (uint8)IntermediateSerialization;
	}
}

#undef LOCTEXT_NAMESPACE
