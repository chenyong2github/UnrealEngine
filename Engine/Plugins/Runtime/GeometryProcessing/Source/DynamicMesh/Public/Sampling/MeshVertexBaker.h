// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Sampling/MeshBaseBaker.h"
#include "Sampling/MeshMapEvaluator.h"
#include "Sampling/MeshConstantMapEvaluator.h"
#include "Image/ImageBuilder.h"
#include "Image/ImageDimensions.h"

namespace UE
{
namespace Geometry
{

class DYNAMICMESH_API FMeshVertexBaker : public FMeshBaseBaker
{
public:
	enum class EBakeMode
	{
		Channel,	// Bake scalar data into color channels
		Color		// Bake color data to RGB
	};

	EBakeMode BakeMode = EBakeMode::Color;

	TSharedPtr<FMeshMapEvaluator, ESPMode::ThreadSafe> ColorEvaluator;
	TSharedPtr<FMeshMapEvaluator, ESPMode::ThreadSafe> ChannelEvaluators[4];

public:	
	//
	// Bake
	//

	/** Process all bakers to generate the image result. */
	void Bake();

	/** @return the bake result image. */
	const TImageBuilder<FVector4f>* GetBakeResult() const;

	/** if this function returns true, we should abort calculation */
	TFunction<bool(void)> CancelF = []() { return false; };

	//
	// Parameters
	//

protected:
	template<EBakeMode ComputeMode>
	static void BakeImpl(void* Data);

protected:
	const bool bParallel = true;

	/** Internally cached image dimensions proportional to the number of unique vertex color elements. */
	FImageDimensions Dimensions;

	/** Bake output image. */
	TUniquePtr<TImageBuilder<FVector4f>> BakeResult;

	/** Internal list of bake evaluators. */
	TArray<FMeshMapEvaluator*> Bakers;

	/** Evaluation contexts for each mesh evaluator. */
	TArray<FMeshMapEvaluator::FEvaluationContext> BakeContexts;

	/** Internal cached default bake data. */
	FVector4f BakeDefaults;

	/** The total size of the temporary float buffer for BakeSample. */
	int32 BakeSampleBufferSize = 0;

	/** */
	using BakeFn = void(*)(void* Data);
	BakeFn BakeInternal = nullptr;

	/** Constant evaluator for empty channels. */
	static FMeshConstantMapEvaluator ZeroEvaluator;
	static FMeshConstantMapEvaluator OneEvaluator;
};

} // end namespace UE::Geometry
} // end namespace UE