// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"

struct GLTFEXPORTER_API FGLTFJsonHotspot : IGLTFJsonIndexedObject
{
	FString Name;

	FGLTFJsonAnimationIndex Animation;
	FGLTFJsonTextureIndex   Image;
	FGLTFJsonTextureIndex   HoveredImage;
	FGLTFJsonTextureIndex   ToggledImage;
	FGLTFJsonTextureIndex   ToggledHoveredImage;

	FGLTFJsonHotspot(int32 Index = INDEX_NONE)
		: IGLTFJsonIndexedObject(Index)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};
