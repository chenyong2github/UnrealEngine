// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"


class FPicpProjectionOverlayViewport
{
public:
	FRHITexture2D* ViewportTexture; // Texture to render
	//@ Add more render options here

public:
	FPicpProjectionOverlayViewport()
		: ViewportTexture(nullptr)
	{ 
	}

	FPicpProjectionOverlayViewport(FRHITexture2D* TextureRef)
		: ViewportTexture(TextureRef)
	{ 
	}
	
	bool IsEnabled() const
	{ 
		return ViewportTexture!=nullptr && ViewportTexture->IsValid(); 
	}

	void Empty()
	{ 
		ViewportTexture = nullptr;  
	}
};
