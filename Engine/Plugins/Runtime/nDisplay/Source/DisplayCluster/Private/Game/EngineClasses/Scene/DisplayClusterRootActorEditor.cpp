// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterRootActor.h"

#include "Components/SceneComponent.h"
#include "Components/DisplayClusterOriginComponent.h"
#include "Components/DisplayClusterCameraComponent.h"
#include "Components/DisplayClusterScreenComponent.h"
#include "Components/DisplayClusterSceneComponentSyncParent.h"
#include "Components/DisplayClusterPreviewComponent.h"
#include "Components/DisplayClusterICVFXCameraComponent.h"
#include "Components/LineBatchComponent.h"

#include "Config/IPDisplayClusterConfigManager.h"
#include "DisplayClusterConfigurationStrings.h"

#include "IDisplayClusterConfiguration.h"
#include "DisplayClusterConfigurationTypes.h"

#include "DisplayClusterPlayerInput.h"

#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterHelpers.h"
#include "Misc/DisplayClusterLog.h"
#include "Misc/DisplayClusterStrings.h"

#include "Render/Viewport/Configuration/DisplayClusterViewportConfigurationHelpers_ICVFX.h"
#include "Render/Viewport/DisplayClusterViewportStrings.h"
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
#include "EditorSupportDelegates.h"

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
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ADisplayClusterRootActor::RerunConstructionScripts_Editor"), STAT_RerunConstructionScripts_Editor, STATGROUP_NDisplay);
	
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
								// handle configurator logic: raise deferred update for preview component RTT
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

		// Now always use RootActor world to preview. 
		UWorld* PreviewWorld = GetWorld();
		if (PreviewWorld)
		{
			// Update preview viewports from settings
			FDisplayClusterRenderFrame PreviewRenderFrame;
			if (ViewportManager->BeginNewFrame(nullptr, PreviewWorld, PreviewRenderFrame))
			{
				ViewportManager->RenderInEditor(PreviewRenderFrame, nullptr);
				// Send event about RTT changed
				OnPreviewGenerated.ExecuteIfBound();
			}
		}

		// preview frustums
		RenderPreviewFrustums();
	}
}

void ADisplayClusterRootActor::RenderPreviewFrustums()
{
	if (!ViewportManager.IsValid())
	{
		return;
	}

	// frustum preview viewports
	TArray<IDisplayClusterViewport*> FrustumPreviewViewports;

	for (const TPair<FString, UDisplayClusterConfigurationClusterNode*>& Node : CurrentConfigData->Cluster->Nodes)
	{
		if (Node.Value == nullptr)
		{
			continue;
		}

		// collect node viewports
		for (const TPair<FString, UDisplayClusterConfigurationViewport*>& ViewportConfig : Node.Value->Viewports)
		{
			if (ViewportConfig.Value->bAllowPreviewFrustumRendering == false || ViewportConfig.Value->bIsVisible == false || ViewportConfig.Value == nullptr)
			{
				continue;
			}

			IDisplayClusterViewport* Viewport = ViewportManager->FindViewport(ViewportConfig.Key);
			if (Viewport != nullptr)
			{
				FrustumPreviewViewports.Add(Viewport);
			}
		}
	}

	// collect incameras
	if (bPreviewICVFXFrustums)
	{
		for (UActorComponent* ActorComponentIt : GetComponents())
		{
			if (ActorComponentIt)
			{
				UDisplayClusterICVFXCameraComponent* CineCameraComponent = Cast<UDisplayClusterICVFXCameraComponent>(ActorComponentIt);
				if (CineCameraComponent && CineCameraComponent->IsICVFXEnabled())
				{
					const FString InnerFrustumID = CineCameraComponent->GetCameraUniqueId();
					FDisplayClusterViewport* IncameraViewport = FDisplayClusterViewportConfigurationHelpers_ICVFX::ImplFindViewport(*this, InnerFrustumID, DisplayClusterViewportStrings::icvfx::camera);
					if (IncameraViewport != nullptr)
					{
						FrustumPreviewViewports.Add(IncameraViewport);
					}
				}
			}
		}
	}

	for (IDisplayClusterViewport* Viewport : FrustumPreviewViewports)
	{
		const TArray<FDisplayClusterViewport_Context>& Contexts = Viewport->GetContexts();
		for (const FDisplayClusterViewport_Context& Context : Contexts)
		{
			FMatrix ProjectionMatrix = Context.ProjectionMatrix;

			uint32 ViewportContextNum = 0;
			Viewport->GetProjectionMatrix(ViewportContextNum, ProjectionMatrix);

			FMatrix ViewRotationMatrix = FInverseRotationMatrix(Context.ViewRotation) * FMatrix(
				FPlane(0, 0, 1, 0),
				FPlane(1, 0, 0, 0),
				FPlane(0, 1, 0, 0),
				FPlane(0, 0, 0, 1));

			const FMatrix ViewMatrix = FTranslationMatrix(-Context.ViewLocation) * ViewRotationMatrix;

			RenderPreviewFrustum(ProjectionMatrix, ViewMatrix, Context.ViewLocation);
		}
	}
}

void ADisplayClusterRootActor::RenderPreviewFrustum(const FMatrix ProjectionMatrix, const FMatrix ViewMatrix, const FVector ViewOrigin)
{
	const float FarPlane = PreviewICVFXFrustumsFarDistance;
	const float NearPlane = GNearClippingPlane;
	const FColor Color = FColor::Green;
	const float Thickness = 1.0f;

	const UWorld* World = GetWorld();
	ULineBatchComponent* LineBatcher = World ? World->LineBatcher : nullptr;
	if (!LineBatcher)
	{
		return;
	}

	// Get FOV and AspectRatio from the view's projection matrix.
	const float AspectRatio = ProjectionMatrix.M[1][1] / ProjectionMatrix.M[0][0];
	const bool bIsPerspectiveProjection = true;

	// Build the camera frustum for this cascade
	const float HalfHorizontalFOV = bIsPerspectiveProjection ? FMath::Atan(1.0f / ProjectionMatrix.M[0][0]) : PI / 4.0f;
	const float HalfVerticalFOV = bIsPerspectiveProjection ? FMath::Atan(1.0f / ProjectionMatrix.M[1][1]) : FMath::Atan((FMath::Tan(PI / 4.0f) / AspectRatio));
	const float AsymmetricFOVScaleX = ProjectionMatrix.M[2][0];
	const float AsymmetricFOVScaleY = ProjectionMatrix.M[2][1];
	
	// Near plane
	const float StartHorizontalTotalLength = NearPlane * FMath::Tan(HalfHorizontalFOV);
	const float StartVerticalTotalLength = NearPlane * FMath::Tan(HalfVerticalFOV);
	const FVector StartCameraLeftOffset = ViewMatrix.GetColumn(0) * -StartHorizontalTotalLength * (1 + AsymmetricFOVScaleX);
	const FVector StartCameraRightOffset = ViewMatrix.GetColumn(0) * StartHorizontalTotalLength * (1 - AsymmetricFOVScaleX);
	const FVector StartCameraBottomOffset = ViewMatrix.GetColumn(1) * -StartVerticalTotalLength * (1 + AsymmetricFOVScaleY);
	const FVector StartCameraTopOffset = ViewMatrix.GetColumn(1) * StartVerticalTotalLength * (1 - AsymmetricFOVScaleY);
	
	// Far plane
	const float EndHorizontalTotalLength = FarPlane * FMath::Tan(HalfHorizontalFOV);
	const float EndVerticalTotalLength = FarPlane * FMath::Tan(HalfVerticalFOV);
	const FVector EndCameraLeftOffset = ViewMatrix.GetColumn(0) * -EndHorizontalTotalLength * (1 + AsymmetricFOVScaleX);
	const FVector EndCameraRightOffset = ViewMatrix.GetColumn(0) * EndHorizontalTotalLength * (1 - AsymmetricFOVScaleX);
	const FVector EndCameraBottomOffset = ViewMatrix.GetColumn(1) * -EndVerticalTotalLength * (1 + AsymmetricFOVScaleY);
	const FVector EndCameraTopOffset = ViewMatrix.GetColumn(1) * EndVerticalTotalLength * (1 - AsymmetricFOVScaleY);
	
	const FVector CameraDirection = ViewMatrix.GetColumn(2);

	// Get the 4 points of the camera frustum near plane, in world space
	PreviewFrustumVerts[0] = ViewOrigin + CameraDirection * NearPlane + StartCameraRightOffset + StartCameraTopOffset;         // 0 Near  Top    Right
	PreviewFrustumVerts[1] = ViewOrigin + CameraDirection * NearPlane + StartCameraRightOffset + StartCameraBottomOffset;      // 1 Near  Bottom Right
	PreviewFrustumVerts[2] = ViewOrigin + CameraDirection * NearPlane + StartCameraLeftOffset + StartCameraTopOffset;          // 2 Near  Top    Left
	PreviewFrustumVerts[3] = ViewOrigin + CameraDirection * NearPlane + StartCameraLeftOffset + StartCameraBottomOffset;       // 3 Near  Bottom Left

	// Get the 4 points of the camera frustum far plane, in world space
	PreviewFrustumVerts[4] = ViewOrigin + CameraDirection * FarPlane + EndCameraRightOffset + EndCameraTopOffset;         // 4 Far  Top    Right
	PreviewFrustumVerts[5] = ViewOrigin + CameraDirection * FarPlane + EndCameraRightOffset + EndCameraBottomOffset;      // 5 Far  Bottom Right
	PreviewFrustumVerts[6] = ViewOrigin + CameraDirection * FarPlane + EndCameraLeftOffset + EndCameraTopOffset;          // 6 Far  Top    Left
	PreviewFrustumVerts[7] = ViewOrigin + CameraDirection * FarPlane + EndCameraLeftOffset + EndCameraBottomOffset;       // 7 Far  Bottom Left	
	
	// frustum lines
	LineBatcher->DrawLine(PreviewFrustumVerts[0], PreviewFrustumVerts[4], Color, SDPG_World, Thickness, 0.f); // right top
	LineBatcher->DrawLine(PreviewFrustumVerts[1], PreviewFrustumVerts[5], Color, SDPG_World, Thickness, 0.f); // right bottom
	LineBatcher->DrawLine(PreviewFrustumVerts[2], PreviewFrustumVerts[6], Color, SDPG_World, Thickness, 0.f); // left top
	LineBatcher->DrawLine(PreviewFrustumVerts[3], PreviewFrustumVerts[7], Color, SDPG_World, Thickness, 0.f); // left bottom

	// near plane square
	LineBatcher->DrawLine(PreviewFrustumVerts[0], PreviewFrustumVerts[1], Color, SDPG_World, Thickness, 0.f); // right top to right bottom
	LineBatcher->DrawLine(PreviewFrustumVerts[1], PreviewFrustumVerts[3], Color, SDPG_World, Thickness, 0.f); // right bottom to left bottom
	LineBatcher->DrawLine(PreviewFrustumVerts[3], PreviewFrustumVerts[2], Color, SDPG_World, Thickness, 0.f); // left bottom to left top
	LineBatcher->DrawLine(PreviewFrustumVerts[2], PreviewFrustumVerts[0], Color, SDPG_World, Thickness, 0.f); // left top to right top

	// far plane square
	LineBatcher->DrawLine(PreviewFrustumVerts[4], PreviewFrustumVerts[5], Color, SDPG_World, Thickness, 0.f); // right top to right bottom
	LineBatcher->DrawLine(PreviewFrustumVerts[5], PreviewFrustumVerts[7], Color, SDPG_World, Thickness, 0.f); // right bottom to left bottom
	LineBatcher->DrawLine(PreviewFrustumVerts[7], PreviewFrustumVerts[6], Color, SDPG_World, Thickness, 0.f); // left bottom to left top
	LineBatcher->DrawLine(PreviewFrustumVerts[6], PreviewFrustumVerts[4], Color, SDPG_World, Thickness, 0.f); // left top to right top
}

static FName Name_RelativeLocation = USceneComponent::GetRelativeLocationPropertyName();
static FName Name_RelativeRotation = USceneComponent::GetRelativeRotationPropertyName();
static FName Name_RelativeScale3D = USceneComponent::GetRelativeScale3DPropertyName();

void ADisplayClusterRootActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	
	// The AActor method, simplified and modified to skip construction scripts.
	// Component registration still needs to occur or the actor will look like it disappeared.
	auto SuperCallWithoutConstructionScripts = [&]
	{
		if (IsPropertyChangedAffectingDataLayers(PropertyChangedEvent))
		{
			FixupDataLayers(/*bRevertChangesOnLockedDataLayer*/true);
		}

		const bool bTransformationChanged = (PropertyName == Name_RelativeLocation || PropertyName == Name_RelativeRotation || PropertyName == Name_RelativeScale3D);
		if ((GEditor && GEditor->bIsSimulatingInEditor && GetWorld() != nullptr) || ReregisterComponentsWhenModified())
		{
			// If a transaction is occurring we rely on the true parent method instead.
			check (!CurrentTransactionAnnotation.IsValid());
			
			UnregisterAllComponents();
			ReregisterAllComponents();
		}

		// Let other systems know that an actor was moved
		if (bTransformationChanged)
		{
			GEngine->BroadcastOnActorMoved( this );
		}

		FEditorSupportDelegates::UpdateUI.Broadcast();
		UObject::PostEditChangeProperty(PropertyChangedEvent);	
	};

	bool bReinitializeActor = true;
	bool bCanSkipConstructionScripts = false;
	if (const UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(GetClass()))
	{
		bCanSkipConstructionScripts = !Blueprint->bRunConstructionScriptOnDrag;
	}
	
	if (bCanSkipConstructionScripts && PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive && !CurrentTransactionAnnotation.IsValid())
	{
		// Avoid calling construction scripts when the change occurs while the user is dragging a slider.
		SuperCallWithoutConstructionScripts();
	}
	else
	{
		Super::PostEditChangeProperty(PropertyChangedEvent);
	}
	
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
