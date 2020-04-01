// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/IncludeLvl1.inl"
#include "Modules/ModuleInterface.h"

class CHAOS_API FChaosEngineModule : public IModuleInterface
{
public:

	virtual void StartupModule() override {};
	virtual void ShutdownModule() override {};
};
