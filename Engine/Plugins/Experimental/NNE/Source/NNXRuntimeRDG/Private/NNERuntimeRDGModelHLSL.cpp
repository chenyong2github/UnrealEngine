// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNERuntimeRDGModelHLSL.h"
#include "NNECoreTensor.h"
#include "NNXRuntimeHLSLOp.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"

namespace UE::NNIRuntimeRDG::Private::Hlsl
{

namespace
{

NNX::FMLOperatorHlsl* OpCreate(const FString& OpName, TConstArrayView<NNX::FTensorDesc> InputTensorDescs, TConstArrayView<NNX::FTensorDesc> OutputTensorDescs, const UE::NNECore::FAttributeMap& AttributeMap)
{
	NNX::FMLOperatorRegistryHlsl::OperatorCreateFunc CreateFn = NNX::FMLOperatorRegistryHlsl::Get()->OpFind(OpName);

	if (!CreateFn)
	{
		UE_LOG(LogNNX, Warning, TEXT("Hlsl MLOperatorRegistry failed to find operator:%s"), *OpName);
		return nullptr;
	}

	NNX::FMLOperatorHlsl* Op = CreateFn();

	if (!Op->Initialize(InputTensorDescs, OutputTensorDescs, AttributeMap))
	{
		UE_LOG(LogNNX, Warning, TEXT("Hlsl engine: Error initializing operator:%s"), *OpName);
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
	TArray<NNX::FTensorRDGRef, TInlineAllocator<MaxExpectedInput>> InputTensors;

	static constexpr int32 MaxExpectedOutput = 2;
	TArray<NNX::FTensorRDGRef, TInlineAllocator<MaxExpectedOutput>> OutputTensors;

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

int ApplyBinding(TArray<FTensorHLSL>& OutTensorsHLSL, TConstArrayView<NNX::FMLTensorBinding> InBindings, bool bIsInput)
{
	check(OutTensorsHLSL.Num() == InBindings.Num());
	
	for (int32 Idx = 0; Idx < InBindings.Num(); ++Idx)
	{
		const NNX::FMLTensorBinding& Binding = InBindings[Idx];
		FTensorHLSL& Tensor = OutTensorsHLSL[Idx];

		if (Binding.BindingType == NNX::EMLTensorBindingDataType::CPUMemory)
		{
			if (bIsInput) Tensor.SetUploadBuffer(Binding.CpuMemory);
			else Tensor.SetDownloadBuffer(Binding.CpuMemory);
		}
		else if (Binding.BindingType == NNX::EMLTensorBindingDataType::RDGBuffer)
		{
			Tensor.SetBuffer(Binding.Buffer);
		}
		else
		{
			// Unsupported tensor binding type
			return Idx;
		}
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
	FMLRuntimeFormat	Format;

	if (!LoadModel(ModelData, Format))
	{
		return false;
	}

	// Create HLSL tensor and upload to GPU
	PrepareWeights();

	// Loop over all operators in the model and create them
	for (int32 Idx = 0; Idx < Format.Operators.Num(); ++Idx)
	{
		const FString TypeName = Format.Operators[Idx].TypeName;

		TArray<NNX::FTensorDesc> Inputs;
		TArray<NNX::FTensorDesc> Outputs;
		UE::NNECore::FAttributeMap AttributeMap;

		for (int32 InputTensorIndex : Format.Operators[Idx].InTensors)
		{
			Inputs.Emplace(AllSymbolicTensorDescs[InputTensorIndex]);
		}
		for (int32 OutputTensorIndex : Format.Operators[Idx].OutTensors)
		{
			Outputs.Emplace(AllSymbolicTensorDescs[OutputTensorIndex]);
		}
		for (const FMLFormatAttributeDesc& Desc : Format.Operators[Idx].Attributes)
		{
			AttributeMap.SetAttribute(Desc.Name, Desc.Value);
		}

		NNX::FMLOperatorHlsl* Op = OpCreate(TypeName, Inputs, Outputs, AttributeMap);

		if (!Op) //Op.Shader.IsNull())
		{
			UE_LOG(LogNNX, Warning, TEXT("Failed to create operator:%s"), *TypeName);

			// TODO: Cleanup operators
			return false;
		}

		Operators.Add(Op);
	}

	return true;
}

int FModel::SetInputTensorShapes(TConstArrayView<NNX::FTensorShape> InputShapes)
{
	int Res = NNX::FMLInferenceModelRDG::SetInputTensorShapes(InputShapes);
	if (Res < 0) return Res;

	auto ConvertTensors = [this] (TArray<FTensorHLSLRef> &TensorRefs, TArray<FTensorHLSL> &Tensors, const NNX::FTensorRDGArray &TensorRDGs, const TArray<int32>& TensorIndices)
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

int FModel::RunSync(TConstArrayView<NNX::FMLTensorBinding> InInputBindings, TConstArrayView<NNX::FMLTensorBinding> InOutputBindings)
{
	// Verify the model inputs were prepared
	if (InputTensorShapes.Num() == 0)
	{
		UE_LOG(LogNNX, Error, TEXT("Run(): Input shapes are not set, please call SetInputTensorShapes."));
		return -1;
	}
	
	int Res = 0;
	FEvent* Signal = FGenericPlatformProcess::GetSynchEventFromPool(false);

	ENQUEUE_RENDER_COMMAND(FMLInferenceModel_Run)
	(
		[&Signal, &Res, this, InInputBindings, InOutputBindings](FRHICommandListImmediate& RHICmdList)
		{
			TOptional<ERHIPipeline>		Pipeline = RHICmdList.GetPipeline();

			if (Pipeline == ERHIPipeline::None)
			{
				RHICmdList.SwitchPipeline(ERHIPipeline::Graphics);
			}

			FRDGBuilder	RDGBuilder(RHICmdList);

			Res = EnqueueRDG(RDGBuilder, InInputBindings, InOutputBindings);
			if (Res == 0)
			{
				RDGBuilder.Execute();

				// FIXME: Using BlockUntilGPUIdle() prevents hang on Linux
				// FIXME: Adapt to redesigned readback API (UE 5.2)
				RHICmdList.BlockUntilGPUIdle();

				for (FTensorHLSLRef Tensor : AllTensorHLSLRefs)
				{
					Tensor->Resolve();
				}
			}
			
			Signal->Trigger();
		}
	);

	// We need to wait for render thread to finish
	Signal->Wait();

	FGenericPlatformProcess::ReturnSynchEventToPool(Signal);

	return Res;
}

int FModel::EnqueueRDG(FRDGBuilder& GraphBuilder, TConstArrayView<NNX::FMLTensorBinding> InInputBindings, TConstArrayView<NNX::FMLTensorBinding> InOutputBindings)
{
	check(IsInRenderingThread());

	int Res;

	// Verify the model inputs were prepared
	if (InputTensorShapes.Num() == 0)
	{
		UE_LOG(LogNNX, Error, TEXT("EnqueueRDG(): Input shapes are not set, please call SetInputTensorShapes."));
		return -1;
	}

	Res = ApplyBinding(InputTensorHLSLs, InInputBindings, true);
	if (Res != 0)
	{
		UE_LOG(LogNNX, Warning, TEXT("Invalid input tensor binding type for tensor index:%d"), Res);
		return -1;
	}

	Res = ApplyBinding(OutputTensorHLSLs, InOutputBindings, false);
	if (Res != 0)
	{
		UE_LOG(LogNNX, Warning, TEXT("Invalid output tensor binding type for tensor index:%d"), Res);
		return -1;
	}

	if (!ApplyWeights(GraphBuilder, WeightTensorHLSLs, WeightsExternalRDGResources))
	{
		UE_LOG(LogNNX, Warning, TEXT("Could not register the weights for graph execution."));
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

	for (FTensorHLSLRef Tensor : AllTensorHLSLRefs)
	{
		Tensor->EnqueueUploadRdg(GraphBuilder);
	}

	InternAddDispatchOps_RenderThread<NNX::FMLOperatorHlsl*>(GraphBuilder, AllTensorHLSLRefs, OperatorInputTensorIndices, OperatorOutputTensorIndices, Operators);

	for (FTensorHLSLRef Tensor : AllTensorHLSLRefs)
	{
		Tensor->EnqueueDownloadRdg(GraphBuilder, bUseManualTransitions);
	}

	return 0;
}

int FModel::PrepareTensorShapesAndData()
{
	check(AllTensorRDGs.Num() == AllSymbolicTensorDescs.Num());
	
	if (Operators.Num() == 0)
	{
		UE_LOG(LogNNX, Warning, TEXT("No operators in model"));
		return -1;
	}

	// Run model preparation (including shape inference) on all operators
	// This loop could be abstracted to a different engine/system as it apply on FTensorRef & IPrepareOperator witch are RDG agnostics.
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

		const NNX::FMLOperatorHlsl* Op = Operators[Idx];

		if (Op->PrepareOutputs(InputTensors, OutputTensors) != 0)
		{
			//Operator could not prepare the output tensors, meaning we can't allocate
			//output buffer before running the model. This engine does not support this.
			UE_LOG(LogNNX, Warning, TEXT("Could not deduce tensor shapes for this model during shape inference, HLSL engine wont support the model as it need to precompute all shapes for performance reasons."));
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

} // namespace UE::NNIRuntimeRDG::Private::Hlsl