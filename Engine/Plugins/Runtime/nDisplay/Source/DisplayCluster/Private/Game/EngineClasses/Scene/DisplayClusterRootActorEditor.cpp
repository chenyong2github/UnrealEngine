// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterRootActor.h"

#include "Components/SceneComponent.h"
#include "Components/DisplayClusterOriginComponent.h"
#include "Components/DisplayClusterCameraComponent.h"
#include "Components/DisplayClusterMeshComponent.h"
#include "Components/DisplayClusterScreenComponent.h"
#include "Components/DisplayClusterXformComponent.h"
#include "Components/DisplayClusterSceneComponentSyncParent.h"
#include "Components/DisplayClusterPreviewComponent.h"

#include "Config/IPDisplayClusterConfigManager.h"
#include "DisplayClusterConfigurationStrings.h"

#include "IDisplayClusterConfiguration.h"
#include "DisplayClusterConfigurationTypes.h"

#include "DisplayClusterPlayerInput.h"

#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#include "Misc/DisplayClusterAppExit.h"
#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterHelpers.h"
#include "Misc/DisplayClusterLog.h"
#include "Misc/DisplayClusterStrings.h"

#if WITH_EDITOR
#include "Async/Async.h"
#include "LevelEditor.h"
#endif



//////////////////////////////////////////////////////////////////////////////////////////////
// IN-EDITOR STUFF
//////////////////////////////////////////////////////////////////////////////////////////////
#if WITH_EDITOR

const FString ADisplayClusterRootActor::PreviewNodeAll  = TEXT("All");
const FString ADisplayClusterRootActor::PreviewNodeNone = TEXT("None");


void ADisplayClusterRootActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	bool bReinitializeActor = false;
	
	// Config file has been changed, we should rebuild the nDisplay hierarchy
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FFilePath, FilePath))
	{
		InitializeFromConfig(PreviewConfigPath.FilePath);
	}
	// Cluster node ID has been changed
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ADisplayClusterRootActor, PreviewNodeId))
	{
		AsyncTask(ENamedThreads::GameThread, [this]()
		{
			RebuildPreview();
		});
	}
	else
	{
		bReinitializeActor = true;
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (bReinitializeActor)
	{
		InitializeRootActor();
	}
}

void ADisplayClusterRootActor::PostEditMove(bool bFinished)
{
	// Don't update the preview with the config data if we're just moving the actor.
	bDontUpdatePreviewData = true;
	Super::PostEditMove(bFinished);
	bDontUpdatePreviewData = false;
}

TSharedPtr<TMap<UObject*, FString>> ADisplayClusterRootActor::GenerateObjectsNamingMap() const
{
	TSharedPtr<TMap<UObject*, FString>> ObjNameMap = MakeShared<TMap<UObject*, FString>>();

	for (const TPair<FString, FDisplayClusterSceneComponentRef*>& Component : AllComponents)
	{
		UDisplayClusterSceneComponent* DisplayClusterSceneComponent = Cast<UDisplayClusterSceneComponent>(Component.Value->GetOrFindSceneComponent());
		if (DisplayClusterSceneComponent)
		{
			ObjNameMap->Emplace(DisplayClusterSceneComponent->GetObject(), Component.Key);
		}
	}

	return ObjNameMap;
}

void ADisplayClusterRootActor::SelectComponent(const FString& SelectedComponent)
{
	for (const TPair<FString, FDisplayClusterSceneComponentRef*>& Component : AllComponents)
	{
		UDisplayClusterSceneComponent* DisplayClusterSceneComponent = Cast<UDisplayClusterSceneComponent>(Component.Value->GetOrFindSceneComponent());
		if (DisplayClusterSceneComponent)
		{
			if (Component.Key.Equals(SelectedComponent, ESearchCase::IgnoreCase))
			{
				DisplayClusterSceneComponent->SetNodeSelection(true);
			}
			else
			{
				DisplayClusterSceneComponent->SetNodeSelection(false);
			}
		}
	}
}

void ADisplayClusterRootActor::GeneratePreview()
{
	if (IsTemplate() || bDeferPreviewGeneration)
	{
		return;
	}

	TArray<UDisplayClusterPreviewComponent*> IteratedPreviewComponents;
	
	if (CurrentConfigData)
	{
		const bool bAllComponentsVisible = PreviewNodeId.Equals(ADisplayClusterRootActor::PreviewNodeAll, ESearchCase::IgnoreCase);

		for (const TPair<FString, UDisplayClusterConfigurationClusterNode*>& Node : CurrentConfigData->Cluster->Nodes)
		{
			for (const TPair<FString, UDisplayClusterConfigurationViewport*>& Viewport : Node.Value->Viewports)
			{
				if (bAllComponentsVisible || Node.Key.Equals(PreviewNodeId, ESearchCase::IgnoreCase))
				{
					const FString PreviewCompId = GeneratePreviewComponentName(Node.Key, Viewport.Key);
					UDisplayClusterPreviewComponent* PreviewComp = PreviewComponents.FindRef(PreviewCompId);
					if (!PreviewComp)
					{
						PreviewComp = NewObject<UDisplayClusterPreviewComponent>(this, FName(*PreviewCompId), RF_DuplicateTransient | RF_Transactional | RF_NonPIEDuplicateTransient);
						check(PreviewComp);

						AddInstanceComponent(PreviewComp);
						PreviewComponents.Emplace(PreviewCompId, PreviewComp);
					}

					if (!bDontUpdatePreviewData)
					{
						PreviewComp->SetConfigData(this, Viewport.Value);
					}
					if (GetWorld() && !PreviewComp->IsRegistered())
					{
						PreviewComp->RegisterComponent();
					}

					PreviewComp->BuildPreview();

					IteratedPreviewComponents.Add(PreviewComp);
				}
			}
		}
	}

	// Cleanup unused components.
	TArray<UDisplayClusterPreviewComponent*> AllPreviewComponents;
	GetComponents<UDisplayClusterPreviewComponent>(AllPreviewComponents);
	
	for (UDisplayClusterPreviewComponent* ExistingComp : AllPreviewComponents)
	{
		if (!IteratedPreviewComponents.Contains(ExistingComp))
		{
			PreviewComponents.Remove(ExistingComp->GetName());
			
			RemoveInstanceComponent(ExistingComp);
			ExistingComp->UnregisterComponent();
			ExistingComp->DestroyComponent();
		}
	}

	OnPreviewGenerated.ExecuteIfBound();
}

void ADisplayClusterRootActor::RebuildPreview()
{
	CleanupPreview();

	if (!PreviewNodeId.Equals(ADisplayClusterRootActor::PreviewNodeNone, ESearchCase::IgnoreCase))
	{
		GeneratePreview();
	}
}

void ADisplayClusterRootActor::CleanupPreview()
{
	FScopeLock Lock(&InternalsSyncScope);

	for (const TPair<FString, UDisplayClusterPreviewComponent*>& CompPair : PreviewComponents)
	{
		if (CompPair.Value)
		{
			RemoveInstanceComponent(CompPair.Value);
			CompPair.Value->UnregisterComponent();
			CompPair.Value->DestroyComponent();
		}
	}

	PreviewComponents.Reset();
	OnPreviewDestroyed.ExecuteIfBound();
}

FString ADisplayClusterRootActor::GeneratePreviewComponentName(const FString& NodeId, const FString& ViewportId) const
{
	return FString::Printf(TEXT("%s - %s"), *NodeId, *ViewportId);
}

UDisplayClusterPreviewComponent* ADisplayClusterRootActor::GetPreviewComponent(const FString& NodeId, const FString& ViewportId)
{
	const FString PreviewCompId = GeneratePreviewComponentName(NodeId, ViewportId);
	if (PreviewComponents.Contains(PreviewCompId))
	{
		return PreviewComponents[PreviewCompId];
	}

	return nullptr;
}
#endif
