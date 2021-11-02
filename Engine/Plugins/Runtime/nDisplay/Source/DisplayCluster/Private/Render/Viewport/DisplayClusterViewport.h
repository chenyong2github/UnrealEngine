// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Viewport/IDisplayClusterViewport.h"
#include "Render/Viewport/DisplayClusterViewport_CustomPostProcessSettings.h"
#include "Render/Viewport/DisplayClusterViewport_VisibilitySettings.h"
#include "Render/Viewport/DisplayClusterViewport_TextureShare.h"

#include "Render/Viewport/Containers/ImplDisplayClusterViewport_CameraMotionBlur.h"
#include "Render/Viewport/Containers/ImplDisplayClusterViewport_Overscan.h"

#include "SceneViewExtensionContext.h"
#include "OpenColorIODisplayExtension.h"

class FDisplayClusterRenderTargetResource;
class FDisplayClusterTextureResource;
class FDisplayClusterViewportManager;
class FDisplayClusterRenderTargetManager;
class FDisplayClusterRenderFrameManager;
class FDisplayClusterViewportProxyData;
class FDisplayClusterViewportProxy;
struct FDisplayClusterRenderFrameSettings;
class FDisplayClusterViewportConfigurationCameraViewport;
class FDisplayClusterViewportConfigurationCameraICVFX;
class FDisplayClusterViewportConfigurationICVFX;

class FDisplayClusterViewportConfigurationHelpers;
class FDisplayClusterViewportConfigurationHelpers_ICVFX;
class FDisplayClusterViewportConfigurationHelpers_OpenColorIO;
class FDisplayClusterViewportConfigurationHelpers_Postprocess;

struct FDisplayClusterViewportConfigurationProjectionPolicy;

/**
 * Rendering viewport (sub-region of the main viewport)
 */

class FDisplayClusterViewport
	: public IDisplayClusterViewport
{
public:
	FDisplayClusterViewport(FDisplayClusterViewportManager& Owner, const FString& ViewportId, const TSharedPtr<IDisplayClusterProjectionPolicy, ESPMode::ThreadSafe>& InProjectionPolicy);
	
	virtual ~FDisplayClusterViewport();

public:
	//////////////////////////////////////////////////////
	/// IDisplayClusterViewport
	//////////////////////////////////////////////////////
	virtual FString GetId() const override
	{ 
		check(IsInGameThread());
		return ViewportId; 
	}

	virtual const FDisplayClusterViewport_RenderSettings& GetRenderSettings() const override
	{
		check(IsInGameThread());
		return RenderSettings;
	}

	virtual void CalculateProjectionMatrix(const uint32 InContextNum, float Left, float Right, float Top, float Bottom, float ZNear, float ZFar, bool bIsAnglesInput) override;

	virtual bool    CalculateView(const uint32 InContextNum, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float NCP, const float FCP) override;
	virtual bool    GetProjectionMatrix(const uint32 InContextNum, FMatrix& OutPrjMatrix)  override;

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

	virtual const TSharedPtr<IDisplayClusterProjectionPolicy, ESPMode::ThreadSafe>& GetProjectionPolicy() const override
	{
		check(IsInGameThread());
		return ProjectionPolicy;
	}


	virtual const TArray<FDisplayClusterViewport_Context>& GetContexts() const override
	{
		check(IsInGameThread());
		return Contexts;
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

	FMatrix ImplCreateProjectionMatrix(float InLeft, float InRight, float InTop, float InBottom, float ZNear, float ZFar) const;

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

	void ResetRuntimeParameters()
	{
		// Reset runtim flags from prev frame:
		RenderSettings.BeginUpdateSettings();
		RenderSettingsICVFX.BeginUpdateSettings();
		PostRenderSettings.BeginUpdateSettings();
		VisibilitySettings.ResetConfiguration();
		CameraMotionBlur.ResetConfiguration();
		OverscanRendering.ResetConfiguration();
	}


	// Active view extension for this viewport
	const TArray<FSceneViewExtensionRef> GatherActiveExtensions(FViewport* InViewport) const;

	bool UpdateFrameContexts(const uint32 InViewPassNum, const FDisplayClusterRenderFrameSettings& InFrameSettings);

	void ImplReleaseOpenColorIODisplayExtension();

public:
	FIntRect GetValidRect(const FIntRect& InRect, const TCHAR* DbgSourceName);

public:
	// Support OCIO:
	FSceneViewExtensionIsActiveFunctor GetSceneViewExtensionIsActiveFunctor() const;

	// OCIO wrapper
	TSharedPtr<FOpenColorIODisplayExtension, ESPMode::ThreadSafe> OpenColorIODisplayExtension;

	//TextureShare
	FDisplayClusterViewport_TextureShare TextureShare;

public:
	// Projection policy instance that serves this viewport
	TSharedPtr<IDisplayClusterProjectionPolicy, ESPMode::ThreadSafe> ProjectionPolicy;
	TSharedPtr<IDisplayClusterProjectionPolicy, ESPMode::ThreadSafe> UninitializedProjectionPolicy;

	// Game thread only settings:
	FDisplayClusterViewport_CustomPostProcessSettings CustomPostProcessSettings;
	FDisplayClusterViewport_VisibilitySettings        VisibilitySettings;

	// Additional features:
	FImplDisplayClusterViewport_CameraMotionBlur CameraMotionBlur;
	FImplDisplayClusterViewport_Overscan         OverscanRendering;

protected:
	friend FDisplayClusterViewportProxy;
	friend FDisplayClusterViewportProxyData;
	friend FDisplayClusterViewportManager;
	friend FDisplayClusterRenderTargetManager;
	friend FDisplayClusterRenderFrameManager;
	friend FDisplayClusterViewportConfigurationCameraViewport;
	friend FDisplayClusterViewportConfigurationCameraICVFX;
	friend FDisplayClusterViewport_TextureShare;

	friend FDisplayClusterViewportConfigurationICVFX;

	friend FDisplayClusterViewportConfigurationHelpers;
	friend FDisplayClusterViewportConfigurationHelpers_ICVFX;
	friend FDisplayClusterViewportConfigurationHelpers_OpenColorIO;
	friend FDisplayClusterViewportConfigurationHelpers_Postprocess;
	friend FDisplayClusterViewportConfigurationProjectionPolicy;

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

#if WITH_EDITOR
	FTextureRHIRef OutputPreviewTargetableResource;
#endif

	// unique viewport resources
	TArray<FDisplayClusterTextureResource*> InputShaderResources;
	TArray<FDisplayClusterTextureResource*> AdditionalTargetableResources;
	TArray<FDisplayClusterTextureResource*> MipsShaderResources;

	FDisplayClusterViewportManager& Owner;

private:
	bool bProjectionPolicyCalculateViewWarningOnce = false;
};
