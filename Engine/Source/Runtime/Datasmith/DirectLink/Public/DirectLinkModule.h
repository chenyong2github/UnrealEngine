// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#define DIRECTLINK_MODULE_NAME TEXT("DirectLink")

/**
 * #ue_directlink_doc: Module description
 */
class FDirectLinkModule : public IModuleInterface
{
public:
	static inline FDirectLinkModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FDirectLinkModule>(DIRECTLINK_MODULE_NAME);
	}

	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded(DIRECTLINK_MODULE_NAME);
	}
};
