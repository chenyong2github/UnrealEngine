// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StereoRendering.h"

struct FDisplayClusterViewportStereoscopicPass
{
	inline static EStereoscopicPass EncodeStereoscopicPass(const uint32 ContextNum, const uint32 ContextAmmount)
	{
		// Monoscopic rendering
		if (ContextAmmount == 1)
		{
			switch (ContextNum)
			{
			case 0:
				return EStereoscopicPass::eSSP_PRIMARY;
			case 1:
				return EStereoscopicPass::eSSP_SECONDARY;
			default:
				// now stereo only with 2 context
				check(false);
				break;
			}
		}

		return EStereoscopicPass::eSSP_PRIMARY;
	}
};

