// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "StereoRendering.h"
#include "SceneView.h"

bool IStereoRendering::DeviceIsAPrimaryView(const FSceneView& View)
{
	return View.StereoPass == eSSP_FULL || View.StereoPass == eSSP_LEFT_EYE;
}

bool IStereoRendering::DeviceIsAnAdditionalView(const FSceneView& View)
{
	return View.StereoPass > eSSP_RIGHT_EYE;
}

bool IStereoRendering::IsAPrimaryView(const FSceneView& View, TSharedPtr<IStereoRendering, ESPMode::ThreadSafe> StereoRenderingDevice)
{
	if (StereoRenderingDevice.IsValid())
	{
		return StereoRenderingDevice->DeviceIsAPrimaryView(View);
	}

	return View.StereoPass == eSSP_FULL || View.StereoPass == eSSP_LEFT_EYE;
}

bool IStereoRendering::IsAnAdditionalView(const FSceneView& View, TSharedPtr<IStereoRendering, ESPMode::ThreadSafe> StereoRenderingDevice)
{
	if (StereoRenderingDevice.IsValid())
	{
		return StereoRenderingDevice->DeviceIsAnAdditionalView(View);
	}

	return View.StereoPass > eSSP_RIGHT_EYE;
}
