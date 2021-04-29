// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPDisplayCluster.h"

#include "Cluster/IPDisplayClusterClusterManager.h"
#include "Config/IPDisplayClusterConfigManager.h"
#include "Game/IPDisplayClusterGameManager.h"
#include "Render/IPDisplayClusterRenderManager.h"

class UDisplayClusterConfigurationData;


/**
 * Display Cluster module implementation
 */
class FDisplayClusterModule :
	public  IPDisplayCluster
{
public:
	FDisplayClusterModule();
	virtual ~FDisplayClusterModule();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayCluster
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool IsModuleInitialized() const override
	{ return bIsModuleInitialized; }
	
	virtual EDisplayClusterOperationMode GetOperationMode() const override
	{ return CurrentOperationMode; }

	virtual IDisplayClusterRenderManager*    GetRenderMgr()    const override { return MgrRender; }
	virtual IDisplayClusterClusterManager*   GetClusterMgr()   const override { return MgrCluster; }
	virtual IDisplayClusterConfigManager*    GetConfigMgr()    const override { return MgrConfig; }
	virtual IDisplayClusterGameManager*      GetGameMgr()      const override { return MgrGame; }

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IPDisplayCluster
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual IPDisplayClusterRenderManager*    GetPrivateRenderMgr()    const override { return MgrRender; }
	virtual IPDisplayClusterClusterManager*   GetPrivateClusterMgr()   const override { return MgrCluster; }
	virtual IPDisplayClusterConfigManager*    GetPrivateConfigMgr()    const override { return MgrConfig; }
	virtual IPDisplayClusterGameManager*      GetPrivateGameMgr()      const override { return MgrGame; }

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IPDisplayClusterManager
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool Init(EDisplayClusterOperationMode OperationMode) override;
	virtual void Release() override;
	virtual bool StartSession(UDisplayClusterConfigurationData* InConfigData, const FString& NodeId) override;
	virtual void EndSession() override;
	virtual bool StartScene(UWorld* InWorld) override;
	virtual void EndScene() override;
	virtual void StartFrame(uint64 FrameNum) override;
	virtual void PreTick(float DeltaSeconds) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PostTick(float DeltaSeconds) override;
	virtual void EndFrame(uint64 FrameNum) override;

public:
	virtual FDisplayClusterStartSessionEvent& OnDisplayClusterStartSession() override
	{ return DisplayClusterStartSessionEvent; }

	virtual FDisplayClusterEndSessionEvent& OnDisplayClusterEndSession() override
	{ return DisplayClusterEndSessionEvent; }

	virtual FDisplayClusterStartFrameEvent& OnDisplayClusterStartFrame() override
	{ return DisplayClusterStartFrameEvent; }

	virtual FDisplayClusterEndFrameEvent& OnDisplayClusterEndFrame() override
	{ return DisplayClusterEndFrameEvent; }

	virtual FDisplayClusterPreTickEvent& OnDisplayClusterPreTick() override
	{ return DisplayClusterPreTickEvent; }

	virtual FDisplayClusterTickEvent& OnDisplayClusterTick() override
	{ return DisplayClusterTickEvent; }

	virtual FDisplayClusterPostTickEvent& OnDisplayClusterPostTick() override
	{ return DisplayClusterPostTickEvent; }

	virtual FDisplayClusterStartSceneEvent& OnDisplayClusterStartScene() override
	{ return DisplayClusterStartSceneEvent; }

	virtual FDisplayClusterEndSceneEvent& OnDisplayClusterEndScene() override
	{ return DisplayClusterEndSceneEvent; }

private:
	FDisplayClusterStartSessionEvent         DisplayClusterStartSessionEvent;
	FDisplayClusterEndSessionEvent           DisplayClusterEndSessionEvent;
	FDisplayClusterStartFrameEvent           DisplayClusterStartFrameEvent;
	FDisplayClusterEndFrameEvent             DisplayClusterEndFrameEvent;
	FDisplayClusterPreTickEvent              DisplayClusterPreTickEvent;
	FDisplayClusterTickEvent                 DisplayClusterTickEvent;
	FDisplayClusterPostTickEvent             DisplayClusterPostTickEvent;
	FDisplayClusterStartSceneEvent           DisplayClusterStartSceneEvent;
	FDisplayClusterEndSceneEvent             DisplayClusterEndSceneEvent;

private:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IModuleInterface
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	// Is module initialized.
	// This flag is not the same as EDisplayClusterOperationMode::Disabled which is used when we turn off the DC functionality in a game mode.
	bool bIsModuleInitialized = false;

	// DisplayCluster subsystems
	IPDisplayClusterClusterManager*   MgrCluster   = nullptr;
	IPDisplayClusterRenderManager*    MgrRender    = nullptr;
	IPDisplayClusterConfigManager*    MgrConfig    = nullptr;
	IPDisplayClusterGameManager*      MgrGame      = nullptr;
	
	// Array of available managers
	TArray<IPDisplayClusterManager*> Managers;

	// Runtime
	EDisplayClusterOperationMode CurrentOperationMode = EDisplayClusterOperationMode::Disabled;
};
