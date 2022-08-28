// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"

struct GLTFEXPORTER_API FGLTFJsonBuffer : IGLTFJsonObject
{
	FString Name;

	FString URI;
	int64   ByteLength;

	FGLTFJsonBuffer()
		: ByteLength(0)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};
