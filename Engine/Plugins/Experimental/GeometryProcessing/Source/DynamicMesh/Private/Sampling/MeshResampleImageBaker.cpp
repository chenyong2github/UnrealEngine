// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sampling/MeshResampleImageBaker.h"

void FMeshResampleImageBaker::Bake()
{
	const FMeshImageBakingCache* BakeCache = GetCache();
	check(BakeCache);
	const FDynamicMesh3* DetailMesh = BakeCache->GetDetailMesh();

	check(DetailUVOverlay);

	FVector4f DefaultValue(0, 0, 0, 1.0);

	auto PropertySampleFunction = [&](const FMeshImageBakingCache::FCorrespondenceSample& SampleData)
	{
		FVector4f Color = DefaultValue;
		int32 DetailTriID = SampleData.DetailTriID;
		if (DetailMesh->IsTriangle(SampleData.DetailTriID) && DetailUVOverlay)
		{
			FVector2d DetailUV;
			DetailUVOverlay->GetTriBaryInterpolate<double>(DetailTriID, &SampleData.DetailBaryCoords[0], &DetailUV.X);

			Color = SampleFunction(DetailUV);
		}
		return Color;
	};

	ResultBuilder = MakeUnique<TImageBuilder<FVector4f>>();
	ResultBuilder->SetDimensions(BakeCache->GetDimensions());
	ResultBuilder->Clear(DefaultColor);

	BakeCache->EvaluateSamples([&](const FVector2i& Coords, const FMeshImageBakingCache::FCorrespondenceSample& Sample)
	{
		FVector4f Color = PropertySampleFunction(Sample);
		ResultBuilder->SetPixel(Coords, Color);
	});

	const FImageOccupancyMap& Occupancy = *BakeCache->GetOccupancyMap();

	for (int64 k = 0; k < Occupancy.GutterTexels.Num(); k++)
	{
		TPair<int64, int64> GutterTexel = Occupancy.GutterTexels[k];
		ResultBuilder->CopyPixel(GutterTexel.Value, GutterTexel.Key);
	}

}