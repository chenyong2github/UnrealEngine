// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/DisplayClusterGameManager.h"

#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"

#include "Config/IPDisplayClusterConfigManager.h"

#include "DisplayClusterConfigurationTypes.h"

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

bool FDisplayClusterGameManager::StartSession(const UDisplayClusterConfigurationData* InConfigData, const FString& InNodeId)
{
	ClusterNodeId = InNodeId;
	return true;
}

void FDisplayClusterGameManager::EndSession()
{
	ClusterNodeId.Reset();
}

bool FDisplayClusterGameManager::StartScene(UWorld* InWorld)
{
	check(InWorld);
	CurrentWorld = InWorld;

	// Find nDisplay root actor
	ADisplayClusterRootActor* RootActor = FindDisplayClusterRootActor(InWorld);
	if (!RootActor)
	{
		// Also search inside streamed levels
		const TArray<ULevelStreaming*>& StreamingLevels = InWorld->GetStreamingLevels();
		for (const ULevelStreaming* const StreamingLevel : StreamingLevels)
		{
			if (StreamingLevel && StreamingLevel->GetCurrentState() == ULevelStreaming::ECurrentState::LoadedVisible)
			{
				// Look for the actor in those sub-levels that have been loaded already
				const TSoftObjectPtr<UWorld>& SubWorldAsset = StreamingLevel->GetWorldAsset();
				RootActor = FindDisplayClusterRootActor(SubWorldAsset.Get());
			}

			if (RootActor)
			{
				// Ok, we found it in a sublevel
				break;
			}
		}
	}

	// In cluster mode we spawn root actor if no actors found
	if (GDisplayCluster->GetOperationMode() == EDisplayClusterOperationMode::Cluster)
	{
		if (!RootActor)
		{
			RootActor = Cast<ADisplayClusterRootActor>(CurrentWorld->SpawnActor(ADisplayClusterRootActor::StaticClass()));
		}
	}

	DisplayClusterRootActorRef.SetSceneActor(RootActor);

	return true;
}

void FDisplayClusterGameManager::EndScene()
{
	FScopeLock Lock(&InternalsSyncScope);
	DisplayClusterRootActorRef.ResetSceneActor();
	CurrentWorld = nullptr;
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterGameManager
//////////////////////////////////////////////////////////////////////////////////////////////
ADisplayClusterRootActor* FDisplayClusterGameManager::GetRootActor() const
{
	FScopeLock Lock(&InternalsSyncScope);
	return Cast<ADisplayClusterRootActor>(DisplayClusterRootActorRef.GetOrFindSceneActor());
}

//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterGameManager
//////////////////////////////////////////////////////////////////////////////////////////////
ADisplayClusterRootActor* FDisplayClusterGameManager::FindDisplayClusterRootActor(UWorld* InWorld)
{
	if (InWorld && InWorld->PersistentLevel)
	{
		UClass* DisplayClusterRootActorClass = StaticLoadClass(UObject::StaticClass(), nullptr, TEXT("/Script/DisplayCluster.DisplayClusterRootActor"), NULL, LOAD_None, NULL);
		if (DisplayClusterRootActorClass)
		{
			for (TActorIterator<AActor> It(InWorld, DisplayClusterRootActorClass, EActorIteratorFlags::SkipPendingKill); It; ++It)
			{
				ADisplayClusterRootActor* RootActor = Cast<ADisplayClusterRootActor>(*It);
				if (RootActor != nullptr && !RootActor->IsTemplate())
				{
					UE_LOG(LogDisplayClusterGame, Log, TEXT("Found root actor - %s"), *RootActor->GetName());
					return RootActor;
				}
			}
		}
	}

	return nullptr;
}
