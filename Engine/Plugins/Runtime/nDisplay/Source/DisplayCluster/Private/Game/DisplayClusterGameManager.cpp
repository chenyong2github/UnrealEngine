// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Game/DisplayClusterGameManager.h"

#include "Config/IPDisplayClusterConfigManager.h"

#include "Misc/DisplayClusterHelpers.h"

#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Config/DisplayClusterConfigTypes.h"
#include "Config/IPDisplayClusterConfigManager.h"

#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "DisplayClusterCameraComponent.h"
#include "DisplayClusterRootComponent.h"
#include "DisplayClusterSceneComponent.h"
#include "DisplayClusterScreenComponent.h"

#include "DisplayClusterGlobals.h"
#include "DisplayClusterLog.h"
#include "DisplayClusterStrings.h"

#include "GameFramework/Actor.h"


FDisplayClusterGameManager::FDisplayClusterGameManager()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterGame);
}

FDisplayClusterGameManager::~FDisplayClusterGameManager()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterGame);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IPDisplayClusterManager
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterGameManager::Init(EDisplayClusterOperationMode OperationMode)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterGame);

	CurrentOperationMode = OperationMode;

	return true;
}

void FDisplayClusterGameManager::Release()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterGame);
}

bool FDisplayClusterGameManager::StartSession(const FString& configPath, const FString& nodeId)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterGame);

	ConfigPath = configPath;
	ClusterNodeId = nodeId;

	return true;
}

void FDisplayClusterGameManager::EndSession()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterGame);
}

bool FDisplayClusterGameManager::StartScene(UWorld* InWorld)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterGame);

	check(InWorld);
	CurrentWorld = InWorld;

	// Look for existing VR root components
	for (AActor* Actor : InWorld->PersistentLevel->Actors)
	{
		if (Actor && !Actor->IsPendingKill())
		{
			CurrentRoot = Actor->FindComponentByClass<UDisplayClusterRootComponent>();
			if (CurrentRoot)
			{
				UE_LOG(LogDisplayClusterGame, Log, TEXT("Found root component - %s"), *CurrentRoot->GetName());
				break;
			}
		}
	}

	APlayerController* const PlayerController = GetWorld()->GetFirstPlayerController();
	if (!CurrentRoot && PlayerController)
	{
		APawn* CurPawn = PlayerController->GetPawn();
		if (CurPawn)
		{
			SpawnRootComponent(CurPawn);
		}
	}

	return true;
}

void FDisplayClusterGameManager::EndScene()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterGame);
	FScopeLock lock(&InternalsSyncScope);

	CurrentRoot = nullptr;
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterGameManager
//////////////////////////////////////////////////////////////////////////////////////////////
UDisplayClusterRootComponent* FDisplayClusterGameManager::GetRoot() const
{
	FScopeLock lock(&InternalsSyncScope);
	return CurrentRoot;
}

void FDisplayClusterGameManager::SetRoot(UDisplayClusterRootComponent* InRoot)
{
	FScopeLock lock(&InternalsSyncScope);
	CurrentRoot = InRoot;
}

TArray<UDisplayClusterScreenComponent*> FDisplayClusterGameManager::GetAllScreens() const
{
	FScopeLock lock(&InternalsSyncScope);

	if (!CurrentRoot)
	{
		return TArray<UDisplayClusterScreenComponent*>();
	}

	return CurrentRoot->GetAllScreens();
}

UDisplayClusterScreenComponent* FDisplayClusterGameManager::GetScreenById(const FString& ScreenId) const
{
	FScopeLock lock(&InternalsSyncScope);

	if (!CurrentRoot)
	{
		return nullptr;
	}

	return CurrentRoot->GetScreenById(ScreenId);
}

int32 FDisplayClusterGameManager::GetScreensAmount() const
{
	FScopeLock lock(&InternalsSyncScope);

	if (!CurrentRoot)
	{
		return 0;
	}

	return CurrentRoot->GetScreensAmount();
}

UDisplayClusterCameraComponent* FDisplayClusterGameManager::GetCameraById(const FString& CameraId) const
{
	FScopeLock lock(&InternalsSyncScope);

	if (!CurrentRoot)
	{
		return nullptr;
	}

	return CurrentRoot->GetCameraById(CameraId);
}

TArray<UDisplayClusterCameraComponent*> FDisplayClusterGameManager::GetAllCameras() const
{
	FScopeLock lock(&InternalsSyncScope);

	if (!CurrentRoot)
	{
		return TArray<UDisplayClusterCameraComponent*>();
	}

	return CurrentRoot->GetAllCameras();
}

int32 FDisplayClusterGameManager::GetCamerasAmount() const
{
	FScopeLock lock(&InternalsSyncScope);

	if (!CurrentRoot)
	{
		return 0;
	}

	return CurrentRoot->GetCamerasAmount();
}

UDisplayClusterCameraComponent* FDisplayClusterGameManager::GetDefaultCamera() const
{
	FScopeLock lock(&InternalsSyncScope);

	if (!CurrentRoot)
	{
		return nullptr;
	}

	return CurrentRoot->GetDefaultCamera();
}

void FDisplayClusterGameManager::SetDefaultCamera(const FString& id)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterGame);

	if(!CurrentRoot)
	{
		return;
	}

	CurrentRoot->SetDefaultCamera(id);
}

UDisplayClusterSceneComponent* FDisplayClusterGameManager::GetNodeById(const FString& NodeId) const
{
	FScopeLock lock(&InternalsSyncScope);

	if (!CurrentRoot)
	{
		return nullptr;
	}

	return CurrentRoot->GetNodeById(NodeId);
}

TArray<UDisplayClusterSceneComponent*> FDisplayClusterGameManager::GetAllNodes() const
{
	FScopeLock lock(&InternalsSyncScope);

	if (!CurrentRoot)
	{
		return TArray<UDisplayClusterSceneComponent*>();
	}

	return CurrentRoot->GetAllNodes();
}

bool FDisplayClusterGameManager::SpawnRootComponent(AActor* Actor)
{
	check(Actor);

	FScopeLock lock(&InternalsSyncScope);

	// Create new root component
	UDisplayClusterRootComponent* NewRoot = NewObject<UDisplayClusterRootComponent>(Actor, FName(TEXT("nDisplay_Root")));
	if (!NewRoot)
	{
		return false;
	}

	// Set up
	NewRoot->AttachToComponent(Actor->GetRootComponent(), FAttachmentTransformRules(EAttachmentRule::KeepRelative, false));
	NewRoot->RegisterComponent();

	return true;
}
