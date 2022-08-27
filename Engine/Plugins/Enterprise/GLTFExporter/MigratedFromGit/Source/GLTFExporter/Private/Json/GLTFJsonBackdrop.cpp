// Copyright Epic Games, Inc. All Rights Reserved.

#include "Json/GLTFJsonBackdrop.h"

void FGLTFJsonBackdrop::WriteObject(IGLTFJsonWriter& Writer) const
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
