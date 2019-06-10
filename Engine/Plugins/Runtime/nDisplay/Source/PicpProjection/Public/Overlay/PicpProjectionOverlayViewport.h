// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PicpProjectionOverlayBase.h"
#include "CoreMinimal.h"
#include "RHI.h"


class FPicpProjectionOverlayViewport
	: public FPicpProjectionOverlayBase
{
public:
	FTexture2DRHIParamRef ViewportTexture; // Texture to render
	//@ Add more render options here

public:
	FPicpProjectionOverlayViewport(FTexture2DRHIParamRef TextureRef)
		: FPicpProjectionOverlayBase()
		, ViewportTexture(TextureRef)
	{ 
		SetEnable(TextureRef->IsValid());
	}

	virtual ~FPicpProjectionOverlayViewport()
	{ }
};
