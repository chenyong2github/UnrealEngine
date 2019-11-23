// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "StereoRendering.h"
#include "SceneView.h"

#include "Engine/Engine.h"


bool IStereoRendering::IsStereoEyePass(EStereoscopicPass Pass)
{
	if (GEngine->StereoRenderingDevice.IsValid())
	{
		return GEngine->StereoRenderingDevice->DeviceIsStereoEyePass(Pass);
	}

	return Pass != EStereoscopicPass::eSSP_FULL;
}

bool IStereoRendering::IsStereoEyeView(const FSceneView& View)
{
	if (GEngine->StereoRenderingDevice.IsValid())
	{
		return GEngine->StereoRenderingDevice->DeviceIsStereoEyeView(View);
	}

	return View.StereoPass != EStereoscopicPass::eSSP_FULL;
}

bool IStereoRendering::DeviceIsStereoEyeView(const FSceneView& View)
{
	return !(View.StereoPass == EStereoscopicPass::eSSP_FULL);
}

bool IStereoRendering::IsAPrimaryPass(EStereoscopicPass Pass)
{
	if (GEngine->StereoRenderingDevice.IsValid())
	{
		return GEngine->StereoRenderingDevice->DeviceIsAPrimaryPass(Pass);
	}

	return Pass == EStereoscopicPass::eSSP_FULL || Pass == EStereoscopicPass::eSSP_LEFT_EYE;
}

bool IStereoRendering::IsAPrimaryView(const FSceneView& View)
{
	if (GEngine->StereoRenderingDevice.IsValid())
	{
		return GEngine->StereoRenderingDevice->DeviceIsAPrimaryView(View);
	}

	return View.StereoPass == EStereoscopicPass::eSSP_FULL || View.StereoPass == EStereoscopicPass::eSSP_LEFT_EYE;
}

bool IStereoRendering::DeviceIsAPrimaryView(const FSceneView& View)
{
	return View.StereoPass == EStereoscopicPass::eSSP_FULL || View.StereoPass == EStereoscopicPass::eSSP_LEFT_EYE;
}

bool IStereoRendering::IsASecondaryPass(EStereoscopicPass Pass)
{
	if (GEngine->StereoRenderingDevice.IsValid())
	{
		return GEngine->StereoRenderingDevice->DeviceIsASecondaryPass(Pass);
	}

	return !(Pass == EStereoscopicPass::eSSP_FULL || Pass == EStereoscopicPass::eSSP_LEFT_EYE);
}

bool IStereoRendering::IsASecondaryView(const FSceneView& View)
{
	if (GEngine->StereoRenderingDevice.IsValid())
	{
		return GEngine->StereoRenderingDevice->DeviceIsASecondaryView(View);
	}

	return !(View.StereoPass == EStereoscopicPass::eSSP_FULL || View.StereoPass == EStereoscopicPass::eSSP_LEFT_EYE);
}

bool IStereoRendering::DeviceIsASecondaryView(const FSceneView& View)
{
	return !DeviceIsAPrimaryView(View);
}

bool IStereoRendering::IsAnAdditionalPass(EStereoscopicPass Pass)
{
	if (GEngine->StereoRenderingDevice.IsValid())
	{
		return GEngine->StereoRenderingDevice->DeviceIsAnAdditionalPass(Pass);
	}

	return Pass > EStereoscopicPass::eSSP_RIGHT_EYE;
}

bool IStereoRendering::IsAnAdditionalView(const FSceneView& View)
{
	if (GEngine->StereoRenderingDevice.IsValid())
	{
		return GEngine->StereoRenderingDevice->DeviceIsAnAdditionalView(View);
	}

	return View.StereoPass > EStereoscopicPass::eSSP_RIGHT_EYE;
}

bool IStereoRendering::DeviceIsAnAdditionalView(const FSceneView& View)
{
	return View.StereoPass > EStereoscopicPass::eSSP_RIGHT_EYE;
}
