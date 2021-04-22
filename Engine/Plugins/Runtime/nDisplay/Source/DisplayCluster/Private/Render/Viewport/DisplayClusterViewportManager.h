// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "SceneTypes.h"

#include "Misc/DisplayClusterObjectRef.h"

#include "Render/Viewport/IDisplayClusterViewportManager.h"
#include "Render/Viewport/DisplayClusterViewport.h"

#include "Render/Viewport/Containers/DisplayClusterViewport_Enums.h"

class FDisplayClusterViewportConfiguration;
class FDisplayClusterRenderTargetManager;
class FDisplayClusterViewportPostProcessManager;
class FDisplayClusterRenderFrameManager;
class FDisplayClusterRenderFrame; 
class FDisplayClusterViewportManagerProxy;

class IDisplayClusterProjectionPolicy;

class  UDisplayClusterConfigurationViewport;
struct FDisplayClusterConfigurationProjection;

class FViewport;

class FDisplayClusterViewportManager
	: public IDisplayClusterViewportManager
{
public:
	FDisplayClusterViewportManager();
	virtual ~FDisplayClusterViewportManager();

public:
	virtual const IDisplayClusterViewportManagerProxy* GetProxy() const override;
	virtual       IDisplayClusterViewportManagerProxy* GetProxy() override;

	virtual UWorld*                   GetCurrentWorld() const override;
	virtual ADisplayClusterRootActor* GetRootActor() const override;

	/** Game thread funcs */
	void StartScene(UWorld* World);
	void EndScene();
	void ResetScene();

	virtual bool IsSceneOpened() const override;

	virtual bool UpdateConfiguration(EDisplayClusterRenderFrameMode InRenderMode, const FString& InClusterNodeId, class ADisplayClusterRootActor* InRootActorPtr) override;

	virtual bool BeginNewFrame(class FViewport* InViewport, UWorld* InWorld, FDisplayClusterRenderFrame& OutRenderFrame) override;
	virtual void FinalizeNewFrame() override;

	virtual void ConfigureViewFamily(const FDisplayClusterRenderFrame::FFrameRenderTarget& InFrameTarget, const FDisplayClusterRenderFrame::FFrameViewFamily& InFrameViewFamily, FSceneViewFamilyContext& InOutViewFamily) override;
	
	virtual void RenderFrame(const bool bWarpBlendEnabled, FRHITexture2D* FrameOutputRTT) override;

#if WITH_EDITOR
	virtual bool UpdatePreviewConfiguration(class UDisplayClusterConfigurationViewportPreview* PreviewConfiguration, class ADisplayClusterRootActor* InRootActorPtr) override;
	virtual bool RenderInEditor(class FDisplayClusterRenderFrame& InRenderFrame, FRHITexture2D* FrameOutputRTT) override;
	
	void ImplUpdatePreviewRTTResources();
#endif

	virtual IDisplayClusterViewport* FindViewport(const FString& InViewportId) const override
	{
		return ImplFindViewport(InViewportId);
	}

	virtual IDisplayClusterViewport* FindViewport(const EStereoscopicPass StereoPassType, uint32* OutContextNum = nullptr) const override;

	virtual const TArrayView<IDisplayClusterViewport*> GetViewports() const override
	{
		return TArrayView<IDisplayClusterViewport*>((IDisplayClusterViewport**)(Viewports.GetData()), Viewports.Num());
	}

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

	FDisplayClusterViewport* ImplFindViewport(const FString& InViewportId) const;

	TSharedPtr<IDisplayClusterProjectionPolicy> CreateProjectionPolicy(const FString& InViewportId, const FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy);

	const FDisplayClusterViewportManagerProxy& ImplGetProxy() const
	{
		check(ViewportManagerProxy);
		check(IsInGameThread());

		return *ViewportManagerProxy;
	}

protected:
	friend FDisplayClusterViewportManagerProxy;
	friend FDisplayClusterViewportConfiguration;

	TSharedPtr<FDisplayClusterRenderTargetManager>        RenderTargetManager;
	TSharedPtr<FDisplayClusterViewportPostProcessManager> PostProcessManager;

public:
	TUniquePtr<FDisplayClusterViewportConfiguration> Configuration;

private:
	TUniquePtr<FDisplayClusterRenderFrameManager>  RenderFrameManager;

	TArray<FDisplayClusterViewport*>      Viewports;

	/** Render thread proxy manager. Deleted on render thread */
	FDisplayClusterViewportManagerProxy* ViewportManagerProxy = nullptr;

	// Pointer to the current scene
	TWeakObjectPtr<UWorld> CurrentWorldRef;};
