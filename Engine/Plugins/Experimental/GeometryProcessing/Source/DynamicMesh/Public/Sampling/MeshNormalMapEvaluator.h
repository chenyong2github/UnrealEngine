// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Sampling/MeshMapEvaluator.h"
#include "MeshTangents.h"

namespace UE
{
namespace Geometry
{

class DYNAMICMESH_API FMeshNormalMapEvaluator : public FMeshMapEvaluator
{
public:
	// Begin FMeshMapEvaluator interface
	virtual void Setup(const FMeshMapBaker& Baker, FEvaluationContext& Context) override;

	virtual EMeshMapEvaluatorType Type() const override { return EMeshMapEvaluatorType::Normal; }
	// End FMeshMapEvaluator interface

	static void EvaluateSample(float*& Out, const FCorrespondenceSample& Sample, void* EvalData);

	static void EvaluateDefault(float*& Out, void* EvalData);

protected:
	// Cached data
	const FDynamicMesh3* DetailMesh = nullptr;
	const FDynamicMeshNormalOverlay* DetailNormalOverlay = nullptr;
	const TMeshTangents<double>* BaseMeshTangents;

	FVector3f DefaultNormal = FVector3f::UnitZ();

private:
	FVector3f SampleFunction(const FCorrespondenceSample& Sample);
};

} // end namespace UE::Geometry
} // end namespace UE

