// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


// Root data container. Render all over basic
class FPicpProjectionOverlayLUT
{
public:
	FRHITexture2D*        LUTTexture;           // if texture defined, applyed brightness correction from view angle
	FVector               ViewCorrectionCoeff;
	//@todo: Add more render options here

public:
	FPicpProjectionOverlayLUT()
		: LUTTexture(nullptr)
		, ViewCorrectionCoeff(0,0,0) 
		, bIsEnabled(false)
	{ }

	void Initialize(FRHITexture2D*& TextureRef, const FVector& ViewCorrection)
	{
		LUTTexture = TextureRef;
		ViewCorrectionCoeff = ViewCorrection;
		bIsEnabled = true;
	}

	bool IsEnabled() const 
	{
		return bIsEnabled; 
	}

	void Empty()
	{
		LUTTexture = nullptr;
		bIsEnabled = false;
	}

private:
	bool    bIsEnabled;
	FVector EyeOriginLocal; // eye in local mpcdi space
};
