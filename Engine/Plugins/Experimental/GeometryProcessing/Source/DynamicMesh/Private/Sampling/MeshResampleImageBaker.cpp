// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sampling/MeshResampleImageBaker.h"
#include "Sampling/MeshMapBaker.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

void FMeshResampleImageBaker::Bake()
{
	const FMeshImageBakingCache* BakeCache = GetCache();
	check(BakeCache);
	DetailMesh = BakeCache->GetDetailMesh();

	check(DetailUVOverlay);

	ResultBuilder = MakeUnique<TImageBuilder<FVector4f>>();
	ResultBuilder->SetDimensions(BakeCache->GetDimensions());
	ResultBuilder->Clear(DefaultColor);

	BakeCache->EvaluateSamples([&](const FVector2i& Coords, const FMeshImageBakingCache::FCorrespondenceSample& Sample)
	{
		FVector4f Color = PropertySampleFunction<FMeshImageBakingCache::FCorrespondenceSample>(Sample);
		ResultBuilder->SetPixel(Coords, Color);
	});

	const FImageOccupancyMap& Occupancy = *BakeCache->GetOccupancyMap();

	for (int64 k = 0; k < Occupancy.GutterTexels.Num(); k++)
	{
		TPair<int64, int64> GutterTexel = Occupancy.GutterTexels[k];
		ResultBuilder->CopyPixel(GutterTexel.Value, GutterTexel.Key);
	}

}

void FMeshResampleImageBaker::PreEvaluate(const FMeshMapBaker& Baker)
{
	DetailMesh = Baker.GetDetailMesh();
}

FVector4f FMeshResampleImageBaker::EvaluateSample(const FMeshMapBaker& Baker, const FCorrespondenceSample& Sample)
{
	return PropertySampleFunction<FCorrespondenceSample>(Sample);
}

template <class SampleType>
FVector4f FMeshResampleImageBaker::PropertySampleFunction(const SampleType& SampleData)
{
	FVector4f Color = DefaultColor;
	int32 DetailTriID = SampleData.DetailTriID;
	if (DetailMesh->IsTriangle(SampleData.DetailTriID) && DetailUVOverlay)
	{
		FVector2d DetailUV;
		DetailUVOverlay->GetTriBaryInterpolate<double>(DetailTriID, &SampleData.DetailBaryCoords.X, &DetailUV.X);

		Color = SampleFunction(DetailUV);
	}
	return Color;
}




void FMeshMultiResampleImageBaker::InitResult()
{
	const FMeshImageBakingCache* BakeCache = GetCache();
	ResultBuilder = MakeUnique<TImageBuilder<FVector4f>>();
	ResultBuilder->SetDimensions(BakeCache->GetDimensions());
	ResultBuilder->Clear(DefaultColor);
}


void FMeshMultiResampleImageBaker::BakeMaterial(int32 MaterialID)
{
	const FMeshImageBakingCache* BakeCache = GetCache();
	if (!ensure(BakeCache))
	{
		return;
	}
	DetailMesh = BakeCache->GetDetailMesh();
	if (!ensure(DetailMesh))
	{
		return;
	}
	DetailMaterialIDAttrib = DetailMesh->Attributes()->GetMaterialID();
	if (!ensure(DetailMaterialIDAttrib))
	{
		return;
	}
	if (!ensure(DetailUVOverlay))
	{
		return;
	}

	BakeCache->EvaluateSamples([this, MaterialID](const FVector2i& Coords, const FMeshImageBakingCache::FCorrespondenceSample& Sample)
	{
		int32 DetailTriID = Sample.DetailTriID;

		if (DetailMesh->IsTriangle(DetailTriID))
		{
			if (DetailMaterialIDAttrib->GetValue(DetailTriID) == MaterialID)
			{
				FVector2d DetailUV;
				DetailUVOverlay->GetTriBaryInterpolate<double>(DetailTriID, &Sample.DetailBaryCoords.X, &DetailUV.X);
				FVector4f Color = SampleFunction(DetailUV);
				ResultBuilder->SetPixel(Coords, Color);
			}
			// otherwise leave the pixel alone
		}
	});

	const FImageOccupancyMap& Occupancy = *BakeCache->GetOccupancyMap();

	for (int64 k = 0; k < Occupancy.GutterTexels.Num(); k++)
	{
		TPair<int64, int64> GutterTexel = Occupancy.GutterTexels[k];
		ResultBuilder->CopyPixel(GutterTexel.Value, GutterTexel.Key);
	}
}


void FMeshMultiResampleImageBaker::Bake()
{
	InitResult();

	// Write into the sample buffer, separate pass for each material ID
	for (TPair< int32, TSharedPtr<UE::Geometry::TImageBuilder<FVector4f>, ESPMode::ThreadSafe>>& MaterialTexture : MultiTextures)
	{
		int32 MaterialID = MaterialTexture.Key;
		TSharedPtr<UE::Geometry::TImageBuilder<FVector4f>, ESPMode::ThreadSafe> TextureImage = MaterialTexture.Value;

		this->SampleFunction = [&TextureImage](FVector2d UVCoord) 
		{
			return TextureImage->BilinearSampleUV<float>(UVCoord, FVector4f(0, 0, 0, 1));
		};

		BakeMaterial(MaterialID);
	}

}

void FMeshMultiResampleImageBaker::PreEvaluate(const FMeshMapBaker& Baker)
{
	DetailMesh = Baker.GetDetailMesh();
	DetailMaterialIDAttrib = ensure(DetailMesh) ? DetailMesh->Attributes()->GetMaterialID() : nullptr;
	bValidDetailMesh = ensure(DetailMaterialIDAttrib) && ensure(DetailUVOverlay);
}

FVector4f FMeshMultiResampleImageBaker::EvaluateSample(const FMeshMapBaker& Baker, const FCorrespondenceSample& Sample)
{
	if (!bValidDetailMesh)
	{
		return DefaultSample();
	}

	FVector4f Color = DefaultSample();
	int32 DetailTriID = Sample.DetailTriID;
	if (DetailMesh->IsTriangle(DetailTriID))
	{
		int32 MaterialID = DetailMaterialIDAttrib->GetValue(DetailTriID);
		TSharedPtr<UE::Geometry::TImageBuilder<FVector4f>, ESPMode::ThreadSafe> TextureImage = MultiTextures.FindRef(MaterialID);
		if (TextureImage)
		{
			FVector2d DetailUV;
			DetailUVOverlay->GetTriBaryInterpolate<double>(DetailTriID, &Sample.DetailBaryCoords.X, &DetailUV.X);
			Color = TextureImage->BilinearSampleUV<float>(DetailUV, FVector4f(0, 0, 0, 1));
		}
	}
	return Color;
}
