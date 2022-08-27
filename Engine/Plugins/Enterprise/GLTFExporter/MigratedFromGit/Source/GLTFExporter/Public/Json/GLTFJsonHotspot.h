// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonObject.h"
#include "Json/GLTFJsonIndex.h"

struct GLTFEXPORTER_API FGLTFJsonHotspot : IGLTFJsonObject
{
	FString Name;

	FGLTFJsonAnimationIndex Animation;
	FGLTFJsonTextureIndex   Image;
	FGLTFJsonTextureIndex   HoveredImage;
	FGLTFJsonTextureIndex   ToggledImage;
	FGLTFJsonTextureIndex   ToggledHoveredImage;

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};
