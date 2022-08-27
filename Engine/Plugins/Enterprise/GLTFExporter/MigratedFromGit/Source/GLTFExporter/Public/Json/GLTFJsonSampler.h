// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"

struct GLTFEXPORTER_API FGLTFJsonSampler : IGLTFJsonIndexedObject
{
	FString Name;

	EGLTFJsonTextureFilter MinFilter;
	EGLTFJsonTextureFilter MagFilter;

	EGLTFJsonTextureWrap WrapS;
	EGLTFJsonTextureWrap WrapT;

	FGLTFJsonSampler(int32 Index = INDEX_NONE)
		: IGLTFJsonIndexedObject(Index)
		, MinFilter(EGLTFJsonTextureFilter::None)
		, MagFilter(EGLTFJsonTextureFilter::None)
		, WrapS(EGLTFJsonTextureWrap::Repeat)
		, WrapT(EGLTFJsonTextureWrap::Repeat)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};
