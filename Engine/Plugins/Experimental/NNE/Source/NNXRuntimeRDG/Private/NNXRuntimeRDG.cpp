// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXRuntimeRDG.h"
#include "NNXInferenceModel.h"
#include "NNXRuntimeFormat.h"
#include "NNXModelOptimizer.h"

#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIGPUReadback.h"

#include "Serialization/MemoryReader.h"

namespace NNX
{

FGuid FMLRuntimeRDG::GUID = FGuid((int32)'R', (int32)'R', (int32)'D', (int32)'G');
int32 FMLRuntimeRDG::Version = 0x00000001;

struct FMLInferenceModelRDG::FReadbackEntry
{
	TUniquePtr<FRHIGPUBufferReadback>	RHI;
	void*					CpuMemory;
	size_t					Offset;
	size_t					Size;
};

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

bool FMLRuntimeRDG::CanCreateModelData(FString FileType, TConstArrayView<uint8> FileData) const
{
	return FileType.Compare("onnx", ESearchCase::IgnoreCase) == 0;
}

bool FMLRuntimeRDG::CanCreateModel(TConstArrayView<uint8> ModelData) const
{
	int32 GuidSize = sizeof(FMLRuntimeRDG::GUID);
	int32 VersionSize = sizeof(FMLRuntimeRDG::Version);
	if (ModelData.Num() <= GuidSize + VersionSize)
	{
		return false;
	}
	bool bResult = FGenericPlatformMemory::Memcmp(&(ModelData[0]), &(FMLRuntimeRDG::GUID), GuidSize) == 0;
	bResult &= FGenericPlatformMemory::Memcmp(&(ModelData[GuidSize]), &(FMLRuntimeRDG::Version), VersionSize) == 0;
	return bResult;
};
	
//
//
//
FMLInferenceModelRDG::FMLInferenceModelRDG()
	: FMLInferenceModel(EMLInferenceModelType::RDG)
	, bUseManualTransitions(false)
{
	
}

FMLInferenceModelRDG::~FMLInferenceModelRDG()
{
	
}

//
//
//
bool FMLInferenceModelRDG::LoadModel(TConstArrayView<uint8> ModelData, FMLRuntimeFormat& Format)
{
	int32 GuidSize = sizeof(FMLRuntimeRDG::GUID);
	int32 VersionSize = sizeof(FMLRuntimeRDG::Version);
	TConstArrayView<uint8> ModelBuffer = {&(ModelData.GetData()[GuidSize + VersionSize]), ModelData.Num() - GuidSize - VersionSize};

	FMemoryReaderView Reader(ModelBuffer);

	FMLRuntimeFormat::StaticStruct()->SerializeBin(Reader, &Format);

	// Data for base class
	InputSymbolicTensors.Empty();
	OutputSymbolicTensors.Empty();
	
	// Data for RDG
	AllSymbolicTensorDescs.Empty();
	IntermediateTensorIndices.Empty();
	WeightTensorIndices.Empty();
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
		else if (FormatTensorDesc.Type == EMLFormatTensorType::Initializer)
		{
			WeightTensorIndices.Emplace(Idx);
			if (!SymbolicTensor.IsConcrete())
			{
				UE_LOG(LogNNX, Error, TEXT("Weight tensor %s should have a concrete shape"), *SymbolicTensor.GetName());
				return false;
			}
			
			const FTensorShape TensorShape = FTensorShape::MakeFromSymbolic(SymbolicTensor.GetShape());
			FTensorRDG& WeightRDG = WeightTensorRDGs.Emplace_GetRef(FTensorRDG::Make(SymbolicTensor, TensorShape, nullptr));
			
			if (WeightRDG.GetDataSize() != FormatTensorDesc.DataSize)
			{
				UE_LOG(LogNNX, Error, TEXT("Weight %s has incorrect size. Expected %d bytes, got %d"), *SymbolicTensor.GetName(), FormatTensorDesc.DataSize, WeightRDG.GetDataSize());
				return false;
			}

			const uint8* DataPtr = Format.TensorData.GetData() + FormatTensorDesc.DataOffset;
			TConstArrayView<uint8> DataView = MakeArrayView(DataPtr, FormatTensorDesc.DataSize);

			WeightRDG.SetPreparedData(DataView);
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
int FMLInferenceModelRDG::RunSync(TConstArrayView<FMLTensorBinding> InInputBindings, TConstArrayView<FMLTensorBinding> InOutputBindings)
{
	// Verify the model inputs were prepared
	if (InputTensorShapes.Num() == 0)
	{
		UE_LOG(LogNNX, Error, TEXT("Run(): Input shapes are not set, please call SetInputTensorShapes."));
		return -1;
	}

	// TODO ok if pending?
	Readbacks.Empty();
	
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
				for (const FReadbackEntry& Readback : Readbacks) {
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

	//Allocate and prime all AllTensorRDGs with concrete shapes defaulting variables dimension to 1 if needed
	AllTensorRDGs.Init(nullptr, AllSymbolicTensorDescs.Num());

	InputTensorRDGs.Empty();
	for (int32 i = 0; i < InputTensorIndices.Num(); ++i)
	{
		const int32 Idx = InputTensorIndices[i];
		const FTensorDesc& TensorDesc = InputSymbolicTensors[i];
		const FTensorShape& TensorShape = InputTensorShapes[i];
		
		InputTensorRDGs.Emplace(FTensorRDG::Make(TensorDesc, TensorShape, nullptr));
		AllTensorRDGs[Idx] = &InputTensorRDGs[i];
	}

	for (int32 i = 0; i < WeightTensorIndices.Num(); ++i)
	{
		const int32 Idx = WeightTensorIndices[i];
		
		AllTensorRDGs[Idx] = &WeightTensorRDGs[i];
	}

	IntermediateTensorRDGs.Empty();
	for (int32 i = 0; i < IntermediateTensorIndices.Num(); ++i)
	{
		const int32 Idx = IntermediateTensorIndices[i];
		const FTensorDesc& TensorDesc = AllSymbolicTensorDescs[Idx];
		const FTensorShape TensorShape = FTensorShape::MakeFromSymbolic(TensorDesc.GetShape());
		
		IntermediateTensorRDGs.Emplace(FTensorRDG::Make(TensorDesc, TensorShape, nullptr));
		AllTensorRDGs[Idx] = &IntermediateTensorRDGs[i];
	}

	OutputTensorRDGs.Empty();
	for (int32 i = 0; i < OutputTensorIndices.Num(); ++i)
	{
		const int32 Idx = OutputTensorIndices[i];
		const FTensorDesc& TensorDesc = OutputSymbolicTensors[i];
		const FTensorShape TensorShape = FTensorShape::MakeFromSymbolic(TensorDesc.GetShape());
		
		OutputTensorRDGs.Emplace(FTensorRDG::Make(TensorDesc, TensorShape, nullptr));
		AllTensorRDGs[Idx] = &OutputTensorRDGs[i];
	}

	checkCode(
		for (int i = 0; i < AllTensorRDGs.Num(); ++i)
		{
			checkf(AllTensorRDGs[i] != nullptr, TEXT("Tensor at index %d, was not allocated for model preparation."), i);
		};
	);

	//Allow the specific engine to run shape inference if supported
	if (PrepareTensorShapesAndData() != 0)
	{
		return -1;
	}

	checkCode(
		for (int i = 0; i < AllTensorRDGs.Num(); ++i)
		{
			checkf(AllTensorRDGs[i] != nullptr, TEXT("Tensor at index %d, was not allocated after model preparation."), i);
			checkf(AllTensorRDGs[i]->GetShape().IsCompatibleWith(AllSymbolicTensorDescs[i].GetShape()), TEXT("Tensor at index %d have a shape incompatible with model definition."), i);
		};
	);

	//Set OutputTensorShapes for the model from preparation result
	for (int32 OutputIndices : OutputTensorIndices)
	{
		OutputTensorShapes.Emplace(AllTensorRDGs[OutputIndices]->GetShape());
	}

	check(InputTensorIndices.Num() + OutputTensorIndices.Num() + WeightTensorIndices.Num() + IntermediateTensorIndices.Num() == AllTensorRDGs.Num());
	check(InputTensorShapes.Num() == InputSymbolicTensors.Num());
	check(OutputTensorShapes.Num() == OutputSymbolicTensors.Num());
	check(WeightTensorIndices.Num() == WeightTensorRDGs.Num());
	check(AllTensorRDGs.Num() == AllSymbolicTensorDescs.Num());
	
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
	FIntArray		InputUploadIndices;
	Res = SetTensors(RDGBuilder, InputTensorRDGs, InputUploadIndices, InInputBindings);
	if (Res != 0)
	{
		UE_LOG(LogNNX, Warning, TEXT("Invalid input tensor binding type for tensor index:%d"), Res);
		return -1;
	}

	// Process output tensors, and if required, allocate RDG buffers
	FIntArray		OutputReadbackIndices;

	Res = SetTensors(RDGBuilder, OutputTensorRDGs, OutputReadbackIndices, InOutputBindings);
	if (Res != 0)
	{
		UE_LOG(LogNNX, Warning, TEXT("Invalid output tensor binding type for tensor index:%d"), Res);
		return -1;
	}

	// If required, upload input tensors to GPU
	AddTensorUploads_RenderThread(RDGBuilder, InputUploadIndices, InputTensorRDGs, InInputBindings);

	//Create buffer for intermediate tensors
	for (FTensorRDG& TensorRDG : IntermediateTensorRDGs)
	{
		const FRDGBufferDesc BufferDesc = CreateRDGBufferDescForTensorRDG(TensorRDG);
		const FRDGBufferRef TensorBuffer = RDGBuilder.CreateBuffer(BufferDesc, *TensorRDG.GetName(), ERDGBufferFlags::None);
		check(TensorRDG.GetBuffer() == nullptr);
		TensorRDG.SetBuffer(TensorBuffer);
	}

	// TODO: FIXME: DirectML uses RHI buffers instead of RDG bufers
	//For now weights tensors are not uploaded to GPU thus GetBuffer will return nullptr for them.
	//checkCode(for (const FTensorRDG* TensorRDG : AllTensorRDGs) { if (TensorRDG != nullptr) { check(TensorRDG->GetBuffer() != nullptr); } });

	//Insert weights tensors
	for (int32 i = 0; i < WeightTensorIndices.Num(); ++i)
	{
		AllTensorRDGs[WeightTensorIndices[i]] = &WeightTensorRDGs[i];
	}
	
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
int FMLInferenceModelRDG::SetTensors(FRDGBuilder& GraphBuilder, FTensorRDGArray& InTensorRDGs, FIntArray& OutIndices, 
	TConstArrayView<FMLTensorBinding> InBindings)
{
	check(InBindings.Num() == InTensorRDGs.Num());
	
	for (int32 Idx = 0; Idx < InBindings.Num(); ++Idx)
	{
		FTensorRDG& TensorRDG = InTensorRDGs[Idx];
		const FMLTensorBinding& Binding = InBindings[Idx];

		if (Binding.BindingType == EMLTensorBindingDataType::CPUMemory)
		{
			FRDGBufferDesc Desc = CreateRDGBufferDescForTensorRDG(TensorRDG);
			
			// FIXME: We should use BUF_SourceCopy for only output buffers (GPU readback)
			Desc.Usage = EBufferUsageFlags(Desc.Usage | BUF_SourceCopy);
			
			FRDGBufferRef TensorBuffer = GraphBuilder.CreateBuffer(Desc, *TensorRDG.GetName(), ERDGBufferFlags::None);
			
			TensorRDG.SetBuffer(TensorBuffer);
			OutIndices.Add(Idx);
		}
		else if (Binding.BindingType == EMLTensorBindingDataType::RDGBuffer)
		{
			TensorRDG.SetBuffer(Binding.Buffer);
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

	check(Readbacks.IsEmpty());
	
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

				FReadbackEntry& Readback = Readbacks.Add_GetRef({});
				Readback.RHI = MakeUnique<FRHIGPUBufferReadback>(FName(TEXT("FMLTensorReadback_") + TensorRDG.GetName()));
				Readback.RHI->EnqueueCopy(RHICmdList, OutputBuffer, TensorRDG.GetDataSize());
				Readback.CpuMemory = Binding.CpuMemory;
				Readback.Offset = 0;
				Readback.Size = TensorRDG.GetDataSize();
			}
		);
	}
}

TArray<uint8> ConvertToModelData(TArrayView<uint8> ModelBuffer)
{
	TArray<uint8> Result;

	FMemoryWriter Writer(Result);
	Writer << FMLRuntimeRDG::GUID;
	Writer << FMLRuntimeRDG::Version;
	Writer.Serialize(ModelBuffer.GetData(), ModelBuffer.Num());

	return Result;
}

} // NNX
