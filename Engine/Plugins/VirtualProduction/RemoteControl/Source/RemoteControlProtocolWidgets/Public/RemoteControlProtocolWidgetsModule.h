// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "Delegates/IDelegateInstance.h"
#include "UObject/ObjectKey.h"
#include "Templates/SharedPointer.h"

class FRemoteControlProtocolWidgetsModule : public IModuleInterface
{
public:
	// IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};