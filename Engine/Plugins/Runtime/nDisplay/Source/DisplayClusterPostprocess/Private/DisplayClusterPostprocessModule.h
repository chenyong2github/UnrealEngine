// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"

#include "Render/PostProcess/IDisplayClusterPostProcessFactory.h"

class FDisplayClusterPostprocessModule
	: public IModuleInterface
{
public:
	FDisplayClusterPostprocessModule();
	virtual ~FDisplayClusterPostprocessModule();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IModuleInterface
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	// Available postprocess policy
	TMap<FString, TSharedPtr<IDisplayClusterPostProcessFactory>> PostprocessAssets;
};
