// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NNERuntimeRDGModel.h"
#include "NNECoreTypes.h"
#include "NNERuntimeRDGTensorHlsl.h"

class FRDGBuilder;

namespace UE::NNERuntimeRDG::Private::Hlsl
{
struct FOperatorHlsl;

class FModel : public FModelRDG
{
	
public:

	~FModel() = default;

	bool Init(TConstArrayView<uint8> ModelData);

	virtual int SetInputTensorShapes(TConstArrayView<NNECore::FTensorShape> InputShapes) override;
	virtual int EnqueueRDG(FRDGBuilder& RDGBuilder, TConstArrayView<NNECore::FTensorBindingRDG> InInputBindings, TConstArrayView<NNECore::FTensorBindingRDG> InOutputBindings) override;

protected:

	virtual void AddDispatchOps_RenderThread(FRDGBuilder& GraphBuilder) override { };
	virtual int PrepareTensorShapesAndData() override;

	bool PrepareWeights();

private:

	TArray<FOperatorHlsl*>	Operators;

	TArray<FTensorHLSLRef> AllTensorHLSLRefs;
	TArray<FTensorHLSL> InputTensorHLSLs;
	TArray<FTensorHLSL> OutputTensorHLSLs;
	TArray<FTensorHLSL> IntermediateTensorHLSLs;
	TArray<FTensorHLSL> WeightTensorHLSLs;
	TArray<TRefCountPtr<FRDGPooledBuffer>> WeightsExternalRDGResources;
};

} // namespace UE::NNERuntimeRDG::Private::Hlsl