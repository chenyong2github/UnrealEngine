// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterEditorEngine.h"
#include "DisplayClusterEditorLog.h"

#include "DisplayClusterRootComponent.h"

#include "DisplayCluster/Private/IPDisplayCluster.h"

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

UDisplayClusterRootComponent* UDisplayClusterEditorEngine::FindDisplayClusterRootComponent(UWorld* InWorld)
{
	for (AActor* const Actor : InWorld->PersistentLevel->Actors)
	{
		if (Actor && !Actor->IsPendingKill())
		{
			UDisplayClusterRootComponent* RootComponent = Actor->FindComponentByClass<UDisplayClusterRootComponent>();
			if (RootComponent && !RootComponent->IsPendingKill())
			{
				UE_LOG(LogDisplayClusterEditorEngine, Log, TEXT("Found root component - %s"), *RootComponent->GetName());
				return RootComponent;
			}
		}
	}

	return nullptr;
}

void UDisplayClusterEditorEngine::PlayInEditor(UWorld* InWorld, bool bInSimulateInEditor, FPlayInEditorOverrides Overrides)
{
	UE_LOG(LogDisplayClusterEditorEngine, VeryVerbose, TEXT("UDisplayClusterEditorEngine::PlayInEditor"));

	if (DisplayClusterModule)
	{
		// Find nDisplay root
		UDisplayClusterRootComponent* RootComponent = FindDisplayClusterRootComponent(InWorld);
		if (!RootComponent)
		{
			//Also search inside streamed levels:
			const TArray<ULevelStreaming*>& StreamingLevels = InWorld->GetStreamingLevels();
			for (ULevelStreaming* SreamingLevel : StreamingLevels)
			{
				switch (SreamingLevel->GetCurrentState())
				{
					case ULevelStreaming::ECurrentState::LoadedVisible:
					{
						// Parse only in loaded sub-levels
						const TSoftObjectPtr<UWorld>& SubWorldAsset = SreamingLevel->GetWorldAsset();
						RootComponent = FindDisplayClusterRootComponent(SubWorldAsset.Get());
						if (RootComponent)
						{
							break;
						}
					}

					default:
						break;
				}
			}
		}

		// If we found a root component, start DisplayCluster PIE session
		if (RootComponent)
		{
			bIsNDisplayPIE = true;

			if (!DisplayClusterModule->StartSession(RootComponent->GetEditorConfigPath(), RootComponent->GetEditorNodeId()))
			{
				UE_LOG(LogDisplayClusterEditorEngine, Error, TEXT("Couldn't start DisplayCluster session"));

				// Couldn't start a new session
				RequestEndPlayMap();
				return;
			}

			DisplayClusterModule->StartScene(InWorld);
		}
	}

	Super::PlayInEditor(InWorld, bInSimulateInEditor, Overrides);
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
