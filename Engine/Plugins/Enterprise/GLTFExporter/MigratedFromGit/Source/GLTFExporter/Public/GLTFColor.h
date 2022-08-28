// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

// UE4 uses an endianness dependent memory layout (ABGR for little endian, ARGB for big endian)
// glTF uses RGBA independent of endianness, hence why we need this dedicated struct
struct GLTFEXPORTER_API FGLTFColor
{
	uint8 R,G,B,A;

	FORCEINLINE FGLTFColor(uint8 R, uint8 G, uint8 B, uint8 A = 255)
		: R(R), G(G), B(B), A(A)
	{
	}
};
