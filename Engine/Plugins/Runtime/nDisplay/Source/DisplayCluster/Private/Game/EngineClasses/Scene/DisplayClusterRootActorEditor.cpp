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

#include "Render/Viewport/IDisplayClusterViewportManager.h"
#include "Render/Viewport/IDisplayClusterViewport.h"
#include "Render/Viewport/IDisplayClusterViewportProxy.h"
#include "Render/Viewport/RenderFrame/DisplayClusterRenderFrame.h"

#include "Engine/TextureRenderTarget2D.h"

#include "Render/Viewport/IDisplayClusterViewport.h"
#include "Render/Viewport/DisplayClusterViewportManager.h"

//////////////////////////////////////////////////////////////////////////////////////////////
// IN-EDITOR STUFF
//////////////////////////////////////////////////////////////////////////////////////////////

#if WITH_EDITOR

#include "Async/Async.h"
#include "LevelEditor.h"

void ADisplayClusterRootActor::Constructor_Editor()
{
	// Allow tick in editor for preview rendering
	PrimaryActorTick.bStartWithTickEnabled = true;
	PreviewViewportManager = MakeUnique<FDisplayClusterViewportManager>();
	PreviewSettings = CreateDefaultSubobject<UDisplayClusterConfigurationViewportPreview>(TEXT("PreviewSettings"));
}

void ADisplayClusterRootActor::Destructor_Editor()
{
	OnPreviewGenerated.Unbind();
	OnPreviewDestroyed.Unbind();
}

void ADisplayClusterRootActor::Tick_Editor(float DeltaSeconds)
{
	if (!IsRunningGameOrPIE())
	{
		if (bDeferPreviewGeneration)
		{
			// Hack to generate preview components on instances during map load.
			// TODO: See if we can move InitializeRootActor out of PostLoad.
			bDeferPreviewGeneration = false;
			UpdatePreviewComponents();
		}

		// Update preview RTTs correspond to 'TickPerFrame' value		
		if (++TickPerFrameCounter >= TickPerFrame)
		{
			TickPerFrameCounter = 0;
			// Render preview RTTs
			RenderPreview_Editor();
		}
	}
}

void ADisplayClusterRootActor::PostLoad_Editor()
{
	// Generating the preview on load for instances in the world can't be done on PostLoad, components may not have loaded flags present.
	bDeferPreviewGeneration = true;
}

void ADisplayClusterRootActor::BeginDestroy_Editor()
{
	ReleasePreviewComponents();
	OnPreviewGenerated.Unbind();
	OnPreviewDestroyed.Unbind();
}

void ADisplayClusterRootActor::RerunConstructionScripts_Editor()
{
	/* We need to reinitialize since our components are being regenerated here. */
	InitializeRootActor();
}

FRHITexture2D* ADisplayClusterRootActor::GetPreviewRenderTargetableTexture_RenderThread(const FString& ViewportId) const
{
	check(IsInRenderingThread());

	for(const TPair<FString, UDisplayClusterPreviewComponent*>& PreviewComponentIt : PreviewComponents)
	{
		if (PreviewComponentIt.Value && PreviewComponentIt.Value->GetViewportId() == ViewportId)
		{
			// Add scope for func GetRenderTargetTexture()
			UTextureRenderTarget2D* RenderTarget2D = PreviewComponentIt.Value->GetRenderTargetTexture();
			if (RenderTarget2D)
			{
				FTextureRenderTargetResource* DstRenderTarget = RenderTarget2D->GetRenderTargetResource();
				if (DstRenderTarget)
				{
					PreviewComponentIt.Value->HandleRenderTargetTextureUpdate_RenderThread();
					return DstRenderTarget->GetTexture2DRHI();
				}
			}

			break;
		}
	}

	return nullptr;
}

float ADisplayClusterRootActor::GetXformGizmoScale() const
{
	return XformGizmoScale * EditorViewportXformGizmoScale;
}

bool ADisplayClusterRootActor::GetXformGizmoVisibility() const
{
	return bAreXformGizmosVisible && bEditorViewportXformGizmoVisibility;
}

void ADisplayClusterRootActor::UpdateXformGizmos()
{
	TMap<FString, UDisplayClusterXformComponent*> Xforms;
	GetAllXforms(Xforms);

	float Scale = GetXformGizmoScale();
	bool bIsVisible = GetXformGizmoVisibility();

	for (TPair<FString, UDisplayClusterXformComponent*> XformPair : Xforms)
	{
		XformPair.Value->SetVisXformScale(Scale);
		XformPair.Value->SetVisXformVisibility(bIsVisible);
	}
}

IDisplayClusterViewport* ADisplayClusterRootActor::FindPreviewViewport(const FString& InViewportId) const
{
	if (PreviewViewportManager.IsValid())
	{
		return PreviewViewportManager->FindViewport(InViewportId);
	}

	return nullptr;
}

void ADisplayClusterRootActor::RenderPreview_Editor()
{
	if (PreviewViewportManager.IsValid())
	{
		check(PreviewSettings);

		//@todo: make correct GUI implementation for preview settings UObject (special widged in configurator bp, etc)
		// now just copy RootActor properties to UObject::PreviewSettings
		{
			PreviewSettings->bEnable = bPreviewEnable;
			PreviewSettings->PreviewNodeId = PreviewNodeId;
			PreviewSettings->TickPerFrame = TickPerFrame;
			PreviewSettings->PreviewRenderTargetRatioMult = PreviewRenderTargetRatioMult;
		}

		if (PreviewSettings->bEnable)
		{
			// Update all preview components resources before render
			TArray<UDisplayClusterPreviewComponent*> AllPreviewComponents;
			GetComponents<UDisplayClusterPreviewComponent>(AllPreviewComponents);
			for (UDisplayClusterPreviewComponent* PreviewComp : AllPreviewComponents)
			{
				PreviewComp->UpdatePreviewResources();
			}

			//@todo: add GUI to change preview scene
			// Now always use RootActor world to preview. 
			UWorld* PreviewWorld = GetWorld();
			if (PreviewWorld)
			{
				// Update preview viewports from settings
				FDisplayClusterRenderFrame PreviewRenderFrame;
				if(PreviewViewportManager->UpdatePreviewConfiguration(PreviewSettings, PreviewWorld, this)
				&& PreviewViewportManager->BeginNewFrame(nullptr, PreviewRenderFrame)
				&& PreviewRenderFrame.DesiredNumberOfViews > 0)
				{
					PreviewViewportManager->RenderPreview(PreviewRenderFrame);
					// Send event about RTT changed
					OnPreviewGenerated.ExecuteIfBound();
				}
			}
		}
	}
}

void ADisplayClusterRootActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	bool bReinitializeActor = true;

	Super::PostEditChangeProperty(PropertyChangedEvent);
	
	// Config file has been changed, we should rebuild the nDisplay hierarchy
	// Cluster node ID has been changed
	if (PropertyName == GET_MEMBER_NAME_CHECKED(ADisplayClusterRootActor, PreviewNodeId))
	{
		bReinitializeActor = false;
		AsyncTask(ENamedThreads::GameThread, [this]()
		{
			UpdatePreviewComponents();
		});
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ADisplayClusterRootActor, XformGizmoScale) || 
			 PropertyName == GET_MEMBER_NAME_CHECKED(ADisplayClusterRootActor, bAreXformGizmosVisible))
	{
		UpdateXformGizmos();
	}

	if (bReinitializeActor)
	{
		InitializeRootActor();
	}
}

void ADisplayClusterRootActor::PostEditMove(bool bFinished)
{
	// Don't update the preview with the config data if we're just moving the actor.
	Super::PostEditMove(bFinished);
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

void ADisplayClusterRootActor::UpdatePreviewComponents()
{
	if (IsTemplate() || bDeferPreviewGeneration)
	{
		return;
	}

	TArray<UDisplayClusterPreviewComponent*> IteratedPreviewComponents;
	
	if (CurrentConfigData != nullptr)
	{
		const bool bAllComponentsVisible = PreviewSettings->PreviewNodeId.Equals(DisplayClusterConfigurationStrings::gui::preview::PreviewNodeAll, ESearchCase::IgnoreCase);

		for (const TPair<FString, UDisplayClusterConfigurationClusterNode*>& Node : CurrentConfigData->Cluster->Nodes)
		{
			for (const TPair<FString, UDisplayClusterConfigurationViewport*>& Viewport : Node.Value->Viewports)
			{
				if (bAllComponentsVisible || Node.Key.Equals(PreviewSettings->PreviewNodeId, ESearchCase::IgnoreCase))
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

					if (GetWorld() && !PreviewComp->IsRegistered())
					{
						PreviewComp->RegisterComponent();
					}

					// Always reinitialize so changes impact the preview component.
					PreviewComp->InitializePreviewComponent(this, Viewport.Key, Viewport.Value);

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
}

void ADisplayClusterRootActor::ReleasePreviewComponents()
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
