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
	PreviewSettings = CreateDefaultSubobject<UDisplayClusterConfigurationViewportPreview>(TEXT("PreviewSettings"));
}

void ADisplayClusterRootActor::Destructor_Editor()
{
	OnPreviewGenerated.Unbind();
	OnPreviewDestroyed.Unbind();
}

void ADisplayClusterRootActor::Tick_Editor(float DeltaSeconds)
{
	if (!IsRunningGameOrPIE() && IsPreviewEnabled())
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
void ADisplayClusterRootActor::Destroyed_Editor()
{
	ReleasePreviewComponents();
	OnPreviewGenerated.Unbind();
	OnPreviewDestroyed.Unbind();
	bDeferPreviewGeneration = true;
}

void ADisplayClusterRootActor::BeginDestroy_Editor()
{
	ReleasePreviewComponents();
	OnPreviewGenerated.Unbind();
	OnPreviewDestroyed.Unbind();
	bDeferPreviewGeneration = true;
}

void ADisplayClusterRootActor::RerunConstructionScripts_Editor()
{
	/* We need to reinitialize since our components are being regenerated here. */
	InitializeRootActor();
}

bool ADisplayClusterRootActor::IsPreviewEnabled() const
{
	//@todo: (GUI) Scene preview can be disabled when the configuration window with internal preview is open.
#if 0
	bool bIsScenePreview = true; //@todo: handle GUI logic
	bool bIsConfigurationPreviewUsed = false; //@todo: handle GUI logic

	if (bIsScenePreview == bIsConfigurationPreviewUsed)
	{
		return false;
	}
#endif

	return true;
}

// Return all RTT RHI resources for preview
void ADisplayClusterRootActor::GetPreviewRenderTargetableTextures(const TArray<FString>& InViewportNames, TArray<FTextureRHIRef>& OutTextures) const
{
	check(IsInGameThread());

	if(IsPreviewEnabled())
	{
		for(const TPair<FString, UDisplayClusterPreviewComponent*>& PreviewComponentIt : PreviewComponents)
		{
			if (PreviewComponentIt.Value)
			{
				int OutTextureIndex = InViewportNames.Find(PreviewComponentIt.Value->GetViewportId());
				if (OutTextureIndex != INDEX_NONE)
				{
					// Add scope for func GetRenderTargetTexture()
					UTextureRenderTarget2D* RenderTarget2D = PreviewComponentIt.Value->GetRenderTargetTexture();
					if (RenderTarget2D)
					{
						FTextureRenderTargetResource* DstRenderTarget = RenderTarget2D->GameThread_GetRenderTargetResource();
						if (DstRenderTarget)
						{
							OutTextures[OutTextureIndex] = DstRenderTarget->TextureRHI;

							// handle configurator logic: raise deffered update for preview component RTT
							PreviewComponentIt.Value->HandleRenderTargetTextureDefferedUpdate();
						}
					}
				}
			}
		}
	}
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

bool ADisplayClusterRootActor::IsSelectedInEditor() const
{
	return bIsSelectedInEditor || Super::IsSelectedInEditor();
}

void ADisplayClusterRootActor::SetIsSelectedInEditor(bool bValue)
{
	bIsSelectedInEditor = bValue;
}

IDisplayClusterViewport* ADisplayClusterRootActor::FindPreviewViewport(const FString& InViewportId) const
{
	if (ViewportManager.IsValid())
	{
		return ViewportManager->FindViewport(InViewportId);
	}

	return nullptr;
}

bool ADisplayClusterRootActor::UpdatePreviewConfiguration_Editor()
{
	if (ViewportManager.IsValid())
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
			return ViewportManager->UpdatePreviewConfiguration(PreviewSettings, this);
		}
	}

	return false;
}

void ADisplayClusterRootActor::RenderPreview_Editor()
{
	if (UpdatePreviewConfiguration_Editor())
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
			if (ViewportManager->BeginNewFrame(nullptr, PreviewWorld, PreviewRenderFrame) && PreviewRenderFrame.DesiredNumberOfViews > 0)
			{
				ViewportManager->RenderInEditor(PreviewRenderFrame, nullptr);
				// Send event about RTT changed
				OnPreviewGenerated.ExecuteIfBound();
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
			if (Node.Value == nullptr)
			{
				continue;
			}
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
