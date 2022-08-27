// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonObject.h"
#include "Json/GLTFJsonEnums.h"

struct GLTFEXPORTER_API FGLTFJsonSampler : IGLTFJsonObject
{
	FString Name;

	EGLTFJsonTextureFilter MinFilter;
	EGLTFJsonTextureFilter MagFilter;

	EGLTFJsonTextureWrap WrapS;
	EGLTFJsonTextureWrap WrapT;

	FGLTFJsonSampler()
		: MinFilter(EGLTFJsonTextureFilter::None)
		, MagFilter(EGLTFJsonTextureFilter::None)
		, WrapS(EGLTFJsonTextureWrap::Repeat)
		, WrapT(EGLTFJsonTextureWrap::Repeat)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};
