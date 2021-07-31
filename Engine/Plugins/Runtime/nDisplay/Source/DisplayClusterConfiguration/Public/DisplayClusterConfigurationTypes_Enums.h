// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "DisplayClusterConfigurationTypes_Enums.generated.h"


UENUM()
enum class EDisplayClusterConfigurationDataSource : uint8
{
	Text UMETA(DisplayName = "Text file"),
	Json UMETA(DisplayName = "JSON file")
};

UENUM()
enum class EDisplayClusterConfigurationKeyboardReflectionType : uint8
{
	None     UMETA(DisplayName = "No reflection"),
	nDisplay UMETA(DisplayName = "nDisplay buttons only"),
	Core     UMETA(DisplayName = "UE core keyboard events only"),
	All      UMETA(DisplayName = "Both nDisplay and UE core events")
};

UENUM()
enum class EDisplayClusterConfigurationTrackerMapping
{
	X    UMETA(DisplayName = "Positive X"),
	NX   UMETA(DisplayName = "Negative X"),
	Y    UMETA(DisplayName = "Positive Y"),
	NY   UMETA(DisplayName = "Negative Y"),
	Z    UMETA(DisplayName = "Positive Z"),
	NZ   UMETA(DisplayName = "Negative Z")
};

UENUM()
enum class EDisplayClusterConfigurationEyeStereoOffset : uint8
{
	None  UMETA(DisplayName = "No offset"),
	Left  UMETA(DisplayName = "Left eye of a stereo pair"),
	Right UMETA(DisplayName = "Right eye of a stereo pair")
};

UENUM()
enum class EDisplayClusterConfiguration_PostRenderBlur : uint8
{
	// Not use blur postprocess
	None      UMETA(DisplayName = "None"),

	// Blur viewport using Gaussian method
	Gaussian  UMETA(DisplayName = "Gaussian"),

	// Blur viewport using Dilate method
	Dilate    UMETA(DisplayName = "Dilate"),
};

UENUM()
enum class EDisplayClusterConfigurationICVFX_ChromakeySource : uint8
{
	// Disable chromakey rendering
	None                   UMETA(DisplayName = "None"),

	// Fill whole camera frame with chromakey color
	FrameColor             UMETA(DisplayName = "Chromakey Color"),

	// Capture to texture RTT from scene layers
	ChromakeyRenderTexture UMETA(DisplayName = "Chromakey Render Texture"),
};

UENUM()
enum class EDisplayClusterConfigurationICVFX_LightcardRenderMode : uint8
{
	/** Render Light Cards over the inner frustum.  Light Cards can be directly visible in camera. */
	Over    UMETA(DisplayName = "Lightcard Over Frustum"),

	/** Render Light Cards under the inner frustum. Light Cards will not be directly visible in camera. */
	Under   UMETA(DisplayName = "Lightcard Under Frustum"),
};

UENUM()
enum class EDisplayClusterConfigurationICVFX_OverrideLightcardRenderMode : uint8
{
	// Use global lightcard mode from StageSettings for this viewport
	Default    UMETA(DisplayName = "Default"),

	// Disable lightcard rendering for this viewport
	Disabled    UMETA(DisplayName = "Disabled"),

	// Render incamera frame over lightcard for this viewport
	Over    UMETA(DisplayName = "Lightcard Over Frustum"),

	// Over lightcard over incamera frame  for this viewport
	Under   UMETA(DisplayName = "Lightcard Under Frustum"),
};

UENUM()
enum class EDisplayClusterConfigurationICVFX_OverrideCameraRenderMode : uint8
{
	// Use default rendering rules
	Default                 UMETA(DisplayName = "Default"),

	// Disable camera frame render for this viewport
	Disabled                UMETA(DisplayName = "Disabled"),

	// Disable chromakey render for this viewport
	DisableChromakey        UMETA(DisplayName = "Disabled Chromakey"),

	// Disable chromakey markers render for this viewport
	DisableChromakeyMarkers UMETA(DisplayName = "Disabled Markers"),
};

UENUM()
enum class EDisplayClusterConfigurationViewport_StereoMode : uint8
{
	// Render incamera frame over lightcard
	Default  UMETA(DisplayName = "Default"),

	// Force monoscopic render mode for this viewport (performance)
	ForceMono   UMETA(DisplayName = "Force Mono"),
};

UENUM()
enum class EDisplayClusterConfigurationRenderFamilyMode : uint8
{
	// Render all viewports to unique RenderTargets
	None                          UMETA(DisplayName = "Disabled"),

	// Merge views by ViewFamilyGroupNum
	AllowMergeForGroups           UMETA(DisplayName = "Groups"),

	// Merge views by ViewFamilyGroupNum and stereo
	AllowMergeForGroupsAndStereo  UMETA(DisplayName = "GroupsAndStereo"),

	// Use rules to merge views to minimal num of families (separate by: buffer_ratio, viewExtension, max RTT size)
	MergeAnyPossible              UMETA(DisplayName = "AnyPossible"),
};

UENUM()
enum class EDisplayClusterConfigurationCameraMotionBlurMode : uint8
{
	/** Subtract blur due to all global motion of the ICVFX camera, but preserve blur from object motion. */
	Off          UMETA(DisplayName = "All Camera Blur Off"),

	/** Allow blur from camera motion. This option should not normally be used for shooting, but may be useful for diagnostic purposes. */
	On           UMETA(DisplayName = "ICVFX Camera Blur On"),

	/** Subtract blur due to motion of the ICVFX camera relative to the nDisplay root, but preserve blur from both object motion and movement of the nDisplay root, which can be animated to represent vehicular motion through an environment. */
	Override     UMETA(DisplayName = "ICVFX Camera Blur Off"),
};


UENUM()
enum class EDisplayClusterConfigurationRenderMGPUMode : uint8
{
	// Disable multi GPU rendering
	None         UMETA(DisplayName = "Disabled"),

	// Use default UE crossGPU transfer
	Enabled      UMETA(DisplayName = "Enabled"),

	// Performance (Experimental): Use optimized transfer once per frame with bLockStepGPUs=true
	Optimized_EnabledLockSteps  UMETA(DisplayName = "Optimization: Enabled Lockstep"),

	// Performance (Experimental): Use optimized transfer once per frame with bLockStepGPUs=false 
	Optimized_DisabledLockSteps  UMETA(DisplayName = "Optimization: Disabled Lockstep"),
};

UENUM()
enum class EDisplayClusterConfigurationViewportOverscanMode : uint8
{
	None     UMETA(DisplayName = "Disabled"),
	Pixels   UMETA(DisplayName = "Enabled: Pixels values"),
	Percent  UMETA(DisplayName = "Enabled: Percent values")
};

UENUM()
enum class EDisplayClusterConfigurationRenderMode : uint8
{
	Mono        UMETA(DisplayName = "Mono"),
	SideBySide  UMETA(DisplayName = "Stereo: Side By Side"),
	TopBottom   UMETA(DisplayName = "Stereo: Top Bottom")
};

