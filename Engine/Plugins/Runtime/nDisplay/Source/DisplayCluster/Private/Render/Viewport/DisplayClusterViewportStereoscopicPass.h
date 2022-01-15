// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StereoRendering.h"

struct FDisplayClusterViewportStereoscopicPass
{
	inline static EStereoscopicPass EncodeStereoscopicPass(const uint32 ContextNum, const uint32 ContextAmmount)
	{
		// Stereoscopic rendering
		if (ContextAmmount > 1)
		{
			switch (ContextNum)
			{
			case 0:
				// Left eye
				// PRIMARY implies the view needs its own pass
				return EStereoscopicPass::eSSP_PRIMARY;
			case 1:
				// Right eye
				// SECONDARY implies the view can be instanced
				return EStereoscopicPass::eSSP_SECONDARY;
			default:
				// now stereo only with 2 context
				check(false);
				break;
			}
		}

		// Monoscopic rendering
		return EStereoscopicPass::eSSP_PRIMARY;
	}
};

