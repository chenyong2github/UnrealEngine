// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryScript/TextureMapFunctions.h"

#include "Async/ParallelFor.h"
#include "AssetUtils/Texture2DUtil.h"
#include "Spatial/SampledScalarField2.h"

using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UGeometryScriptLibrary_TextureMapFunctions"

void UGeometryScriptLibrary_TextureMapFunctions::SampleTexture2DAtUVPositions(
	FGeometryScriptUVList UVList,
	UTexture2D* TextureAsset,
	FGeometryScriptSampleTextureOptions SampleOptions,
	FGeometryScriptColorList& ColorList,
	UGeometryScriptDebug* Debug)
{
	if (TextureAsset == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("SampleTexture2DAtUVPositions_InvalidInput2", "SampleTexture2DAtUVPositions: Texture is Null"));
		return;
	}

	TImageBuilder<FVector4f> ImageData;
	if (UE::AssetUtils::ReadTexture(TextureAsset, ImageData, false) == false)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::OperationFailed, LOCTEXT("SampleTexture2DAtUVPositions_TexReadFailed", "SampleTexture2DAtUVPositions: Error reading source texture data"));
		return;
	}

	const TArray<FVector2D>& UVs = *UVList.List;
	int32 NumUVs = UVs.Num();

	ColorList.Reset();
	TArray<FLinearColor>& Colors = *ColorList.List;
	Colors.SetNumUninitialized(NumUVs);

	for (int32 k = 0; k < NumUVs; ++k)
	{
		FVector2d UV = UVs[k];

		// Adjust UV value and tile it. 
		UV = UV * SampleOptions.UVScale + SampleOptions.UVOffset;
		if (SampleOptions.bWrap)
		{
			UV = UV - FVector2d(FMathd::Floor(UV.X), FMathd::Floor(UV.Y));
		}
		else
		{
			UV.X = FMathd::Clamp(UV.X, 0.0, 1.0);
			UV.Y = FMathd::Clamp(UV.Y, 0.0, 1.0);
		}

		FVector4f InterpValue = (SampleOptions.SamplingMethod == EGeometryScriptPixelSamplingMethod::Bilinear) ?
			ImageData.BilinearSampleUV<double>(UV, FVector4f::Zero()) : ImageData.NearestSampleUV<double>(UV, FVector4f::Zero());

		Colors[k] = (FLinearColor)InterpValue;
	}


}


#undef LOCTEXT_NAMESPACE