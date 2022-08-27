// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonObject.h"
#include "Json/GLTFJsonEnums.h"
#include "Json/GLTFJsonIndex.h"

struct FGLTFJsonImage : IGLTFJsonObject
{
	FString Name;
	FString Uri;

	EGLTFJsonMimeType MimeType;

	FGLTFJsonBufferViewIndex BufferView;

	FGLTFJsonImage()
		: MimeType(EGLTFJsonMimeType::None)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		if (!Name.IsEmpty())
		{
			Writer.Write(TEXT("name"), Name);
		}

		if (!Uri.IsEmpty())
		{
			Writer.Write(TEXT("uri"), Uri);
		}

		if (MimeType != EGLTFJsonMimeType::None)
		{
			Writer.Write(TEXT("mimeType"), MimeType);
		}

		if (BufferView != INDEX_NONE)
		{
			Writer.Write(TEXT("bufferView"), BufferView);
		}
	}
};
