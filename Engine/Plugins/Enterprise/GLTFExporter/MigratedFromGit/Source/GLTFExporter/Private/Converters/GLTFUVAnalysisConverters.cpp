// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFUVAnalysisConverters.h"
#include "Converters/GLTFMaterialUtility.h"
#include "StaticMeshAttributes.h"

void FGLTFUVAnalysisConverter::Sanitize(const FMeshDescription*& Description, const TArray<int32>& SectionIndices, int32& TexCoord)
{
	if (Description != nullptr)
	{
		const TVertexInstanceAttributesConstRef<FVector2D> VertexInstanceUVs =
			Description->VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
		const int32 TexCoordCount = VertexInstanceUVs.GetNumIndices();

		if (TexCoord < 0 || TexCoord >= TexCoordCount)
		{
			Description = nullptr;
		}

		const int32 MinSectionIndex = FMath::Min(SectionIndices);
		const int32 MaxSectionIndex = FMath::Max(SectionIndices);
		const int32 SectionCount = Description->PolygonGroups().GetArraySize();

		if (MinSectionIndex < 0 || MaxSectionIndex >= SectionCount)
		{
			Description = nullptr;
		}
	}
}

const FGLTFUVAnalysis* FGLTFUVAnalysisConverter::Convert(const FMeshDescription* Description, const TArray<int32> SectionIndices, int32 TexCoord)
{
	if (Description == nullptr)
	{
		// TODO: add warning?

		return nullptr;
	}

	// TODO: investigate if the fixed size is high enough to properly calculate overlap
	const FIntPoint TextureSize(512, 512);
	const float OverlapPercentage = FGLTFMaterialUtility::CalcOverlappingUVPercentage(TexCoord, TextureSize, *Description, SectionIndices);

	return Outputs.Add_GetRef(MakeUnique<FGLTFUVAnalysis>(OverlapPercentage)).Get();
}
