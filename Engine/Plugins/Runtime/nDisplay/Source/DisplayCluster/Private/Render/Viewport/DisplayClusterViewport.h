// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Viewport/IDisplayClusterViewport.h"
#include "Render/Viewport/DisplayClusterViewport_CustomPostProcessSettings.h"
#include "Render/Viewport/DisplayClusterViewport_VisibilitySettings.h"

#include "Render/Viewport/Containers/ImplDisplayClusterViewport_CameraMotionBlur.h"

#include "SceneViewExtensionContext.h"
#include "OpenColorIODisplayExtension.h"

class FDisplayClusterRenderTargetResource;
class FDisplayClusterTextureResource;
class FDisplayClusterViewportManager;
class FDisplayClusterRenderTargetManager;
class FDisplayClusterRenderFrameManager;
class FDisplayClusterViewportProxy_ExchangeContainer;
class FDisplayClusterViewportProxy;
struct FDisplayClusterRenderFrameSettings;

/**
 * Rendering viewport (sub-region of the main viewport)
 */

class FDisplayClusterViewport
	: public IDisplayClusterViewport
{
public:
	FDisplayClusterViewport(FDisplayClusterViewportManager& Owner, const FString& ViewportId, TSharedPtr<IDisplayClusterProjectionPolicy> InProjectionPolicy);
	
	virtual ~FDisplayClusterViewport() = default;

public:
	//////////////////////////////////////////////////////
	/// IDisplayClusterViewport
	//////////////////////////////////////////////////////
	virtual FString GetId() const override
	{ 
		check(IsInGameThread());
		return ViewportId; 
	}

	virtual FDisplayClusterViewport_RenderSettings& GetRenderSettings() override
	{
		check(IsInGameThread());
		return RenderSettings;
	}

	virtual FDisplayClusterViewport_RenderSettingsICVFX& GetRenderSettingsICVFX() override
	{
		check(IsInGameThread());
		return RenderSettingsICVFX;
	}

	virtual FDisplayClusterViewport_PostRenderSettings& GetPostRenderSettings() override
	{
		check(IsInGameThread());
		return PostRenderSettings;
	}

	virtual const FDisplayClusterViewport_RenderSettings& GetRenderSettings() const override
	{
		check(IsInGameThread());
		return RenderSettings;
	}

	virtual const FDisplayClusterViewport_RenderSettingsICVFX& GetRenderSettingsICVFX() const override
	{
		check(IsInGameThread());
		return RenderSettingsICVFX;
	}

	virtual const FDisplayClusterViewport_PostRenderSettings& GetPostRenderSettings() const override
	{
		check(IsInGameThread());
		return PostRenderSettings;
	}

	virtual const TSharedPtr<IDisplayClusterProjectionPolicy>& GetProjectionPolicy() const override
	{
		check(IsInGameThread());
		return ProjectionPolicy;
	}

	virtual TArray<FDisplayClusterViewport_Context>& GetContexts() override
	{
		check(IsInGameThread());
		return Contexts;
	}

	virtual const TArray<FDisplayClusterViewport_Context>& GetContexts() const override
	{
		check(IsInGameThread());
		return Contexts;
	}

	// Override postprocess settings for this viewport
	virtual       IDisplayClusterViewport_CustomPostProcessSettings& GetViewport_CustomPostProcessSettings() override
	{
		check(IsInGameThread());
		return CustomPostProcessSettings;
	}
	virtual const IDisplayClusterViewport_CustomPostProcessSettings& GetViewport_CustomPostProcessSettings() const override
	{
		check(IsInGameThread());
		return CustomPostProcessSettings;
	}

	// Setup scene view for rendering specified Context
	virtual void SetupSceneView(uint32 ContextNum, class UWorld* World, FSceneViewFamily& InViewFamily, FSceneView& InView) const override;

	virtual IDisplayClusterViewportManager& GetOwner() const override;

	//////////////////////////////////////////////////////
	/// ~IDisplayClusterViewport
	//////////////////////////////////////////////////////

#if WITH_EDITOR
	FSceneView* ImplCalcScenePreview(class FSceneViewFamilyContext& InOutViewFamily, uint32 ContextNum);
	bool    ImplPreview_CalculateStereoViewOffset(const uint32 InContextNum, FRotator& ViewRotation, const float WorldToMeters, FVector& ViewLocation);
	FMatrix ImplPreview_GetStereoProjectionMatrix(const uint32 InContextNum);
#endif //WITH_EDITOR

	// Get from logic request for additional targetable resource
	bool ShouldUseAdditionalTargetableResource() const;

	inline bool FindContext(const enum EStereoscopicPass StereoPassType, uint32* OutContextNum)
	{
		check(IsInGameThread());

		for (int ContextNum = 0; ContextNum < Contexts.Num(); ContextNum++)
		{
			if (StereoPassType == Contexts[ContextNum].StereoscopicPass)
			{
				if (OutContextNum != nullptr)
				{
					*OutContextNum = ContextNum;
				}

				return true;
			}
		}

		return false;
	}

	bool HandleStartScene();
	void HandleEndScene();

	void UpdateViewExtensions(FViewport* InViewport);
	bool UpdateFrameContexts(const uint32 InViewPassNum, const FDisplayClusterRenderFrameSettings& InFrameSettings);

	// Send viewport data to render thread scene proxy
	void UpdateSceneProxyData();

public:
	// Support OCIO:
	FSceneViewExtensionIsActiveFunctor GetSceneViewExtensionIsActiveFunctor() const;

	// OCIO wrapper
	TSharedPtr<FOpenColorIODisplayExtension, ESPMode::ThreadSafe> OpenColorIODisplayExtension;

public:
	// Active view extension for this viewport
	TArray<TSharedRef<class ISceneViewExtension, ESPMode::ThreadSafe>> ViewExtensions;

	// Projection policy instance that serves this viewport
	TSharedPtr<IDisplayClusterProjectionPolicy> ProjectionPolicy;
	TSharedPtr<IDisplayClusterProjectionPolicy> UninitializedProjectionPolicy;

	// Game thread only settings:
	FDisplayClusterViewport_CustomPostProcessSettings CustomPostProcessSettings;
	FDisplayClusterViewport_VisibilitySettings        VisibilitySettings;

	// Additional features:
	FImplDisplayClusterViewport_CameraMotionBlur CameraMotionBlur;

private:
	friend FDisplayClusterViewportProxy;
	friend FDisplayClusterViewportProxy_ExchangeContainer;
	friend FDisplayClusterViewportManager;
	friend FDisplayClusterRenderTargetManager;
	friend FDisplayClusterRenderFrameManager;

	// viewport render thread data
	FDisplayClusterViewportProxy* ViewportProxy = nullptr;

	// Unique viewport name
	const FString ViewportId;

	// Viewport render params
	FDisplayClusterViewport_RenderSettings       RenderSettings;
	FDisplayClusterViewport_RenderSettingsICVFX  RenderSettingsICVFX;
	FDisplayClusterViewport_PostRenderSettings   PostRenderSettings;

	// Viewport contexts (left/center/right eyes)
	TArray<FDisplayClusterViewport_Context> Contexts;

	// View family render to this resources
	TArray<FDisplayClusterRenderTargetResource*> RenderTargets;
	
	// Projection policy output resources
	TArray<FDisplayClusterTextureResource*> OutputFrameTargetableResources;
	TArray<FDisplayClusterTextureResource*> AdditionalFrameTargetableResources;

	// unique viewport resources
	TArray<FDisplayClusterTextureResource*> InputShaderResources;
	TArray<FDisplayClusterTextureResource*> AdditionalTargetableResources;
	TArray<FDisplayClusterTextureResource*> MipsShaderResources;

	FDisplayClusterViewportManager& Owner;
};
