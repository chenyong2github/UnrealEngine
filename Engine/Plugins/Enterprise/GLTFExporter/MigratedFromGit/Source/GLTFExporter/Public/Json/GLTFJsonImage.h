// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"

struct GLTFEXPORTER_API FGLTFJsonImage : IGLTFJsonIndexedObject
{
	FString Name;
	FString Uri;

	EGLTFJsonMimeType MimeType;

	FGLTFJsonBufferView* BufferView;

	FGLTFJsonImage(int32 Index = INDEX_NONE)
		: IGLTFJsonIndexedObject(Index)
		, MimeType(EGLTFJsonMimeType::None)
		, BufferView(nullptr)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};
