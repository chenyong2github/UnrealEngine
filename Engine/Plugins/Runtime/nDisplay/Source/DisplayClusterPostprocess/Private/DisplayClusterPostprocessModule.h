// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDisplayClusterPostProcess.h"
#include "Render/PostProcess/IDisplayClusterPostProcess.h"

class IDisplayClusterProjectionPolicyFactory;


class FDisplayClusterPostprocessModule
	: public IDisplayClusterPostprocess
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
	TMap<FString, TSharedPtr<IDisplayClusterPostProcess>> PostprocessAssets;
};
