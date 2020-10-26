// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class ALandscape;
struct FLandscapeFileResolution;
struct FLandscapeImportLayerInfo;
class ULandscapeEditorObject;

// This class has been replaced by FLandscapeImportHelper & ULandscapeEditorObject methods.
// @todo_ow: deprecate (probably just delete since it was added for Datasmith which was updated to the new import code)
class LANDSCAPEEDITOR_API FNewLandscapeUtils
{
public:
	static void ChooseBestComponentSizeForImport( ULandscapeEditorObject* UISettings );
	static void ImportLandscapeData( ULandscapeEditorObject* UISettings, TArray< FLandscapeFileResolution >& ImportResolutions );
	static TOptional< TArray< FLandscapeImportLayerInfo > > CreateImportLayersInfo( ULandscapeEditorObject* UISettings, int32 NewLandscapePreviewMode );
	static TArray< uint16 > ComputeHeightData( ULandscapeEditorObject* UISettings, TArray< FLandscapeImportLayerInfo >& ImportLayers, int32 NewLandscapePreviewMode );
};
