// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDisplayClusterConfiguration.h"

class UDisplayClusterConfigurationData;


/**
 * Display Cluster configuration module
 */
class FDisplayClusterConfigurationModule :
	public IDisplayClusterConfiguration
{
public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IModuleInterface
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterConfiguration
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual UDisplayClusterConfigurationData* LoadConfig(const FString& FilePath, UObject* Owner = nullptr) override;
	virtual bool SaveConfig(const UDisplayClusterConfigurationData* Config, const FString& FilePath) override;
};
