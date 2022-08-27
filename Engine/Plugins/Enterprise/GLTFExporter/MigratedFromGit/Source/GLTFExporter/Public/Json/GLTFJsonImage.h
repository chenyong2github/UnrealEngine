// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonObject.h"
#include "Json/GLTFJsonEnums.h"
#include "Json/GLTFJsonIndex.h"

struct GLTFEXPORTER_API FGLTFJsonImage : IGLTFJsonObject
{
	FString Name;
	FString Uri;

	EGLTFJsonMimeType MimeType;

	FGLTFJsonBufferViewIndex BufferView;

	FGLTFJsonImage()
		: MimeType(EGLTFJsonMimeType::None)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};
