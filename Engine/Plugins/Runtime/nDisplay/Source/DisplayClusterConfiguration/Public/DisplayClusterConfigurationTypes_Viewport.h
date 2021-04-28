// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "DisplayClusterConfigurationStrings.h"
#include "DisplayClusterConfigurationTypes_PostRender.h"

#include "OpenColorIOColorSpace.h"
#include "Engine/Scene.h"

#include "DisplayClusterConfigurationTypes_Viewport.generated.h"

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationViewport_CustomPostprocessSettings
{
	GENERATED_BODY()

public:
	// Enable custom postprocess
	UPROPERTY(EditAnywhere, Category = "Display Cluster Viewport")
	bool bIsEnabled = false;

	// Apply postprocess for one frame
	UPROPERTY(EditAnywhere, Category = "Display Cluster Viewport")
	bool bIsOneFrame = false;

	// Custom postprocess settings
	UPROPERTY(EditAnywhere, Category = "Display Cluster Viewport")
	FPostProcessSettings PostProcessSettings;

	// Override blend weight
	UPROPERTY(EditAnywhere, Category = "Display Cluster Viewport")
	float BlendWeight = 1;
};

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationViewport_CustomPostprocess
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FDisplayClusterConfigurationViewport_CustomPostprocessSettings Start;

	UPROPERTY()
	FDisplayClusterConfigurationViewport_CustomPostprocessSettings Override;

	UPROPERTY()
	FDisplayClusterConfigurationViewport_CustomPostprocessSettings Final;
};

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationViewport_Overscan
{
	GENERATED_BODY()

public:
	// Allow Render overscan
	UPROPERTY(EditAnywhere, Category = "Display Cluster Viewport")
	EDisplayClusterConfigurationViewportOverscanMode Mode = EDisplayClusterConfigurationViewportOverscanMode::None;

	UPROPERTY(EditAnywhere, Category = "Display Cluster Viewport")
	float Left = 0;

	UPROPERTY(EditAnywhere, Category = "Display Cluster Viewport")
	float Right = 0;

	UPROPERTY(EditAnywhere, Category = "Display Cluster Viewport")
	float Top  = 0;

	UPROPERTY(EditAnywhere, Category = "Display Cluster Viewport")
	float Bottom = 0;

	// If true (increased RTT size, same image quality) - increase the RTT size from overscan_pixel or overscan_percent (of actual size)
	// If false (same RTT size, loss of image quality) - use the original viewport size to render overscan, but finally the small inner rectangle copy into the frame 
	UPROPERTY(EditAnywhere, Category = "Display Cluster Viewport")
	bool bOversize = true;
};



USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationViewport_ICVFX
{
	GENERATED_BODY()

public:
	// Allow use ICVFX for this viewport (Must be supported by projection policy)
	UPROPERTY(EditAnywhere, Category = "Display Cluster Viewport")
	bool bAllowICVFX = true;

	// Disable incamera render to this viewport
	UPROPERTY(EditAnywhere, Category = "Display Cluster Viewport")
	EDisplayClusterConfigurationICVFX_OverrideCameraRenderMode CameraRenderMode = EDisplayClusterConfigurationICVFX_OverrideCameraRenderMode::Default;

	// Use unique lightcard mode for this viewport
	UPROPERTY(EditAnywhere, Category = "Display Cluster Viewport")
	EDisplayClusterConfigurationICVFX_OverrideLightcardRenderMode LightcardRenderMode = EDisplayClusterConfigurationICVFX_OverrideLightcardRenderMode::Default;
};

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationViewport_RenderSettings
{
	GENERATED_BODY()

public:
	// Allow ScreenPercentage 
	UPROPERTY(EditAnywhere, Category = "Display Cluster Viewport", meta = (ClampMin = "0.05", UIMin = "0.05", ClampMax = "10.0", UIMax = "10.0"))
	float BufferRatio = 1;

	// Experimental: Overscan rendering
	UPROPERTY(EditAnywhere, Category = "Display Cluster Viewport")
	FDisplayClusterConfigurationViewport_Overscan Overscan;

	UPROPERTY()
	FDisplayClusterConfigurationViewport_CustomPostprocess CustomPostprocess;

	// Override viewport render from source texture
	UPROPERTY(EditAnywhere, Category = "Display Cluster Viewport")
	FDisplayClusterConfigurationPostRender_Override Override;

	// Add postprocess blur to viewport
	UPROPERTY(EditAnywhere, Category = "Display Cluster Viewport")
	FDisplayClusterConfigurationPostRender_BlurPostprocess PostprocessBlur;

	// Generate Mips texture for this viewport (used, only if projection policy supports this feature)
	UPROPERTY(EditAnywhere, Category = "Display Cluster Viewport")
	FDisplayClusterConfigurationPostRender_GenerateMips GenerateMips;

	// Performance: force monoscopic render, resolved to stereo viewport
	UPROPERTY(EditAnywhere, Category = "Display Cluster Viewport")
	EDisplayClusterConfigurationViewport_StereoMode StereoMode = EDisplayClusterConfigurationViewport_StereoMode::Default;

	// Performance, Multi-GPU: Customize GPU for stereo mode second view (EYE_RIGHT GPU)
	UPROPERTY(EditAnywhere, Category = "Display Cluster Viewport")
	int StereoGPUIndex = -1;

	// Performance: Render to scale RTT, resolved with shader to viewport (Custom value)
	UPROPERTY(EditAnywhere, Category = "Display Cluster Viewport")
	float RenderTargetRatio = 1;

	// Experimental: Support special frame builder mode - merge viewports to single viewfamily by group num
	// [not implemented yet]
	UPROPERTY(EditAnywhere, Category = "Display Cluster Viewport")
	int RenderFamilyGroup = -1;
};

UCLASS()
class DISPLAYCLUSTERCONFIGURATION_API UDisplayClusterConfigurationViewport
	: public UDisplayClusterConfigurationData_Base
{
	GENERATED_BODY()
public:

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPostEditChangeChainProperty, const FPropertyChangedChainEvent&);

	FOnPostEditChangeChainProperty OnPostEditChangeChainProperty;

public:
	UDisplayClusterConfigurationViewport();

public:
	void GetReferencedMeshNames(TArray<FString>& OutMeshNames) const;

private:
#if WITH_EDITOR
	// UObject interface
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
	// End of UObject interface
#endif

public:
	// MultiUser : control this viewport rendering
	UPROPERTY(EditAnywhere, Category = "Display Cluster Viewport")
	bool bAllowRendering = true;

	UPROPERTY(EditAnywhere, Category = "Display Cluster Viewport")
	FString Camera;

	UPROPERTY(EditDefaultsOnly, Category = "Display Cluster Viewport")
	FDisplayClusterConfigurationProjection ProjectionPolicy;

	UPROPERTY(EditAnywhere, Category = "Display Cluster Viewport")
	FDisplayClusterConfigurationRectangle Region;

	// Performance, Multi-GPU: Asign GPU for viewport rendering. The Value '-1' used to default gpu mapping (EYE_LEFT and EYE_RIGHT GPU)
	UPROPERTY(EditAnywhere, Category = "Display Cluster Viewport")
	int GPUIndex = -1;

	// Viewport can overlap each other on backbuffer. This value uses to sorting order
	UPROPERTY(EditAnywhere, Category = "Display Cluster Viewport")
	int OverlapOrder = 0;

	// Configure render for this viewport
	UPROPERTY(EditAnywhere, Category = "Display Cluster Viewport")
	FDisplayClusterConfigurationViewport_RenderSettings RenderSettings;

	// Configure ICVFX for this viewport
	UPROPERTY(EditAnywhere, Category = "Display Cluster Viewport")
	FDisplayClusterConfigurationViewport_ICVFX ICVFX;

	// OCIO Display look configuration 
	UPROPERTY(EditAnywhere, Category = "Display Cluster Viewport")
	FOpenColorIODisplayConfiguration OCIO_Configuration;

	UPROPERTY(EditAnywhere, Category = "Display Cluster Viewport")
	bool bIsShared = false;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditDefaultsOnly, Category = "nDisplay", meta = (nDisplayHidden))
	bool bIsEnabled = true;

	UPROPERTY(EditAnywhere, Category = "Display Cluster Viewport")
	bool bFixedAspectRatio;

	UPROPERTY(EditDefaultsOnly, Category = "nDisplay", meta = (nDisplayHidden))
	bool bIsVisible = true;
#endif
};

// This struct now stored in UDisplayClusterConfigurationData, and replicated with MultiUser
UCLASS(ClassGroup = (DisplayCluster), meta = (BlueprintSpawnableComponent))
class DISPLAYCLUSTERCONFIGURATION_API UDisplayClusterConfigurationRenderFrame
	: public UActorComponent
{
	GENERATED_BODY()

public:
	UDisplayClusterConfigurationRenderFrame()
	{};

public:
	// Performance: Allow change global MGPU settings
	UPROPERTY(EditAnywhere, Category = "Display Cluster Render")
	EDisplayClusterConfigurationRenderMGPUMode MultiGPUMode = EDisplayClusterConfigurationRenderMGPUMode::Enabled;

	// Performance: Allow merge multiple viewports on single RTT with atlasing (required for bAllowViewFamilyMergeOptimization)
	UPROPERTY(EditAnywhere, Category = "Display Cluster Render")
	bool bAllowRenderTargetAtlasing = false;

	// Performance: Allow viewfamily merge optimization (render multiple viewports contexts within single family)
	// [not implemented yet] Experimental
	UPROPERTY(EditAnywhere, Category = "Display Cluster Render")
	EDisplayClusterConfigurationRenderFamilyMode ViewFamilyMode = EDisplayClusterConfigurationRenderFamilyMode::None;

	// Performance: Allow to use parent ViewFamily from parent viewport 
	// (icvfx has child viewports: lightcard and chromakey with prj_view matrices copied from parent viewport. May sense to use same viewfamily?)
	// [not implemented yet] Experimental
	UPROPERTY(EditAnywhere, Category = "Display Cluster Render")
	bool bShouldUseParentViewportRenderFamily = false;

	// Multiply all downscale ratio inside all viewports settings for whole cluster
	UPROPERTY(EditAnywhere, Category = "Display Cluster Render", meta = (ClampMin = "0.05", UIMin = "0.05", ClampMax = "10", UIMax = "10"))
	float ClusterRenderTargetRatioMult = 1.f;

	// Multiply all buffer ratios for whole cluster by this value
	UPROPERTY(EditAnywhere, Category = "Display Cluster Render", meta = (ClampMin = "0.05", UIMin = "0.05", ClampMax = "10", UIMax = "10"))
	float ClusterBufferRatioMult = 1.f;

	// Allow warpblend render
	UPROPERTY(EditAnywhere, Category = "Display Cluster Render")
	bool bAllowWarpBlend = true;
};

UCLASS(ClassGroup = (DisplayCluster), meta = (BlueprintSpawnableComponent))
class DISPLAYCLUSTERCONFIGURATION_API UDisplayClusterConfigurationViewportPreview
	: public UDisplayClusterConfigurationData_Base
{
	GENERATED_BODY()

public:
	UDisplayClusterConfigurationViewportPreview();

public:
	// Allow preview render
	UPROPERTY(EditAnywhere, Category = "Display Cluster Preview (Editor only)")
	bool bEnable = true;

	// Render single node preview or whole cluster
	UPROPERTY(EditAnywhere, Category = "Display Cluster Preview (Editor only)")
	FString PreviewNodeId = DisplayClusterConfigurationStrings::gui::preview::PreviewNodeAll;

	// Update preview texture period in tick
	UPROPERTY(EditAnywhere, Category = "Display Cluster Preview (Editor only)", meta = (ClampMin = "1", UIMin = "1", ClampMax = "200", UIMax = "200"))
	int TickPerFrame = 1;

	// Preview texture size get from viewport, and scaled by this value
	UPROPERTY(EditAnywhere, Category = "Display Cluster Preview (Editor only)", meta = (ClampMin = "0.05", UIMin = "0.05", ClampMax = "1", UIMax = "1"))
	float PreviewRenderTargetRatioMult = 0.25;
};

