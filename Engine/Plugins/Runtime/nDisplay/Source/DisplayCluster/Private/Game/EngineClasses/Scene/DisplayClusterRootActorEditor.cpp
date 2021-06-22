// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterRootActor.h"

#include "Components/SceneComponent.h"
#include "Components/DisplayClusterOriginComponent.h"
#include "Components/DisplayClusterCameraComponent.h"
#include "Components/DisplayClusterScreenComponent.h"
#include "Components/DisplayClusterSceneComponentSyncParent.h"
#include "Components/DisplayClusterPreviewComponent.h"
#include "Components/DisplayClusterICVFXCameraComponent.h"

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

	UpdateInnerFrustumPriority();
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
void ADisplayClusterRootActor::GetPreviewRenderTargetableTextures(const TArray<FString>& InViewportNames, TArray<FTextureRHIRef>& OutTextures)
{
	check(IsInGameThread());

	if(IsPreviewEnabled())
	{
		const bool bPerformDeferredUpdate = PreviewRenderTargetUpdatesRequired > 0;
		if (bPerformDeferredUpdate)
		{
			// Optimization to limit number of deferred updates. This process is expensive
			// and only needs to happen for a few frames after the BP was modified.
			--PreviewRenderTargetUpdatesRequired;
		}

		for(const TPair<FString, UDisplayClusterPreviewComponent*>& PreviewComponentIt : PreviewComponents)
		{
			if (PreviewComponentIt.Value)
			{
				const int32 OutTextureIndex = InViewportNames.Find(PreviewComponentIt.Value->GetViewportId());
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

							if(bPerformDeferredUpdate)
							{
								// handle configurator logic: raise deffered update for preview component RTT
								PreviewComponentIt.Value->HandleRenderTargetTextureDeferredUpdate();
							}
						}
					}
				}
			}
		}
	}
}

void ADisplayClusterRootActor::UpdateInnerFrustumPriority()
{
	if (InnerFrustumPriority.Num() == 0)
	{
		ResetInnerFrustumPriority();
		return;
	}
	
	TArray<UDisplayClusterICVFXCameraComponent*> Components;
	GetComponents<UDisplayClusterICVFXCameraComponent>(Components);

	TArray<FString> ValidCameras;
	for (UDisplayClusterICVFXCameraComponent* Camera : Components)
	{
		FString CameraName = Camera->GetName();
		InnerFrustumPriority.AddUnique(CameraName);
		ValidCameras.Add(CameraName);
	}

	// Removes invalid cameras or duplicate cameras.
	InnerFrustumPriority.RemoveAll([ValidCameras, this](const FDisplayClusterComponentRef& CameraRef)
	{
		return !ValidCameras.Contains(CameraRef.Name) || InnerFrustumPriority.FilterByPredicate([CameraRef](const FDisplayClusterComponentRef& CameraRefN2)
		{
			return CameraRef == CameraRefN2;
		}).Num() > 1;
	});
	
	for (int32 Idx = 0; Idx < InnerFrustumPriority.Num(); ++Idx)
	{
		if (UDisplayClusterICVFXCameraComponent* CameraComponent = FindObjectFast<UDisplayClusterICVFXCameraComponent>(this, *InnerFrustumPriority[Idx].Name))
		{
			CameraComponent->CameraSettings.RenderSettings.RenderOrder = Idx;
		}
	}
}

void ADisplayClusterRootActor::ResetInnerFrustumPriority()
{
	TArray<UDisplayClusterICVFXCameraComponent*> Components;
	GetComponents<UDisplayClusterICVFXCameraComponent>(Components);

	InnerFrustumPriority.Reset(Components.Num());
	for (UDisplayClusterICVFXCameraComponent* Camera : Components)
	{
		InnerFrustumPriority.Add(Camera->GetName());
	}
	
	// Initialize based on current render priority.
	InnerFrustumPriority.Sort([this](const FDisplayClusterComponentRef& CameraA, const FDisplayClusterComponentRef& CameraB)
	{
		UDisplayClusterICVFXCameraComponent* CameraComponentA = FindObjectFast<UDisplayClusterICVFXCameraComponent>(this, *CameraA.Name);
		UDisplayClusterICVFXCameraComponent* CameraComponentB = FindObjectFast<UDisplayClusterICVFXCameraComponent>(this, *CameraB.Name);

		if (CameraComponentA && CameraComponentB)
		{
			return CameraComponentA->CameraSettings.RenderSettings.RenderOrder < CameraComponentB->CameraSettings.RenderSettings.RenderOrder;
		}

		return false;
	});
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

bool ADisplayClusterRootActor::UpdatePreviewConfiguration_Editor(bool bUpdateAllViewports)
{
	if (ViewportManager.IsValid())
	{
		//@todo: make correct GUI implementation for preview settings UObject (special widged in configurator bp, etc)
		// now just copy RootActor properties to UObject::PreviewSettings
		FDisplayClusterConfigurationViewportPreview PreviewSettings;
		if(bPreviewEnable)
		{
			PreviewSettings.bEnable = bPreviewEnable;
			PreviewSettings.PreviewNodeId = bUpdateAllViewports ? DisplayClusterConfigurationStrings::gui::preview::PreviewNodeAll : PreviewNodeId;
			PreviewSettings.TickPerFrame = TickPerFrame;
			PreviewSettings.PreviewRenderTargetRatioMult = PreviewRenderTargetRatioMult;

			return ViewportManager->UpdatePreviewConfiguration(PreviewSettings, this);
		}
	}

	return false;
}

void ADisplayClusterRootActor::RenderPreview_Editor()
{
	if (UpdatePreviewConfiguration_Editor(false))
	{
		// Update all preview components resources before render
		for (const TTuple<FString, UDisplayClusterPreviewComponent*>& PreviewKeyVal : PreviewComponents)
		{
			if (UDisplayClusterPreviewComponent* PreviewComp = PreviewKeyVal.Value)
			{
				PreviewComp->UpdatePreviewResources();
			}
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
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ADisplayClusterRootActor, InnerFrustumPriority))
	{
		ResetInnerFrustumPriority();
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

void ADisplayClusterRootActor::UpdatePreviewComponents()
{
	if (IsTemplate() || bDeferPreviewGeneration)
	{
		return;
	}

	UpdatePreviewConfiguration_Editor(true);

	TArray<UDisplayClusterPreviewComponent*> IteratedPreviewComponents;
	
	if (CurrentConfigData != nullptr && bPreviewEnable)
	{
		const bool bAllComponentsVisible = PreviewNodeId.Equals(DisplayClusterConfigurationStrings::gui::preview::PreviewNodeAll, ESearchCase::IgnoreCase);

		for (const TPair<FString, UDisplayClusterConfigurationClusterNode*>& Node : CurrentConfigData->Cluster->Nodes)
		{
			if (Node.Value == nullptr)
			{
				continue;
			}

			if (bAllComponentsVisible || Node.Key.Equals(PreviewNodeId, ESearchCase::IgnoreCase))
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
	}

	// Cleanup unused components.
	TArray<UDisplayClusterPreviewComponent*> AllPreviewComponents;
	GetComponents<UDisplayClusterPreviewComponent>(AllPreviewComponents);
	
	for (UDisplayClusterPreviewComponent* ExistingComp : AllPreviewComponents)
	{
		if (!IteratedPreviewComponents.Contains(ExistingComp))
		{
			PreviewComponents.Remove(ExistingComp->GetName());
			
			ExistingComp->UnregisterComponent();
			ExistingComp->DestroyComponent();
		}
	}

	// Update render target for output mapping.
	// Compile - 3 frames
	// Load    - 2 frames
	// Modify  - 1 frame
	PreviewRenderTargetUpdatesRequired = 3;
}

void ADisplayClusterRootActor::ReleasePreviewComponents()
{
	for (const TPair<FString, UDisplayClusterPreviewComponent*>& CompPair : PreviewComponents)
	{
		if (CompPair.Value)
		{
			CompPair.Value->UnregisterComponent();
			CompPair.Value->DestroyComponent();
		}
	}

	PreviewComponents.Reset();
	OnPreviewDestroyed.ExecuteIfBound();
}

FString ADisplayClusterRootActor::GeneratePreviewComponentName(const FString& NodeId, const FString& ViewportId) const
{
	return FString::Printf(TEXT("%s_%s"), *NodeId, *ViewportId);
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
