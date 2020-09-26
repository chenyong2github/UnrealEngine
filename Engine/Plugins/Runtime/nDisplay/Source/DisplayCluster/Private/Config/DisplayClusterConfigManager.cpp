// Copyright Epic Games, Inc. All Rights Reserved.

#include "Config/DisplayClusterConfigManager.h"

#include "IPDisplayCluster.h"
#include "Misc/DisplayClusterGlobals.h"
#include "Cluster/IPDisplayClusterClusterManager.h"

#include "IDisplayClusterConfiguration.h"

#include "Misc/Paths.h"

#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterStrings.h"
#include "Misc/DisplayClusterLog.h"

#include "HAL/FileManager.h"


//////////////////////////////////////////////////////////////////////////////////////////////
// IPDisplayClusterManager
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterConfigManager::Init(EDisplayClusterOperationMode OperationMode)
{
	return true;
}

void FDisplayClusterConfigManager::Release()
{
}

bool FDisplayClusterConfigManager::StartSession(const FString& InConfigPath, const FString& InNodeId)
{
	ConfigPath = InConfigPath;
	ClusterNodeId = InNodeId;

	UE_LOG(LogDisplayClusterConfig, Log, TEXT("Starting session with config: %s"), *ConfigPath);
	ConfigData.Reset(IDisplayClusterConfiguration::Get().LoadConfig(ConfigPath));

	return ConfigData.IsValid();
}

void FDisplayClusterConfigManager::EndSession()
{
	ConfigPath.Empty();
	ClusterNodeId.Empty();
	ConfigData.Reset(nullptr);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterConfigManager
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterConfigManager::GetMasterNodeId() const
{
	return ConfigData ? ConfigData->Cluster->MasterNode.Id : FString();
}

const UDisplayClusterConfigurationClusterNode* FDisplayClusterConfigManager::GetMasterNode() const
{
	return ConfigData ? ConfigData->GetClusterNode(GetMasterNodeId()) : nullptr;
}

const UDisplayClusterConfigurationClusterNode* FDisplayClusterConfigManager::GetLocalNode() const
{
	return ConfigData ? ConfigData->GetClusterNode(GetLocalNodeId()) : nullptr;
}

const UDisplayClusterConfigurationViewport* FDisplayClusterConfigManager::GetLocalViewport(const FString& ViewportId) const
{
	return ConfigData ? ConfigData->GetViewport(GetLocalNodeId(), ViewportId) : nullptr;
}

bool FDisplayClusterConfigManager::GetLocalPostprocess(const FString& PostprocessId, FDisplayClusterConfigurationPostprocess& OutPostprocess) const
{
	return ConfigData ? ConfigData->GetPostprocess(GetLocalNodeId(), PostprocessId, OutPostprocess) : false;
}

bool FDisplayClusterConfigManager::GetLocalProjection(const FString& ViewportId, FDisplayClusterConfigurationProjection& OutProjection) const
{
	return ConfigData ? ConfigData->GetProjectionPolicy(GetLocalNodeId(), ViewportId, OutProjection) : false;
}
