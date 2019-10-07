// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithFBXImportOptions.h"

#include "DatasmithAssetImportData.h"

#include "CoreTypes.h"

#define LOCTEXT_NAMESPACE "DatasmithFBXImporter"

UDatasmithFBXImportOptions::UDatasmithFBXImportOptions(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bGenerateLightmapUVs(false)
	, IntermediateSerialization(EDatasmithFBXIntermediateSerializationType::Disabled)
	, bColorizeMaterials(false)
{
}

void UDatasmithFBXImportOptions::FromSceneImportData(UDatasmithFBXSceneImportData* InImportData)
{
	if (InImportData)
	{
		bGenerateLightmapUVs		= InImportData->bGenerateLightmapUVs;
		//TexturesDir.Path			= InImportData->TexturesDir;
		IntermediateSerialization	= (EDatasmithFBXIntermediateSerializationType)InImportData->IntermediateSerialization;
	}
}

void UDatasmithFBXImportOptions::ToSceneImportData(UDatasmithFBXSceneImportData* OutImportData)
{
	if (OutImportData)
	{
		OutImportData->bGenerateLightmapUVs			= bGenerateLightmapUVs;
		//OutImportData->TexturesDir					= TexturesDir.Path;
		OutImportData->IntermediateSerialization	= (uint8)IntermediateSerialization;
	}
}

#undef LOCTEXT_NAMESPACE
