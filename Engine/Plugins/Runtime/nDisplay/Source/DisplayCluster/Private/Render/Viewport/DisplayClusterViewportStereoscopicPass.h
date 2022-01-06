// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StereoRendering.h"

struct FDisplayClusterViewportStereoscopicPass
{
	inline static EStereoscopicPass EncodeStereoscopicPass(const uint32 ContextNum, const uint32 ContextAmmount)
	{
		EStereoscopicPass EncodedPass = EStereoscopicPass::eSSP_PRIMARY;

		// Monoscopic rendering
		if (ContextAmmount == 1)
		{
			EncodedPass = EStereoscopicPass::eSSP_FULL;
		}
		// Stereoscopic rendering
		else
		{
			switch (ContextNum)
			{
			case 0:
				EncodedPass = EStereoscopicPass::eSSP_PRIMARY;
				break;
			case 1:
				EncodedPass = EStereoscopicPass::eSSP_SECONDARY;
				break;
			default:
				// now stereo only with 2 context
				check(false);
				break;
			}
		}

		return EncodedPass;
	}
};

