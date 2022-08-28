// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"

struct GLTFEXPORTER_API FGLTFJsonHotspot : IGLTFJsonIndexedObject
{
	FString Name;

	FGLTFJsonAnimation* Animation;
	FGLTFJsonTexture*   Image;
	FGLTFJsonTexture*   HoveredImage;
	FGLTFJsonTexture*   ToggledImage;
	FGLTFJsonTexture*   ToggledHoveredImage;

	FGLTFJsonHotspot(int32 Index = INDEX_NONE)
		: IGLTFJsonIndexedObject(Index)
		, Animation(nullptr)
		, Image(nullptr)
		, HoveredImage(nullptr)
		, ToggledImage(nullptr)
		, ToggledHoveredImage(nullptr)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};
