// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonObject.h"
#include "Json/GLTFJsonEnums.h"
#include "Json/GLTFJsonIndex.h"

struct FGLTFJsonAccessor : IGLTFJsonObject
{
	FString Name;

	FGLTFJsonBufferViewIndex BufferView;
	int64                    ByteOffset;
	int32                    Count;
	EGLTFJsonAccessorType    Type;
	EGLTFJsonComponentType   ComponentType;
	bool                     bNormalized;

	int32 MinMaxLength;
	float Min[16];
	float Max[16];

	FGLTFJsonAccessor()
		: ByteOffset(0)
		, Count(0)
		, Type(EGLTFJsonAccessorType::None)
		, ComponentType(EGLTFJsonComponentType::None)
		, bNormalized(false)
		, MinMaxLength(0)
		, Min{0}
		, Max{0}
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		if (!Name.IsEmpty())
		{
			Writer.Write(TEXT("name"), Name);
		}

		Writer.Write(TEXT("bufferView"), BufferView);

		if (ByteOffset != 0)
		{
			Writer.Write(TEXT("byteOffset"), ByteOffset);
		}

		Writer.Write(TEXT("count"), Count);
		Writer.Write(TEXT("type"), Type);
		Writer.Write(TEXT("componentType"), ComponentType);

		if (bNormalized)
		{
			Writer.Write(TEXT("normalized"), bNormalized);
		}

		if (MinMaxLength > 0)
		{
			Writer.Write(TEXT("min"), Min, MinMaxLength);
			Writer.Write(TEXT("max"), Max, MinMaxLength);
		}
	}
};
