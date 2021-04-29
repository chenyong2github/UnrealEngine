// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterModule.h"

#include "Cluster/DisplayClusterClusterManager.h"
#include "Config/DisplayClusterConfigManager.h"
#include "Game/DisplayClusterGameManager.h"
#include "Render/DisplayClusterRenderManager.h"

#include "DisplayClusterConfigurationTypes.h"

#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterLog.h"
#include "Misc/DisplayClusterTypesConverter.h"


FDisplayClusterModule::FDisplayClusterModule()
{
	GDisplayCluster = this;

	UE_LOG(LogDisplayClusterModule, Log, TEXT("Instantiating subsystem managers..."));

	// Initialize internals (the order is important)
	Managers.Add(MgrConfig  = new FDisplayClusterConfigManager);
	Managers.Add(MgrCluster = new FDisplayClusterClusterManager);
	Managers.Add(MgrGame    = new FDisplayClusterGameManager);
	Managers.Add(MgrRender  = new FDisplayClusterRenderManager);
}

FDisplayClusterModule::~FDisplayClusterModule()
{
	GDisplayCluster = nullptr;
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IModuleInterface
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterModule::StartupModule()
{
	UE_LOG(LogDisplayClusterModule, Log, TEXT("DisplayCluster module has been started"));
}

void FDisplayClusterModule::ShutdownModule()
{
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
	CurrentOperationMode = OperationMode;

	UE_LOG(LogDisplayClusterModule, Log, TEXT("Initializing subsystems to %s operation mode"), *DisplayClusterTypesConverter::template ToString(CurrentOperationMode));

	bool bResult = true;
	auto it = Managers.CreateIterator();
	while (bResult && it)
	{
		bResult = bResult && (*it)->Init(CurrentOperationMode);
		++it;
	}

	if (!bResult)
	{
		UE_LOG(LogDisplayClusterModule, Error, TEXT("An error occurred during internal initialization"));
	}

	// Set internal initialization flag
	bIsModuleInitialized = bResult;

	return bResult;
}

void FDisplayClusterModule::Release()
{
	UE_LOG(LogDisplayClusterModule, Log, TEXT("Cleaning up internals..."));

	for (auto pMgr : Managers)
	{
		pMgr->Release();
		delete pMgr;
	}

	Managers.Empty();
}

bool FDisplayClusterModule::StartSession(UDisplayClusterConfigurationData* InConfigData, const FString& NodeId)
{
	UE_LOG(LogDisplayClusterModule, Log, TEXT("StartSession with node ID '%s'"), *NodeId);

	bool bResult = true;
	auto it = Managers.CreateIterator();
	while (bResult && it)
	{
		bResult = bResult && (*it)->StartSession(InConfigData, NodeId);
		++it;
	}

	DisplayClusterStartSessionEvent.Broadcast();

	if (!bResult)
	{
		UE_LOG(LogDisplayClusterModule, Error, TEXT("An error occurred during session start"));
	}

	return bResult;
}

void FDisplayClusterModule::EndSession()
{
	UE_LOG(LogDisplayClusterModule, Log, TEXT("Stopping DisplayCluster session..."));

	DisplayClusterEndSessionEvent.Broadcast();

	for (auto pMgr : Managers)
	{
		pMgr->EndSession();
	}
}

bool FDisplayClusterModule::StartScene(UWorld* InWorld)
{
	UE_LOG(LogDisplayClusterModule, Log, TEXT("Starting game..."));

	check(InWorld);

	DisplayClusterStartSceneEvent.Broadcast();

	bool bResult = true;
	auto it = Managers.CreateIterator();
	while (bResult && it)
	{
		bResult = bResult && (*it)->StartScene(InWorld);
		++it;
	}

	if (!bResult)
	{
		UE_LOG(LogDisplayClusterModule, Error, TEXT("An error occurred during game (level) start"));
	}

	return bResult;
}

void FDisplayClusterModule::EndScene()
{
	UE_LOG(LogDisplayClusterModule, Log, TEXT("Stopping game..."));

	DisplayClusterEndSceneEvent.Broadcast();

	for (auto pMgr : Managers)
	{
		pMgr->EndScene();
	}
}

void FDisplayClusterModule::StartFrame(uint64 FrameNum)
{
	UE_LOG(LogDisplayClusterModule, Verbose, TEXT("StartFrame: frame num - %llu"), FrameNum);

	for (auto pMgr : Managers)
	{
		pMgr->StartFrame(FrameNum);
	}

	DisplayClusterStartFrameEvent.Broadcast(FrameNum);
}

void FDisplayClusterModule::EndFrame(uint64 FrameNum)
{
	UE_LOG(LogDisplayClusterModule, Verbose, TEXT("EndFrame: frame num - %llu"), FrameNum);

	for (auto pMgr : Managers)
	{
		pMgr->EndFrame(FrameNum);
	}

	DisplayClusterEndFrameEvent.Broadcast(FrameNum);
}

void FDisplayClusterModule::PreTick(float DeltaSeconds)
{
	UE_LOG(LogDisplayClusterModule, Verbose, TEXT("PreTick: delta time - %f"), DeltaSeconds);

	for (auto pMgr : Managers)
	{
		pMgr->PreTick(DeltaSeconds);
	}

	DisplayClusterPreTickEvent.Broadcast();
}

void FDisplayClusterModule::Tick(float DeltaSeconds)
{
	UE_LOG(LogDisplayClusterModule, Verbose, TEXT("Tick: delta time - %f"), DeltaSeconds);

	for (auto pMgr : Managers)
	{
		pMgr->Tick(DeltaSeconds);
	}

	DisplayClusterTickEvent.Broadcast();
}

void FDisplayClusterModule::PostTick(float DeltaSeconds)
{
	UE_LOG(LogDisplayClusterModule, Verbose, TEXT("PostTick: delta time - %f"), DeltaSeconds);

	for (auto pMgr : Managers)
	{
		pMgr->PostTick(DeltaSeconds);
	}

	DisplayClusterPostTickEvent.Broadcast();
}

IMPLEMENT_MODULE(FDisplayClusterModule, DisplayCluster)
