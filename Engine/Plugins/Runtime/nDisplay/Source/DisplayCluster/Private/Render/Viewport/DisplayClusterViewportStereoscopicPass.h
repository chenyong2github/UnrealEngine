// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StereoRendering.h"

struct FDisplayClusterViewportStereoscopicPass
{
	// Eye stereoscopic pass (mono, left, right)
	inline static EStereoscopicPass EncodeStereoscopicEye(const uint32 ContextNum, const uint32 ContextAmmount)
	{
		EStereoscopicPass EyePass = EStereoscopicPass::eSSP_FULL;

		// Monoscopic rendering
		if (ContextAmmount == 1)
		{
			EyePass = EStereoscopicPass::eSSP_FULL;
		}
		// Stereoscopic rendering
		else
		{
			switch (ContextNum)
			{
			case 0:
				EyePass = EStereoscopicPass::eSSP_LEFT_EYE;
				break;
			case 1:
				EyePass = EStereoscopicPass::eSSP_RIGHT_EYE;
				break;
			default:
				// now stereo only with 2 context
				check(false);
				break;
			}
		}

		return EyePass;
	}

	// Unique stereoscopic pass (ndisplay viewports hack)
	// EStereoscopicPass enum used as int value range
	// Use to encode sceneview in viewFamily
	inline static EStereoscopicPass EncodeStereoscopicPass(const uint32 ViewIndex)
	{
		EStereoscopicPass EncodedPass = EStereoscopicPass::eSSP_FULL;

		// We don't care about mono/stereo. We need to fulfill ViewState and StereoViewStates in a proper way.
		// Look at ULocalPlayer::CalcSceneViewInitOptions for view states mapping.
		if (ViewIndex < 2)
		{
			EncodedPass = (ViewIndex == 0 ? EStereoscopicPass::eSSP_LEFT_EYE : EStereoscopicPass::eSSP_RIGHT_EYE);
		}
		else
		{
			EncodedPass = EStereoscopicPass(int(EStereoscopicPass::eSSP_RIGHT_EYE) + ViewIndex - 1);
		}

		return EncodedPass;
	}
};

