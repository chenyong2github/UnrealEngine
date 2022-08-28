// Copyright Epic Games, Inc. All Rights Reserved.

#include "Json/GLTFJsonHotspot.h"
#include "Json/GLTFJsonAnimation.h"
#include "Json/GLTFJsonTexture.h"

void FGLTFJsonHotspot::WriteObject(IGLTFJsonWriter& Writer) const
{
	if (!Name.IsEmpty())
	{
		Writer.Write(TEXT("name"), Name);
	}

	Writer.Write(TEXT("animation"), Animation);
	Writer.Write(TEXT("image"), Image);

	if (HoveredImage != nullptr)
	{
		Writer.Write(TEXT("hoveredImage"), HoveredImage);
	}

	if (ToggledImage != nullptr)
	{
		Writer.Write(TEXT("toggledImage"), ToggledImage);
	}

	if (ToggledHoveredImage != nullptr)
	{
		Writer.Write(TEXT("toggledHoveredImage"), ToggledHoveredImage);
	}
}
