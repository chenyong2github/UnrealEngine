// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/Tuple.h"
#include "Containers/Map.h"
#include "Sampling/MeshMapEvaluator.h"
#include "Sampling/MeshBakerCommon.h"
#include "DynamicMesh/MeshTangents.h"

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
	const IMeshBakerDetailSampler* DetailSampler = nullptr;
	using FDetailNormalTexture = IMeshBakerDetailSampler::FBakeDetailTexture;
	using FDetailNormalTextureMap = TMap<const void*, FDetailNormalTexture>;
	FDetailNormalTextureMap DetailNormalTextures;
	bool bHasDetailNormalTextures = false;
	const TMeshTangents<double>* BaseMeshTangents = nullptr;

	FVector3f DefaultNormal = FVector3f::UnitZ();

private:
	template <bool bUseDetailNormalMap>
	FVector3f SampleFunction(const FCorrespondenceSample& SampleData) const;
};

} // end namespace UE::Geometry
} // end namespace UE

