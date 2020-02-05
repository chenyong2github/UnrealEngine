// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PicpProjectionOverlayCamera.h"
#include "PicpProjectionOverlayViewport.h"
#include "PicpProjectionOverlayLUT.h"

class FPicpProjectionMPCDIPolicy;

// This is data for shader
class FPicpProjectionOverlayViewportData
{
public:
	FPicpProjectionOverlayViewportData()
	{ }

	~FPicpProjectionOverlayViewportData()
	{
		Empty();
	}

public:
	void Empty();

	bool IsAnyViewportUsed() const
	{
		return IsViewportUnderUsed() || IsViewportOverUsed();
	}

	bool IsViewportOverUsed() const
	{
		return ViewportOver.IsEnabled(); 
	}

	bool IsViewportUnderUsed() const
	{
		return ViewportUnder.IsEnabled(); 
	}

	bool IsCameraUsed(int CameraIndex) const
	{
		return (Cameras.Num() > CameraIndex) && CameraIndex >= 0;
	}

	bool IsAnyCameraUsed() const
	{
		return (Cameras.Num() > 0); 
	}

	bool IsMultiCamerasUsed() const
	{
		return (Cameras.Num() > 1);
	}

	bool IsLUTused() const
	{
		return LUTCorrection.IsEnabled(); 
	}

	bool IsValid()
	{
		return IsAnyViewportUsed() || IsAnyCameraUsed();
	}

	void Initialize(const FPicpProjectionOverlayViewportData& Source);

public:
	FPicpProjectionOverlayLUT                LUTCorrection;    //@todo: LUT correction (Not implemented now)
	FPicpProjectionOverlayViewport           ViewportOver;     // viewport over overlay texture (over all camera frames)
	FPicpProjectionOverlayViewport           ViewportUnder;    // viewport overlay texture
	TArray<FPicpProjectionOverlayCamera>     Cameras;          // Multi cams, in render order
};

// This is filled from BP
class FPicpProjectionOverlayFrameData
{
public:
	FPicpProjectionOverlayFrameData()
	{
	}

	~FPicpProjectionOverlayFrameData()
	{
		Empty();
	}

public:
	static void GenerateDebugContent(const FString& ViewportId, FPicpProjectionMPCDIPolicy* OutPolicy);

	void GetViewportData(const FString& ViewportId, FPicpProjectionMPCDIPolicy* OutPolicy, const FTransform& Origin2WorldTransform) const;
	void Empty();

public:
	FPicpProjectionOverlayLUT                               LUTCorrection;   //@todo: Global LUT correction (Not implemented now)
	TMap<FString, FPicpProjectionOverlayViewport>           ViewportsOver;   // Overlay under inner frame for all viewports by name
	TMap<FString, FPicpProjectionOverlayViewport>           ViewportsUnder;  // Overlay on top of inner frame for all viewports by name
	TArray<FPicpProjectionOverlayCamera>                    Cameras;         // Camera's overlay, in render order
};
