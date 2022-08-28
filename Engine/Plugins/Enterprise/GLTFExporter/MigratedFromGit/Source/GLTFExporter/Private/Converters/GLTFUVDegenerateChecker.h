// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Converters/GLTFConverter.h"
#include "Engine.h"

class FGLTFUVDegenerateChecker final : public TGLTFConverter<float, const FMeshDescription*, TArray<int32>, int32>
{
	virtual void Sanitize(const FMeshDescription*& Description, TArray<int32>& SectionIndices, int32& TexCoord) override;

	virtual float Convert(const FMeshDescription* Description, TArray<int32> SectionIndices, int32 TexCoord) override;

	static bool IsDegenerateTriangle(const TStaticArray<FVector2D, 3>& Points);
	static bool IsDegenerateTriangle(const TStaticArray<FVector, 3>& Points);
};
