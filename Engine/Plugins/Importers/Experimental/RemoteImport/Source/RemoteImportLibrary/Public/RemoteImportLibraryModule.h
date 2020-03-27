// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#define REMOTEIMPORTLIBRARY_MODULE_NAME TEXT("RemoteImportLibrary")

/**
 * This module exposes base RemoteImport features such as Anchors, and their API.
 *
 * RemoteImport is based on an Anchor system, that are setup server-side, discoverable by clients, and act as possible remote import destination.
 * When a client trigger an import on a destination Anchor, the anchor's Delegate is triggered, allowing any custom implementation to handle the import.
 *
 * The client/server communication rely on MessageBus over UDP.
 * The server is off by default, URemoteImportLibrary provides the API to handle it's life cycle.
 */
class FRemoteImportLibraryModule : public IModuleInterface
{
public:
	static inline FRemoteImportLibraryModule& Get()
	{
		return FModuleManager::LoadModuleChecked< FRemoteImportLibraryModule >(REMOTEIMPORTLIBRARY_MODULE_NAME);
	}

	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded(REMOTEIMPORTLIBRARY_MODULE_NAME);
	}
};
