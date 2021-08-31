// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Sampling/MeshMapEvaluator.h"
#include "DynamicMesh/MeshTangents.h"
#include "Image/ImageBuilder.h"

namespace UE
{
namespace Geometry
{

/**
 * A mesh evaluator for tangent space normals.
 */
class DYNAMICMESH_API FMeshNormalMapEvaluator : public FMeshMapEvaluator
{
public:
	// Begin FMeshMapEvaluator interface
	virtual void Setup(const FMeshBaseBaker& Baker, FEvaluationContext& Context) override;

	virtual EMeshMapEvaluatorType Type() const override { return EMeshMapEvaluatorType::Normal; }
	// End FMeshMapEvaluator interface

	template <bool bUseDetailNormalMap>
	static void EvaluateSample(float*& Out, const FCorrespondenceSample& Sample, void* EvalData);

	static void EvaluateDefault(float*& Out, void* EvalData);

	static void EvaluateColor(const int DataIdx, float*& In, FVector4f& Out, void* EvalData);

protected:
	// Cached data
	const FDynamicMesh3* DetailMesh = nullptr;
	const FDynamicMeshNormalOverlay* DetailNormalOverlay = nullptr;
	const FDynamicMeshUVOverlay* DetailUVOverlay = nullptr;
	const TMeshTangents<double>* DetailMeshTangents = nullptr;
	const TImageBuilder<FVector4f>* DetailMeshNormalMap = nullptr;
	const TMeshTangents<double>* BaseMeshTangents = nullptr;

	FVector3f DefaultNormal = FVector3f::UnitZ();

private:
	template <bool bUseDetailNormalMap>
	FVector3f SampleFunction(const FCorrespondenceSample& SampleData) const;

	FVector4f SampleNormalMapFunction(const FVector2d& UVCoord) const;
};

} // end namespace UE::Geometry
} // end namespace UE

