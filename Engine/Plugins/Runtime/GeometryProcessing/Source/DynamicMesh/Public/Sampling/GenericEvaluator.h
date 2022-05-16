// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Sampling/MeshMapEvaluator.h"

namespace UE
{
namespace Geometry
{

template <
	typename DataType,
	FMeshMapEvaluator::EComponents NumComponents = FMeshMapEvaluator::EComponents::Float4,
	FMeshMapEvaluator::EAccumulateMode Mode = FMeshMapEvaluator::EAccumulateMode::Add>
class FGenericEvaluator : public FMeshMapEvaluator
{
public:

	DataType DefaultResult;
	TFunction<DataType(const FCorrespondenceSample& Sample)> EvaluateSampleCallback;
	TFunction<FVector4f(const int DataIdx, float*& In)> EvaluateColorCallback;

public:
	virtual ~FGenericEvaluator() {}

	// Begin FMeshMapEvaluator interface
	virtual void Setup(const FMeshBaseBaker& Baker, FEvaluationContext& Context) override
	{
		Context.Evaluate = &EvaluateSample;
		Context.EvaluateDefault = &EvaluateDefault;
		Context.EvaluateColor = &EvaluateColor;
		Context.EvalData = this;
		Context.AccumulateMode = Mode;
		Context.DataLayout = { NumComponents };
	}

	virtual EMeshMapEvaluatorType Type() const override { return EMeshMapEvaluatorType::Generic; }
	// End FMeshMapEvaluator interface

	static void EvaluateSample(float*& Out, const FCorrespondenceSample& Sample, void* EvalData)
	{
		const FGenericEvaluator* Eval = static_cast<FGenericEvaluator*>(EvalData);
		const DataType SampleResult = Eval->EvaluateSampleCallback(Sample);
		WriteToBuffer(Out, SampleResult);
	}

	static void EvaluateDefault(float*& Out, void* EvalData)
	{
		const FGenericEvaluator* Eval = static_cast<FGenericEvaluator*>(EvalData);
		WriteToBuffer(Out, Eval->DefaultResult);
	}

	static void EvaluateColor(const int DataIdx, float*& In, FVector4f& Out, void* EvalData)
	{
		const FGenericEvaluator* Eval = static_cast<FGenericEvaluator*>(EvalData);
		Out = Eval->EvaluateColorCallback(DataIdx, In);
	}
};


} // end namespace UE::Geometry
} // end namespace UE
