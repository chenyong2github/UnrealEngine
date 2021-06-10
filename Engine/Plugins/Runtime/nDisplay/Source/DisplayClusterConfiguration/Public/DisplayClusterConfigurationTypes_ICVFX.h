// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "Components/ActorComponent.h"
#include "ActorLayerUtilities.h"

#include "DisplayClusterConfigurationTypes_PostRender.h"
#include "DisplayClusterConfigurationTypes_Postprocess.h"
#include "DisplayClusterConfigurationTypes_OCIO.h"

#include "DisplayClusterConfigurationTypes_ICVFX.generated.h"

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_VisibilityList
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = NDisplay)
	TArray<FActorLayer> ActorLayers;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	TArray<TSoftObjectPtr<AActor>> Actors;

	//@todo change link, now by names
	// Reference to RootActor components by names
	UPROPERTY(EditAnywhere, Category = NDisplay)
	TArray<FString> RootActorComponentNames;
};

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_CustomSize
{
	GENERATED_BODY()

public:
	// Use custom size
	UPROPERTY(EditAnywhere, Category = NDisplay)
	bool bUseCustomSize = false;

	// Used when enabled "bUseCustomSize"
	UPROPERTY(EditAnywhere, Category = NDisplay, meta = (ClampMin = "32", UIMin = "32"))
	int CustomWidth = 2560;

	// Used when enabled "bUseCustomSize"
	UPROPERTY(EditAnywhere, Category = NDisplay, meta = (ClampMin = "32", UIMin = "32"))
	int CustomHeight = 1440;
};

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_Size
{
	GENERATED_BODY()

public:
	// Viewport width in pixels
	UPROPERTY(EditAnywhere, Category = NDisplay, meta = (ClampMin = "32", UIMin = "32"))
	int Width = 2560;

	// Viewport height  in pixels
	UPROPERTY(EditAnywhere, Category = NDisplay, meta = (ClampMin = "32", UIMin = "32"))
	int Height = 1440;
};

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_OverlayAdvancedRenderSettings
{
	GENERATED_BODY()

public:
	// Allow ScreenPercentage 
	UPROPERTY(EditAnywhere, Category = NDisplay, meta = (ClampMin = "0.05", UIMin = "0.05", ClampMax = "10.0", UIMax = "10.0"))
	float BufferRatio = 1;

	// Performance: Render to scale RTT, resolved with shader to viewport (Custom value)
	UPROPERTY(EditAnywhere, Category = NDisplay, meta = (ClampMin = "0.01", UIMin = "0.01", ClampMax = "1.0", UIMax = "1.0"))
	float RenderTargetRatio = 1;

	// Performance, Multi-GPU: Asign GPU for viewport rendering. The Value '-1' used to default gpu mapping (EYE_LEFT and EYE_RIGHT GPU)
	UPROPERTY(EditAnywhere, Category = NDisplay)
	int GPUIndex = -1;

	// Performance, Multi-GPU: Customize GPU for stereo mode second view (EYE_RIGHT GPU)
	UPROPERTY(EditAnywhere, Category = NDisplay)
	int StereoGPUIndex = -1;

	// Performance: force monoscopic render, resolved to stereo viewport
	UPROPERTY(EditAnywhere, Category = NDisplay)
	EDisplayClusterConfigurationViewport_StereoMode StereoMode = EDisplayClusterConfigurationViewport_StereoMode::Default;

	// Experimental: Support special frame builder mode - merge viewports to single viewfamily by group num
	// [not implemented yet]
	UPROPERTY()
	int RenderFamilyGroup = -1;
};

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_ChromakeyRenderSettings
{
	GENERATED_BODY()

public:
	// Render chromakey actors from ShowOnlyList into texture
	UPROPERTY(EditAnywhere, Category = NDisplay)
	bool bEnable = false;

	// Debug: override the texture of the camera viewport from this chromakey RTT
	UPROPERTY(EditAnywhere, Category = NDisplay)
	bool bOverrideCameraViewport = false;

	// Performance: Use custom size (low-res) for chromakey RTT frame. Default size same as camera frame
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationICVFX_CustomSize CustomSize;

	// Render actors from this layers to chromakey texture
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationICVFX_VisibilityList ShowOnlyList;

	// Override viewport render from source texture
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationPostRender_Override Override;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationPostRender_BlurPostprocess PostprocessBlur;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationPostRender_GenerateMips GenerateMips;

	// Advanced render settings
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationICVFX_OverlayAdvancedRenderSettings AdvancedRenderSettings;
};

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_ChromakeyMarkers
{
	GENERATED_BODY()

public:
	FDisplayClusterConfigurationICVFX_ChromakeyMarkers()
		//@todo: need to approve this default color for chromakey markers
		// Default chromakey marker color is (0,64,0)
		: MarkerColor(0,0.25f,0)
	{ }

public:
	// Allow shromakey markers rendering (Also require not empty MarkerTileRGBA)
	UPROPERTY(EditAnywhere, Category = NDisplay)
	bool bEnable = true;

	// Color of chromakey marker
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FLinearColor MarkerColor;

	// (*required) This texture must by tiled in both directions. Alpha channel used to composing
	UPROPERTY(EditAnywhere, Category = NDisplay)
	UTexture2D* MarkerTileRGBA = nullptr;

	// Scale markers UV source
	UPROPERTY(EditAnywhere, Category = NDisplay)
	float MarkerTileScale = 1;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	float MarkerTileDistance = 0;
};

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_ChromakeySettings
{
	GENERATED_BODY()

public:
	FDisplayClusterConfigurationICVFX_ChromakeySettings()
		// Default chromakey color is (0,128,0)
		: ChromakeyColor(0, 0.5f, 0)
	{ }

public:
	// Allow chromakey rendering
	UPROPERTY(EditAnywhere, Category = NDisplay)
	bool bEnable = false;

	// Color of chromakey
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FLinearColor ChromakeyColor;

	// Settings for chromakey texture source rendering
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationICVFX_ChromakeyRenderSettings ChromakeyRenderTexture;

	// Global setup for chromakey markers rendering
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationICVFX_ChromakeyMarkers ChromakeyMarkers;
};

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_LightcardRenderSettings
{
	GENERATED_BODY()

public:
	// Debug: override the texture of the target viewport from this lightcard RTT
	UPROPERTY(EditAnywhere, Category = NDisplay)
	bool bOverrideViewport = false;

	// Override viewport render from source texture
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationPostRender_Override Override;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationPostRender_BlurPostprocess PostprocessBlur;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationPostRender_GenerateMips GenerateMips;

	// Advanced render settings
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationICVFX_OverlayAdvancedRenderSettings AdvancedRenderSettings;
};

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_LightcardSettings
{
	GENERATED_BODY()

public:
	// Allow lightcard rendering (also require not empty LightCardLayers)
	UPROPERTY(EditAnywhere, Category = NDisplay, meta = (DisplayName = "Enable Light Cards"))
	bool bEnable = true;

	// Global lighcard rendering mode
	UPROPERTY(EditAnywhere, Category = NDisplay, meta = (DisplayName = "Blending Mode"))
	EDisplayClusterConfigurationICVFX_LightcardRenderMode Blendingmode = EDisplayClusterConfigurationICVFX_LightcardRenderMode::Under;

	// Render actors from this layers to lightcard textures
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationICVFX_VisibilityList ShowOnlyList;

	// Configure global render settings for this viewports
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationICVFX_LightcardRenderSettings RenderSettings;

	// OCIO Display look configuration 
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FOpenColorIODisplayConfiguration OCIO_Configuration;
};

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_CameraAdvancedRenderSettings
{
	GENERATED_BODY()

public:
	// Performance: Render to scale RTT, resolved with shader to viewport (Custom value)
	UPROPERTY(EditAnywhere, Category = NDisplay, meta = (ClampMin = "0.01", UIMin = "0.01", ClampMax = "1.0", UIMax = "1.0"))
	float RenderTargetRatio = 1;

	// Performance, Multi-GPU: Asign GPU for viewport rendering. The Value '-1' used to default gpu mapping (EYE_LEFT and EYE_RIGHT GPU)
	UPROPERTY(EditAnywhere, Category = NDisplay)
	int GPUIndex = -1;

	// Performance, Multi-GPU: Customize GPU for stereo mode second view (EYE_RIGHT GPU)
	UPROPERTY(EditAnywhere, Category = NDisplay)
	int StereoGPUIndex = -1;

	// Performance: force monoscopic render, resolved to stereo viewport
	UPROPERTY(EditAnywhere, Category = NDisplay)
	EDisplayClusterConfigurationViewport_StereoMode StereoMode = EDisplayClusterConfigurationViewport_StereoMode::Default;

	// Experimental: Support special frame builder mode - merge viewports to single viewfamily by group num
	// [not implemented yet]
	UPROPERTY()
	int RenderFamilyGroup = -1;
};

USTRUCT(BlueprintType)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_CameraRenderSettings
{
	GENERATED_BODY()

public:
	// Define custom inner camera viewport size
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationICVFX_CustomSize CustomFrameSize;

	// Camera render order, bigger value is over
	UPROPERTY(EditAnywhere, Category = NDisplay)
	int RenderOrder = -1;

	UPROPERTY()
	FDisplayClusterConfigurationViewport_CustomPostprocess CustomPostprocess;

	// Use postprocess settings from camera component
	UPROPERTY(EditAnywhere, Category = NDisplay)
	bool bUseCameraComponentPostprocess = false;

	// Override viewport render from source texture
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationPostRender_Override Override;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationPostRender_BlurPostprocess PostprocessBlur;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationPostRender_GenerateMips GenerateMips;

	// Advanced render settings
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationICVFX_CameraAdvancedRenderSettings AdvancedRenderSettings;
};

USTRUCT(BlueprintType)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_CameraMotionBlurOverridePPS
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = NDisplay)
	bool bOverrideEnable = false;

	/** Strength of motion blur, 0:off, should be renamed to intensity */
	UPROPERTY(interp, BlueprintReadWrite, Category = NDisplay, meta = (ClampMin = "0.0", ClampMax = "1.0", editcondition = "bOverrideEnable", DisplayName = "Amount"))
	float MotionBlurAmount = 1;

	/** max distortion caused by motion blur, in percent of the screen width, 0:off */
	UPROPERTY(interp, BlueprintReadWrite, Category = NDisplay, meta = (ClampMin = "0.0", ClampMax = "100.0", editcondition = "bOverrideEnable", DisplayName = "Max"))
	float MotionBlurMax = 50;

	/** The minimum projected screen radius for a primitive to be drawn in the velocity pass, percentage of screen width. smaller numbers cause more draw calls, default: 4% */
	UPROPERTY(interp, BlueprintReadWrite, Category = NDisplay, meta = (ClampMin = "0.0", UIMax = "100.0", editcondition = "bOverrideEnable", DisplayName = "Per Object Size"))
	float MotionBlurPerObjectSize = 4;
};

USTRUCT(BlueprintType)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_CameraMotionBlur
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = NDisplay)
	EDisplayClusterConfigurationCameraMotionBlurMode MotionBlurMode = EDisplayClusterConfigurationCameraMotionBlurMode::Off;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = NDisplay)
	float TranslationScale = 1.f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationICVFX_CameraMotionBlurOverridePPS OverrideMotionBlurPPS;

	// GUI: Add ext camera refs
};

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_CameraPostProcessSettings
{
	GENERATED_BODY()

	// Exclude this viewport from the global cluster post process
	UPROPERTY(EditAnywhere, Category = "PostProcess Settings")
	bool bExcludeFromOverallClusterPostProcess = true;

	// Allow using a separate post process for this viewport
	UPROPERTY(EditAnywhere, Category = "PostProcess Settings")
	bool bIsEnabled = false;

	// Post process settings
	UPROPERTY(EditAnywhere, Category = "PostProcess Settings", Meta = (EditCondition = "bIsEnabled"))
	FDisplayClusterConfigurationViewport_PerViewportSettings ViewportSettings;
};

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_CameraSoftEdge
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = NDisplay, meta = (ClampMin = "0.0", UIMin = "0.0", ClampMax = "1.0", UIMax = "1.0"))
	float Vertical = 0.f;

	UPROPERTY(EditAnywhere, Category = NDisplay, meta = (ClampMin = "0.0", UIMin = "0.0", ClampMax = "1.0", UIMax = "1.0"))
	float Horizontal = 0.f;
};

USTRUCT(BlueprintType)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_CameraSettings
{
	GENERATED_BODY()

public:
	// Enable this camera
	UPROPERTY(EditAnywhere, Category = NDisplay, meta = (DisplayName = "Enable Inner Frustum"))
	bool bEnable = true;

	// Allow ScreenPercentage, for values!=1
	UPROPERTY(EditAnywhere, Category = NDisplay, meta = (DisplayName = "Inner Frustum Screen Percentage", ClampMin = "0.05", UIMin = "0.05", ClampMax = "10.0", UIMax = "10.0"))
	float BufferRatio = 1;

	UPROPERTY(EditAnywhere, Category = NDisplay, meta = (ClampMin = "0.05", UIMin = "0.05", ClampMax = "5.0", UIMax = "5.0"))
	float FieldOfViewMultiplier = 1.0f;

	// Basic soft edges setup for incamera
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationICVFX_CameraSoftEdge SoftEdge;

	// Rotate incamera frustum on this value to fix broken lens on physic camera
	UPROPERTY(EditAnywhere, Category = NDisplay, meta = (DisplayName = "Inner Frustum Rotation"))
	FRotator  FrustumRotation = FRotator::ZeroRotator;

	// Move incamera frustum on this value to fix broken lens on physic camera
	UPROPERTY(EditAnywhere, Category = NDisplay, meta = (DisplayName = "Inner Frustum Offset"))
	FVector FrustumOffset = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationICVFX_CameraMotionBlur CameraMotionBlur;

	// Configure global render settings for this viewports
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationICVFX_CameraRenderSettings RenderSettings;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationICVFX_ChromakeySettings Chromakey;

	// Per viewport post processing for camera
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationICVFX_CameraPostProcessSettings PostProcessSettings;

	// OCIO Display look configuration for this camera
	UPROPERTY(EditAnywhere, Category = "OCIO")
	FOpenColorIODisplayConfiguration OCIO_Configuration;

	// Enable cluster node configuration for this camera
	UPROPERTY(EditInstanceOnly, Category = "OCIO", meta = (DisplayName = "Enable cluster nodes OCIO"))
	bool bEnableInnerFrustumOCIO = false;

	// Define special OCIO for cluster nodes for this camera
	UPROPERTY(EditInstanceOnly, Category = "OCIO", meta = (DisplayName = "Cluster nodes OCIO Configurations", ConfigurationMode = "ClusterNodes"))
	TArray<FDisplayClusterConfigurationOCIOProfile> InnerFrustumOCIOConfigurations;

	// Special hide list for this camera viewport
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationICVFX_VisibilityList CameraHideList;
};

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_StageSettings
{
	GENERATED_BODY()

public:
	FDisplayClusterConfigurationICVFX_StageSettings();

public:
	// Allow ICVFX features
	UPROPERTY(EditAnywhere, Category = NDisplay)
	bool bEnable = true;

	// Default incameras RTT texture size.
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationICVFX_Size DefaultFrameSize;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationICVFX_LightcardSettings Lightcard;

	// Hide list for all icvfx viewports (outer, inner, cameras, etc)
	// (This allow to hide all actors from layers for icvfx render logic)
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationICVFX_VisibilityList HideList;

	// Special hide list for Outer viewports
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationICVFX_VisibilityList OuterViewportHideList;
};

