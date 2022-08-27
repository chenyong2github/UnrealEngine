// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Converters/GLTFConverter.h"
#include "Converters/GLTFUVAnalysis.h"
#include "Engine.h"

class FGLTFUVAnalysisConverter final : public TGLTFConverter<FGLTFUVAnalysis, const FMeshDescription*, const TArray<int32>, int32>
{
	virtual void Sanitize(const FMeshDescription*& Description, const TArray<int32>& SectionIndices, int32& TexCoord) override;

	virtual FGLTFUVAnalysis Convert(const FMeshDescription* Description, const TArray<int32> SectionIndices, int32 TexCoord) override;

	static const UMaterialInterface* GetOverlapMaterial();

	static float CalcOverlapPercentage(int32 TexCoord, const FIntPoint& OutputSize, const FMeshDescription& MeshDescription, const TArray<int32>& MeshSectionIndices);
};
