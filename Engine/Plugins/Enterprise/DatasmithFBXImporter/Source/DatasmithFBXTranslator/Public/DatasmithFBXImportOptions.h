// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "DatasmithFBXImportOptions.generated.h"

class UDatasmithFBXSceneImportData;

UENUM()
enum class EDatasmithFBXIntermediateSerializationType : uint8
{
	Disabled UMETA(Tooltip="Import Fbx file"),
	Enabled UMETA(Tooltip="Import Fbx, save intermediate during import"),
	SaveLoadSkipFurtherImport UMETA(Tooltip="Just convert Fbx into intermediate format and do not import")
};

UCLASS(config = EditorPerProjectUserSettings, HideCategories=(DebugProperty))
class DATASMITHFBXTRANSLATOR_API UDatasmithFBXImportOptions : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category=AssetImporting, meta=(DisplayName="Texture folders", ToolTip="Where to look for textures"))
	TArray<FDirectoryPath> TextureDirs;

	UPROPERTY(config, EditAnywhere, Category=DebugProperty, meta=(DisplayName="Intermediate Serialization", ToolTip="Cache imported Fbx file in intermediate format for faster debugging"))
	EDatasmithFBXIntermediateSerializationType IntermediateSerialization;

	UPROPERTY(config, EditAnywhere, Category=DebugProperty, meta=(DisplayName="Colorize materials", ToolTip="Do not import actual materials from Fbx, but generate dummy colorized materials instead"))
	bool bColorizeMaterials;

public:
	/**
	 * Overwrites our data with data from a UDatasmithFBXSceneImportData object
	 */
	virtual void FromSceneImportData(UDatasmithFBXSceneImportData* InImportData);

	/**
	 * Places our data into a UDatasmithVREDSceneImportData object
	 */
	virtual void ToSceneImportData(UDatasmithFBXSceneImportData* OutImportData);
};
