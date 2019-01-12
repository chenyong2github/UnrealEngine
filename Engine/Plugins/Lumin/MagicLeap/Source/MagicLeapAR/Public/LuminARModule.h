// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

/**
 * The public interface to this module.
 */
class ILuminARModule : public IModuleInterface
{
public:
	//create for mutual connection (regardless of construction order)
	virtual TSharedPtr<IARSystemSupport, ESPMode::ThreadSafe> CreateARImplementation() = 0;
	//Now connect (regardless of connection order)
	virtual void ConnectARImplementationToXRSystem(FXRTrackingSystemBase* InXRTrackingSystem) = 0;
	//Now initialize fully connected systems
	virtual void InitializeARImplementation() = 0;
};
