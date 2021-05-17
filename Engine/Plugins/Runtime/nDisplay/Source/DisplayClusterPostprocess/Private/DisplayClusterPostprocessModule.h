// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "Render/PostProcess/IDisplayClusterPostProcess.h"

class IDisplayClusterProjectionPolicyFactory;


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
	TMap<FString, TSharedPtr<IDisplayClusterPostProcess, ESPMode::ThreadSafe>> PostprocessAssets;
};
