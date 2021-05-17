// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "Sampling/MeshImageBaker.h"
#include "Sampling/MeshImageBakingCache.h"
#include "Image/ImageBuilder.h"

namespace UE
{
namespace Geometry
{

class DYNAMICMESH_API FMeshResampleImageBaker : public FMeshImageBaker
{
public:
	virtual ~FMeshResampleImageBaker() {}

	//
	// Required input data
	//

	TFunction<FVector4f(FVector2d)> SampleFunction = [](FVector2d Position) { return FVector4f::Zero(); };
	const FDynamicMeshUVOverlay* DetailUVOverlay = nullptr;

	FVector4f DefaultColor = FVector4f(0, 0, 0, 1);

	//
	// Compute functions
	//

	virtual void Bake() override;

	//
	// Output
	//

	const TUniquePtr<TImageBuilder<FVector4f>>& GetResult() const { return ResultBuilder; }

	TUniquePtr<TImageBuilder<FVector4f>> TakeResult() { return MoveTemp(ResultBuilder); }

protected:
	TUniquePtr<TImageBuilder<FVector4f>> ResultBuilder;

public:
	//
	// New Baker interface
	//

	/** Invoked at start of bake to initialize sampler. */
	virtual void PreEvaluate(const FMeshMapBaker& Baker) override;

	virtual FVector4f EvaluateSample(const FMeshMapBaker& Baker, const FCorrespondenceSample& Sample) override;

	virtual FVector4f DefaultSample() const override { return FVector4f(0.0f, 0.0f, 0.0f, 1.0f); }

protected:
	// Cached data
	const FDynamicMesh3* DetailMesh = nullptr;

private:
	template <class SampleType>
	FVector4f PropertySampleFunction(const SampleType& Sample);
};



class DYNAMICMESH_API FMeshMultiResampleImageBaker : public FMeshResampleImageBaker
{
public:

	TMap<int32, TSharedPtr<UE::Geometry::TImageBuilder<FVector4f>, ESPMode::ThreadSafe>> MultiTextures;

	virtual void Bake() override;

protected:

	void InitResult();
	void BakeMaterial(int32 MaterialID);

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
	const FDynamicMeshMaterialAttribute* DetailMaterialIDAttrib = nullptr;
	bool bValidDetailMesh = false;
};

} // end namespace UE::Geometry
} // end namespace UE
