// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonObject.h"
#include "Json/GLTFJsonEnums.h"

struct FGLTFJsonSampler : IGLTFJsonObject
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

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		if (!Name.IsEmpty())
		{
			Writer.Write(TEXT("name"), Name);
		}

		if (MinFilter != EGLTFJsonTextureFilter::None)
		{
			Writer.Write(TEXT("minFilter"), MinFilter);
		}

		if (MagFilter != EGLTFJsonTextureFilter::None)
		{
			Writer.Write(TEXT("magFilter"), MagFilter);
		}

		if (WrapS != EGLTFJsonTextureWrap::Repeat)
		{
			Writer.Write(TEXT("wrapS"), WrapS);
		}

		if (WrapT != EGLTFJsonTextureWrap::Repeat)
		{
			Writer.Write(TEXT("wrapT"), WrapT);
		}
	}
};
