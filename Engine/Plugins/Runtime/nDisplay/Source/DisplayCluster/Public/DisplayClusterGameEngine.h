// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/GameEngine.h"

#include "DisplayClusterEnums.h"
#include "DisplayClusterConfigurationTypes.h"

#include "DisplayClusterGameEngine.generated.h"


class IPDisplayClusterClusterManager;
class IPDisplayClusterInputManager;
class IDisplayClusterNodeController;
class IDisplayClusterClusterSyncObject;
class UDisplayClusterConfigurationData;


/**
 * Extended game engine
 */
UCLASS()
class DISPLAYCLUSTER_API UDisplayClusterGameEngine
	: public UGameEngine
{
	GENERATED_BODY()

public:
	virtual void Init(class IEngineLoop* InEngineLoop) override;
	virtual void PreExit() override;
	virtual void Tick(float DeltaSeconds, bool bIdleMode) override;
	virtual bool LoadMap(FWorldContext& WorldContext, FURL URL, class UPendingNetGame* Pending, FString& Error) override;

public:
	EDisplayClusterOperationMode GetOperationMode() const
	{
		return OperationMode;
	}

protected:
	virtual bool InitializeInternals();
	EDisplayClusterOperationMode DetectOperationMode() const;
	bool GetResolvedNodeId(const UDisplayClusterConfigurationData* ConfigData, FString& NodeId) const;

private:
	IPDisplayClusterClusterManager* ClusterMgr = nullptr;
	IPDisplayClusterInputManager*   InputMgr = nullptr;

	IDisplayClusterNodeController* NodeController = nullptr;

	FDisplayClusterConfigurationDiagnostics Diagnostics;

	EDisplayClusterOperationMode OperationMode = EDisplayClusterOperationMode::Disabled;
};
