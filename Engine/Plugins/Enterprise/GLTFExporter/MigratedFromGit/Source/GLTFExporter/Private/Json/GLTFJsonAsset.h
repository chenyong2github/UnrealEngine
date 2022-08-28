// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonObject.h"

struct FGLTFJsonAsset : IGLTFJsonObject
{
	FString Version;
	FString Generator;
	FString Copyright;

	FGLTFJsonAsset()
		: Version(TEXT("2.0"))
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		Writer.Write(TEXT("version"), Version);

		if (!Generator.IsEmpty())
		{
			Writer.Write(TEXT("generator"), Generator);
		}

		if (!Copyright.IsEmpty())
		{
			Writer.Write(TEXT("copyright"), Copyright);
		}
	}
};
