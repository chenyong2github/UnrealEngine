// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"


class FPicpProjectionCameraChromakey
{
public:
	FRHITexture   *ChromakeyTexture;       // Chromakey texture (can be null)

	FRHITexture2D *ChromakeyMarkerTexture;    // Texture to render
	float          ChromakeyMarkerScale;      // Scale marker uv
	bool           bChromakeyMarkerUseMeshUV; // Enable using custom warp mesh uv channel to map chromakey markers tiled texture

	//@todo: Add more render options here

public:
	FPicpProjectionCameraChromakey()
		: ChromakeyTexture(nullptr)
		, ChromakeyMarkerTexture(nullptr)
		, ChromakeyMarkerScale(1)
		, bChromakeyMarkerUseMeshUV(false)
	{ }

	inline bool IsChromakeyUsed() const
	{
		return (ChromakeyTexture != nullptr) && ChromakeyTexture->IsValid();
	}

	inline bool IsChromakeyMarkerUsed() const
	{
		return (ChromakeyMarkerTexture != nullptr) && ChromakeyMarkerTexture->IsValid();
	}
};
