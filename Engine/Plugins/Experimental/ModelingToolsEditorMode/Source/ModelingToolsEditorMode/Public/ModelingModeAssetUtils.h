// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UWorld;


namespace UE
{
namespace Modeling
{

//
// Utility functions that can be used to plug in the client-configurable parts of UEditorModelingObjectsCreationAPI.
// See ModelingToolsEditorMode for usage.
//

/**
 * Determines path and name for a new Asset based on current mode settings, etc
 * @param BaseName desired initial string for the name
 * @param TargetWorld World the new Asset will be used in, which can determine its generated path based on various settings
 * @param SuggestedFolder a suggested path for the asset, ignored if empty
 * @return Path+Name for the new Asset (package)
 */
MODELINGTOOLSEDITORMODE_API FString GetNewAssetPathName(const FString& BaseName, const UWorld* TargetWorld, FString SuggestedFolder);


/**
 * Utility function that may auto-save a newly created asset depending on current mode settings,
 * and posts notifications for the Editor
 * @param Asset the new asset object (eg UStaticMesh, UTexture2D, etc)
 */
MODELINGTOOLSEDITORMODE_API void OnNewAssetCreated(UObject* Asset);


/**
 * Utility function that will attempt to auto-save an asset, and post Editor change notifications
 * @param Asset the  asset object (eg UStaticMesh, UTexture2D, etc)
 */
MODELINGTOOLSEDITORMODE_API bool AutoSaveAsset(UObject* Asset);

}
}