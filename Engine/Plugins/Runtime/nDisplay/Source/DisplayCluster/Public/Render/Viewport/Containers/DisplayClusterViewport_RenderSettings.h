// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum class EDisplayClusterViewportCaptureMode : uint8
{
	// Use current scene format, no alpha
	Default = 0,

	// use small BGRA 8bit texture with alpha for masking
	Chromakey,

	// use hi-res float texture with alpha for compisiting
	Lightcard,
	Lightcard_OCIO,
};

class FDisplayClusterViewport_RenderSettings
{
public:
	// Assigned camera. If empty, the currently active camera must be used
	FString CameraId;

	// Location and size on a backbuffer.
	FIntRect Rect;

public:
	// Enable this viewport and related resources rendering
	bool bEnable = true;

	// This viewport visible on final frame texture (backbuffer)
	bool bVisible = true;

	// Skip rendering for this viewport
	bool bSkipRendering = false;

	// Useful to render some viewports in mono, then copied to stereo backbuffers identical image
	bool bForceMono = false;

	// Performance, Multi-GPU: Asign GPU for viewport rendering. The Value '-1' used to default gpu mapping
	int GPUIndex = -1;

	// Performance, Multi-GPU: Customize GPU for stereo mode second view (EYE_RIGHT)
	int StereoGPUIndex = -1;

	// Allow ScreenPercentage 
	float BufferRatio = 1;

	// Performance: Render to scale RTT, resolved with shader to viewport (Custom value)
	float RenderTargetRatio = 1;

	// Viewport can overlap each other on backbuffer. This value uses to sorting order
	int OverlapOrder = 0;

	// Performance: Support special frame builder mode - merge viewports to single viewfamily by group num
	// [not implemented yet] Experimental
	int RenderFamilyGroup = -1;

	// Special capture modes (chromakey, lightcard) change RTT format and render flags
	EDisplayClusterViewportCaptureMode CaptureMode = EDisplayClusterViewportCaptureMode::Default;

	// Override image from this viewport
	FString OverrideViewportId;

public:
	// Reset runtime values from prev frame
	inline void BeginUpdateSettings()
	{
		bVisible = true;
		bEnable = true;
		bSkipRendering = false;

		CaptureMode = EDisplayClusterViewportCaptureMode::Default;

		OverrideViewportId.Empty();
	}

	inline const FString& GetParentViewportId() const
	{
		return ParentViewportId;
	}

	// Call this after UpdateSettings()
	inline void AssignParentViewport(const FString& InParentViewportId, const FDisplayClusterViewport_RenderSettings& InParentSettings, bool Inherit = true)
	{
		ParentViewportId = InParentViewportId;

		// Inherit values from parent viewport:
		if (Inherit)
		{
			CameraId = InParentSettings.CameraId;
			Rect = InParentSettings.Rect;

			bForceMono = InParentSettings.bForceMono;

			GPUIndex = (GPUIndex < 0) ? InParentSettings.GPUIndex : GPUIndex;
			StereoGPUIndex = (StereoGPUIndex < 0) ? InParentSettings.StereoGPUIndex : StereoGPUIndex;

			RenderFamilyGroup = (RenderFamilyGroup < 0) ? InParentSettings.RenderFamilyGroup : RenderFamilyGroup;
		}
	}

protected:
	// Parent viewport name
	FString ParentViewportId;
};

