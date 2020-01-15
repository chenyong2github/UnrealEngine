// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterEditorEngine.h"
#include "DisplayClusterEditorLog.h"

#include "DisplayClusterRootActor.h"

#include "DisplayCluster/Private/IPDisplayCluster.h"

#include "Engine/LevelStreaming.h"

#include "Editor.h"


void UDisplayClusterEditorEngine::Init(IEngineLoop* InEngineLoop)
{
	UE_LOG(LogDisplayClusterEditorEngine, VeryVerbose, TEXT("UDisplayClusterEditorEngine::Init"));

	// Initialize DisplayCluster module for editor mode
	DisplayClusterModule = static_cast<IPDisplayCluster*>(&IDisplayCluster::Get());
	if (!DisplayClusterModule)
	{
		UE_LOG(LogDisplayClusterEditorEngine, Error, TEXT("Couldn't initialize DisplayCluster module"));
		return;
	}

	// Initialize DisplayCluster module for operating in Editor mode
	const bool bResult = DisplayClusterModule->Init(EDisplayClusterOperationMode::Editor);
	if (!bResult)
	{
		UE_LOG(LogDisplayClusterEditorEngine, Error, TEXT("An error occured during DisplayCluster initialization"));
		return;
	}

	UE_LOG(LogDisplayClusterEditorEngine, Log, TEXT("DisplayCluster module has been initialized"));

	// Subscribe to PIE events
	BeginPIEDelegate = FEditorDelegates::BeginPIE.AddUObject(this, &UDisplayClusterEditorEngine::OnBeginPIE);
	EndPIEDelegate   = FEditorDelegates::EndPIE.AddUObject(this, &UDisplayClusterEditorEngine::OnEndPIE);

	return Super::Init(InEngineLoop);
}

void UDisplayClusterEditorEngine::PreExit()
{
	UE_LOG(LogDisplayClusterEditorEngine, VeryVerbose, TEXT("UDisplayClusterEditorEngine::PreExit"));

	Super::PreExit();
}

ADisplayClusterRootActor* UDisplayClusterEditorEngine::FindDisplayClusterRootActor(UWorld* InWorld)
{
	for (AActor* const Actor : InWorld->PersistentLevel->Actors)
	{
		if (Actor && !Actor->IsPendingKill())
		{
			ADisplayClusterRootActor* RootActor = Cast<ADisplayClusterRootActor>(Actor);
			if (RootActor)
			{
				UE_LOG(LogDisplayClusterEditorEngine, Log, TEXT("Found root actor - %s"), *RootActor->GetName());
				return RootActor;
			}
		}
	}

	return nullptr;
}

void UDisplayClusterEditorEngine::StartPlayInEditorSession(FRequestPlaySessionParams& InRequestParams)
{
	UE_LOG(LogDisplayClusterEditorEngine, VeryVerbose, TEXT("UDisplayClusterEditorEngine::StartPlayInEditorSession"));

	UWorld* EditorWorldPreDup = GetEditorWorldContext().World();

	if (DisplayClusterModule)
	{
		// Find nDisplay root actor
		ADisplayClusterRootActor* RootActor = FindDisplayClusterRootActor(EditorWorldPreDup);
		if (!RootActor)
		{
			// Also search inside streamed levels
			const TArray<ULevelStreaming*>& StreamingLevels = EditorWorld->GetStreamingLevels();
			for (ULevelStreaming* StreamingLevel : StreamingLevels)
			{
				switch (StreamingLevel->GetCurrentState())
				{
					case ULevelStreaming::ECurrentState::LoadedVisible:
					{
						// Look for the actor in those sub-levels that have been loaded already
						const TSoftObjectPtr<UWorld>& SubWorldAsset = StreamingLevel->GetWorldAsset();
						RootActor = FindDisplayClusterRootActor(SubWorldAsset.Get());
						if (RootActor)
						{
							break;
						}
					}

					default:
						break;
				}
			}
		}

		// If we found a root actor, start DisplayCluster PIE session
		if (RootActor)
		{
			bIsNDisplayPIE = true;

			if (!DisplayClusterModule->StartSession(RootActor->GetEditorConfigPath(), RootActor->GetEditorNodeId()))
			{
				UE_LOG(LogDisplayClusterEditorEngine, Error, TEXT("Couldn't start DisplayCluster session"));

				// Couldn't start a new session
				RequestEndPlayMap();
				return;
			}

			DisplayClusterModule->StartScene(EditorWorldPreDup);
		}
	}

	Super::StartPlayInEditorSession(InRequestParams);
}

void UDisplayClusterEditorEngine::Tick(float DeltaSeconds, bool bIdleMode)
{
	if (DisplayClusterModule && bIsActivePIE && bIsNDisplayPIE)
	{
		DisplayClusterModule->StartFrame(GFrameCounter);
		DisplayClusterModule->PreTick(DeltaSeconds);
		DisplayClusterModule->Tick(DeltaSeconds);
		DisplayClusterModule->PostTick(DeltaSeconds);
		DisplayClusterModule->EndFrame(GFrameCounter);
	}

	Super::Tick(DeltaSeconds, bIdleMode);
}

void UDisplayClusterEditorEngine::OnBeginPIE(const bool bSimulate)
{
	UE_LOG(LogDisplayClusterEditorEngine, VeryVerbose, TEXT("UDisplayClusterEditorEngine::OnBeginPIE"));

	bIsActivePIE = true;
}

void UDisplayClusterEditorEngine::OnEndPIE(const bool bSimulate)
{
	UE_LOG(LogDisplayClusterEditorEngine, VeryVerbose, TEXT("UDisplayClusterEditorEngine::OnEndPIE"));

	bIsActivePIE   = false;
	bIsNDisplayPIE = false;

	DisplayClusterModule->EndScene();
	DisplayClusterModule->EndSession();
}
