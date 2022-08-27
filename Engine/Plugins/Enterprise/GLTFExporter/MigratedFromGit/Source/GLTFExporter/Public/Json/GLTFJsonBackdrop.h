// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"

struct GLTFEXPORTER_API FGLTFJsonBackdrop : IGLTFJsonIndexedObject
{
	FString Name;

	FGLTFJsonMeshIndex Mesh;
	FGLTFJsonTextureIndex Cubemap[6];

	float Intensity;
	float Size;
	float Angle;

	FGLTFJsonVector3 ProjectionCenter;

	float LightingDistanceFactor;
	bool UseCameraProjection;

	FGLTFJsonBackdrop(int32 Index = INDEX_NONE)
		: IGLTFJsonIndexedObject(Index)
		, Intensity(0)
		, Size(0)
		, Angle(0)
		, ProjectionCenter(FGLTFJsonVector3::Zero)
		, LightingDistanceFactor(0)
		, UseCameraProjection(false)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};
