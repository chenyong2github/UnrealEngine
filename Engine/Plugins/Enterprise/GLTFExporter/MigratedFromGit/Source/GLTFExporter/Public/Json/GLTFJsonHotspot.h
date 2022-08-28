// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonObject.h"
#include "Json/GLTFJsonIndex.h"

struct FGLTFJsonHotspot : IGLTFJsonObject
{
	FString Name;

	FGLTFJsonAnimationIndex Animation;
	FGLTFJsonTextureIndex   Image;
	FGLTFJsonTextureIndex   HoveredImage;
	FGLTFJsonTextureIndex   ToggledImage;
	FGLTFJsonTextureIndex   ToggledHoveredImage;

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		if (!Name.IsEmpty())
		{
			Writer.Write(TEXT("name"), Name);
		}

		Writer.Write(TEXT("animation"), Animation);
		Writer.Write(TEXT("image"), Image);

		if (HoveredImage != INDEX_NONE)
		{
			Writer.Write(TEXT("hoveredImage"), HoveredImage);
		}

		if (ToggledImage != INDEX_NONE)
		{
			Writer.Write(TEXT("toggledImage"), ToggledImage);
		}

		if (ToggledHoveredImage != INDEX_NONE)
		{
			Writer.Write(TEXT("toggledHoveredImage"), ToggledHoveredImage);
		}
	}
};
