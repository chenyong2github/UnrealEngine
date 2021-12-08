// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterOperatorModule.h"

#include "SDisplayClusterOperatorPanel.h"
#include "DisplayClusterRootActor.h"

#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "LevelEditorViewport.h"
#include "Toolkits/AssetEditorToolkit.h"

#define LOCTEXT_NAMESPACE "DisplayClusterOperator"

void FDisplayClusterOperatorModule::StartupModule()
{
	OperatorToolBarExtensibilityManager = MakeShared<FExtensibilityManager>();

	RegisterTabSpawners();
}

void FDisplayClusterOperatorModule::ShutdownModule()
{
	UnregisterTabSpawners();
}

FName FDisplayClusterOperatorModule::GetOperatorExtensionId()
{
	return SDisplayClusterOperatorPanel::TabExtensionId;
}

void FDisplayClusterOperatorModule::GetRootActorLevelInstances(TArray<ADisplayClusterRootActor*>& OutRootActorInstances)
{
	UWorld* World = GCurrentLevelEditingViewportClient ? GCurrentLevelEditingViewportClient->GetWorld() : nullptr;
	if (World)
	{
		for (TActorIterator<ADisplayClusterRootActor> Iter(World); Iter; ++Iter)
		{
			ADisplayClusterRootActor* RootActor = *Iter;
			if (IsValid(RootActor))
			{
				OutRootActorInstances.Add(RootActor);
			}
		}
	}
}

void FDisplayClusterOperatorModule::ShowDetailsForObject(UObject* Object)
{
	TArray<UObject*> Objects;
	Objects.Add(Object);
	ShowDetailsForObjects(Objects);
}

void FDisplayClusterOperatorModule::ShowDetailsForObjects(const TArray<UObject*>& Objects)
{
	DetailObjectsChanged.Broadcast(Objects);
}

void FDisplayClusterOperatorModule::RegisterTabSpawners()
{
	SDisplayClusterOperatorPanel::RegisterTabSpawner();
}

void FDisplayClusterOperatorModule::UnregisterTabSpawners()
{
	SDisplayClusterOperatorPanel::UnregisterTabSpawner();
}

IMPLEMENT_MODULE(FDisplayClusterOperatorModule, DisplayClusterOperator);

#undef LOCTEXT_NAMESPACE
