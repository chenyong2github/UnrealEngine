// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"

struct GLTFEXPORTER_API FGLTFJsonBuffer : IGLTFJsonIndexedObject
{
	FString Name;

	FString URI;
	int64   ByteLength;

	FGLTFJsonBuffer(int32 Index = INDEX_NONE)
		: IGLTFJsonIndexedObject(Index)
		, ByteLength(0)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};
