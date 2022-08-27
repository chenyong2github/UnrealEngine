// Copyright Epic Games, Inc. All Rights Reserved.

#include "Json/GLTFJsonImage.h"

void FGLTFJsonImage::WriteObject(IGLTFJsonWriter& Writer) const
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
