// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"

struct GLTFEXPORTER_API FGLTFJsonBufferView : IGLTFJsonObject
{
	FString Name;

	FGLTFJsonBufferIndex Buffer;

	int64 ByteLength;
	int64 ByteOffset;
	int32 ByteStride;

	EGLTFJsonBufferTarget Target;

	FGLTFJsonBufferView()
		: ByteLength(0)
		, ByteOffset(0)
		, ByteStride(0)
		, Target(EGLTFJsonBufferTarget::None)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};
