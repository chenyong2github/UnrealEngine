// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonObject.h"

struct FGLTFJsonBuffer : IGLTFJsonObject
{
	FString Name;

	FString URI;
	int64   ByteLength;

	FGLTFJsonBuffer()
		: ByteLength(0)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		if (!Name.IsEmpty())
		{
			Writer.Write(TEXT("name"), Name);
		}

		if (!URI.IsEmpty())
		{
			Writer.Write(TEXT("uri"), URI);
		}

		Writer.Write(TEXT("byteLength"), ByteLength);
	}
};
