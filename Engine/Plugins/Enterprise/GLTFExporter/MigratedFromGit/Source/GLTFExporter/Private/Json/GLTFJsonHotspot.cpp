// Copyright Epic Games, Inc. All Rights Reserved.

#include "Json/GLTFJsonHotspot.h"

void FGLTFJsonHotspot::WriteObject(IGLTFJsonWriter& Writer) const
{
	if (!Name.IsEmpty())
	{
		Writer.Write(TEXT("name"), Name);
	}

	Writer.Write(TEXT("animation"), Animation);
	Writer.Write(TEXT("image"), Image);

	if (HoveredImage != INDEX_NONE)
	{
		Writer.Write(TEXT("hoveredImage"), HoveredImage);
	}

	if (ToggledImage != INDEX_NONE)
	{
		Writer.Write(TEXT("toggledImage"), ToggledImage);
	}

	if (ToggledHoveredImage != INDEX_NONE)
	{
		Writer.Write(TEXT("toggledHoveredImage"), ToggledHoveredImage);
	}
}
