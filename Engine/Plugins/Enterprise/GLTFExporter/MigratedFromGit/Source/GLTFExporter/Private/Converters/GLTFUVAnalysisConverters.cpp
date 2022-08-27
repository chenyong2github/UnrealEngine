// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFUVAnalysisConverters.h"
#include "Converters/GLTFMaterialUtility.h"
#include "StaticMeshAttributes.h"
#include "MeshDescription.h"
#include "Modules/ModuleManager.h"
#include "GLTFMaterialBaking/Public/IMaterialBakingModule.h"
#include "GLTFMaterialBaking/Public/MaterialBakingStructures.h"

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

	return Outputs.Add_GetRef(MakeUnique<FGLTFUVAnalysis>(CalcOverlapPercentage(TexCoord, TextureSize, *Description, SectionIndices))).Get();
}

const UMaterialInterface* FGLTFUVAnalysisConverter::GetOverlapMaterial()
{
	static const UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/GLTFExporter/Materials/OverlappingUVs.OverlappingUVs"));
	check(Material);
	return Material;
}

float FGLTFUVAnalysisConverter::CalcOverlapPercentage(int32 TexCoord, const FIntPoint& OutputSize, const FMeshDescription& MeshDescription, const TArray<int32>& MeshSectionIndices)
{
	const UMaterialInterface* Material = GetOverlapMaterial();
	const FMaterialPropertyEx Property = MP_EmissiveColor;

	FMeshData MeshSet;
	MeshSet.TextureCoordinateBox = { { 0.0f, 0.0f }, { 1.0f, 1.0f } };
	MeshSet.TextureCoordinateIndex = TexCoord;
	MeshSet.RawMeshDescription = const_cast<FMeshDescription*>(&MeshDescription);
	MeshSet.MaterialIndices = MeshSectionIndices; // NOTE: MaterialIndices is actually section indices

	FMaterialDataEx MatSet;
	MatSet.Material = const_cast<UMaterialInterface*>(Material);
	MatSet.PropertySizes.Add(Property, OutputSize);
	MatSet.BlendMode = Material->GetBlendMode();
	MatSet.bPerformBorderSmear = false;

	TArray<FMeshData*> MeshSettings;
	TArray<FMaterialDataEx*> MatSettings;
	MeshSettings.Add(&MeshSet);
	MatSettings.Add(&MatSet);

	TArray<FBakeOutputEx> BakeOutputs;
	IMaterialBakingModule& Module = FModuleManager::Get().LoadModuleChecked<IMaterialBakingModule>("GLTFMaterialBaking");

	Module.BakeMaterials(MatSettings, MeshSettings, BakeOutputs);

	FBakeOutputEx& BakeOutput = BakeOutputs[0];
	TArray<FColor> BakedPixels = MoveTemp(BakeOutput.PropertyData.FindChecked(Property));

	// NOTE: the emissive value of each pixel will be incremented by 10 each time a triangle is drawn on it.
	// Therefore a value of 0.0 indicates an unreferenced pixel, a value of 10.0 indicates a uniquely referenced pixel,
	// and a value of 20+ indicates an overlapping pixel.
	// The value may differ somewhat from what was specified in the material due to conversions (color-space, gamma, float => uint etc),
	// so to be safe we set the limit at 15 instead of 10 to prevent incorrectly flagging some pixels as overlapping.
	const float EmissiveThreshold = 15.0f;

	uint32 ProcessedPixels = 0;
	uint32 OverlappingPixels = 0;

	if (BakeOutput.EmissiveScale > EmissiveThreshold)
	{
		const FColor Magenta(255, 0, 255, 255);
		const uint8 ColorThreshold = FMath::RoundToInt((EmissiveThreshold / BakeOutput.EmissiveScale) * 255.0f); // This should never overflow since we know that the quotient will be <= 1.0

		bool bIsMagenta;
		bool bIsOverlapping;

		for (const FColor& Pixel: BakedPixels)
		{
			bIsMagenta = Pixel == Magenta;
			bIsOverlapping = Pixel.G > ColorThreshold;

			ProcessedPixels += !bIsMagenta ? 1 : 0;
			OverlappingPixels += !bIsMagenta && bIsOverlapping ? 1 : 0;
		}
	}

	return ProcessedPixels > 0
		? (static_cast<float>(OverlappingPixels) / ProcessedPixels) * 100.0f
		: 0.0f;
}
