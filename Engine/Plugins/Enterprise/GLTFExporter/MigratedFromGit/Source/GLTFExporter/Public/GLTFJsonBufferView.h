// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFJsonEnums.h"
#include "GLTFJsonObject.h"

struct GLTFEXPORTER_API FGLTFJsonBufferView : FGLTFJsonObject
{
	FString Name;

	FGLTFJsonIndex Buffer;

	int64 ByteLength;
	int64 ByteOffset;
	int32 ByteStride;

	EGLTFJsonBufferTarget Target;

	FGLTFJsonBufferView()
		: Buffer(INDEX_NONE)
		, ByteLength(0)
		, ByteOffset(0)
		, ByteStride(0)
		, Target(EGLTFJsonBufferTarget::None)
	{
	}
};
