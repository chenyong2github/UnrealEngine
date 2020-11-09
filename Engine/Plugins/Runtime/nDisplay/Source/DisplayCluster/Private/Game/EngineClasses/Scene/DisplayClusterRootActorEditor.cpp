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
	FName PropertyName = (PropertyChangedEvent.Property != NULL) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

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

	Super::PostEditChangeProperty(PropertyChangedEvent);
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

FString ADisplayClusterRootActor::GeneratePreviewComponentName(const FString& NodeId, const FString& ViewportId) const
{
	return FString::Printf(TEXT("%s - %s"), *NodeId, *ViewportId);
}

void ADisplayClusterRootActor::CleanupPreview()
{
	FScopeLock Lock(&InternalsSyncScope);

	for (const TPair<FString, UDisplayClusterPreviewComponent*>& CompPair : PreviewComponents)
	{
		if (CompPair.Value)
		{
			CompPair.Value->UnregisterComponent();
			CompPair.Value->DestroyComponent();
		}
	}

	PreviewComponents.Reset();
}

void ADisplayClusterRootActor::RebuildPreview()
{
	CleanupPreview();

	if (!PreviewNodeId.Equals(ADisplayClusterRootActor::PreviewNodeNone, ESearchCase::IgnoreCase))
	{
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
						if (!PreviewComponents.Contains(PreviewCompId))
						{
							UDisplayClusterPreviewComponent* PreviewComp = NewObject<UDisplayClusterPreviewComponent>(this, FName(*PreviewCompId));
							check(PreviewComp);

							AddInstanceComponent(PreviewComp);

							PreviewComp->SetFlags(EObjectFlags::RF_DuplicateTransient | RF_Transient | RF_TextExportTransient);
							PreviewComp->SetConfigData(this, Viewport.Value);
							PreviewComp->RegisterComponent();
							PreviewComp->BuildPreview();

							PreviewComponents.Emplace(PreviewCompId, PreviewComp);
						}
					}
				}
			}
		}
	}

	if (GIsEditor)
	{
		// Force SActorDetails redraw
		FLevelEditorModule& LevelEditor = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		LevelEditor.BroadcastComponentsEdited();
	}
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
