// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"


class DMXCONTROLCONSOLE_API IDMXControlConsoleModule
	: public IModuleInterface
{
public:
	static IDMXControlConsoleModule& Get()
	{
		return FModuleManager::Get().GetModuleChecked<IDMXControlConsoleModule>("DMXControlConsole");
	}
};
