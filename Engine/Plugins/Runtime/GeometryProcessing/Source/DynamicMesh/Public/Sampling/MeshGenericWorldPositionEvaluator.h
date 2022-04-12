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
* Bake Colors based on arbitrary Position/Normal sampling of the base/target mesh.
* Useful for (eg) baking from an arbitrary function defined over space
*/
class DYNAMICMESH_API FMeshGenericWorldPositionColorEvaluator : public FMeshMapEvaluator
{
public:

	FVector4f DefaultColor = FVector4f(0, 0, 0, 1);
	TFunction<FVector4f(FVector3d, FVector3d)> ColorSampleFunction 
		= [this](FVector3d BaseSurfacePoint, FVector3d BaseSurfaceNormal) { return DefaultColor; };

public:
	virtual ~FMeshGenericWorldPositionColorEvaluator() {}

	// Begin FMeshMapEvaluator interface
	virtual void Setup(const FMeshBaseBaker& Baker, FEvaluationContext& Context) override;

	virtual EMeshMapEvaluatorType Type() const override { return EMeshMapEvaluatorType::GenericWorldPositionColor; }
	// End FMeshMapEvaluator interface

	static void EvaluateSample(float*& Out, const FCorrespondenceSample& Sample, void* EvalData);

	static void EvaluateDefault(float*& Out, void* EvalData);

	static void EvaluateColor(const int DataIdx, float*& In, FVector4f& Out, void* EvalData);
};



/**
 * Bake Tangent-Space Normals based on arbitrary Position/Normal sampling of the base/target mesh.
 * Useful for (eg) baking from an arbitrary function defined over space.
 */
class DYNAMICMESH_API FMeshGenericWorldPositionNormalEvaluator : public FMeshMapEvaluator
{
public:

	FVector3f DefaultUnitWorldNormal = FVector3f::UnitZ();
	TFunction<FVector3f(FVector3d, FVector3d)> UnitWorldNormalSampleFunction 
		= [this](FVector3d Position, FVector3d Normal) { return DefaultUnitWorldNormal; };

public:
	virtual ~FMeshGenericWorldPositionNormalEvaluator() {}

	// Begin FMeshMapEvaluator interface
	virtual void Setup(const FMeshBaseBaker& Baker, FEvaluationContext& Context) override;

	virtual EMeshMapEvaluatorType Type() const override { return EMeshMapEvaluatorType::GenericWorldPositionNormal; }
	// End FMeshMapEvaluator interface

	static void EvaluateSample(float*& Out, const FCorrespondenceSample& Sample, void* EvalData);

	static void EvaluateDefault(float*& Out, void* EvalData);

	static void EvaluateColor(const int DataIdx, float*& In, FVector4f& Out, void* EvalData);

private:

	FVector3f EvaluateSampleImpl(const FCorrespondenceSample& SampleData) const;
	const FMeshTangentsd* BaseMeshTangents = nullptr;
};


} // end namespace UE::Geometry
} // end namespace UE