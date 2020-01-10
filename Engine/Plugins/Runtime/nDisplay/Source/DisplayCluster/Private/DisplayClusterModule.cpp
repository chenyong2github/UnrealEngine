// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterModule.h"

#include "Cluster/DisplayClusterClusterManager.h"
#include "Config/DisplayClusterConfigManager.h"
#include "Game/DisplayClusterGameManager.h"
#include "Input/DisplayClusterInputManager.h"
#include "Render/DisplayClusterRenderManager.h"

#include "DisplayClusterGlobals.h"
#include "DisplayClusterLog.h"


FDisplayClusterModule::FDisplayClusterModule()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterModule);

	GDisplayCluster = this;

	UE_LOG(LogDisplayClusterModule, Log, TEXT("Instantiating subsystem managers..."));

	// Initialize internals (the order is important)
	Managers.Add(MgrConfig  = new FDisplayClusterConfigManager);
	Managers.Add(MgrCluster = new FDisplayClusterClusterManager);
	Managers.Add(MgrGame    = new FDisplayClusterGameManager);
	Managers.Add(MgrRender  = new FDisplayClusterRenderManager);
	Managers.Add(MgrInput   = new FDisplayClusterInputManager);
}

FDisplayClusterModule::~FDisplayClusterModule()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterModule);

#if 1
	GDisplayCluster = nullptr;
#else
	// WORKAROUND
	// UE4 does something like that:
	// 1. inst1 = new FDisplayClusterModule
	// 2. inst2 = new FDisplayClusterModule
	// 3. delete inst1
	// To store valid pointer (inst2) I need the check below.
	if (GDisplayCluster == this)
	{
		GDisplayCluster = nullptr;
	}
#endif
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IModuleInterface
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterModule::StartupModule()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterModule);

	UE_LOG(LogDisplayClusterModule, Log, TEXT("DisplayCluster module has been started"));
}

void FDisplayClusterModule::ShutdownModule()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterModule);

	// Clean everything before .dtor call
	Release();
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IPDisplayCluster
//////////////////////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////////////////////
// IPDisplayClusterManager
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterModule::Init(EDisplayClusterOperationMode OperationMode)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterModule);

	CurrentOperationMode = OperationMode;

	UE_LOG(LogDisplayClusterModule, Log, TEXT("Initializing subsystems to %s operation mode"), *FDisplayClusterTypesConverter::ToString(CurrentOperationMode));

	bool result = true;
	auto it = Managers.CreateIterator();
	while (result && it)
	{
		result = result && (*it)->Init(CurrentOperationMode);
		++it;
	}

	if (!result)
	{
		UE_LOG(LogDisplayClusterModule, Error, TEXT("An error occurred during internal initialization"));
	}

	// Set internal initialization flag
	bIsModuleInitialized = result;

	return result;
}

void FDisplayClusterModule::Release()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterModule);

	UE_LOG(LogDisplayClusterModule, Log, TEXT("Cleaning up internals..."));

	for (auto pMgr : Managers)
	{
		pMgr->Release();
		delete pMgr;
	}

	Managers.Empty();
}

bool FDisplayClusterModule::StartSession(const FString& configPath, const FString& nodeId)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterModule);

	UE_LOG(LogDisplayClusterModule, Log, TEXT("StartSession: config path is %s"), *configPath);

	bool result = true;
	auto it = Managers.CreateIterator();
	while (result && it)
	{
		result = result && (*it)->StartSession(configPath, nodeId);
		++it;
	}

	DisplayClusterStartSessionEvent.Broadcast();

	if (!result)
	{
		UE_LOG(LogDisplayClusterModule, Error, TEXT("An error occurred during session start"));
	}

	return result;
}

void FDisplayClusterModule::EndSession()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterModule);

	UE_LOG(LogDisplayClusterModule, Log, TEXT("Stopping DisplayCluster session..."));

	DisplayClusterEndSessionEvent.Broadcast();

	for (auto pMgr : Managers)
	{
		pMgr->EndSession();
	}
}

bool FDisplayClusterModule::StartScene(UWorld* pWorld)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterModule);

	UE_LOG(LogDisplayClusterModule, Log, TEXT("Starting game..."));

	check(pWorld);

	bool result = true;
	auto it = Managers.CreateIterator();
	while (result && it)
	{
		result = result && (*it)->StartScene(pWorld);
		++it;
	}

	if (!result)
	{
		UE_LOG(LogDisplayClusterModule, Error, TEXT("An error occurred during game (level) start"));
	}

	return result;
}

void FDisplayClusterModule::EndScene()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterModule);

	UE_LOG(LogDisplayClusterModule, Log, TEXT("Stopping game..."));

	for (auto pMgr : Managers)
	{
		pMgr->EndScene();
	}
}

void FDisplayClusterModule::StartFrame(uint64 FrameNum)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterModule);

	UE_LOG(LogDisplayClusterModule, Verbose, TEXT("StartFrame: frame num - %llu"), FrameNum);

	for (auto pMgr : Managers)
	{
		pMgr->StartFrame(FrameNum);
	}

	DisplayClusterStartFrameEvent.Broadcast(FrameNum);
}

void FDisplayClusterModule::EndFrame(uint64 FrameNum)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterModule);

	UE_LOG(LogDisplayClusterModule, Verbose, TEXT("EndFrame: frame num - %llu"), FrameNum);

	for (auto pMgr : Managers)
	{
		pMgr->EndFrame(FrameNum);
	}

	DisplayClusterEndFrameEvent.Broadcast(FrameNum);
}

void FDisplayClusterModule::PreTick(float DeltaSeconds)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterModule);

	UE_LOG(LogDisplayClusterModule, Verbose, TEXT("PreTick: delta time - %f"), DeltaSeconds);

	for (auto pMgr : Managers)
	{
		pMgr->PreTick(DeltaSeconds);
	}

	DisplayClusterPreTickEvent.Broadcast();
}

void FDisplayClusterModule::Tick(float DeltaSeconds)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterModule);

	UE_LOG(LogDisplayClusterModule, Verbose, TEXT("Tick: delta time - %f"), DeltaSeconds);

	for (auto pMgr : Managers)
	{
		pMgr->Tick(DeltaSeconds);
	}

	DisplayClusterTickEvent.Broadcast();
}

void FDisplayClusterModule::PostTick(float DeltaSeconds)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterModule);

	UE_LOG(LogDisplayClusterModule, Verbose, TEXT("PostTick: delta time - %f"), DeltaSeconds);

	for (auto pMgr : Managers)
	{
		pMgr->PostTick(DeltaSeconds);
	}

	DisplayClusterPostTickEvent.Broadcast();
}

IMPLEMENT_MODULE(FDisplayClusterModule, DisplayCluster)
