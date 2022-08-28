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

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;

protected:

	friend TGLTFJsonIndexedObjectArray<FGLTFJsonHotspot, void>;

	FGLTFJsonHotspot(int32 Index)
		: IGLTFJsonIndexedObject(Index)
		, Animation(nullptr)
		, Image(nullptr)
		, HoveredImage(nullptr)
		, ToggledImage(nullptr)
		, ToggledHoveredImage(nullptr)
	{
	}
};
