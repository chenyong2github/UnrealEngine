// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#define REMOTEIMPORTMESSAGING_MODULE_NAME TEXT("RemoteImportMessaging")

/**
 * Exposes Messages types used between RemoteImport clients and servers.
 */
class FRemoteImportMessagingModule : public IModuleInterface
{
public:
	static FRemoteImportMessagingModule& Get()
	{
		return FModuleManager::LoadModuleChecked< FRemoteImportMessagingModule >(REMOTEIMPORTMESSAGING_MODULE_NAME);
	}

	static bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded(REMOTEIMPORTMESSAGING_MODULE_NAME);
	}
};
