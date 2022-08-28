// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonObject.h"
#include "Json/GLTFJsonIndex.h"
#include "Json/GLTFJsonVector3.h"

struct FGLTFJsonBackdrop : IGLTFJsonObject
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

	FGLTFJsonBackdrop()
		: Intensity(0)
		, Size(0)
		, Angle(0)
		, ProjectionCenter(FGLTFJsonVector3::Zero)
		, LightingDistanceFactor(0)
		, UseCameraProjection(false)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		if (!Name.IsEmpty())
		{
			Writer.Write(TEXT("name"), Name);
		}

		if (Mesh != INDEX_NONE)
		{
			Writer.Write(TEXT("mesh"), Mesh);
		}

		Writer.Write(TEXT("cubemap"), Cubemap);

		Writer.Write(TEXT("intensity"), Intensity);
		Writer.Write(TEXT("size"), Size);

		if (!FMath::IsNearlyZero(Angle))
		{
			Writer.Write(TEXT("angle"), Angle);
		}

		Writer.Write(TEXT("projectionCenter"), ProjectionCenter);

		Writer.Write(TEXT("lightingDistanceFactor"), LightingDistanceFactor);
		Writer.Write(TEXT("useCameraProjection"), UseCameraProjection);
	}
};
