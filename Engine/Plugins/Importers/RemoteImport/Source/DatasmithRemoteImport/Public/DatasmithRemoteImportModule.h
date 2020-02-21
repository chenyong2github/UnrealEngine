// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#define DATASMITHREMOTEIMPORT_MODULE_NAME TEXT("DatasmithRemoteImport")

/**
 * Exposes:
 * - DatasmithAdapter                   Proxy for Datasmith Translators
 * - UDatasmithRemoteEndpointComponent  SceneComponent that scopes a Remote Import Anchor while in Play
 */
class DATASMITHREMOTEIMPORT_API FDatasmithRemoteImportModule : public IModuleInterface
{
public:
    static inline FDatasmithRemoteImportModule& Get()
    {
        return FModuleManager::LoadModuleChecked< FDatasmithRemoteImportModule >(DATASMITHREMOTEIMPORT_MODULE_NAME);
    }

    static inline bool IsAvailable()
    {
        return FModuleManager::Get().IsModuleLoaded(DATASMITHREMOTEIMPORT_MODULE_NAME);
    }
};
