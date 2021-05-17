// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "Sampling/MeshImageBaker.h"
#include "Sampling/MeshImageBakingCache.h"
#include "Image/ImageBuilder.h"
#include "MeshTangents.h"

namespace UE
{
namespace Geometry
{

class DYNAMICMESH_API FMeshNormalMapBaker : public FMeshImageBaker
{
public:
	virtual ~FMeshNormalMapBaker() {}

	//
	// Required input data
	//

	const TMeshTangents<double>* BaseMeshTangents;

	FVector3f DefaultNormal = FVector3f::UnitZ();

	//
	// Compute functions
	//

	virtual void Bake() override;

	//
	// Output
	//

	const TUniquePtr<TImageBuilder<FVector3f>>& GetResult() const { return NormalsBuilder; }
	
	TUniquePtr<TImageBuilder<FVector3f>> TakeResult() { return MoveTemp(NormalsBuilder); }

protected:
	TUniquePtr<TImageBuilder<FVector3f>> NormalsBuilder;

public:
	//
	// New Baker interface
	//

	/** Invoked at start of bake to initialize baker. */
	virtual void PreEvaluate(const FMeshMapBaker& Baker) override;

	/** Evaluate the sample at this mesh correspondence point. */
	virtual FVector4f EvaluateSample(const FMeshMapBaker& Baker, const FCorrespondenceSample& Sample) override;

	/** @return the default sample value for the baker. */
	virtual FVector4f DefaultSample() const override
	{
		// Map normal space [-1,1] to floating point color space [0,1]
		FVector3f Normal = (DefaultNormal + FVector3f::One()) * 0.5f;
		return FVector4f(Normal.X, Normal.Y, Normal.Z, 1.0f);
	}

protected:
	// Cached data
	const FDynamicMesh3* DetailMesh = nullptr;
	const FDynamicMeshNormalOverlay* DetailNormalOverlay = nullptr;

private:
	template <class SampleType>
	FVector3f SampleFunction(const SampleType& Sample);
};


} // end namespace UE::Geometry
} // end namespace UE
