// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Viewport/DisplayClusterViewportManager.h"
#include "Render/Viewport/DisplayClusterViewportManagerProxy.h"

#include "IDisplayCluster.h"
#include "Render/IDisplayClusterRenderManager.h"
#include "Render/Projection/IDisplayClusterProjectionPolicyFactory.h"
#include "Render/Projection/IDisplayClusterProjectionPolicy.h"

#include "Render/Viewport/DisplayClusterViewport.h"
#include "Render/Viewport/DisplayClusterViewportProxy.h"
#include "Render/Viewport/DisplayClusterViewport_CustomPostProcessSettings.h"
#include "Render/Viewport/RenderFrame/DisplayClusterRenderFrameManager.h"
#include "Render/Viewport/RenderTarget/DisplayClusterRenderTargetManager.h"
#include "Render/Viewport/RenderTarget/DisplayClusterRenderTargetResource.h"
#include "Render/Viewport/DisplayClusterViewportStrings.h"
#include "Render/Viewport/Postprocess/DisplayClusterViewportPostProcessManager.h"

#include "Render/Viewport/Configuration/DisplayClusterViewportConfiguration.h"
#include "Render/Viewport/Configuration/DisplayClusterViewportConfigurationBase.h"

#include "SceneViewExtension.h"

#include "DisplayClusterRootActor.h" 

#include "Misc/DisplayClusterLog.h"

#include "Engine/Console.h"

///////////////////////////////////////////////////////////////////////////////////////
//          FDisplayClusterViewportManager
///////////////////////////////////////////////////////////////////////////////////////

FDisplayClusterViewportManager::FDisplayClusterViewportManager()
{
	Configuration      = MakeUnique<FDisplayClusterViewportConfiguration>(*this);
	RenderFrameManager = MakeUnique<FDisplayClusterRenderFrameManager>();

	RenderTargetManager = MakeShared<FDisplayClusterRenderTargetManager, ESPMode::ThreadSafe>();
	PostProcessManager  = MakeShared<FDisplayClusterViewportPostProcessManager, ESPMode::ThreadSafe>(*this);

	ViewportManagerProxy = new FDisplayClusterViewportManagerProxy(*this);

	// Always reset RTT when root actor re-created
	ResetSceneRenderTargetSize();
}

FDisplayClusterViewportManager::~FDisplayClusterViewportManager()
{
	// Remove viewports
	{
		TArray<FDisplayClusterViewport*> ExistViewports = Viewports;
		for (FDisplayClusterViewport* Viewport : ExistViewports)
		{
			ImplDeleteViewport(Viewport);
		}
		ExistViewports.Empty();
	}

	if (ViewportManagerProxy)
	{
		ViewportManagerProxy->ImplSafeRelease();
		ViewportManagerProxy = nullptr;
	}
}

const IDisplayClusterViewportManagerProxy* FDisplayClusterViewportManager::GetProxy() const
{
	return ViewportManagerProxy;
}

IDisplayClusterViewportManagerProxy* FDisplayClusterViewportManager::GetProxy()
{
	return ViewportManagerProxy;
}

UWorld* FDisplayClusterViewportManager::GetCurrentWorld() const
{
	check(IsInGameThread());

	if (!CurrentWorldRef.IsValid() || CurrentWorldRef.IsStale())
	{
		return nullptr;
	}

	return CurrentWorldRef.Get();
}

ADisplayClusterRootActor* FDisplayClusterViewportManager::GetRootActor() const
{
	return Configuration->GetRootActor();
}

bool FDisplayClusterViewportManager::IsSceneOpened() const
{
	check(IsInGameThread());

	return CurrentWorldRef.IsValid() && !CurrentWorldRef.IsStale();
}

void FDisplayClusterViewportManager::StartScene(UWorld* InWorld)
{
	check(IsInGameThread());

	CurrentWorldRef = TWeakObjectPtr<UWorld>(InWorld);

	for (FDisplayClusterViewport* Viewport : Viewports)
	{
		if (Viewport)
		{
			Viewport->HandleStartScene();
		}
	}

	if (PostProcessManager.IsValid())
	{
		PostProcessManager->HandleStartScene();
	}
}

void FDisplayClusterViewportManager::EndScene()
{
	check(IsInGameThread());

	for (FDisplayClusterViewport* Viewport : Viewports)
	{
		if (Viewport)
		{
			Viewport->HandleEndScene();
		}
	}

	if (PostProcessManager.IsValid())
	{
		PostProcessManager->HandleEndScene();
	}

	CurrentWorldRef.Reset();
}

void FDisplayClusterViewportManager::ResetScene()
{
	check(IsInGameThread());

	for (FDisplayClusterViewport* Viewport : Viewports)
	{
		if (Viewport)
		{
			Viewport->HandleEndScene();
			Viewport->HandleStartScene();
		}
	}
}

bool FDisplayClusterViewportManager::UpdateConfiguration(EDisplayClusterRenderFrameMode InRenderMode, const FString& InClusterNodeId, ADisplayClusterRootActor* InRootActorPtr)
{
	if (InRootActorPtr)
	{
		bool bIsRootActorChanged = Configuration->SetRootActor(InRootActorPtr);

		// When the root actor changes, we have to ResetScene() to reinitialize the internal references of the projection policy.
		if (bIsRootActorChanged)
		{
			ResetScene();
		}

		return Configuration->UpdateConfiguration(InRenderMode, InClusterNodeId);
	}

	return false;
}

void FDisplayClusterViewportManager::SetViewportBufferRatio(FDisplayClusterViewport& DstViewport, float InBufferRatio)
{
	if (DstViewport.RenderSettings.BufferRatio > InBufferRatio)
	{
		// Reset scene RTT when buffer ratio changed down
		ResetSceneRenderTargetSize();
	}

	DstViewport.RenderSettings.BufferRatio = InBufferRatio;
}

void FDisplayClusterViewportManager::HandleViewportRTTChanges(const TArray<FDisplayClusterViewport_Context>& PrevContexts, const TArray<FDisplayClusterViewport_Context>& Contexts)
{
	// Support for resetting RTT size when viewport size is changed
	if (PrevContexts.Num() != Contexts.Num())
	{
		// Reset scene RTT size when viewport disabled
		ResetSceneRenderTargetSize();
	}
	else
	{
		for (int32 ContextIt = 0; ContextIt < Contexts.Num(); ContextIt++)
		{
			if (Contexts[ContextIt].RenderTargetRect.Size() != PrevContexts[ContextIt].RenderTargetRect.Size())
			{
				ResetSceneRenderTargetSize();
				break;
			}
		}
	}
}

void FDisplayClusterViewportManager::ResetSceneRenderTargetSize()
{
	SceneRenderTargetResizeMethod = ESceneRenderTargetResizeMethod::Reset;
}

void FDisplayClusterViewportManager::UpdateSceneRenderTargetSize()
{
	if (SceneRenderTargetResizeMethod != ESceneRenderTargetResizeMethod::None)
	{
		IConsoleVariable* const RTResizeCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SceneRenderTargetResizeMethod"));
		if (RTResizeCVar)
		{
			switch (SceneRenderTargetResizeMethod)
			{
			case ESceneRenderTargetResizeMethod::Reset:
				// Resize to match requested render size (Default) (Least memory use, can cause stalls when size changes e.g. ScreenPercentage)
				RTResizeCVar->Set(0);

				// Wait for frame history is done
				// static const uint32 FSceneRenderTargets::FrameSizeHistoryCount = 3;
				FrameHistoryCounter = 3;
				SceneRenderTargetResizeMethod = ESceneRenderTargetResizeMethod::WaitFrameSizeHistory;
				break;

			case ESceneRenderTargetResizeMethod::WaitFrameSizeHistory:
				if (FrameHistoryCounter-- < 0)
				{
					SceneRenderTargetResizeMethod = ESceneRenderTargetResizeMethod::Restore;
				}
				break;

			case ESceneRenderTargetResizeMethod::Restore:
				// Expands to encompass the largest requested render dimension. (Most memory use, least prone to allocation stalls.)
				RTResizeCVar->Set(2);
				SceneRenderTargetResizeMethod = ESceneRenderTargetResizeMethod::None;
				break;

			default:
				break;
			}
		}
	}
}

bool FDisplayClusterViewportManager::BeginNewFrame(class FViewport* InViewport, UWorld* InWorld, FDisplayClusterRenderFrame& OutRenderFrame)
{
	check(IsInGameThread());

	OutRenderFrame.ViewportManager = this;

	// Handle world runtime update
	UWorld* CurrentWorld = GetCurrentWorld();
	if (CurrentWorld != InWorld)
	{
		// Handle end current scene
		if (CurrentWorld)
		{
			EndScene();
		}

		// Handle begin new scene
		if (InWorld)
		{
			StartScene(InWorld);
		}
		else
		{
			// no world for render
			return false;
		}
	}
	const FDisplayClusterRenderFrameSettings& RenderFrameSettingsConstRef = Configuration->GetRenderFrameSettingsConstRef();
	const FDisplayClusterTextureShareSettings& TextureShareSettingsConstRef = Configuration->GetTextureShareSettingsConstRef();

	// generate unique stereopass for each frame
	uint32 ViewPassNum = 0;

	// Initialize viewports from new render settings, and create new contexts, reset prev frame resources
	for (FDisplayClusterViewport* Viewport : Viewports)
	{
		// Save orig viewport contexts
		TArray<FDisplayClusterViewport_Context> PrevContexts;
		PrevContexts.Append(Viewport->GetContexts());

		if (Viewport->UpdateFrameContexts(ViewPassNum, RenderFrameSettingsConstRef))
		{
			ViewPassNum += Viewport->Contexts.Num();
		}

		HandleViewportRTTChanges(PrevContexts, Viewport->GetContexts());
	}

	// Handle scene RTT resize
	UpdateSceneRenderTargetSize();

	// Build new frame structure
	if (!RenderFrameManager->BuildRenderFrame(InViewport, RenderFrameSettingsConstRef, Viewports, OutRenderFrame))
	{
		return false;
	}

	// Allocate resources for frame
	if (!RenderTargetManager->AllocateRenderFrameResources(InViewport, RenderFrameSettingsConstRef, Viewports, OutRenderFrame))
	{
		return false;
	}

	// Support TextureShare:
	if(TextureShareSettingsConstRef.bIsEnabled)
	{
		// Update TextureShare StereoContextIndex link for viewports
		for (FDisplayClusterViewport* Viewport : Viewports)
		{
			Viewport->TextureShare.UpdateLinkSceneContextToShare(*Viewport);
		}

		// Synchronize global frame
		FDisplayClusterViewport_TextureShare::BeginSyncFrame(TextureShareSettingsConstRef);
	}

	// Update desired views number
	OutRenderFrame.UpdateDesiredNumberOfViews();

#if WITH_EDITOR
	// Get preview resources from root actor
	ImplUpdatePreviewRTTResources();
#endif /*WITH_EDITOR*/

	return true;
}

void FDisplayClusterViewportManager::FinalizeNewFrame()
{
	check(IsInGameThread());

	// When all viewports processed, we remove all single frame custom postprocess
	for (FDisplayClusterViewport* Viewport : Viewports)
	{
		if (Viewport)
		{
			Viewport->CustomPostProcessSettings.FinalizeFrame();

			// update projection policy proxy data
			if (Viewport->ProjectionPolicy.IsValid())
			{
				Viewport->ProjectionPolicy->UpdateProxyData(Viewport);
			}
		}
	}

	// Send configuration settings to rendering thread
	ViewportManagerProxy->ImplUpdateSettings(*Configuration.Get());

	// Send updated viewports data to render thread proxy
	ViewportManagerProxy->ImplUpdateViewports(Viewports);

	// Update postprocess data from game thread
	PostProcessManager->Tick();

	// Send updated postprocess data to rendering thread
	PostProcessManager->FinalizeNewFrame();
}

void FDisplayClusterViewportManager::ConfigureViewFamily(const FDisplayClusterRenderFrame::FFrameRenderTarget& InFrameTarget, const FDisplayClusterRenderFrame::FFrameViewFamily& InFrameViewFamily, FSceneViewFamilyContext& ViewFamily)
{
	// Gather Scene View Extensions
	// Scene View Extension activation with ViewportId granularity only works if you have one ViewFamily per ViewportId
	{
		ViewFamily.ViewExtensions = InFrameViewFamily.ViewExtensions;
		for (FSceneViewExtensionRef& ViewExt : ViewFamily.ViewExtensions)
		{
			ViewExt->SetupViewFamily(ViewFamily);
		}
	}

	ViewFamily.SceneCaptureCompositeMode = ESceneCaptureCompositeMode::SCCM_Overwrite;

	// Setup capture mode:
	{
		switch (InFrameTarget.CaptureMode)
		{
		case EDisplayClusterViewportCaptureMode::Chromakey:
		case EDisplayClusterViewportCaptureMode::Lightcard:
			ViewFamily.SceneCaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
			ViewFamily.bResolveScene = false;

			ViewFamily.EngineShowFlags.PostProcessing = 0;

			ViewFamily.EngineShowFlags.SetAtmosphere(0);
			ViewFamily.EngineShowFlags.SetFog(0);
			ViewFamily.EngineShowFlags.SetMotionBlur(0); // motion blur doesn't work correctly with scene captures.
			ViewFamily.EngineShowFlags.SetSeparateTranslucency(0);
			ViewFamily.EngineShowFlags.SetHMDDistortion(0);
			ViewFamily.EngineShowFlags.SetOnScreenDebug(0);
			break;

		default:
			break;
		}
	}
}

void FDisplayClusterViewportManager::RenderFrame(FViewport* InViewport)
{
	ViewportManagerProxy->ImplRenderFrame(InViewport);
}

bool FDisplayClusterViewportManager::CreateViewport(const FString& InViewportId, const class UDisplayClusterConfigurationViewport* ConfigurationViewport)
{
	check(IsInGameThread());
	check(ConfigurationViewport != nullptr);

	// Check viewport ID
	if (InViewportId.IsEmpty())
	{
		UE_LOG(LogDisplayClusterViewport, Warning, TEXT("Wrong viewport ID"));
		return false;
	}

	// ID must be unique
	if (FindViewport(InViewportId) != nullptr)
	{
		UE_LOG(LogDisplayClusterViewport, Warning, TEXT("Viewport '%s' already exists"), *InViewportId);
		return false;
	}

	// Create projection policy for viewport
	const TSharedPtr<IDisplayClusterProjectionPolicy, ESPMode::ThreadSafe> NewProjectionPolicy = CreateProjectionPolicy(InViewportId, &ConfigurationViewport->ProjectionPolicy);
	if (NewProjectionPolicy.IsValid())
	{
		// Create viewport for new projection policy
		FDisplayClusterViewport* NewViewport = ImplCreateViewport(InViewportId, NewProjectionPolicy);
		if (NewViewport != nullptr)
		{
			ADisplayClusterRootActor* RootActorPtr = GetRootActor();
			if (RootActorPtr)
			{
				FDisplayClusterViewportConfigurationBase::UpdateViewportConfiguration(*this, *RootActorPtr, NewViewport, ConfigurationViewport);
				return true;
			}
		}
	}

	UE_LOG(LogDisplayClusterViewport, Error, TEXT("Viewports '%s' not created."), *InViewportId);
	return false;
}

FDisplayClusterViewport* FDisplayClusterViewportManager::ImplFindViewport(const FString& ViewportId) const
{
	check(IsInGameThread());

	// Ok, we have a request for a particular viewport. Let's find it.
	FDisplayClusterViewport* const* DesiredViewport = Viewports.FindByPredicate([ViewportId](const FDisplayClusterViewport* ItemViewport)
	{
		return ViewportId.Equals(ItemViewport->ViewportId, ESearchCase::IgnoreCase);
	});

	return (DesiredViewport != nullptr) ? *DesiredViewport : nullptr;
}

IDisplayClusterViewport* FDisplayClusterViewportManager::CreateViewport(const FString& ViewportId, const TSharedPtr<IDisplayClusterProjectionPolicy, ESPMode::ThreadSafe>& InProjectionPolicy)
{
	check(IsInGameThread());

	FDisplayClusterViewport* ExistViewport = ImplFindViewport(ViewportId);
	if (ExistViewport != nullptr)
	{
		//add error log: Viewport with name '%s' already exist
		return nullptr;
	}

	return ImplCreateViewport(ViewportId, InProjectionPolicy);
}

bool FDisplayClusterViewportManager::DeleteViewport(const FString& ViewportId)
{
	check(IsInGameThread());

	FDisplayClusterViewport* ExistViewport = ImplFindViewport(ViewportId);
	if (ExistViewport != nullptr)
	{
		ImplDeleteViewport(ExistViewport);
		return true;
	}

	return false;
}

FDisplayClusterViewport* FDisplayClusterViewportManager::ImplCreateViewport(const FString& ViewportId, const TSharedPtr<IDisplayClusterProjectionPolicy, ESPMode::ThreadSafe>& InProjectionPolicy)
{
	check(IsInGameThread());

	check(InProjectionPolicy.IsValid())

	// Create viewport
	FDisplayClusterViewport* NewViewport = new FDisplayClusterViewport(*this, ViewportId, InProjectionPolicy);

	// Add viewport on gamethread
	Viewports.Add(NewViewport);

	// Add viewport proxy on renderthread
	ViewportManagerProxy->ImplCreateViewport(NewViewport->ViewportProxy);

	// Handle start scene for viewport
	NewViewport->HandleStartScene();

	return NewViewport;
}

void FDisplayClusterViewportManager::ImplDeleteViewport(FDisplayClusterViewport* ExistViewport)
{
	// Handle projection policy event
	ExistViewport->ProjectionPolicy.Reset();
	ExistViewport->UninitializedProjectionPolicy.Reset();

	// Delete viewport proxy on render thread
	ViewportManagerProxy->ImplDeleteViewport(ExistViewport->ViewportProxy);

	// Remove viewport obj from manager
	int ViewportIndex = Viewports.Find(ExistViewport);
	if (ViewportIndex != INDEX_NONE)
	{
		Viewports[ViewportIndex] = nullptr;
		Viewports.RemoveAt(ViewportIndex);
	}

	delete ExistViewport;

	// Reset RTT size after viewport delete
	ResetSceneRenderTargetSize();
}

IDisplayClusterViewport* FDisplayClusterViewportManager::FindViewport(const enum EStereoscopicPass StereoPassType, uint32* OutContextNum) const
{
	check(IsInGameThread());

	for (FDisplayClusterViewport* Viewport : Viewports)
	{
		if (Viewport && Viewport->FindContext(StereoPassType, OutContextNum))
		{
			return Viewport;
		}
	}

	// Viewport not found
	return nullptr;
}

TSharedPtr<IDisplayClusterProjectionPolicy, ESPMode::ThreadSafe> FDisplayClusterViewportManager::CreateProjectionPolicy(const FString& InViewportId, const FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy)
{
	check(IsInGameThread());
	check(InConfigurationProjectionPolicy != nullptr);

	// Generate unique projection policy id from viewport name
	const FString ProjectionPolicyId = FString::Printf(TEXT("%s_%s"), DisplayClusterViewportStrings::prefix::projection, *InViewportId);

	IDisplayClusterRenderManager* const DCRenderManager = IDisplayCluster::Get().GetRenderMgr();
	check(DCRenderManager);

	TSharedPtr<IDisplayClusterProjectionPolicyFactory> ProjPolicyFactory = DCRenderManager->GetProjectionPolicyFactory(InConfigurationProjectionPolicy->Type);
	if (ProjPolicyFactory.IsValid())
	{
		TSharedPtr<IDisplayClusterProjectionPolicy, ESPMode::ThreadSafe> ProjPolicy = ProjPolicyFactory->Create(ProjectionPolicyId, InConfigurationProjectionPolicy);
		if (ProjPolicy.IsValid())
		{
			return ProjPolicy;
		}
		else
		{
			FString RHIName = GDynamicRHI->GetName();
			UE_LOG(LogDisplayClusterViewport, Warning, TEXT("Invalid projection policy: type '%s', RHI '%s', viewport '%s'"), *InConfigurationProjectionPolicy->Type, *RHIName, *ProjectionPolicyId);
		}
	}
	else
	{
		UE_LOG(LogDisplayClusterViewport, Warning, TEXT("No projection factory found for projection type '%s'"), *InConfigurationProjectionPolicy->Type);
	}

	return nullptr;
}

