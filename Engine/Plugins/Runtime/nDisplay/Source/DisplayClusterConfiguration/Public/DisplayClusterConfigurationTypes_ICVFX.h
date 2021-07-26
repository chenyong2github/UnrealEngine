// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "Components/ActorComponent.h"
#include "ActorLayerUtilities.h"

#include "CineCameraActor.h"

#include "DisplayClusterConfigurationTypes_PostRender.h"
#include "DisplayClusterConfigurationTypes_Postprocess.h"
#include "DisplayClusterConfigurationTypes_OCIO.h"

#include "DisplayClusterConfigurationTypes_ICVFX.generated.h"

USTRUCT(Blueprintable)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_VisibilityList
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (DisplayName = "Layers"))
	TArray<FActorLayer> ActorLayers;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay)
	TArray<TSoftObjectPtr<AActor>> Actors;

	//@todo change link, now by names
	// Reference to RootActor components by names
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = NDisplay)
	TArray<FString> RootActorComponentNames;
};

USTRUCT(Blueprintable)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_CustomSize
{
	GENERATED_BODY()

public:
	// Use custom size
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay)
	bool bUseCustomSize = false;

	// Used when enabled "bUseCustomSize"
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (ClampMin = "32", UIMin = "32", EditCondition = "bUseCustomSize"))
	int CustomWidth = 2560;

	// Used when enabled "bUseCustomSize"
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (ClampMin = "32", UIMin = "32", EditCondition = "bUseCustomSize"))
	int CustomHeight = 1440;
};

USTRUCT(Blueprintable)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_Size
{
	GENERATED_BODY()

public:
	// Viewport width in pixels
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (ClampMin = "32", UIMin = "32"))
	int Width = 2560;

	// Viewport height  in pixels
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (ClampMin = "32", UIMin = "32"))
	int Height = 1440;
};

USTRUCT(Blueprintable)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_OverlayAdvancedRenderSettings
{
	GENERATED_BODY()

public:
	// Allow ScreenPercentage 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (ClampMin = "0.05", UIMin = "0.05", ClampMax = "10.0", UIMax = "10.0"))
	float BufferRatio = 1;

	// Performance: Render to scale RTT, resolved with shader to viewport (Custom value)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (ClampMin = "0.01", UIMin = "0.01", ClampMax = "1.0", UIMax = "1.0"))
	float RenderTargetRatio = 1;

	// Performance, Multi-GPU: Asign GPU for viewport rendering. The Value '-1' used to default gpu mapping (EYE_LEFT and EYE_RIGHT GPU)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay)
	int GPUIndex = -1;

	// Performance, Multi-GPU: Customize GPU for stereo mode second view (EYE_RIGHT GPU)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay)
	int StereoGPUIndex = -1;

	// Performance: force monoscopic render, resolved to stereo viewport
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay)
	EDisplayClusterConfigurationViewport_StereoMode StereoMode = EDisplayClusterConfigurationViewport_StereoMode::Default;

	// Experimental: Support special frame builder mode - merge viewports to single viewfamily by group num
	// [not implemented yet]
	UPROPERTY()
	int RenderFamilyGroup = -1;
};

USTRUCT(Blueprintable)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_ChromakeyRenderSettings
{
	GENERATED_BODY()

public:
	// Render chromakey actors from ShowOnlyList into texture
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (DisplayName = "Use Custom Chromakey"))
	bool bEnable = false;

	// Replace the texture of the camera viewport from this chromakey RTT
	UPROPERTY(BlueprintReadWrite,Category = NDisplay, meta = (EditCondition = "bEnable"))
	bool bReplaceCameraViewport = false;

	// Performance: Use custom size (low-res) for chromakey RTT frame. Default size same as camera frame
	UPROPERTY(BlueprintReadWrite, Category = NDisplay, meta = (EditCondition = "bEnable"))
	FDisplayClusterConfigurationICVFX_CustomSize CustomSize;

	// Render actors from this layers to chromakey texture
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (EditCondition = "bEnable", DisplayName = "Custom Chromakey Content"))
	FDisplayClusterConfigurationICVFX_VisibilityList ShowOnlyList;

	// Replace viewport render from source texture
	UPROPERTY(BlueprintReadWrite, Category = NDisplay, meta = (EditCondition = "bEnable"))
	FDisplayClusterConfigurationPostRender_Override Replace;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (EditCondition = "bEnable", DisplayName = "Post Process Blur"))
	FDisplayClusterConfigurationPostRender_BlurPostprocess PostprocessBlur;

	UPROPERTY(BlueprintReadWrite, Category = NDisplay, meta = (EditCondition = "bEnable"))
	FDisplayClusterConfigurationPostRender_GenerateMips GenerateMips;

	// Advanced render settings
	UPROPERTY(BlueprintReadWrite, Category = NDisplay, meta = (EditCondition = "bEnable"))
	FDisplayClusterConfigurationICVFX_OverlayAdvancedRenderSettings AdvancedRenderSettings;
};

USTRUCT(Blueprintable)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_ChromakeyMarkers
{
	GENERATED_BODY()

public:
	FDisplayClusterConfigurationICVFX_ChromakeyMarkers()
		// Default chromakey marker color is (0,64,0)
		: MarkerColor(0,0.25f,0)
		, MarkerTileOffset(0)
	{ }

public:
	// Allow chromakey markers rendering (Also require not empty MarkerTileRGBA)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (DisplayName = "Enable Chromakey Markers"))
	bool bEnable = true;

	// Color of chromakey marker
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (EditCondition = "bEnable"))
	FLinearColor MarkerColor;

	// (*required) This texture must by tiled in both directions. Alpha channel used to composing
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (EditCondition = "bEnable"))
	UTexture2D* MarkerTileRGBA = nullptr;

	// Scale markers UV source
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (EditCondition = "bEnable", ClampMin = "0", UIMin = "0"))
	float MarkerSizeScale = 1;

	// Distance (or spacing) between tile markers
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (EditCondition = "bEnable", ClampMin = "0", UIMin = "0"))
	float MarkerTileDistance = 1;

	// 2D tile marker offset (normalized to the tile distance)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (EditCondition = "bEnable"))
	FVector2D MarkerTileOffset;
};

USTRUCT(Blueprintable)
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (DisplayName = "Enable Inner Frustum Chromakey"))
	bool bEnable = false;

	// Color of chromakey
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (EditCondition = "bEnable"))
	FLinearColor ChromakeyColor;

	// Settings for chromakey texture source rendering
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (EditCondition = "bEnable", DisplayName = "Custom Chromakey"))
	FDisplayClusterConfigurationICVFX_ChromakeyRenderSettings ChromakeyRenderTexture;

	// Global setup for chromakey markers rendering
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (EditCondition = "bEnable"))
	FDisplayClusterConfigurationICVFX_ChromakeyMarkers ChromakeyMarkers;
};

USTRUCT(Blueprintable)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_LightcardRenderSettings
{
	GENERATED_BODY()

public:
	// override the texture of the target viewport from this lightcard RTT
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay)
	bool bReplaceViewport = false;

	// Override viewport render from source texture
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay)
	FDisplayClusterConfigurationPostRender_Override Replace;

	UPROPERTY()
	FDisplayClusterConfigurationPostRender_BlurPostprocess PostprocessBlur;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay)
	FDisplayClusterConfigurationPostRender_GenerateMips GenerateMips;

	// Advanced render settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay)
	FDisplayClusterConfigurationICVFX_OverlayAdvancedRenderSettings AdvancedRenderSettings;
};

USTRUCT(Blueprintable)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_LightcardSettings
{
	GENERATED_BODY()

public:
	// Allow lightcard rendering (also require not empty LightCardLayers)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (DisplayName = "Enable Light Cards"))
	bool bEnable = true;

	// Global lighcard rendering mode
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (DisplayName = "Blending Mode", EditCondition = "bEnable"))
	EDisplayClusterConfigurationICVFX_LightcardRenderMode Blendingmode = EDisplayClusterConfigurationICVFX_LightcardRenderMode::Under;

	// Render actors from this layers to lightcard textures
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (EditCondition = "bEnable"))
	FDisplayClusterConfigurationICVFX_VisibilityList ShowOnlyList;

	// Configure global render settings for this viewports
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (EditCondition = "bEnable"))
	FDisplayClusterConfigurationICVFX_LightcardRenderSettings RenderSettings;

	// Enable using outer viewport OCIO from DCRA for lightcard rendering
	UPROPERTY()
	bool bEnableOuterViewportOCIO = false;

	// Enable using outer viewport Color Grading from DCRA for lightcard rendering
	UPROPERTY()
	bool bEnableOuterViewportColorGrading = false;
};

USTRUCT(Blueprintable)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_CameraAdvancedRenderSettings
{
	GENERATED_BODY()

public:
	// Performance: Render to scale RTT, resolved with shader to viewport (Custom value)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (ClampMin = "0.01", UIMin = "0.01", ClampMax = "1.0", UIMax = "1.0"))
	float RenderTargetRatio = 1;

	// Performance, Multi-GPU: Asign GPU for viewport rendering. The Value '-1' used to default gpu mapping (EYE_LEFT and EYE_RIGHT GPU)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay)
	int GPUIndex = -1;

	// Performance, Multi-GPU: Customize GPU for stereo mode second view (EYE_RIGHT GPU)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay)
	int StereoGPUIndex = -1;

	// Performance: force monoscopic render, resolved to stereo viewport
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay)
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
	FDisplayClusterConfigurationICVFX_CameraRenderSettings();

public:
	// Define custom inner camera viewport size
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay)
	FDisplayClusterConfigurationICVFX_CustomSize CustomFrameSize;

	// Camera render order, bigger value is over
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay)
	int RenderOrder = -1;

	UPROPERTY()
	FDisplayClusterConfigurationViewport_CustomPostprocess CustomPostprocess;

	// Use postprocess settings from camera component
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay)
	bool bUseCameraComponentPostprocess = true;

	// Replace viewport render from source texture
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (DisplayName = "Mipmapping"))
	FDisplayClusterConfigurationPostRender_Override Replace;

	UPROPERTY()
	FDisplayClusterConfigurationPostRender_BlurPostprocess PostprocessBlur;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (DisplayName = "Mipmapping"))
	FDisplayClusterConfigurationPostRender_GenerateMips GenerateMips;

	// Advanced render settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay)
	FDisplayClusterConfigurationICVFX_CameraAdvancedRenderSettings AdvancedRenderSettings;
};

USTRUCT(BlueprintType)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_CameraMotionBlurOverridePPS
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = NDisplay, meta = (DisplayName = "Enable Settings Replace"))
	bool bReplaceEnable = false;

	/** Strength of motion blur, 0:off, should be renamed to intensity */
	UPROPERTY(interp, BlueprintReadWrite, Category = NDisplay, meta = (ClampMin = "0.0", ClampMax = "1.0", editcondition = "bReplaceEnable", DisplayName = "Amount"))
	float MotionBlurAmount = 1;

	/** max distortion caused by motion blur, in percent of the screen width, 0:off */
	UPROPERTY(interp, BlueprintReadWrite, Category = NDisplay, meta = (ClampMin = "0.0", ClampMax = "100.0", editcondition = "bReplaceEnable", DisplayName = "Max"))
	float MotionBlurMax = 50;

	/** The minimum projected screen radius for a primitive to be drawn in the velocity pass, percentage of screen width. smaller numbers cause more draw calls, default: 4% */
	UPROPERTY(interp, BlueprintReadWrite, Category = NDisplay, meta = (ClampMin = "0.0", UIMax = "100.0", editcondition = "bReplaceEnable", DisplayName = "Per Object Size"))
	float MotionBlurPerObjectSize = 4;
};

USTRUCT(BlueprintType)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_CameraMotionBlur
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay)
	EDisplayClusterConfigurationCameraMotionBlurMode MotionBlurMode = EDisplayClusterConfigurationCameraMotionBlurMode::Override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay)
	float TranslationScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (DisplayName = "Motion Blur Settings"))
	FDisplayClusterConfigurationICVFX_CameraMotionBlurOverridePPS MotionBlurPPS;
};

USTRUCT(BlueprintType)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_CameraSoftEdge
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (ClampMin = "0.0", UIMin = "0.0", ClampMax = "1.0", UIMax = "1.0"))
	float Vertical = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (ClampMin = "0.0", UIMin = "0.0", ClampMax = "1.0", UIMax = "1.0"))
	float Horizontal = 0.f;
};

USTRUCT(BlueprintType)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_CameraSettings
{
	GENERATED_BODY()

	FDisplayClusterConfigurationICVFX_CameraSettings();

public:
	// Enable this camera
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (DisplayName = "Enable Inner Frustum"))
	bool bEnable = true;

	// Use external cine camera actor
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = NDisplay, meta = (DisplayName = "Cine Camera Actor", EditCondition = "bEnable"))
	TSoftObjectPtr<ACineCameraActor> ExternalCameraActor;

	// Allow ScreenPercentage, for values!=1
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (DisplayName = "Inner Frustum Screen Percentage", ClampMin = "0.05", UIMin = "0.05", ClampMax = "10.0", UIMax = "10.0", EditCondition = "bEnable"))
	float BufferRatio = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (ClampMin = "0.05", UIMin = "0.05", ClampMax = "5.0", UIMax = "5.0", EditCondition = "bEnable"))
	float FieldOfViewMultiplier = 1.0f;

	// Basic soft edges setup for incamera
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (EditCondition = "bEnable"))
	FDisplayClusterConfigurationICVFX_CameraSoftEdge SoftEdge;

	// Rotate incamera frustum on this value to fix broken lens on physic camera
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (DisplayName = "Inner Frustum Rotation", EditCondition = "bEnable"))
	FRotator  FrustumRotation = FRotator::ZeroRotator;

	// Move incamera frustum on this value to fix broken lens on physic camera
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (DisplayName = "Inner Frustum Offset", EditCondition = "bEnable"))
	FVector FrustumOffset = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, BlueprintReadWrite, EditAnywhere, Category = NDisplay, meta = (EditCondition = "bEnable"))
	FDisplayClusterConfigurationICVFX_CameraMotionBlur CameraMotionBlur;

	// Configure global render settings for this viewports
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (EditCondition = "bEnable"))
	FDisplayClusterConfigurationICVFX_CameraRenderSettings RenderSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (EditCondition = "bEnable"))
	FDisplayClusterConfigurationICVFX_ChromakeySettings Chromakey;

	// OCIO Display look configuration for this camera
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OCIO", meta = (DisplayName = "All Nodes OCIO Configuration", EditCondition = "bEnable"))
	FDisplayClusterConfigurationOCIOConfiguration AllNodesOCIOConfiguration;

	// Define special OCIO for cluster nodes for this camera
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OCIO", meta = (DisplayName = "Per-Node OCIO Overrides", ConfigurationMode = "ClusterNodes", EditCondition = "bEnable"))
	TArray<FDisplayClusterConfigurationOCIOProfile> PerNodeOCIOProfiles;

	// Inner Frustum Color Grading look configuration
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Frustum Color Grading", meta = (DisplayName = "All Nodes Color Grading", EditCondition = "bEnable && bUseInnerFrustumColorGrading"))
	FDisplayClusterConfigurationViewport_AllNodesColorGrading AllNodesColorGrading;

	// Define special per-node Inner Frustum Color Grading
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Frustum Color Grading", meta = (DisplayName = "Per-Node Color Grading", ConfigurationMode = "ClusterNodes", EditCondition = "bEnable && bUseInnerFrustumColorGrading"))
	TArray<FDisplayClusterConfigurationViewport_PerNodeColorGrading> PerNodeColorGrading;

	// Special hide list for this camera viewport
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (EditCondition = "bEnable"))
	FDisplayClusterConfigurationICVFX_VisibilityList CameraHideList;
};

USTRUCT(Blueprintable)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_StageSettings
{
	GENERATED_BODY()

public:
	// Allow ICVFX features
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (DisplayName = "Enable Inner Frustum"))
	bool bEnableInnerFrustums = true;

	// Default incameras RTT texture size.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (EditCondition = "bEnable"))
	FDisplayClusterConfigurationICVFX_Size DefaultFrameSize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (EditCondition = "bEnable"))
	FDisplayClusterConfigurationICVFX_LightcardSettings Lightcard;

	// Hide list for all icvfx viewports (outer, inner, cameras, etc)
	// (This allow to hide all actors from layers for icvfx render logic)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (EditCondition = "bEnable"))
	FDisplayClusterConfigurationICVFX_VisibilityList HideList;

	// Special hide list for Outer viewports
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay, meta = (EditCondition = "bEnable"))
	FDisplayClusterConfigurationICVFX_VisibilityList OuterViewportHideList;

	// Global cluster post process settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport Color Grading", meta = (DisplayName = "Entire Cluster"))
	FDisplayClusterConfigurationViewport_EntireClusterColorGrading EntireClusterColorGrading;

	// Define special per-viewport Color Grading
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport Color Grading", meta = (DisplayName = "Per-Viewport Color Grading", ConfigurationMode = "Viewports"))
	TArray<FDisplayClusterConfigurationViewport_PerViewportColorGrading> PerViewportColorGrading;

	// Apply the global cluster OCIO settings to all viewports
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OCIO", meta = (DisplayName = "Enable Viewport OCIO"))
	bool bUseOverallClusterOCIOConfiguration = true;

	// OCIO Display look configuration for outer viewports
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OCIO", meta = (DisplayName = "All Viewports Color Configuration", EditCondition = "bUseOverallClusterOCIOConfiguration"))
	FDisplayClusterConfigurationOCIOConfiguration AllViewportsOCIOConfiguration;

	// Define special OCIO for outer viewports
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OCIO", meta = (DisplayName = "Per-Viewport OCIO Overrides", ConfigurationMode = "Viewports", EditCondition = "bUseOverallClusterOCIOConfiguration"))
	TArray<FDisplayClusterConfigurationOCIOProfile> PerViewportOCIOProfiles;
};
