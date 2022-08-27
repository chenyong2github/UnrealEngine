// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

struct FGLTFMaterialAnalysis
{
	/** Tracks the texture coordinates used by this material */
	TBitArray<> TextureCoordinates;

	/** True if this material reads any vertex data */
	bool bRequiresVertexData;
};
