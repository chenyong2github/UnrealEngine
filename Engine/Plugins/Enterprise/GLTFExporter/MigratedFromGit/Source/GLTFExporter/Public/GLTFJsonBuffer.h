// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFJsonObject.h"

struct GLTFEXPORTER_API FGLTFJsonBuffer : FGLTFJsonObject
{
	FString Name;

	FString URI;
	int64   ByteLength;

	FGLTFJsonBuffer()
		: ByteLength(0)
	{
	}
};
