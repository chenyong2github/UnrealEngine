// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "Components/ActorComponent.h"
#include "ActorLayerUtilities.h"

#include "DisplayClusterConfigurationTypes_Viewport.h"

#include "DisplayClusterConfigurationTypes_ICVFX.generated.h"

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_VisibilityList
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = NDisplay)
	TArray<FActorLayer> ActorLayers;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	TArray<AActor*> Actors;

	//@todo change link, now by names
	// Reference to RootActor components by names
	UPROPERTY(EditAnywhere, Category = NDisplay)
	TArray<FString> RootActorComponentNames;
};

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_ChromakeyMarkers
{
	GENERATED_BODY()

public:
	// Allow shromakey markers rendering (Also require not empty MarkerTileRGBA)
	UPROPERTY(EditAnywhere, Category = NDisplay)
	bool bEnable = true;

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
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_OverlayAdvancedRenderSettings
{
	GENERATED_BODY()

public:
	// Allow ScreenPercentage 
	UPROPERTY(EditAnywhere, Category = NDisplay, meta = (ClampMin = "0.05", UIMin = "0.05", ClampMax = "10.0", UIMax = "10.0"))
	float BufferRatio = 1;

	// Performance: Render to scale RTT, resolved with shader to viewport (Custom value)
	UPROPERTY(EditAnywhere, Category = NDisplay)
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
	// Debug: override the texture of the camera viewport from this chromakey RTT
	UPROPERTY(EditAnywhere, Category = NDisplay)
	bool bOverrideCameraViewport = false;

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
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_ChromakeySettings
{
	GENERATED_BODY()

public:
	FDisplayClusterConfigurationICVFX_ChromakeySettings()
		// Default chromakey color is (0,127,0)
		: ChromakeyColor(0, 0.5f, 0)
	{ }

public:
	// Allow chromakey rendering (also require not empty ChromakeyLayers)
	UPROPERTY(EditAnywhere, Category = NDisplay)
	EDisplayClusterConfigurationICVFX_ChromakeySource Source = EDisplayClusterConfigurationICVFX_ChromakeySource::None;

	// Color to fill camera frame
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
	UPROPERTY(EditAnywhere, Category = NDisplay)
	bool bEnable = true;

	// Global lighcard rendering mode
	UPROPERTY(EditAnywhere, Category = NDisplay)
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
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_CameraFrameSize
{
	GENERATED_BODY()

public:
	FDisplayClusterConfigurationICVFX_CameraFrameSize()
		: CustomSizeValue(2560, 1440)
	{ }

public:
	// Camera frame size value source
	UPROPERTY(EditAnywhere, Category = NDisplay)
	EDisplayClusterConfigurationICVFX_CameraFrameSizeSource Size = EDisplayClusterConfigurationICVFX_CameraFrameSizeSource::Default;

	// Frame size for this camera, used when selected "Custom size value"
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FIntPoint CustomSizeValue;
};

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_CameraAdvancedRenderSettings
{
	GENERATED_BODY()

public:
	// Performance: Render to scale RTT, resolved with shader to viewport (Custom value)
	UPROPERTY(EditAnywhere, Category = NDisplay)
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
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_CameraRenderSettings
{
	GENERATED_BODY()

public:
	// Define camera RTT texture size
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationICVFX_CameraFrameSize FrameSize;

	// Camera render order, bigger value is over
	UPROPERTY(EditAnywhere, Category = NDisplay)
	int RenderOrder = -1;

	UPROPERTY()
	FDisplayClusterConfigurationViewport_CustomPostprocess CustomPostprocess;

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


USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_CameraCustomChromakeySettings
{
	GENERATED_BODY()

public:
	// Allow use local settings for chromakey and markers
	UPROPERTY(EditAnywhere, Category = NDisplay)
	bool bEnable = false;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationICVFX_ChromakeySettings Chromakey;
};



USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_CameraMotionBlur
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = NDisplay)
	EDisplayClusterConfigurationCameraMotionBlurMode MotionBlurMode = EDisplayClusterConfigurationCameraMotionBlurMode::Off;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	float TranslationScale = 1.f;

	// GUI: Add ext camera refs
};


USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFX_CameraSettings
{
	GENERATED_BODY()

public:
	// Enable this camera
	UPROPERTY(EditAnywhere, Category = NDisplay)
	bool bEnable = true;

	// Allow ScreenPercentage, for values!=1
	UPROPERTY(EditAnywhere, Category = NDisplay, meta = (ClampMin = "0.05", UIMin = "0.05", ClampMax = "10.0", UIMax = "10.0"))
	float BufferRatio = 1;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	float FieldOfViewMultiplier = 1.0f;

	// Basic soft edges setup for incamera
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FVector  SoftEdge = FVector::ZeroVector;

	// Rotate incamera frustum on this value to fix broken lens on physic camera
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FRotator  FrustumRotation = FRotator::ZeroRotator;

	// Move incamera frustum on this value to fix broken lens on physic camera
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FVector FrustumOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationICVFX_CameraMotionBlur CameraMotionBlur;

	// Configure global render settings for this viewports
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationICVFX_CameraRenderSettings RenderSettings;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationICVFX_CameraCustomChromakeySettings CustomChromakey;

	// OCIO Display look configuration 
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FOpenColorIODisplayConfiguration OCIO_Configuration;
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
	FIntPoint DefaultFrameSize;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationICVFX_ChromakeySettings Chromakey;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationICVFX_LightcardSettings Lightcard;

	// Should be to add to this list all defined lightcards and chromakeys layers
	// (This allow to hide all actors from layers for icvfx render logic)
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationICVFX_VisibilityList HideList;
};

