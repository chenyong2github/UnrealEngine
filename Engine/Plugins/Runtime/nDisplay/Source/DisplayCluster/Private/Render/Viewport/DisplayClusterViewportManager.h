// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "SceneTypes.h"

#include "Misc/DisplayClusterObjectRef.h"

#include "Render/Viewport/IDisplayClusterViewportManager.h"
#include "Render/Viewport/DisplayClusterViewport.h"
#include "Render/Viewport/DisplayClusterViewportProxy.h"

#include "Render/Viewport/Containers/DisplayClusterViewport_Enums.h"

#include "OpenColorIOConfiguration.h"
#include "OpenColorIODisplayExtensionWrapper.h"

class FDisplayClusterRenderTargetManager;
class FDisplayClusterRenderFrameManager;
class FDisplayClusterICVFX_Manager;
class FDisplayClusterViewportManager_PostProcess;

class FDisplayClusterRenderFrame;
class IDisplayClusterProjectionPolicy;
class UDisplayClusterConfigurationViewport;
class FDisplayClusterViewportConfiguration;
class FViewport;
class FSceneInterface;
struct FDisplayClusterConfigurationProjection;



class FDisplayClusterViewportManager
	: public IDisplayClusterViewportManager
{
public:
	FDisplayClusterViewportManager();
	virtual ~FDisplayClusterViewportManager() = default;

public:
	virtual UWorld*                   GetWorld() const override;
	virtual ADisplayClusterRootActor* GetRootActor() const override;

	/** Game thread funcs */
	virtual void StartScene(UWorld* World) override;
	virtual void EndScene() override;

	virtual bool IsSceneOpened() const override;

	virtual bool UpdateConfiguration(EDisplayClusterRenderFrameMode InRenderMode, const FString& InClusterNodeId, class ADisplayClusterRootActor* InRootActorPtr) override;

	virtual bool BeginNewFrame(class FViewport* InViewport, FDisplayClusterRenderFrame& OutRenderFrame) override;
	virtual void FinalizeNewFrame() override;

	virtual void ConfigureViewFamily(const FDisplayClusterRenderFrame::FFrameRenderTarget& InFrameTarget, const FDisplayClusterRenderFrame::FFrameViewFamily& InFrameViewFamily, FSceneViewFamilyContext& InOutViewFamily) override;

#if WITH_EDITOR
	virtual bool UpdatePreviewConfiguration(class UDisplayClusterConfigurationViewportPreview* PreviewConfiguration, UWorld* PreviewWorld, class ADisplayClusterRootActor* InRootActorPtr) override;
	virtual bool RenderPreview(class FDisplayClusterRenderFrame& InPreviewRenderFrame) override;
#endif

	virtual void DoCrossGPUTransfers_RenderThread(class FViewport* InViewport, FRHICommandListImmediate& RHICmdList) const override;
	virtual void UpdateDeferredResources_RenderThread(FRHICommandListImmediate& RHICmdList) const override;
	virtual void UpdateFrameResources_RenderThread(FRHICommandListImmediate& RHICmdList, bool bWarpBlendEnabled) const override;

	/** Game thread funcs */
	virtual IDisplayClusterViewport* FindViewport(const FString& InViewportId) const override
	{
		return ImplFindViewport(InViewportId);
	}

	virtual IDisplayClusterViewport* FindViewport(const EStereoscopicPass StereoPassType, uint32* OutContextNum = nullptr) const override;

	virtual const TArrayView<IDisplayClusterViewport*> GetViewports() const override
	{
		return TArrayView<IDisplayClusterViewport*>((IDisplayClusterViewport**)(Viewports.GetData()), Viewports.Num());
	}

	/** Render thread funcs */
	virtual IDisplayClusterViewportProxy* FindViewport_RenderThread(const FString& InViewportId) const override
	{
		return ImplFindViewport_RenderThread(InViewportId);
	}

	virtual IDisplayClusterViewportProxy* FindViewport_RenderThread(const EStereoscopicPass StereoPassType, uint32* OutContextNum = nullptr) const override;

	virtual const TArrayView<IDisplayClusterViewportProxy*> GetViewports_RenderThread() const override
	{
		return TArrayView<IDisplayClusterViewportProxy*>((IDisplayClusterViewportProxy**)(ViewportProxies.GetData()), ViewportProxies.Num());
	}

	virtual bool GetFrameTargets_RenderThread(TArray<FRHITexture2D*>& OutFrameResources, TArray<FIntPoint>& OutTargetOffsets, TArray<FRHITexture2D*>* OutAdditionalFrameResources = nullptr) const override;
	virtual bool ResolveFrameTargetToBackBuffer_RenderThread(FRHICommandListImmediate& RHICmdList, const uint32 InContextNum, const int DestArrayIndex, FRHITexture2D* DestTexture, FVector2D WindowSize) const override;

	// internal use only

	bool CreateViewport(const FString& ViewportId, const class UDisplayClusterConfigurationViewport* ConfigurationViewport);
	IDisplayClusterViewport* CreateViewport(const FString& ViewportId, TSharedPtr<class IDisplayClusterProjectionPolicy> InProjectionPolicy);
	bool                     DeleteViewport(const FString& ViewportId);

	FDisplayClusterViewport* ImplCreateViewport(const FString& ViewportId, TSharedPtr<IDisplayClusterProjectionPolicy> InProjectionPolicy);
	void ImplDeleteViewport(FDisplayClusterViewport* Viewport);

	const TArray<FDisplayClusterViewport*>& ImplGetViewports() const
	{
		check(IsInGameThread());
		return Viewports;
	}

	const TArray<FDisplayClusterViewportProxy*>& ImplGetViewportProxies_RenderThread() const
	{
		check(IsInRenderingThread());
		return ViewportProxies;
	}

	FDisplayClusterViewport* ImplFindViewport(const FString& InViewportId) const;
	FDisplayClusterViewportProxy* ImplFindViewport_RenderThread(const FString& InViewportId) const;

	TSharedPtr<IDisplayClusterProjectionPolicy> CreateProjectionPolicy(const FString& InViewportId, const FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy);

public:
	TUniquePtr<FDisplayClusterViewportConfiguration>       Configuration;
	TUniquePtr<FDisplayClusterViewportManager_PostProcess> PostProcessManager;

private:
	TUniquePtr<FDisplayClusterRenderTargetManager> RenderTargetManager;
	TUniquePtr<FDisplayClusterRenderFrameManager>  RenderFrameManager;


	TArray<FDisplayClusterViewport*>      Viewports;
	TArray<FDisplayClusterViewportProxy*> ViewportProxies;

	// Pointer to the current scene
	UWorld* CurrentScene;
};
