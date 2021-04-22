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

#include "Misc\DisplayClusterLog.h"

///////////////////////////////////////////////////////////////////////////////////////
//          FDisplayClusterViewportManager
///////////////////////////////////////////////////////////////////////////////////////

FDisplayClusterViewportManager::FDisplayClusterViewportManager()
{
	Configuration      = MakeUnique<FDisplayClusterViewportConfiguration>(*this);
	RenderFrameManager = MakeUnique<FDisplayClusterRenderFrameManager>();

	RenderTargetManager = MakeShared<FDisplayClusterRenderTargetManager>();
	PostProcessManager  = MakeShared<FDisplayClusterViewportPostProcessManager>();

	ViewportManagerProxy = new FDisplayClusterViewportManagerProxy(*this);
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

	// generate unique stereopass for each frame
	uint32 ViewPassNum = 0;

	// Initialize viewports from new render settings, and create new contexts, reset prev frame resources
	for (FDisplayClusterViewport* Viewport : Viewports)
	{
		if (Viewport->UpdateFrameContexts(ViewPassNum, Configuration->GetRenderFrameSettings()))
		{
			ViewPassNum += Viewport->Contexts.Num();
		}
	}

	// Build new frame structure
	if (!RenderFrameManager->BuildRenderFrame(InViewport, Configuration->GetRenderFrameSettings(), Viewports, OutRenderFrame))
	{
		return false;
	}

	// Allocate resources for frame
	if (!RenderTargetManager->AllocateRenderFrameResources(InViewport, Configuration->GetRenderFrameSettings(), Viewports, OutRenderFrame))
	{
		return false;
	}

	// Update desired views number
	OutRenderFrame.UpdateDesiredNumberOfViews();

#if WITH_EDITOR
	// Get preview resources from root actor
	ImplUpdatePreviewRTTResources();
#endif /*WITH_EDITOR*/

	// Send render frame settings to rendering thread
	ViewportManagerProxy->ImplUpdateRenderFrameSettings(Configuration->GetRenderFrameSettings());

	// Send updated viewports data to render thread proxy
	ViewportManagerProxy->ImplUpdateViewports(Viewports);

	return true;
}

void FDisplayClusterViewportManager::FinalizeNewFrame()
{
	check(IsInGameThread());

	// When all viewports processed, we remove all single frame custom postprocess
	for (FDisplayClusterViewport* Viewport : Viewports)
	{
		Viewport->CustomPostProcessSettings.FinalizeFrame();
	}
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

void FDisplayClusterViewportManager::RenderFrame(const bool bWarpBlendEnabled, FRHITexture2D* FrameOutputRTT)
{
	ViewportManagerProxy->ImplRenderFrame(bWarpBlendEnabled, FrameOutputRTT);
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
	TSharedPtr<IDisplayClusterProjectionPolicy> NewProjectionPolicy = CreateProjectionPolicy(InViewportId, &ConfigurationViewport->ProjectionPolicy);
	if (NewProjectionPolicy.IsValid())
	{
		// Create viewport for new projection policy
		FDisplayClusterViewport* NewViewport = ImplCreateViewport(InViewportId, NewProjectionPolicy);
		if (NewViewport != nullptr)
		{
			FDisplayClusterViewportConfigurationBase::UpdateViewportConfiguration(*this, NewViewport, ConfigurationViewport);
			return true;
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

IDisplayClusterViewport* FDisplayClusterViewportManager::CreateViewport(const FString& ViewportId, TSharedPtr<class IDisplayClusterProjectionPolicy> InProjectionPolicy)
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

FDisplayClusterViewport* FDisplayClusterViewportManager::ImplCreateViewport(const FString& ViewportId, TSharedPtr<class IDisplayClusterProjectionPolicy> InProjectionPolicy)
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

TSharedPtr<IDisplayClusterProjectionPolicy> FDisplayClusterViewportManager::CreateProjectionPolicy(const FString& InViewportId, const FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy)
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
		TSharedPtr<IDisplayClusterProjectionPolicy> ProjPolicy = ProjPolicyFactory->Create(ProjectionPolicyId, InConfigurationProjectionPolicy);
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

