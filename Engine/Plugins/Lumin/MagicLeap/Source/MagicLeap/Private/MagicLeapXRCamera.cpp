// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapXRCamera.h"
#include "SceneView.h"
#include "MagicLeapCustomPresent.h"
#include "MagicLeapHMD.h"
#include "IMagicLeapPlugin.h"


FMagicLeapXRCamera::FMagicLeapXRCamera(const FAutoRegister& AutoRegister, FMagicLeapHMD& InMagicLeapSystem, int32 InDeviceID)
	: FDefaultXRCamera(AutoRegister, &InMagicLeapSystem, InDeviceID)
	, MagicLeapSystem(InMagicLeapSystem)
{
}

void FMagicLeapXRCamera::PreRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& View)
{
#if WITH_MLSDK
	// this needs to happen before the FDefaultXRCamera call, because UpdateProjectionMatrix is somewhat destructive. 
	
	if (MagicLeapSystem.IsStereoEyePass(View.StereoPass))
	{
		const int EyeIdx = MagicLeapSystem.DeviceIsAPrimaryView(View) ? 0 : 1;

		const FTrackingFrame& Frame = MagicLeapSystem.GetCurrentFrame();

		// update to use render projection matrix
		// Set the near clipping plane to GNearClippingPlane which is clamped to the minimum value allowed for the device. (ref: MLGraphicsGetRenderTargets())
		// #todo: Roll UpdateProjectionMatrix into UpdateViewMatrix?
		FMatrix RenderInfoProjectionMatrix = MagicLeap::ToUEProjectionMatrix(Frame.FrameInfo.virtual_camera_info_array.virtual_cameras[EyeIdx].projection, GNearClippingPlane);

		View.UpdateProjectionMatrix(RenderInfoProjectionMatrix);
	}

	FDefaultXRCamera::PreRenderView_RenderThread(RHICmdList, View);
#endif //WITH_MLSDK
}
