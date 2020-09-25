// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/IDisplayClusterConfigurator.h"

class FDisplayClusterConfiguratorAssetTypeActions;

/**
 * Display Cluster Configurator editor module
 */
class FDisplayClusterConfiguratorModule :
	public IDisplayClusterConfigurator
{
public:
	//~ IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

public:
	//~ Begin IDisplayClusterConfigurator Interface
	virtual const FDisplayClusterConfiguratorCommands& GetCommands() const override;
	//~ End IDisplayClusterConfigurator Interface

private:
	TSharedPtr<FDisplayClusterConfiguratorAssetTypeActions> ConfiguratorAssetTypeAction;
};
