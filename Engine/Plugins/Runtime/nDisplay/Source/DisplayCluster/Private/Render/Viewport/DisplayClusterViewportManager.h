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
	
	virtual void RenderFrame(FViewport* InViewport) override;

#if WITH_EDITOR
	virtual bool UpdatePreviewConfiguration(const FDisplayClusterConfigurationViewportPreview& PreviewConfiguration, class ADisplayClusterRootActor* InRootActorPtr) override;
	virtual bool RenderInEditor(class FDisplayClusterRenderFrame& InRenderFrame, FViewport* InViewport) override;
	
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
	IDisplayClusterViewport* CreateViewport(const FString& ViewportId, const TSharedPtr<IDisplayClusterProjectionPolicy, ESPMode::ThreadSafe>& InProjectionPolicy);
	bool                     DeleteViewport(const FString& ViewportId);

	FDisplayClusterViewport* ImplCreateViewport(const FString& ViewportId, const TSharedPtr<IDisplayClusterProjectionPolicy, ESPMode::ThreadSafe>& InProjectionPolicy);
	void ImplDeleteViewport(FDisplayClusterViewport* Viewport);

	const TArray<FDisplayClusterViewport*>& ImplGetViewports() const
	{
		check(IsInGameThread());
		return Viewports;
	}

	FDisplayClusterViewport* ImplFindViewport(const FString& InViewportId) const;

	static TSharedPtr<IDisplayClusterProjectionPolicy, ESPMode::ThreadSafe> CreateProjectionPolicy(const FString& InViewportId, const FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy);

	const FDisplayClusterViewportManagerProxy& ImplGetProxy() const
	{
		check(ViewportManagerProxy);
		check(IsInGameThread());

		return *ViewportManagerProxy;
	}

	TSharedPtr<FDisplayClusterViewportPostProcessManager, ESPMode::ThreadSafe> GetPostProcessManager() const
	{ return PostProcessManager; }

	void SetViewportBufferRatio(FDisplayClusterViewport& DstViewport, float InBufferRatio);

private:
	void ResetSceneRenderTargetSize();
	void UpdateSceneRenderTargetSize();
	void HandleViewportRTTChanges(const TArray<FDisplayClusterViewport_Context>& PrevContexts, const TArray<FDisplayClusterViewport_Context>& Contexts);

protected:
	friend FDisplayClusterViewportManagerProxy;
	friend FDisplayClusterViewportConfiguration;

	TSharedPtr<FDisplayClusterRenderTargetManager, ESPMode::ThreadSafe>        RenderTargetManager;
	TSharedPtr<FDisplayClusterViewportPostProcessManager, ESPMode::ThreadSafe> PostProcessManager;

public:
	TUniquePtr<FDisplayClusterViewportConfiguration> Configuration;

private:
	TUniquePtr<FDisplayClusterRenderFrameManager>  RenderFrameManager;

	TArray<FDisplayClusterViewport*>      Viewports;

	/** Render thread proxy manager. Deleted on render thread */
	FDisplayClusterViewportManagerProxy* ViewportManagerProxy = nullptr;

	// Pointer to the current scene
	TWeakObjectPtr<UWorld> CurrentWorldRef;

	enum class ESceneRenderTargetResizeMethod : uint8
	{
		None = 0,
		Reset,
		WaitFrameSizeHistory,
		Restore
	};

	// Support for resetting RTT size (GROW method always grows and does not recover FPS when the viewport size or buffer ratio is changed)
	ESceneRenderTargetResizeMethod SceneRenderTargetResizeMethod = ESceneRenderTargetResizeMethod::None;
	int32 FrameHistoryCounter = 0;
};
