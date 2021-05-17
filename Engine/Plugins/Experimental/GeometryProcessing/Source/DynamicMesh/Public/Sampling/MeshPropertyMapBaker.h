// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "Sampling/MeshImageBaker.h"
#include "Sampling/MeshImageBakingCache.h"
#include "Image/ImageBuilder.h"

namespace UE
{
namespace Geometry
{

enum class EMeshPropertyBakeType
{
	Position = 1,
	Normal = 2,
	FacetNormal = 3,
	UVPosition = 4,
	MaterialID = 5
};


class DYNAMICMESH_API FMeshPropertyMapBaker : public FMeshImageBaker
{
public:
	virtual ~FMeshPropertyMapBaker() {}

	//
	// Options
	//

	EMeshPropertyBakeType Property = EMeshPropertyBakeType::Normal;

	//
	// Compute functions
	//

	virtual void Bake() override;

	//
	// Output
	//

	const TUniquePtr<TImageBuilder<FVector3f>>& GetResult() const { return ResultBuilder; }

	TUniquePtr<TImageBuilder<FVector3f>> TakeResult() { return MoveTemp(ResultBuilder); }

protected:
	TUniquePtr<TImageBuilder<FVector3f>> ResultBuilder;

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

	/** @return true if this baker supports multisampling. */
	virtual bool SupportsMultisampling() const override { return Property != EMeshPropertyBakeType::MaterialID; }

protected:
	// Cached data
	const FDynamicMesh3* DetailMesh = nullptr;
	const FDynamicMeshNormalOverlay* DetailNormalOverlay = nullptr;
	const FDynamicMeshUVOverlay* DetailUVOverlay = nullptr;
	FAxisAlignedBox3d Bounds;
	FVector3f DefaultValue = FVector3f::Zero();

private:
	template <class SampleType>
	FVector3f SampleFunction(const SampleType& Sample);
};

} // end namespace UE::Geometry
} // end namespace UE
