// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "Containers/Array.h"
#include "Modules/ModuleManager.h"

class FLiveLinkHeartbeatEmitter;
class FLiveLinkMessageBusDiscoveryManager;

/**
 * Interface for messaging modules.
 */
class ILiveLinkModule
	: public IModuleInterface
{
public:

	/**
	 * Gets a reference to the live link module instance.
	 *
	 * @return A reference to the live link module.
	 */
	static ILiveLinkModule& Get()
	{
#if PLATFORM_IOS
        static ILiveLinkModule& LiveLinkModule = FModuleManager::LoadModuleChecked<ILiveLinkModule>("LiveLink");
        return LiveLinkModule;
#else
        return FModuleManager::LoadModuleChecked<ILiveLinkModule>("LiveLink");
#endif
	}

	virtual FLiveLinkHeartbeatEmitter& GetHeartbeatEmitter() = 0;
	virtual FLiveLinkMessageBusDiscoveryManager& GetMessageBusDiscoveryManager() = 0;

public:

	/** Virtual destructor. */
	virtual ~ILiveLinkModule() { }
};

