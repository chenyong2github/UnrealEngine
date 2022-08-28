// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonObject.h"
#include "Json/GLTFJsonEnums.h"
#include "Json/GLTFJsonIndex.h"

struct FGLTFJsonBufferView : IGLTFJsonObject
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
		// check that view fits completely inside the buffer
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		if (!Name.IsEmpty())
		{
			Writer.Write(TEXT("name"), Name);
		}

		Writer.Write(TEXT("buffer"), Buffer);
		Writer.Write(TEXT("byteLength"), ByteLength);

		if (ByteOffset != 0)
		{
			Writer.Write(TEXT("byteOffset"), ByteOffset);
		}

		if (ByteStride != 0)
		{
			Writer.Write(TEXT("byteStride"), ByteStride);
		}

		if (Target != EGLTFJsonBufferTarget::None)
		{
			Writer.Write(TEXT("target"), Target);
		}
	}
};
