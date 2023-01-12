// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NNXRuntimeRDG.h"
#include "NNECoreTypes.h"
#include "NNERuntimeRDGTensorHLSL.h"

class FRDGBuilder;
namespace NNX { struct FMLOperatorHlsl; }

namespace UE::NNIRuntimeRDG::Private::Hlsl
{

class FModel : public NNX::FMLInferenceModelRDG
{
	
public:

	~FModel() = default;

	bool Init(TConstArrayView<uint8> ModelData);

	virtual int SetInputTensorShapes(TConstArrayView<NNX::FTensorShape> InputShapes) override;

	/**
	 * Run the inference model (synchronous version)
	 */
	virtual int RunSync(TConstArrayView<NNX::FMLTensorBinding> InputBindings, TConstArrayView<NNX::FMLTensorBinding> OutputBindings) override;
	
	/**
	 * Enqueue operators to RDG, the caller will run the GraphBuilder.Execute()
	 */
	virtual int EnqueueRDG(FRDGBuilder& GraphBuilder, TConstArrayView<NNX::FMLTensorBinding> InputBindings, TConstArrayView<NNX::FMLTensorBinding> OutputBindings) override;

protected:

	virtual void AddDispatchOps_RenderThread(FRDGBuilder& GraphBuilder) override { };
	virtual int PrepareTensorShapesAndData() override;

	bool PrepareWeights();

private:

	TArray<NNX::FMLOperatorHlsl*>	Operators;

	TArray<FTensorHLSLRef> AllTensorHLSLRefs;
	TArray<FTensorHLSL> InputTensorHLSLs;
	TArray<FTensorHLSL> OutputTensorHLSLs;
	TArray<FTensorHLSL> IntermediateTensorHLSLs;
	TArray<FTensorHLSL> WeightTensorHLSLs;
	TArray<TRefCountPtr<FRDGPooledBuffer>> WeightsExternalRDGResources;
};

} // namespace UE::NNIRuntimeRDG::Private::Hlsl