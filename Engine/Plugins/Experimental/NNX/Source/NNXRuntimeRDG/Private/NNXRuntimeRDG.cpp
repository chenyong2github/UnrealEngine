// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXRuntimeRDG.h"
#include "NNXInferenceModel.h"
#include "NNXRuntimeFormat.h"

#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"

#include "Serialization/MemoryReader.h"

namespace NNX
{

//
//
//
bool AlwaysValidValidationFunction(
	const UE::NNECore::FAttributeMap& AttributeMap, 
	TConstArrayView<EMLTensorDataType> InputTensorTypes,
	TConstArrayView<FSymbolicTensorShape> InputShapes)
{
	return true;
}


//
//
//
FInputValidator::FInputValidator() : 
	NumRequiredInput(0), NumOptionalInput(0)
{
	TemplateTypes.SetNum(1);
}

bool FInputValidator::Validate(TConstArrayView<EMLTensorDataType> InputTypes)
{
	check(InputTemplateIndices.Num() == NumRequiredInput + NumOptionalInput);

	bool bAreInputValid = true;
	int32 NumInputsToValidate = FMath::Min(InputTemplateIndices.Num(), InputTypes.Num());
	
	if (InputTypes.Num() < NumRequiredInput)
	{
		UE_LOG(LogNNX, Warning, TEXT("Required '%d' inputs but found '%d'."), NumRequiredInput, InputTypes.Num());
		bAreInputValid = false;
	}
	if (InputTypes.Num() > NumRequiredInput+NumOptionalInput)
	{
		UE_LOG(LogNNX, Warning, TEXT("Got a total of '%d' inputs but should have '%d' maximum."), InputTypes.Num(), NumRequiredInput + NumOptionalInput);
		bAreInputValid = false;
	}
	
	for (int32 Idx = 0; Idx < NumInputsToValidate; ++Idx)
	{
		const int32 TemplateIdx = InputTemplateIndices[Idx];
		
		check(TemplateIdx < TemplateTypes.Num());
		if (INDEX_NONE == TemplateTypes[TemplateIdx].Find(InputTypes[Idx]))
		{
			UE_LOG(LogNNX, Warning, TEXT("Input '%d' from template idx '%d' is of type '%d' is not supported."), Idx, TemplateIdx, (int)InputTypes[Idx]);
			bAreInputValid = false;
		}
	}
	return bAreInputValid;
}
void FInputValidator::SetTemplateCount(int TemplateCount)
{
	TemplateTypes.SetNum(TemplateCount);
}
void FInputValidator::AddSupportedType(EMLTensorDataType Type, int TemplateIdx)
{
	check(TemplateTypes.Num() > TemplateIdx);
	TemplateTypes[TemplateIdx].Add(Type);
}
void FInputValidator::AddOptional(int32 TemplateIdx)
{
	InputTemplateIndices.Add(TemplateIdx);
	++NumOptionalInput;
}

void FInputValidator::AddRequired(int32 TemplateIdx)
{
	checkf(NumOptionalInput==0, TEXT("All required attribute should be declared before the optional ones as they are referenced by indices"));
	InputTemplateIndices.Add(TemplateIdx);
	++NumRequiredInput;
}


//
//
//
void FAttributeValidator::AddOptional(const FString& Name, ENNEAttributeDataType Type)
{
	checkf(nullptr == OptionalAttributes.FindByPredicate([Name](const FEntry& Other) { return Other.Name == Name; }), TEXT("Attribute name should be unique"));
	checkf(nullptr == RequiredAttributes.FindByPredicate([Name](const FEntry& Other) { return Other.Name == Name; }), TEXT("Attribute name should be unique"));
	OptionalAttributes.Emplace(Name, Type);
}

void FAttributeValidator::AddRequired(const FString& Name, ENNEAttributeDataType Type)
{
	checkf(nullptr == OptionalAttributes.FindByPredicate([Name](const FEntry& Other) { return Other.Name == Name; }), TEXT("Attribute name should be unique"));
	checkf(nullptr == RequiredAttributes.FindByPredicate([Name](const FEntry& Other) { return Other.Name == Name; }), TEXT("Attribute name should be unique"));
	RequiredAttributes.Emplace(Name, Type);
}

bool FAttributeValidator::Validate(const UE::NNECore::FAttributeMap& AttributesToValidate)
{
	bool bAreAttributesValid = true;

	//Verify all required attribute are matching specifications
	for (int32 Idx = 0; Idx < RequiredAttributes.Num(); ++Idx)
	{
		const FNNEAttributeValue* FoundAttribute = AttributesToValidate.GetAttributeValue(RequiredAttributes[Idx].Name);
		
		if (FoundAttribute == nullptr)
		{
			bAreAttributesValid = false;
			UE_LOG(LogNNX, Warning, TEXT("Required attribute '%s' not found."),
				*RequiredAttributes[Idx].Name);
		}
		else if (RequiredAttributes[Idx].Type != FoundAttribute->GetType())
		{
			bAreAttributesValid = false;
			UE_LOG(LogNNX, Warning, TEXT("Required attribute '%s' type '%d' does not match expected type '%d'."),
				*RequiredAttributes[Idx].Name,
				(int)FoundAttribute->GetType(),
				(int)RequiredAttributes[Idx].Type);
		}
	}

	//Verify all optional attribute are matching specifications
	for (int32 Idx = 0; Idx < OptionalAttributes.Num(); ++Idx)
	{
		const FNNEAttributeValue* FoundAttribute = AttributesToValidate.GetAttributeValue(OptionalAttributes[Idx].Name);
		
		if ((FoundAttribute != nullptr) && (OptionalAttributes[Idx].Type != FoundAttribute->GetType()))
		{
			bAreAttributesValid = false;
			UE_LOG(LogNNX, Warning, TEXT("Optional attribute '%s' type '%d' does not match expected type '%d'."),
				*OptionalAttributes[Idx].Name,
				(int)FoundAttribute->GetType(),
				(int)OptionalAttributes[Idx].Type);
		}
	}

	//Verify all attributes are either required or optional, otherwise they are unsupported
	for (int32 Idx = 0; Idx < AttributesToValidate.Num(); ++Idx)
	{
		const FString& Name = AttributesToValidate.GetName(Idx);
		const FEntry* OptionalAttribute = OptionalAttributes.FindByPredicate([Name](const FEntry& Other) { return Other.Name == Name; });
		const FEntry* RequiredAttribute = RequiredAttributes.FindByPredicate([Name](const FEntry& Other) { return Other.Name == Name; });
		
		if (OptionalAttribute == nullptr && RequiredAttribute == nullptr)
		{
			bAreAttributesValid = false;
			UE_LOG(LogNNX, Warning, TEXT("Found unsupported attribute '%s'."), *Name);
		}
	}

	return bAreAttributesValid;
}
	
//
//
//
FMLInferenceModelRDG::FMLInferenceModelRDG()
	: FMLInferenceModel(EMLInferenceModelType::RDG)
	, bUseManualTransitions(false)
{
	Readback.RHI = new FRHIGPUBufferReadback("FMLTensorReadback");
}

FMLInferenceModelRDG::~FMLInferenceModelRDG()
{
	delete Readback.RHI;
}

//
//
//
bool FMLInferenceModelRDG::LoadModel(const FNNIModelRaw& InModel, FMLRuntimeFormat& Format)
{
	ENNXInferenceFormat FormatType = InModel.Format;

	if (FormatType != ENNXInferenceFormat::NNXRT)
	{
		UE_LOG(LogNNX, Warning, TEXT("Unsupported format type for NNX inference model"));
		return false;
	}

	FMemoryReader Reader(InModel.Data);

	FMLRuntimeFormat::StaticStruct()->SerializeBin(Reader, &Format);

	// Data for base class
	InputSymbolicTensors.Empty();
	OutputSymbolicTensors.Empty();
	
	// Data for RDG
	AllSymbolicTensorDescs.Empty();
	IntermediateTensorIndices.Empty();
	InputTensorIndices.Empty();
	OutputTensorIndices.Empty();
	OperatorInputTensorIndices.Empty();
	OperatorOutputTensorIndices.Empty();

	// Add tensors
	for (int32 Idx = 0; Idx < Format.Tensors.Num(); ++Idx)
	{
		const FMLFormatTensorDesc& FormatTensorDesc = Format.Tensors[Idx];

		FSymbolicTensorShape SymbolicShape = FSymbolicTensorShape::Make(FormatTensorDesc.Shape);
		FTensorDesc SymbolicTensor = FTensorDesc::Make(FormatTensorDesc.Name, SymbolicShape, FormatTensorDesc.DataType);
		AllSymbolicTensorDescs.Emplace(SymbolicTensor);
		
		if (FormatTensorDesc.Type == EMLFormatTensorType::Input)
		{
			InputTensorIndices.Emplace(Idx);
			InputSymbolicTensors.Emplace(SymbolicTensor);
		}
		else if (FormatTensorDesc.Type == EMLFormatTensorType::Output)
		{
			OutputTensorIndices.Emplace(Idx);
			OutputSymbolicTensors.Emplace(SymbolicTensor);
		}
		else if (FormatTensorDesc.Type == EMLFormatTensorType::Intermediate)
		{
			IntermediateTensorIndices.Emplace(Idx);
		}
		checkf(FormatTensorDesc.Type != EMLFormatTensorType::None, TEXT("Unsupported tensor type None"));
	}

	// Loop over all operators in the model and store tensor indices for input/output
	for (int32 Idx = 0; Idx < Format.Operators.Num(); ++Idx)
	{
		OperatorInputTensorIndices.Emplace(Format.Operators[Idx].InTensors);
		OperatorOutputTensorIndices.Emplace(Format.Operators[Idx].OutTensors);
	}

	return true;
}

/**
 * Run the inference model (synchronous version)
 */
int FMLInferenceModelRDG::Run(TConstArrayView<FMLTensorBinding> InInputBindings, TConstArrayView<FMLTensorBinding> InOutputBindings)
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

				// Process readback
				{
					const void* BuffData = Readback.RHI->Lock(Readback.Size);
					check(BuffData);
					FMemory::Memcpy(Readback.CpuMemory, BuffData, Readback.Size);
					Readback.RHI->Unlock();
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

int FMLInferenceModelRDG::SetInputTensorShapes(TConstArrayView<FTensorShape> InInputShapes)
{
	OutputTensorShapes.Empty();
	
	//Verify input shape are valid for the model and set InputTensorShapes
	if (FMLInferenceModel::SetInputTensorShapes(InInputShapes) != 0)
	{
		return -1;
	}

	//Run shape inference filling AllTensors
	if (RunShapeInference() != 0)
	{
		return -1;
	}

	//Set OutputTensorShapes for the model from shape inference result
	for (int32 OutputIndices : OutputTensorIndices)
	{
		OutputTensorShapes.Emplace(AllShapes[OutputIndices]);
	}

	check(InputTensorIndices.Num() + OutputTensorIndices.Num() + IntermediateTensorIndices.Num() == AllShapes.Num());
	check(InputTensorShapes.Num() == InputSymbolicTensors.Num());
	check(OutputTensorShapes.Num() == OutputSymbolicTensors.Num());
	check(AllShapes.Num() == AllSymbolicTensorDescs.Num());
	
	return 0;
}

FRDGBufferDesc CreateRDGBufferDescForTensorRDG(const FTensorRDG& Tensor)
{
	// FIXME: CreateStructuredDesc() creates a crash on VulkanRHI
	//FRDGBufferDesc Desc = FRDGBufferDesc::CreateStructuredDesc(Tensor.GetElemByteSize(), Tensor.GetVolume());
	FRDGBufferDesc Desc = FRDGBufferDesc::CreateBufferDesc(Tensor.GetElemByteSize(), Tensor.GetVolume());

	return Desc;
}

/**
 * Enqueue operators to RDG, the caller will run the GraphBuilder.Execute()
 */
int FMLInferenceModelRDG::EnqueueRDG(FRDGBuilder& RDGBuilder, TConstArrayView<FMLTensorBinding> InInputBindings, TConstArrayView<FMLTensorBinding> InOutputBindings)
{
	check(IsInRenderingThread());

	int Res;

	// Verify the model inputs were prepared
	if (InputTensorShapes.Num() == 0)
	{
		UE_LOG(LogNNX, Error, TEXT("EnqueueRDG(): Input shapes are not set, please call SetInputTensorShapes."));
		return -1;
	}

	// Process input binding, and if required, allocate RDG buffers
	FTensorRDGArray	InputTensorRDGs;
	FIntArray		InputUploadIndices;

	Res = SetTensors(RDGBuilder, InputTensorRDGs, InputUploadIndices, InInputBindings, InputSymbolicTensors, InputTensorShapes);
	if (Res != 0)
	{
		UE_LOG(LogNNX, Warning, TEXT("Invalid input tensor binding type for tensor index:%d"), Res);
		return -1;
	}

	// Process output tensors, and if required, allocate RDG buffers
	FTensorRDGArray	OutputTensorRDGs;
	FIntArray		OutputReadbackIndices;

	Res = SetTensors(RDGBuilder, OutputTensorRDGs, OutputReadbackIndices, InOutputBindings, OutputSymbolicTensors, OutputTensorShapes);
	if (Res != 0)
	{
		UE_LOG(LogNNX, Warning, TEXT("Invalid output tensor binding type for tensor index:%d"), Res);
		return -1;
	}

	// If required, upload input tensors to GPU
	AddTensorUploads_RenderThread(RDGBuilder, InputUploadIndices, InputTensorRDGs, InInputBindings);

	AllTensorRDGs.Empty();
	AllTensorRDGs.SetNum(AllShapes.Num());
	checkCode( for (FTensorRDG& TensorRDG : AllTensorRDGs) { TensorRDG.SetBuffer(nullptr); } );
	
	//Create intermediate tensors bindings
	for (int32 Idx : IntermediateTensorIndices)
	{
		const FTensorDesc& TensorDesc = AllSymbolicTensorDescs[Idx];
		const FTensorShape& TensorShape = AllShapes[Idx];
		FTensorRDG TensorRDG = FTensorRDG::Make(TensorDesc, TensorShape, nullptr);
		const FRDGBufferDesc BufferDesc = CreateRDGBufferDescForTensorRDG(TensorRDG);
		const FRDGBufferRef TensorBuffer = RDGBuilder.CreateBuffer(BufferDesc, *TensorRDG.GetName(), ERDGBufferFlags::None);
		
		TensorRDG.SetBuffer(TensorBuffer);
		AllTensorRDGs[Idx] = TensorRDG;
	}

	//Insert input tensors bindings
	for (int32 i = 0; i < InputTensorIndices.Num(); ++i)
	{
		AllTensorRDGs[InputTensorIndices[i]] = InputTensorRDGs[i];
	}

	//Insert output tensors bindings
	for (int32 i = 0; i < OutputTensorIndices.Num(); ++i)
	{
		AllTensorRDGs[OutputTensorIndices[i]] = OutputTensorRDGs[i];
	}
	checkCode(for (const FTensorRDG& TensorRDG : AllTensorRDGs) { check(TensorRDG.GetBuffer() != nullptr); });
	
	// We can now dispatch operators
	AddDispatchOps_RenderThread(RDGBuilder);

	// If required, readback the output tensors to CPU
	AddTensorReadbacks_RenderThread(RDGBuilder, OutputReadbackIndices, OutputTensorRDGs, InOutputBindings);

	return 0;
}

/** 
 * Process binding and check if we need to create RDG Buffer for CPU binding 
 * Returns 0 on success, or index of a tensor if the tensor type is not supported.
 */
int FMLInferenceModelRDG::SetTensors(FRDGBuilder& GraphBuilder, FTensorRDGArray& OutTensorRDGs, FIntArray& OutIndices, 
	TConstArrayView<FMLTensorBinding> InBindings, TConstArrayView<FTensorDesc> InTensorDescs, TConstArrayView<FTensorShape> InTensorShapes)
{
	check(InBindings.Num() == InTensorDescs.Num());
	check(InBindings.Num() == InTensorShapes.Num());
	
	for (int32 Idx = 0; Idx < InBindings.Num(); ++Idx)
	{
		const FMLTensorBinding& Binding = InBindings[Idx];
		const FTensorDesc& TensorDesc = InTensorDescs[Idx];
		const FTensorShape& TensorShape = InTensorShapes[Idx];

		if (Binding.BindingType == EMLTensorBindingDataType::CPUMemory)
		{
			FTensorRDG TensorRDG = FTensorRDG::Make(TensorDesc, TensorShape, nullptr);
			FRDGBufferDesc Desc = CreateRDGBufferDescForTensorRDG(TensorRDG);
			
			// FIXME: We should use BUF_SourceCopy for only output buffers (GPU readback)
			Desc.Usage = EBufferUsageFlags(Desc.Usage | BUF_SourceCopy);
			
			FRDGBufferRef TensorBuffer = GraphBuilder.CreateBuffer(Desc, *TensorDesc.GetName(), ERDGBufferFlags::None);
			
			TensorRDG.SetBuffer(TensorBuffer);
			OutTensorRDGs.Add(TensorRDG);
			OutIndices.Add(Idx);
		}
		else if (Binding.BindingType == EMLTensorBindingDataType::RDGBuffer)
		{
			FTensorRDG TensorRDG = FTensorRDG::Make(TensorDesc, TensorShape, Binding.Buffer);
			OutTensorRDGs.Add(TensorRDG);
		}
		else
		{
			// Unsupported tensor binding type
			return Idx;
		}
	}

	return 0;
}

//
//
//
void FMLInferenceModelRDG::AddTensorUploads_RenderThread(FRDGBuilder& GraphBuilder, TConstArrayView<int32> InUploadIndices, 
	TConstArrayView<FTensorRDG> InTensorRDGs, TConstArrayView<FMLTensorBinding> InBindings)
{
	check(InTensorRDGs.Num() == InBindings.Num());
	
	for (int32 Idx = 0; Idx < InUploadIndices.Num(); ++Idx)
	{
		const int32			TensorIdx = InUploadIndices[Idx];
		const FTensorRDG&	TensorRDG = InTensorRDGs[TensorIdx];
		const FMLTensorBinding& Binding = InBindings[TensorIdx];
		check(Binding.BindingType == EMLTensorBindingDataType::CPUMemory);
		GraphBuilder.QueueBufferUpload(TensorRDG.GetBuffer(), Binding.CpuMemory, TensorRDG.GetDataSize(), ERDGInitialDataFlags::NoCopy);
	}
}

//
//
//
void FMLInferenceModelRDG::AddTensorReadbacks_RenderThread(FRDGBuilder& GraphBuilder, TConstArrayView<int32> InReadbackIndices,
	TConstArrayView<FTensorRDG> InTensorRDGs, TConstArrayView<FMLTensorBinding> InBindings)
{
	check(InTensorRDGs.Num() == InBindings.Num());
	
	for (int32 Idx = 0; Idx < InReadbackIndices.Num(); ++Idx)
	{
		const int32			TensorIdx = InReadbackIndices[Idx];
		const FTensorRDG&	TensorRDG = InTensorRDGs[TensorIdx];
		const FMLTensorBinding& Binding = InBindings[TensorIdx];
		check(Binding.BindingType == EMLTensorBindingDataType::CPUMemory);
		
		FMLTensorReadbackParameters* TensorReadbackParams = GraphBuilder.AllocParameters<FMLTensorReadbackParameters>();

		TensorReadbackParams->Buffer = TensorRDG.GetBuffer();

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("FMLInferenceModelAddTensorReadback:%s", *TensorRDG.GetName()),
			TensorReadbackParams,
			ERDGPassFlags::Readback | ERDGPassFlags::NeverCull,
			[this, Binding, TensorRDG, TensorReadbackParams](FRHICommandListImmediate& RHICmdList)
			{
				FRHIBuffer* OutputBuffer = TensorReadbackParams->Buffer->GetRHI();

				// TODO: FIXME: We need to transition the resources for DirectML
				if (bUseManualTransitions)
				{
					FRHITransitionInfo Transitions[] =
					{
						FRHITransitionInfo(OutputBuffer, ERHIAccess::UAVCompute, ERHIAccess::CopySrc)
					};

					RHICmdList.Transition(MakeArrayView(Transitions, UE_ARRAY_COUNT(Transitions)));
					RHICmdList.SubmitCommandsHint();
				}

				Readback.RHI->EnqueueCopy(RHICmdList, OutputBuffer, TensorRDG.GetDataSize());
				Readback.CpuMemory = Binding.CpuMemory;
				Readback.Offset = 0;
				Readback.Size = TensorRDG.GetDataSize();
			}
		);
	}
}


//
//
//
//void FMLInferenceModelRDG::AddTensorUpload_RenderThread(FRDGBuilder& GraphBuilder, FRDGBufferRef TensorBuffer, const FMLTensorBinding& InTensorBinding, const FMLTensorDesc& TensorDesc)
//{
//	FMLTensorUploadParameters* TensorUploadParams = GraphBuilder.AllocParameters<FMLTensorUploadParameters>();
//
//	TensorUploadParams->Buffer = TensorBuffer;
//
//	GraphBuilder.AddPass(
//		RDG_EVENT_NAME("NNXDmlTensorUpload"),
//		TensorUploadParams,
//		ERDGPassFlags::Copy | ERDGPassFlags::NeverCull,
//		[InTensorBinding, TensorDesc, TensorUploadParams](FRHICommandListImmediate& RHICmdList)
//		{
//			// Copy input					
//			void* BuffData = RHICmdList.LockBuffer(TensorUploadParams->Buffer->GetRHI(), 0, TensorDesc.DataSize, RLM_WriteOnly);
//			FMemory::Memcpy(BuffData, InTensorBinding.CpuMemory, TensorDesc.DataSize);
//			RHICmdList.UnlockBuffer(TensorUploadParams->Buffer->GetRHI());
//		}
//	);
//}

//
//
//
//void FMLInferenceModelRDG::AddTensorReadback_RenderThread(FRDGBuilder& GraphBuilder, const FMLTensorBinding& InTensorBinding, const FMLTensorDesc& TensorDesc)
//{	
//	FMLTensorReadbackParameters* TensorReadbackParams = GraphBuilder.AllocParameters<FMLTensorReadbackParameters>();
//
//	TensorReadbackParams->Buffer = TensorBuffer;
//
//	GraphBuilder.AddPass(
//		RDG_EVENT_NAME("NNXDmlTensorReadback"),
//		TensorReadbackParams,
//		ERDGPassFlags::Copy | ERDGPassFlags::NeverCull,
//		[InTensorBinding, TensorDesc, TensorReadbackParams](FRHICommandListImmediate& RHICmdList)
//		{
//			// Copy input					
//			void* BuffData = RHICmdList.LockBuffer(TensorReadbackParams->Buffer->GetRHI(), 0, TensorDesc.DataSize, RLM_WriteOnly);
//			FMemory::Memcpy(BuffData, InTensorBinding.CpuMemory, TensorDesc.DataSize);
//			RHICmdList.UnlockBuffer(TensorReadbackParams->Buffer->GetRHI());
//		}
//	);
//}

} // NNX
