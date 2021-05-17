// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sampling/MeshNormalMapBaker.h"
#include "Sampling/MeshMapBaker.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

void FMeshNormalMapBaker::Bake()
{
	const FMeshImageBakingCache* BakeCache = GetCache();
	check(BakeCache);
	DetailMesh = BakeCache->GetDetailMesh();
	DetailNormalOverlay = BakeCache->GetDetailNormals();
	check(DetailNormalOverlay);

	// make sure we have per-triangle tangents computed so that GetInterpolatedTriangleTangent() below works
	int32 MaxTriID = BakeCache->GetBakeTargetMesh()->MaxTriangleID();
	check( BaseMeshTangents->GetTangents().Num() >= 3*BakeCache->GetBakeTargetMesh()->MaxTriangleID());

	NormalsBuilder = MakeUnique<TImageBuilder<FVector3f>>();
	NormalsBuilder->SetDimensions(BakeCache->GetDimensions());
	FVector3f DefaultMapNormal = (DefaultNormal + FVector3f::One()) * 0.5f;
	NormalsBuilder->Clear(DefaultMapNormal);

	BakeCache->EvaluateSamples([&](const FVector2i& Coords, const FMeshImageBakingCache::FCorrespondenceSample& Sample)
	{
		FVector3f RelativeDetailNormal = SampleFunction<FMeshImageBakingCache::FCorrespondenceSample>(Sample);
		FVector3f MapNormal = (RelativeDetailNormal + FVector3f::One()) * 0.5f;
		NormalsBuilder->SetPixel(Coords, MapNormal);
	});

	const FImageOccupancyMap& Occupancy = *BakeCache->GetOccupancyMap();
	for (int64 k = 0; k < Occupancy.GutterTexels.Num(); k++)
	{
		TPair<int64, int64> GutterTexel = Occupancy.GutterTexels[k];
		NormalsBuilder->CopyPixel(GutterTexel.Value, GutterTexel.Key);
	}
}

void FMeshNormalMapBaker::PreEvaluate(const FMeshMapBaker& Baker)
{
	DetailMesh = Baker.GetDetailMesh();
	DetailNormalOverlay = Baker.GetDetailMeshNormals();
	check(DetailNormalOverlay);
	BaseMeshTangents = Baker.GetTargetMeshTangents().Get();

	// make sure we have per-triangle tangents computed so that GetInterpolatedTriangleTangent() below works
	int32 MaxTriID = Baker.GetTargetMesh()->MaxTriangleID();
	check(BaseMeshTangents->GetTangents().Num() >= 3 * Baker.GetTargetMesh()->MaxTriangleID());
}

FVector4f FMeshNormalMapBaker::EvaluateSample(const FMeshMapBaker& Baker, const FCorrespondenceSample& Sample)
{
	FVector3f SampleResult = SampleFunction<FCorrespondenceSample>(Sample);
	// Map normal space [-1,1] to floating point color space [0,1]
	FVector3f Result = (SampleResult + FVector3f::One()) * 0.5f;
	return FVector4f(Result.X, Result.Y, Result.Z, 1.0f);
}

template<class SampleType>
FVector3f FMeshNormalMapBaker::SampleFunction(const SampleType& SampleData)
{
	int32 DetailTriID = SampleData.DetailTriID;
	if (DetailMesh->IsTriangle(DetailTriID))
	{
		// get tangents on base mesh
		FVector3d BaseTangentX, BaseTangentY;
		BaseMeshTangents->GetInterpolatedTriangleTangent(
			SampleData.BaseSample.TriangleIndex,
			SampleData.BaseSample.BaryCoords,
			BaseTangentX, BaseTangentY);

		// sample normal on detail mesh
		FVector3d DetailNormal;
		DetailNormalOverlay->GetTriBaryInterpolate<double>(DetailTriID, &SampleData.DetailBaryCoords.X, &DetailNormal.X);
		Normalize(DetailNormal);

		// compute normal in tangent space
		double dx = DetailNormal.Dot(BaseTangentX);
		double dy = DetailNormal.Dot(BaseTangentY);
		double dz = DetailNormal.Dot(SampleData.BaseNormal);

		return FVector3f((float)dx, (float)dy, (float)dz);
	}
	return FVector3f::UnitZ();
}
