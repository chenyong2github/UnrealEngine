// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

struct FGLTFMaterialAnalysis
{
	/** Tracks the texture coordinates used by this material */
	TBitArray<> TextureCoordinates;

	/** Will contain all the shading models picked up from the material expression graph */
	FMaterialShadingModelField ShadingModels;

	/** True if this material reads any vertex data */
	bool bRequiresVertexData;
};
