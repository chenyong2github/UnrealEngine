// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/GameEngine.h"

#include "Config/DisplayClusterConfigTypes.h"

#include "DisplayClusterEnums.h"

#include "Tickable.h"
#include "Stats/Stats2.h"

#include "DisplayClusterGameEngine.generated.h"


class IPDisplayClusterClusterManager;
class IPDisplayClusterNodeController;
class IPDisplayClusterInputManager;
class IDisplayClusterClusterSyncObject;


/**
 * This helper allows to properly synchronize objects on the current frame
 */
UCLASS()
class DISPLAYCLUSTER_API UDisplayClusterGameEngineTickableHelper
	: public UObject
	, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual ETickableTickType GetTickableTickType() const override
	{ return ETickableTickType::Always; }

	virtual TStatId GetStatId() const override;
	virtual void Tick(float DeltaSeconds) override;
};


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

	EDisplayClusterOperationMode GetOperationMode() const { return OperationMode; }

protected:
	virtual bool InitializeInternals();
	EDisplayClusterOperationMode DetectOperationMode();

private:
	IPDisplayClusterClusterManager* ClusterMgr = nullptr;
	IPDisplayClusterNodeController* NodeController = nullptr;
	IPDisplayClusterInputManager*   InputMgr = nullptr;

	FDisplayClusterConfigDebug CfgDebug;
	EDisplayClusterOperationMode OperationMode = EDisplayClusterOperationMode::Disabled;

	UDisplayClusterGameEngineTickableHelper* TickableHelper;
};
