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

template<class OperatorRef>
void InternAddDispatchOps_RenderThread(FRDGBuilder& GraphBuilder, TConstArrayView<FTensorHLSLRef> AllTensorHLSLRefs, TConstArrayView<TArray<uint32>> OperatorInputTensorIndices,
	TConstArrayView<TArray<uint32>> OperatorOutputTensorIndices, TArrayView<OperatorRef> Operators)
{
	static constexpr int32 MaxExpectedInput = 10;
	TArray<FTensorRDGRef, TInlineAllocator<MaxExpectedInput>> InputTensors;

	static constexpr int32 MaxExpectedOutput = 2;
	TArray<FTensorRDGRef, TInlineAllocator<MaxExpectedOutput>> OutputTensors;

	// Add passes for all operators
	for (int32 Idx = 0; Idx < Operators.Num(); ++Idx)
	{
		InputTensors.Empty();
		for (int32 i : OperatorInputTensorIndices[Idx])
		{
			InputTensors.Add(AllTensorHLSLRefs[i]);
		}
		OutputTensors.Empty();
		for (int32 i : OperatorOutputTensorIndices[Idx])
		{
			OutputTensors.Add(AllTensorHLSLRefs[i]);
		}

		Operators[Idx]->Dispatch(GraphBuilder, InputTensors, OutputTensors);
	}
}

int ApplyBinding(TArray<FTensorHLSL>& OutTensorsHLSL, TConstArrayView<NNECore::FTensorBindingRDG> InBindings, bool bIsInput)
{
	check(OutTensorsHLSL.Num() == InBindings.Num());
	
	for (int32 Idx = 0; Idx < InBindings.Num(); ++Idx)
	{
		const NNECore::FTensorBindingRDG& Binding = InBindings[Idx];
		FTensorHLSL& Tensor = OutTensorsHLSL[Idx];
		
		if (Binding.Buffer == nullptr)
		{
			return -1;
		}
		Tensor.SetBuffer(Binding.Buffer);
	}

	return 0;
}

bool ApplyWeights(FRDGBuilder& GraphBuilder, TArray<FTensorHLSL>& OutTensorsHLSL, TArray<TRefCountPtr<FRDGPooledBuffer>> ExternalWeightsRDG)
{
	check(OutTensorsHLSL.Num() == ExternalWeightsRDG.Num());

	for (int32 Idx = 0; Idx < ExternalWeightsRDG.Num(); ++Idx)
	{
		const TRefCountPtr<FRDGPooledBuffer>& PooledBuffer = ExternalWeightsRDG[Idx];
		FTensorHLSL& Tensor = OutTensorsHLSL[Idx];
		FRDGBufferRef Buffer = GraphBuilder.RegisterExternalBuffer(PooledBuffer);
		Tensor.SetBuffer(Buffer);
	}

	return true;
}

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

int FModel::SetInputTensorShapes(TConstArrayView<NNECore::FTensorShape> InputShapes)
{
	int Res = FModelRDG::SetInputTensorShapes(InputShapes);
	if (Res < 0) return Res;

	auto ConvertTensors = [this] (TArray<FTensorHLSLRef> &TensorRefs, TArray<FTensorHLSL> &Tensors, const FTensorRDGArray &TensorRDGs, const TArray<int32>& TensorIndices)
	{
		Tensors.Empty();
		Tensors.SetNum(TensorRDGs.Num());

		for (int32 i = 0; i < TensorIndices.Num(); ++i)
		{
			const int32 Idx = TensorIndices[i];
			Tensors[i] = FTensorHLSL(TensorRDGs[i]);
			AllTensorHLSLRefs[Idx]= &Tensors[i];
		}
	};

	AllTensorHLSLRefs.Empty();
	AllTensorHLSLRefs.SetNumUninitialized(AllTensorRDGs.Num());
	ConvertTensors(AllTensorHLSLRefs, InputTensorHLSLs, InputTensorRDGs, InputTensorIndices);
	ConvertTensors(AllTensorHLSLRefs, OutputTensorHLSLs, OutputTensorRDGs, OutputTensorIndices);
	ConvertTensors(AllTensorHLSLRefs, IntermediateTensorHLSLs, IntermediateTensorRDGs, IntermediateTensorIndices);
	for (int32 i = 0; i < WeightTensorIndices.Num(); ++i)
	{
		const int32 Idx = WeightTensorIndices[i];
		AllTensorHLSLRefs[Idx] = &WeightTensorHLSLs[i];
	}

	return 0;
}

int FModel::EnqueueRDG(FRDGBuilder& GraphBuilder, TConstArrayView<NNECore::FTensorBindingRDG> InInputBindings, TConstArrayView<NNECore::FTensorBindingRDG> InOutputBindings)
{
	check(IsInRenderingThread());

	int Res;

	// Verify the model inputs were prepared
	if (InputTensorShapes.Num() == 0)
	{
		UE_LOG(LogNNE, Error, TEXT("EnqueueRDG(): Input shapes are not set, please call SetInputTensorShapes."));
		return -1;
	}

	Res = ModelUtils::ApplyBinding(InputTensorHLSLs, InInputBindings, true);
	if (Res != 0)
	{
		UE_LOG(LogNNE, Warning, TEXT("Invalid input tensor binding for tensor index:%d"), Res);
		return -1;
	}

	Res = ModelUtils::ApplyBinding(OutputTensorHLSLs, InOutputBindings, false);
	if (Res != 0)
	{
		UE_LOG(LogNNE, Warning, TEXT("Invalid output tensor binding for tensor index:%d"), Res);
		return -1;
	}

	if (!ModelUtils::ApplyWeights(GraphBuilder, WeightTensorHLSLs, WeightsExternalRDGResources))
	{
		UE_LOG(LogNNE, Warning, TEXT("Could not register the weights for graph execution."));
		return -1;
	}

	// Create RDG buffers where not yet present
	for (FTensorHLSLRef Tensor : AllTensorHLSLRefs)
	{
		if (!Tensor->HasBuffer())
		{
			FRDGBufferDesc BufferDesc = FRDGBufferDesc::CreateBufferDesc(Tensor->GetElemByteSize(), Tensor->GetVolume());
			if (Tensor->HasDownloadBuffer())
			{
				BufferDesc.Usage = EBufferUsageFlags(BufferDesc.Usage | BUF_SourceCopy);
			}

			const FRDGBufferRef TensorBuffer = GraphBuilder.CreateBuffer(BufferDesc, *Tensor->GetName(), ERDGBufferFlags::None);
			
			Tensor->SetBuffer(TensorBuffer);
		}
	}

	ModelUtils::InternAddDispatchOps_RenderThread<FOperatorHlsl*>(GraphBuilder, AllTensorHLSLRefs, OperatorInputTensorIndices, OperatorOutputTensorIndices, Operators);

	return 0;
}

int FModel::PrepareTensorShapesAndData()
{
	check(AllTensorRDGs.Num() == AllSymbolicTensorDescs.Num());
	
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
		InputTensors.Empty();
		OutputTensors.Empty();

		//Operator inputs
		for (int32 i : OperatorInputTensorIndices[Idx])
		{
			checkf(AllInitializedTensors[i] == true, TEXT("Input tensor %d for operator %d should have been initialized."), i, Idx);
			InputTensors.Emplace(AllTensorRDGs[i]);
		}
		//Operator outputs
		for (int32 i : OperatorOutputTensorIndices[Idx])
		{
			OutputTensors.Emplace(AllTensorRDGs[i]);
			checkf(AllInitializedTensors[i] == false, TEXT("Output tensor %d for operator %d should not have been initialized yet."), i, Idx);
			checkCode(AllInitializedTensors[i] = true);
		}

		const FOperatorHlsl* Op = Operators[Idx];

		if (Op->PrepareOutputs(InputTensors, OutputTensors) != 0)
		{
			//Operator could not prepare the output tensors, meaning we can't allocate
			//output buffer before running the model. This runtime does not support this.
			UE_LOG(LogNNE, Warning, TEXT("Could not deduce tensor shapes for this model during shape inference, HLSL runtime wont support the model as it need to precompute all shapes for performance reasons."));
			AllTensorRDGs.Empty();
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
		check(WeightsExternalRDGResources.Num() == WeightTensorHLSLs.Num())
		return true;
	}

	//Convert to HLSL tensor
	//This copy the weight. To be improved.
	check(WeightTensorHLSLs.IsEmpty());
	WeightTensorHLSLs.SetNum(WeightTensorRDGs.Num());
	for (int32 Idx = 0; Idx < WeightTensorRDGs.Num(); ++Idx)
	{
		WeightTensorHLSLs[Idx] = FTensorHLSL(WeightTensorRDGs[Idx]);
	}

	//Upload to GPU
	FEvent* Signal = FGenericPlatformProcess::GetSynchEventFromPool(false);
	WeightsExternalRDGResources.SetNum(WeightTensorHLSLs.Num());

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

			for (int32 i = 0; i < WeightTensorHLSLs.Num(); ++i)
			{
				FTensorHLSL& Tensor = WeightTensorHLSLs[i];
				check(!Tensor.HasBuffer());
				check(Tensor.HasPreparedData());
				FRDGBufferDesc BufferDesc = FRDGBufferDesc::CreateBufferDesc(Tensor.GetElemByteSize(), Tensor.GetVolume());
				const FRDGBufferRef TransientRDGBuffer = RDGBuilder.CreateBuffer(BufferDesc, *Tensor.GetName(), ERDGBufferFlags::None);
				WeightsExternalRDGResources[i] = RDGBuilder.ConvertToExternalBuffer(TransientRDGBuffer);
				Tensor.SetBuffer(TransientRDGBuffer);
				Tensor.EnqueueUploadRdg(RDGBuilder);
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