// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNERuntimeRDGModelHlsl.h"
#include "NNECoreTensor.h"
#include "NNERuntimeRDGHlsl.h"
#include "NNERuntimeRDGHlslOp.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"

namespace UE::NNERuntimeRDG::Private::Hlsl
{

namespace ModelUtils
{

FOperatorHlsl* OpCreate(const FString& OpName, TConstArrayView<NNECore::FTensorDesc> InputTensorDescs, TConstArrayView<NNECore::FTensorDesc> OutputTensorDescs, const NNECore::FAttributeMap& AttributeMap)
{
	FOperatorRegistryHlsl::OperatorCreateFunc CreateFn = FOperatorRegistryHlsl::Get()->OpFind(OpName);

	if (!CreateFn)
	{
		UE_LOG(LogNNE, Warning, TEXT("Hlsl MLOperatorRegistry failed to find operator:%s"), *OpName);
		return nullptr;
	}

	FOperatorHlsl* Op = CreateFn();

	if (!Op->Initialize(InputTensorDescs, OutputTensorDescs, AttributeMap))
	{
		UE_LOG(LogNNE, Warning, TEXT("Hlsl runtime: Error initializing operator:%s"), *OpName);
		delete Op;
		return nullptr;
	}

	return Op;
}

} // namespace ModelUtils

bool FModel::AddWeightsToRDGGraph(FRDGBuilder& RDGBuilder)
{
	check(WeightTensorRDGs.Num() == WeightsExternalRDGResources.Num());

	for (int32 Idx = 0; Idx < WeightsExternalRDGResources.Num(); ++Idx)
	{
		const TRefCountPtr<FRDGPooledBuffer>& PooledBuffer = WeightsExternalRDGResources[Idx];
		FTensorRDG& Tensor = WeightTensorRDGs[Idx];
		FRDGBufferRef Buffer = RDGBuilder.RegisterExternalBuffer(PooledBuffer);
		Tensor.SetBuffer(Buffer);
	}

	return true;
}

bool FModel::Init(TConstArrayView<uint8> ModelData)
{
	check(ModelData.Num() > 0);
	FNNERuntimeFormat	Format;
	int32 GuidSize = sizeof(UNNERuntimeRDGHlslImpl::GUID);
	int32 VersionSize = sizeof(UNNERuntimeRDGHlslImpl::Version);
	
	if (!LoadModel(ModelData, Format, GuidSize+VersionSize))
	{
		return false;
	}

	// Create HLSL tensor and upload to GPU
	PrepareWeights();

	// Loop over all operators in the model and create them
	for (int32 Idx = 0; Idx < Format.Operators.Num(); ++Idx)
	{
		const FString TypeName = Format.Operators[Idx].TypeName;

		TArray<NNECore::FTensorDesc> Inputs;
		TArray<NNECore::FTensorDesc> Outputs;
		UE::NNECore::FAttributeMap AttributeMap;

		for (int32 InputTensorIndex : Format.Operators[Idx].InTensors)
		{
			Inputs.Emplace(AllSymbolicTensorDescs[InputTensorIndex]);
		}
		for (int32 OutputTensorIndex : Format.Operators[Idx].OutTensors)
		{
			Outputs.Emplace(AllSymbolicTensorDescs[OutputTensorIndex]);
		}
		for (const FNNEFormatAttributeDesc& Desc : Format.Operators[Idx].Attributes)
		{
			AttributeMap.SetAttribute(Desc.Name, Desc.Value);
		}

		FOperatorHlsl* Op = ModelUtils::OpCreate(TypeName, Inputs, Outputs, AttributeMap);

		if (!Op) //Op.Shader.IsNull())
		{
			UE_LOG(LogNNE, Warning, TEXT("Failed to create operator:%s"), *TypeName);

			//Note: Need to cleanup operators
			return false;
		}

		Operators.Add(Op);
	}

	return true;
}

void FModel::AddDispatchOps_RenderThread(FRDGBuilder& GraphBuilder)
{
	static constexpr int32 MaxExpectedInput = 10;
	TArray<FTensorRDGRef, TInlineAllocator<MaxExpectedInput>> InputTensors;

	static constexpr int32 MaxExpectedOutput = 2;
	TArray<FTensorRDGRef, TInlineAllocator<MaxExpectedOutput>> OutputTensors;

	// Add passes for all operators
	for (int32 Idx = 0; Idx < Operators.Num(); ++Idx)
	{
		InputTensors.Reset(OperatorInputTensorIndices.Num());
		for (int32 i : OperatorInputTensorIndices[Idx])
		{
			InputTensors.Add(AllTensorRDGRefs[i]);
		}
		OutputTensors.Reset(OperatorOutputTensorIndices.Num());
		for (int32 i : OperatorOutputTensorIndices[Idx])
		{
			OutputTensors.Add(AllTensorRDGRefs[i]);
		}

		Operators[Idx]->Dispatch(GraphBuilder, InputTensors, OutputTensors);
	}
}

int FModel::PrepareTensorShapesAndData()
{
	check(AllTensorRDGRefs.Num() == AllSymbolicTensorDescs.Num());
	
	if (Operators.Num() == 0)
	{
		UE_LOG(LogNNE, Warning, TEXT("No operators in model"));
		return -1;
	}

	// Run model preparation (including shape inference) on all operators
	// This loop could be abstracted to a different runtime/system as it apply on FTensorRef & IPrepareOperator witch are RDG agnostics.
	static constexpr int32 MaxExpectedInput = 10;
	TArray<NNECore::Internal::FTensorRef, TInlineAllocator<MaxExpectedInput>> InputTensors;
	TArray<NNECore::Internal::FTensorRef> OutputTensors;
	TArray<bool> AllInitializedTensors;

	checkCode(
		AllInitializedTensors.Init(false, AllSymbolicTensorDescs.Num());
		for (int32 Idx : InputTensorIndices)
		{
			AllInitializedTensors[Idx] = true;
		}
		for (int32 Idx : WeightTensorIndices)
		{
			AllInitializedTensors[Idx] = true;
		}
	);

	// Run model preparation (including shape inference) on all operators
	// This loop could be abstracted to a different system as it apply on FTensorRef & IPrepareOperator witch are RDG agnostics.
	for (int32 Idx = 0; Idx < Operators.Num(); ++Idx)
	{
		InputTensors.Reset();
		OutputTensors.Reset();

		//Operator inputs
		for (int32 i : OperatorInputTensorIndices[Idx])
		{
			checkf(AllInitializedTensors[i] == true, TEXT("Input tensor %d for operator %d should have been initialized."), i, Idx);
			InputTensors.Emplace(AllTensorRDGRefs[i]);
		}
		//Operator outputs
		for (int32 i : OperatorOutputTensorIndices[Idx])
		{
			OutputTensors.Emplace(AllTensorRDGRefs[i]);
			checkf(AllInitializedTensors[i] == false, TEXT("Output tensor %d for operator %d should not have been initialized yet."), i, Idx);
			checkCode(AllInitializedTensors[i] = true);
		}

		const FOperatorHlsl* Op = Operators[Idx];

		if (Op->PrepareOutputs(InputTensors, OutputTensors) != 0)
		{
			//Operator could not prepare the output tensors, meaning we can't allocate
			//output buffer before running the model. This runtime does not support this.
			UE_LOG(LogNNE, Warning, TEXT("Could not deduce tensor shapes for this model during shape inference, HLSL runtime wont support the model as it need to precompute all shapes for performance reasons."));
			AllTensorRDGRefs.Reset(AllSymbolicTensorDescs.Num());
			return -1;
		}
	}

	checkCode(
		for (int i = 0; i < AllInitializedTensors.Num(); ++i)
		{
			checkf(AllInitializedTensors[i], TEXT("Tensor at index %d, was not initialized by model preparation."));
		};
	);

	return 0;
}

bool FModel::PrepareWeights()
{
	if (!WeightsExternalRDGResources.IsEmpty())
	{
		check(WeightsExternalRDGResources.Num() == WeightTensorRDGs.Num())
		return true;
	}

	//Upload to GPU
	FEvent* Signal = FGenericPlatformProcess::GetSynchEventFromPool(false);
	WeightsExternalRDGResources.SetNum(WeightTensorRDGs.Num());

	ENQUEUE_RENDER_COMMAND(FModel_PrepareWeights)
	(
		[&Signal, this](FRHICommandListImmediate& RHICmdList)
		{
			TOptional<ERHIPipeline> Pipeline = RHICmdList.GetPipeline();
			if (Pipeline == ERHIPipeline::None)
			{
				RHICmdList.SwitchPipeline(ERHIPipeline::Graphics);
			}

			FRDGBuilder	RDGBuilder(RHICmdList);

			for (int32 i = 0; i < WeightTensorRDGs.Num(); ++i)
			{
				FTensorRDG& Tensor = WeightTensorRDGs[i];
				check(!Tensor.HasBuffer());
				check(Tensor.HasPreparedData());
				FRDGBufferDesc BufferDesc = FRDGBufferDesc::CreateBufferDesc(Tensor.GetElemByteSize(), Tensor.GetVolume());
				const FRDGBufferRef TransientRDGBuffer = RDGBuilder.CreateBuffer(BufferDesc, *Tensor.GetName(), ERDGBufferFlags::None);
				const uint8* TensorData = Tensor.GetPreparedData<uint8>().GetData();
				
				WeightsExternalRDGResources[i] = RDGBuilder.ConvertToExternalBuffer(TransientRDGBuffer);
				Tensor.SetBuffer(TransientRDGBuffer);
				RDGBuilder.QueueBufferUpload(TransientRDGBuffer, TensorData, Tensor.GetDataSize(), ERDGInitialDataFlags::NoCopy);
			}

			RDGBuilder.Execute();

			//To prevent any problem if model is released before upload is done to the GPU. To be improved.
			RHICmdList.BlockUntilGPUIdle();

			Signal->Trigger();
		}
	);

	// We need to wait for render thread to finish
	Signal->Wait();

	FGenericPlatformProcess::ReturnSynchEventToPool(Signal);

	return true;
}

} // namespace UE::NNERuntimeRDG::Private::Hlsl