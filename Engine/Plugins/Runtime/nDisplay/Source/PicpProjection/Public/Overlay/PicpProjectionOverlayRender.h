// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PicpProjectionOverlayCamera.h"
#include "PicpProjectionOverlayViewport.h"
#include "PicpProjectionOverlayLUT.h"


// This is data for shader
class FPicpProjectionOverlayViewportData
{
public:
	FPicpProjectionOverlayViewportData()
		: LUTCorrection(nullptr)
		, ViewportOver(nullptr)
		, ViewportUnder(nullptr)
	{ }

	~FPicpProjectionOverlayViewportData()
	{
		Empty();
	}

public:
	void Empty();

	bool isAnyViewportUsed() const 
	{ return iViewportUnderUsed() || iViewportOverUsed(); }

	bool iViewportOverUsed() const
	{ return ViewportOver!=nullptr && ViewportOver->IsEnabled(); }

	bool iViewportUnderUsed() const
	{ return ViewportUnder!=nullptr && ViewportUnder->IsEnabled(); }

	bool isAnyCameraUsed() const
	{ return (Cameras.Num() > 0); }

	bool isLUTused() const
	{ return LUTCorrection!=nullptr && LUTCorrection->IsEnabled(); }

	bool isValid()
	{ return isAnyViewportUsed() || isAnyCameraUsed(); }

	void Initialize(const FPicpProjectionOverlayViewportData& Source);

public:
	FPicpProjectionOverlayLUT*               LUTCorrection;
	FPicpProjectionOverlayViewport*          ViewportOver; // viewport overlay texture
	FPicpProjectionOverlayViewport*          ViewportUnder; // viewport overlay texture
	TArray<FPicpProjectionOverlayCamera*>    Cameras; // Multi cams, in render order
};


class FPicpProjectionMPCDIPolicy;


// This is filled from BP
class FPicpProjectionOverlayFrameData
{
public:
	FPicpProjectionOverlayFrameData()
		: LUTCorrection(nullptr)
	{ }

	~FPicpProjectionOverlayFrameData()
	{
		Empty();
	}

public:
	static void GenerateDebugContent(const FString& ViewportId, FPicpProjectionMPCDIPolicy* OutPolicy);
	void GetViewportData(const FString& ViewportId, FPicpProjectionMPCDIPolicy* OutPolicy, const FTransform& Origin2WorldTransform) const;
	void Empty();

public:
	FPicpProjectionOverlayLUT*                      LUTCorrection;  // Global LUT correction
	TMap<FString, FPicpProjectionOverlayViewport*>  ViewportsOver;  // Overlay under inner frame for all viewports by name
	TMap<FString, FPicpProjectionOverlayViewport*>  ViewportsUnder; // Overlay on top of inner frame for all viewports by name
	TArray<FPicpProjectionOverlayCamera*>           Cameras;        // Camera's overlay, in render order
};
