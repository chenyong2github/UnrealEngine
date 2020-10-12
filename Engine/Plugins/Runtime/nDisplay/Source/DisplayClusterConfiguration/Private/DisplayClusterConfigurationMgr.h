// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UDisplayClusterConfigurationData;


/**
 * Display Cluster configuration manager
 */
class FDisplayClusterConfigurationMgr
{
protected:
	FDisplayClusterConfigurationMgr();
	~FDisplayClusterConfigurationMgr();

public:
	// Singletone getter
	static FDisplayClusterConfigurationMgr& Get();

public:
	UDisplayClusterConfigurationData* LoadConfig(const FString& FilePath, UObject* Owner = nullptr);
	bool SaveConfig(const UDisplayClusterConfigurationData* Config, const FString& FilePath);
	UDisplayClusterConfigurationData* CreateDefaultStandaloneConfigData();

protected:
	enum class EConfigFileType
	{
		Unknown,
		Text,
		Json
	};

	EConfigFileType GetConfigFileType(const FString& InConfigPath) const;
};
