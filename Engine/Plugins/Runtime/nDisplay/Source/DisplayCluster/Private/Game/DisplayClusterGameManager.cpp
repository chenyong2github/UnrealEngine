// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/DisplayClusterGameManager.h"

#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/DisplayClusterHelpers.h"
#include "Config/DisplayClusterConfigTypes.h"
#include "Config/IPDisplayClusterConfigManager.h"

#include "DisplayClusterRootActor.h"
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
#include "Engine/LevelStreaming.h"


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

	// Find nDisplay root actor
	DisplayClusterRootActor = FindDisplayClusterRootActor(InWorld);
	if (!DisplayClusterRootActor)
	{
		// Also search inside streamed levels
		const TArray<ULevelStreaming*>& StreamingLevels = InWorld->GetStreamingLevels();
		for (const ULevelStreaming* StreamingLevel : StreamingLevels)
		{
			switch (StreamingLevel->GetCurrentState())
			{
			case ULevelStreaming::ECurrentState::LoadedVisible:
			{
				// Look for the actor in those sub-levels that have been loaded already
				const TSoftObjectPtr<UWorld>& SubWorldAsset = StreamingLevel->GetWorldAsset();
				DisplayClusterRootActor = FindDisplayClusterRootActor(SubWorldAsset.Get());
				if (DisplayClusterRootActor)
				{
					break;
				}
			}

			default:
				break;
			}
		}
	}

	// In cluster mode we spawn root actor if no actors found
	if (GDisplayCluster->GetOperationMode() == EDisplayClusterOperationMode::Cluster)
	{
		if (!DisplayClusterRootActor)
		{
			DisplayClusterRootActor = Cast<ADisplayClusterRootActor>(CurrentWorld->SpawnActor(ADisplayClusterRootActor::StaticClass()));
		}
	}

	return true;
}

void FDisplayClusterGameManager::EndScene()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterGame);
	FScopeLock lock(&InternalsSyncScope);

	DisplayClusterRootActor = nullptr;
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterGameManager
//////////////////////////////////////////////////////////////////////////////////////////////
ADisplayClusterRootActor* FDisplayClusterGameManager::GetRootActor() const
{
	FScopeLock lock(&InternalsSyncScope);
	return DisplayClusterRootActor;
}

UDisplayClusterRootComponent* FDisplayClusterGameManager::GetRootComponent() const
{
	FScopeLock lock(&InternalsSyncScope);
	return DisplayClusterRootActor->GetDisplayClusterRootComponent();
}

TArray<UDisplayClusterScreenComponent*> FDisplayClusterGameManager::GetAllScreens() const
{
	FScopeLock lock(&InternalsSyncScope);

	if (!DisplayClusterRootActor)
	{
		return TArray<UDisplayClusterScreenComponent*>();
	}

	return DisplayClusterRootActor->GetDisplayClusterRootComponent()->GetAllScreens();
}

UDisplayClusterScreenComponent* FDisplayClusterGameManager::GetScreenById(const FString& ScreenId) const
{
	FScopeLock lock(&InternalsSyncScope);

	if (!DisplayClusterRootActor)
	{
		return nullptr;
	}

	return DisplayClusterRootActor->GetDisplayClusterRootComponent()->GetScreenById(ScreenId);
}

int32 FDisplayClusterGameManager::GetScreensAmount() const
{
	FScopeLock lock(&InternalsSyncScope);

	if (!DisplayClusterRootActor)
	{
		return 0;
	}

	return DisplayClusterRootActor->GetDisplayClusterRootComponent()->GetScreensAmount();
}

UDisplayClusterCameraComponent* FDisplayClusterGameManager::GetCameraById(const FString& CameraId) const
{
	FScopeLock lock(&InternalsSyncScope);

	if (!DisplayClusterRootActor)
	{
		return nullptr;
	}

	return DisplayClusterRootActor->GetDisplayClusterRootComponent()->GetCameraById(CameraId);
}

TArray<UDisplayClusterCameraComponent*> FDisplayClusterGameManager::GetAllCameras() const
{
	FScopeLock lock(&InternalsSyncScope);

	if (!DisplayClusterRootActor)
	{
		return TArray<UDisplayClusterCameraComponent*>();
	}

	return DisplayClusterRootActor->GetDisplayClusterRootComponent()->GetAllCameras();
}

int32 FDisplayClusterGameManager::GetCamerasAmount() const
{
	FScopeLock lock(&InternalsSyncScope);

	if (!DisplayClusterRootActor)
	{
		return 0;
	}

	return DisplayClusterRootActor->GetDisplayClusterRootComponent()->GetCamerasAmount();
}

UDisplayClusterCameraComponent* FDisplayClusterGameManager::GetDefaultCamera() const
{
	FScopeLock lock(&InternalsSyncScope);

	if (!DisplayClusterRootActor)
	{
		return nullptr;
	}

	return DisplayClusterRootActor->GetDisplayClusterRootComponent()->GetDefaultCamera();
}

void FDisplayClusterGameManager::SetDefaultCamera(const FString& id)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterGame);

	if(!DisplayClusterRootActor)
	{
		return;
	}

	DisplayClusterRootActor->GetDisplayClusterRootComponent()->SetDefaultCamera(id);
}

UDisplayClusterSceneComponent* FDisplayClusterGameManager::GetNodeById(const FString& NodeId) const
{
	FScopeLock lock(&InternalsSyncScope);

	if (!DisplayClusterRootActor)
	{
		return nullptr;
	}

	return DisplayClusterRootActor->GetDisplayClusterRootComponent()->GetNodeById(NodeId);
}

TArray<UDisplayClusterSceneComponent*> FDisplayClusterGameManager::GetAllNodes() const
{
	FScopeLock lock(&InternalsSyncScope);

	if (!DisplayClusterRootActor)
	{
		return TArray<UDisplayClusterSceneComponent*>();
	}

	return DisplayClusterRootActor->GetDisplayClusterRootComponent()->GetAllNodes();
}

ADisplayClusterRootActor* FDisplayClusterGameManager::FindDisplayClusterRootActor(UWorld* InWorld)
{
	for (AActor* const Actor : InWorld->PersistentLevel->Actors)
	{
		if (Actor && !Actor->IsPendingKill())
		{
			ADisplayClusterRootActor* RootActor = Cast<ADisplayClusterRootActor>(Actor);
			if (RootActor)
			{
				UE_LOG(LogDisplayClusterGame, Log, TEXT("Found root actor - %s"), *RootActor->GetName());
				return RootActor;
			}
		}
	}

	return nullptr;
}
