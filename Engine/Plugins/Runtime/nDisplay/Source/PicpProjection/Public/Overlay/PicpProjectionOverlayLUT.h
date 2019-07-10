// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PicpProjectionOverlayBase.h"


// Root data container. Render all over basic
class FPicpProjectionOverlayLUT
	: public FPicpProjectionOverlayBase
{
public:
	FRHITexture2D* LUTTexture;           // if texture defined, applyed brightness correction from view angle
	FVector               ViewCorrectionCoeff;
	FVector               EyeOrigin;            // Eye position in world space
	//@ Add more render options here

public:
	FPicpProjectionOverlayLUT()
		: LUTTexture(nullptr)
		, ViewCorrectionCoeff(0,0,0) 
		, EyeOrigin(0,0,0)
	{ }

	void Initialize(FRHITexture2D*& TextureRef, const FVector& ViewCorrection, const FVector& EyeLocation)
	{
		LUTTexture = TextureRef;
		ViewCorrectionCoeff = ViewCorrection;
		EyeOrigin = EyeLocation;
		SetEnable(true);
	}

private:
	FVector EyeOriginLocal; // eye in local mpcdi space
};
