// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"

#include "MagicLeapGraphics.h"
#include "MagicLeapMath.h"
#include "MagicLeapUtils.h"
#include "Lumin/CAPIShims/LuminAPIPerception.h"
#include "MagicLeapPluginUtil.h"
#include "Math/Vector4.h"

class FMagicLeapHMD;
class FViewport;
struct FWorldContext;

static const uint8 kNumEyes = 2;

struct FTrackingFrame
{
public:
	uint64 FrameNumber; // current frame number
	bool HasHeadTrackingPosition;
	FTransform RawPose;
	float HFov;
	float VFov;
	float WorldToMetersScale;
	float FocusDistance;
	float NearClippingPlane;
	float FarClippingPlane;
	float RecommendedFarClippingPlane;
	float StabilizationDepth;
	bool bBeginFrameSucceeded;

#if WITH_MLSDK
	MLSnapshot* Snapshot;
	MLCoordinateFrameUID FrameId;
	MLGraphicsFrameInfo FrameInfo; // render information for the frame
	MLGraphicsClipExtentsInfoArrayEx UpdateInfoArray; // update information for the frame
	MLGraphicsProjectionType ProjectionType;
#endif //WITH_MLSDK

	float PixelDensity;
	FWorldContext* WorldContext;

	FTrackingFrame()
		: FrameNumber(0)
		, HasHeadTrackingPosition(false)
		, RawPose(FTransform::Identity)
		, HFov(0.0f)
		, VFov(0.0f)
		, WorldToMetersScale(100.0f)
		, FocusDistance(1.0f)
		, NearClippingPlane(GNearClippingPlane)
		, FarClippingPlane(1000.0f) // 10m
		, RecommendedFarClippingPlane(FarClippingPlane)
		, StabilizationDepth(1000.0f) // 10m
		, bBeginFrameSucceeded(false)
#if WITH_MLSDK
		, Snapshot(nullptr)
		, ProjectionType(MLGraphicsProjectionType_ReversedInfiniteZ)
#endif //WITH_MLSDK
		, PixelDensity(1.0f)
		, WorldContext(nullptr)
	{
#if WITH_MLSDK
		FrameId.data[0] = 0;
		FrameId.data[1] = 0;

		MagicLeap::ResetClipExtentsInfoArray(UpdateInfoArray);
		MLGraphicsFrameInfoInit(&FrameInfo);
		MagicLeap::ResetVirtualCameraInfoArray(FrameInfo.virtual_camera_info_array);
#endif //WITH_MLSDK
	}
};

class FMagicLeapCustomPresent : public FRHICustomPresent
{
public:
	FMagicLeapCustomPresent(FMagicLeapHMD* plugin);

	virtual void BeginRendering() = 0;
	virtual void FinishRendering() = 0;
	virtual bool NeedsNativePresent() override;
	virtual void OnBackBufferResize() override;
	virtual bool Present(int& SyncInterval) override;

	virtual void UpdateViewport(const FViewport& Viewport, FRHIViewport* InViewportRHI) = 0;
	virtual void UpdateViewport_RenderThread() = 0;
	virtual void GetFieldOfView(float& OutHFOVInDegrees, float& OutVFOVInDegrees);
	virtual void Reset();
	virtual void Shutdown();

	// Override if custom present needs to render to ML surfaces using Unreal's render pipeline instead of direct blit / texture copy.
	virtual void RenderToMLSurfaces_RenderThread(class FRHICommandListImmediate& RHICmdList, class FRHITexture2D* SrcTexture) {}

protected:
	void BeginFrame(FTrackingFrame& Frame);
	void NotifyFirstRender();
	void RenderToTextureSlice_RenderThread(class FRHICommandListImmediate& RHICmdList, class FRHITexture2D* SrcTexture, class FRHITexture* DstTexture, uint32 ArraySlice, const FVector4& UVandSize);
	FMagicLeapHMD* Plugin;
	bool bNotifyLifecycleOfFirstPresent;
	bool bCustomPresentIsSet;
	uint32 PlatformAPILevel;
	volatile int64 HFOV;
	volatile int64 VFOV;

	template <typename CameraParamsType>
	void InitCameraParams(CameraParamsType & CameraParams, FTrackingFrame & Frame);

	template <typename CameraParamsType>
	void InitExtraCameraParams(CameraParamsType & CameraParams, FTrackingFrame & Frame);
};
