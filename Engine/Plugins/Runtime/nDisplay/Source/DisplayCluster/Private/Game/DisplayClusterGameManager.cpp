// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/DisplayClusterGameManager.h"

#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"

#include "Config/IPDisplayClusterConfigManager.h"

#include "DisplayClusterRootActor.h"
#include "Camera/CameraComponent.h"

#include "Components/SceneComponent.h"
#include "Components/DisplayClusterCameraComponent.h"
#include "Components/DisplayClusterSceneComponent.h"
#include "Components/DisplayClusterScreenComponent.h"

#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterHelpers.h"
#include "Misc/DisplayClusterLog.h"
#include "Misc/DisplayClusterStrings.h"

#include "GameFramework/Actor.h"
#include "Engine/LevelStreaming.h"


FDisplayClusterGameManager::FDisplayClusterGameManager()
{
}

FDisplayClusterGameManager::~FDisplayClusterGameManager()
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IPDisplayClusterManager
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterGameManager::Init(EDisplayClusterOperationMode OperationMode)
{
	CurrentOperationMode = OperationMode;

	return true;
}

void FDisplayClusterGameManager::Release()
{
}

bool FDisplayClusterGameManager::StartSession(const FString& InConfigPath, const FString& InNodeId)
{
	ConfigPath = InConfigPath;
	ClusterNodeId = InNodeId;

	return true;
}

void FDisplayClusterGameManager::EndSession()
{

	ConfigPath.Reset();
	ClusterNodeId.Reset();
}

bool FDisplayClusterGameManager::StartScene(UWorld* InWorld)
{
	check(InWorld);
	CurrentWorld = InWorld;

	// Find nDisplay root actor
	DisplayClusterRootActor = FindDisplayClusterRootActor(InWorld);
	if (!DisplayClusterRootActor)
	{
		// Also search inside streamed levels
		const TArray<ULevelStreaming*>& StreamingLevels = InWorld->GetStreamingLevels();
		for (const ULevelStreaming* const StreamingLevel : StreamingLevels)
		{
			if (StreamingLevel && StreamingLevel->GetCurrentState() == ULevelStreaming::ECurrentState::LoadedVisible)
			{
				// Look for the actor in those sub-levels that have been loaded already
				const TSoftObjectPtr<UWorld>& SubWorldAsset = StreamingLevel->GetWorldAsset();
				DisplayClusterRootActor = FindDisplayClusterRootActor(SubWorldAsset.Get());
			}

			if (DisplayClusterRootActor)
			{
				// Ok, we found it in a sublevel
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
	FScopeLock Lock(&InternalsSyncScope);
	DisplayClusterRootActor = nullptr;
	CurrentWorld = nullptr;
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterGameManager
//////////////////////////////////////////////////////////////////////////////////////////////
ADisplayClusterRootActor* FDisplayClusterGameManager::GetRootActor() const
{
	FScopeLock Lock(&InternalsSyncScope);
	return DisplayClusterRootActor;
}

//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterGameManager
//////////////////////////////////////////////////////////////////////////////////////////////
ADisplayClusterRootActor* FDisplayClusterGameManager::FindDisplayClusterRootActor(UWorld* InWorld)
{
	if (InWorld && InWorld->PersistentLevel)
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
	}

	return nullptr;
}
