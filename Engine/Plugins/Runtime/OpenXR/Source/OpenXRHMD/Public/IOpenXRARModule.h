// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Features/IModularFeature.h"

class IOpenXRARModule : public IModuleInterface, public IModularFeature
{
public:
	/** Used to init our AR system */
	virtual class IARSystemSupport* CreateARSystem() = 0;
	virtual void SetTrackingSystem(TSharedPtr<class FXRTrackingSystemBase, ESPMode::ThreadSafe> InTrackingSystem) = 0;
	virtual bool GetExtensions(TArray<const ANSICHAR*>& OutExtensions) = 0;
};
