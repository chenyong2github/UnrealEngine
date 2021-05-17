// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Sampling/MeshImageBaker.h"
#include "Sampling/MeshImageBakingCache.h"
#include "Image/ImageBuilder.h"

namespace UE
{
namespace Geometry
{

class FMeshVertexCurvatureCache;

class DYNAMICMESH_API FMeshCurvatureMapBaker : public FMeshImageBaker
{
public:
	virtual ~FMeshCurvatureMapBaker() {}

	//
	// Options
	//

	enum class ECurvatureType
	{
		Mean = 0,
		Gaussian = 1,
		MaxPrincipal = 2,
		MinPrincipal = 3
	};
	ECurvatureType UseCurvatureType = ECurvatureType::Mean;

	enum class EColorMode
	{
		BlackGrayWhite = 0,
		RedGreenBlue = 1,
		RedBlue = 2
	};
	EColorMode UseColorMode = EColorMode::RedGreenBlue;

	enum class EClampMode
	{
		FullRange = 0,
		Positive = 1,
		Negative = 2
	};
	EClampMode UseClampMode = EClampMode::FullRange;

	double RangeScale = 1.0;
	double MinRangeScale = 0.0;

	// allows override of the max curvature; if false, range is set based on [-(avg+stddev), avg+stddev]
	bool bOverrideCurvatureRange = false;
	double OverrideRangeMax = .1;


	double BlurRadius = 0.0;

	//
	// Required input data, can be provided, will be computed otherwise
	//

	TSharedPtr<FMeshVertexCurvatureCache> Curvatures;


	//
	// Compute functions
	//

	/** Calculate bake result */
	virtual void Bake() override;

	/** populate Curvatures member if valid data has not been provided */
	void CacheDetailCurvatures(const FDynamicMesh3* DetailMesh);

	//
	// Output
	//

	const TUniquePtr<TImageBuilder<FVector3f>>& GetResult() const { return ResultBuilder; }

	TUniquePtr<TImageBuilder<FVector3f>> TakeResult() { return MoveTemp(ResultBuilder); }


protected:
	TUniquePtr<TImageBuilder<FVector3f>> ResultBuilder;

	void Bake_Single();

	void Bake_Multi();

	double GetCurvature(int32 vid);

	void GetColorMapRange(FVector3f& NegativeColor, FVector3f& ZeroColor, FVector3f& PositiveColor);

public:
	//
	// New Baker interface
	//

	/** Invoked at start of bake to initialize baker. */
	virtual void PreEvaluate(const FMeshMapBaker& Baker) override;

	/** Evaluate the sample at this mesh correspondence point. */
	virtual FVector4f EvaluateSample(const FMeshMapBaker& Baker, const FCorrespondenceSample& Sample) override;

	/** @return the default sample value for the baker. */
	virtual FVector4f DefaultSample() const override { return FVector4f(0.0f, 0.0f, 0.0f, 1.0f); }

protected:
	// Cached data
	const FDynamicMesh3* DetailMesh = nullptr;
	double MinPreClamp = -TNumericLimits<double>::Max();
	double MaxPreClamp = TNumericLimits<double>::Max();
	FInterval1d ClampRange;
	FVector3f NegativeColor;
	FVector3f ZeroColor;
	FVector3f PositiveColor;

private:
	template <class SampleType>
	double SampleFunction(const SampleType& Sample);
};


} // end namespace UE::Geometry
} // end namespace UE
