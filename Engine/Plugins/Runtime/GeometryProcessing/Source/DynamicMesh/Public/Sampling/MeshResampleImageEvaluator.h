// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Sampling/MeshMapEvaluator.h"
#include "Image/ImageBuilder.h"

namespace UE
{
namespace Geometry
{

/**
 * A mesh evaluator for sampling 2D texture data. 
 */
class DYNAMICMESH_API FMeshResampleImageEvaluator : public FMeshMapEvaluator
{
public:
	TFunction<FVector4f(FVector2d)> SampleFunction = [](FVector2d Position) { return FVector4f::Zero(); };
	const FDynamicMeshUVOverlay* DetailUVOverlay = nullptr;

	FVector4f DefaultColor = FVector4f(0, 0, 0, 1);

public:
	// Begin FMeshMapEvaluator interface
	virtual void Setup(const FMeshBaseBaker& Baker, FEvaluationContext& Context) override;

	virtual EMeshMapEvaluatorType Type() const override { return EMeshMapEvaluatorType::ResampleImage; }
	// End FMeshMapEvaluator interface

	static void EvaluateSample(float*& Out, const FCorrespondenceSample& Sample, void* EvalData);

	static void EvaluateDefault(float*& Out, void* EvalData);

	static void EvaluateColor(const int DataIdx, float*& In, FVector4f& Out, void* EvalData);

protected:
	// Cached data
	const FDynamicMesh3* DetailMesh = nullptr;

private:
	FVector4f ImageSampleFunction(const FCorrespondenceSample& Sample);
};

/**
 * A mesh evaluator for sampling multiple 2D textures by material ID
 */
class DYNAMICMESH_API FMeshMultiResampleImageEvaluator : public FMeshResampleImageEvaluator
{
public:
	TMap<int32, TSharedPtr<UE::Geometry::TImageBuilder<FVector4f>, ESPMode::ThreadSafe>> MultiTextures;

public:
	// Begin FMeshMapEvaluator interface
	virtual void Setup(const FMeshBaseBaker& Baker, FEvaluationContext& Context) override;

	virtual EMeshMapEvaluatorType Type() const override { return EMeshMapEvaluatorType::MultiResampleImage; }
	// End FMeshMapEvaluator interface

	static void EvaluateSampleMulti(float*& Out, const FCorrespondenceSample& Sample, void* EvalData);

protected:
	const FDynamicMeshMaterialAttribute* DetailMaterialIDAttrib = nullptr;
	bool bValidDetailMesh = false;

private:
	FVector4f ImageSampleFunction(const FCorrespondenceSample& Sample);
};

} // end namespace UE::Geometry
} // end namespace UE

