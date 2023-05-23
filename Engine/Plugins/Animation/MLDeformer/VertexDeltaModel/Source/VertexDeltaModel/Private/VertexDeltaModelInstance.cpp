// Copyright Epic Games, Inc. All Rights Reserved.

#include "VertexDeltaModelInstance.h"
#include "VertexDeltaModel.h"
#include "MLDeformerAsset.h"
#include "RenderGraphBuilder.h"
#include "Components/SkeletalMeshComponent.h"

UVertexDeltaModel* UVertexDeltaModelInstance::GetVertexDeltaModel() const
{
	return Cast<UVertexDeltaModel>(Model);
}


UE::NNECore::IModelRDG* UVertexDeltaModelInstance::GetNNEModelRDG() const
{
	return ModelRDG.Get();
}

TRefCountPtr<FRDGPooledBuffer> UVertexDeltaModelInstance::GetOutputRDGBuffer() const
{
	return RDGVertexDeltaBuffer; 
}

FString UVertexDeltaModelInstance::CheckCompatibility(USkeletalMeshComponent* InSkelMeshComponent, bool bLogIssues)
{
	FString ErrorString = Super::CheckCompatibility(InSkelMeshComponent, bLogIssues);

	// Verify the number of inputs versus the expected number of inputs.	
	const UE::NNECore::IModelRDG* RDGModel = GetNNEModelRDG();
	if (RDGModel && Model->GetDeformerAsset())
	{
		TConstArrayView<UE::NNECore::FTensorDesc> GPUTensorDesc = RDGModel->GetInputTensorDescs();
		const int64 NumNeuralNetInputs = GPUTensorDesc[0].GetShape().Rank();
		const int64 NumDeformerAssetInputs = static_cast<int64>(Model->GetInputInfo()->CalcNumNeuralNetInputs(Model->GetNumFloatsPerBone(), Model->GetNumFloatsPerCurve()));
		if (NumNeuralNetInputs != NumDeformerAssetInputs)
		{
			const FString InputErrorString = "The number of network inputs doesn't match the asset. Please retrain the asset."; 
			ErrorText += InputErrorString + "\n";
			if (bLogIssues)
			{
				UE_LOG(LogVertexDeltaModel, Error, TEXT("Deformer '%s': %s"), *(Model->GetDeformerAsset()->GetName()), *InputErrorString);
			}
		}
	}

	return ErrorString;
}

bool UVertexDeltaModelInstance::IsValidForDataProvider() const
{
	return ModelRDG.IsValid(); 
}

void UVertexDeltaModelInstance::Execute(float ModelWeight)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UMLDeformerModelInstance::Execute)

	if (ModelRDG)
	{
		ENQUEUE_RENDER_COMMAND(RunNeuralNetwork)
			(
				[this](FRHICommandListImmediate& RHICmdList)
				{
					ERHIPipeline Pipeline = RHICmdList.GetPipeline();

					if (Pipeline == ERHIPipeline::None)
					{
						RHICmdList.SwitchPipeline(ERHIPipeline::Graphics);
					}
					// Output deltas will be available on GPU for DeformerGraph via UMLDeformerDataProvider.
					FRDGBuilder GraphBuilder(RHICmdList);

					// Build Input Bindings
					TArray<UE::NNECore::FTensorBindingRDG> InputBindingsRDG;
					UE::NNECore::FTensorBindingRDG& BindingRDG = InputBindingsRDG.Emplace_GetRef();
					BindingRDG.Buffer = GraphBuilder.RegisterExternalBuffer(RDGInputBuffer);
					GraphBuilder.QueueBufferUpload(BindingRDG.Buffer, NNEInputTensorBuffer.GetData(), NNEInputTensorBuffer.Num() * sizeof(float), ERDGInitialDataFlags::NoCopy);
	
					// Build Output Bindings
					TArray<UE::NNECore::FTensorBindingRDG> OutputBindingsRDG;
					UE::NNECore::FTensorBindingRDG& OutputBindingRDG = OutputBindingsRDG.Emplace_GetRef();
					OutputBindingRDG.Buffer = GraphBuilder.RegisterExternalBuffer(RDGVertexDeltaBuffer);

					if (!ModelRDG.IsValid())
					{
						return;
					}
					int32 Res = ModelRDG->EnqueueRDG(GraphBuilder, InputBindingsRDG, OutputBindingsRDG);
	
					GraphBuilder.Execute();
				}
		);
	}
}

bool UVertexDeltaModelInstance::GetRDGVertexBufferDesc(TConstArrayView<UE::NNECore::FTensorDesc>& InOutputTensorDescs, FRDGBufferDesc& OutBufferDesc)
{
	if (InOutputTensorDescs.Num() > 0)
	{
		const uint32 ElemByteSize = InOutputTensorDescs[0].GetElemByteSize();
		const UE::NNECore::FSymbolicTensorShape& SymShape = InOutputTensorDescs[0].GetShape();
		for (int32 i = 1; i < InOutputTensorDescs.Num(); i++)
		{
			if (InOutputTensorDescs[i].GetElemByteSize() != ElemByteSize || SymShape != InOutputTensorDescs[i].GetShape())
			{
				return false;
			}
		}
		// Create a single flat output buffer
		const UE::NNECore::FTensorShape OutputShape = UE::NNECore::FTensorShape::MakeFromSymbolic(SymShape);
		OutBufferDesc.BytesPerElement = ElemByteSize;
		OutBufferDesc.NumElements = OutputShape.Volume() * InOutputTensorDescs.Num();
		OutBufferDesc.Usage = EBufferUsageFlags::UnorderedAccess | EBufferUsageFlags::ShaderResource | EBufferUsageFlags::VertexBuffer;
		return true; 
	}
	return false;
}

void UVertexDeltaModelInstance::CreateRDGBuffers(TConstArrayView<UE::NNECore::FTensorDesc>& OutputTensorDescs)
{
	ENQUEUE_RENDER_COMMAND(VertexDeltaModelInstance_CreateOuputRDGBuffer)(
		[this, &OutputTensorDescs](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder Builder(RHICmdList);
			FRDGBufferDesc VertexBufferDesc;
			if (GetRDGVertexBufferDesc(OutputTensorDescs, VertexBufferDesc))
			{
				FRDGBuffer* RDGBuffer = Builder.CreateBuffer(VertexBufferDesc, TEXT("UVertexDeltaModelInstance_OutputBuffer"));
				RDGVertexDeltaBuffer = Builder.ConvertToExternalBuffer(RDGBuffer);
			}

			FRDGBufferDesc InputDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(float), NNEInputTensorBuffer.Num());
			InputDesc.Usage = EBufferUsageFlags(InputDesc.Usage | BUF_SourceCopy);

			FRDGBuffer* RDGBuffer = Builder.CreateBuffer(InputDesc, TEXT("UVertexDeltaModelInstance_InputBuffer"), ERDGBufferFlags::None);
			RDGInputBuffer = Builder.ConvertToExternalBuffer(RDGBuffer);

			Builder.Execute();
		}
	);

	FRenderCommandFence RenderFence;
	RenderFence.BeginFence();
	RenderFence.Wait();

}

void UVertexDeltaModelInstance::PostMLDeformerComponentInit()
{
	if (!bNNECreationAttempted)
	{
		bNNECreationAttempted = true;
		CreateNNEModel();
	}
}

void UVertexDeltaModelInstance::CreateNNEModel()
{
	if (!ModelRDG.IsValid())
	{
		UVertexDeltaModel* VertexDeltaModel = Cast<UVertexDeltaModel>(Model);
		if (VertexDeltaModel)
		{
			TWeakInterfacePtr<INNERuntime> Runtime = UE::NNECore::GetRuntime<INNERuntime>(VertexDeltaModel->GetNNERuntimeName());
			TWeakInterfacePtr<INNERuntimeRDG> RuntimeRDG = UE::NNECore::GetRuntime<INNERuntimeRDG>(VertexDeltaModel->GetNNERuntimeName());

			if (!Runtime.IsValid())
			{
				UE_LOG(LogNNE, Error, TEXT("Can't get %s runtime."), *VertexDeltaModel->GetNNERuntimeName());
				return;
			}

			TObjectPtr<UNNEModelData> ModelData = VertexDeltaModel->NNEModel;
			if (ModelData)
			{
				if (RuntimeRDG.IsValid())
				{
					// allocate tensor inputs and outputs
					ModelRDG = RuntimeRDG->CreateModelRDG(ModelData);

					if (ModelRDG)
					{
						// setup inputs
						TConstArrayView<UE::NNECore::FTensorDesc> InputTensorDescs = ModelRDG->GetInputTensorDescs();
						UE::NNECore::FTensorShape InputTensorShape = UE::NNECore::FTensorShape::MakeFromSymbolic(InputTensorDescs[0].GetShape());
						ModelRDG->SetInputTensorShapes({ InputTensorShape });
						check(InputTensorDescs[0].GetElemByteSize() == sizeof(float));
						NNEInputTensorBuffer.SetNumUninitialized(InputTensorShape.Volume());

						// setup outputs
						TConstArrayView<UE::NNECore::FTensorDesc> OutputTensorDescs = ModelRDG->GetOutputTensorDescs();
						CreateRDGBuffers(OutputTensorDescs);
					}
					else
					{
						UE_LOG(LogNNE, Error, TEXT("Failed to create NNE RDG Model for VertexDeltaModel."));
						return;
					}
				}
			}
		}
	}
}

bool UVertexDeltaModelInstance::SetupInputs()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UMLDeformerModelInstance::SetupInputs)

	// Some safety checks.
	if (Model == nullptr ||
		SkeletalMeshComponent == nullptr ||
		SkeletalMeshComponent->GetSkeletalMeshAsset() == nullptr ||
		!bIsCompatible)
	{
		return false;
	}

	// Get the network and make sure it's loaded.

	if (!ModelRDG)
	{
		return false;
	}

	TConstArrayView<UE::NNECore::FTensorDesc> InputTensorDescs = ModelRDG->GetInputTensorDescs();
	const UE::NNECore::FTensorShape InputTensorShape = UE::NNECore::FTensorShape::MakeFromSymbolic(InputTensorDescs[0].GetShape());
	const int64 NumNeuralNetInputs = InputTensorShape.Volume();
	const int64 NumDeformerAssetInputs = Model->GetInputInfo()->CalcNumNeuralNetInputs(Model->GetNumFloatsPerBone(), Model->GetNumFloatsPerCurve());
	if (NumNeuralNetInputs != NumDeformerAssetInputs)
	{
		return false;
	}

	if (NNEInputTensorBuffer.Num() != NumNeuralNetInputs)
	{
		return false; 
	}
	// Update and write the input values directly into the input tensor.
	const int64 NumFloatsWritten = SetNeuralNetworkInputValues(NNEInputTensorBuffer.GetData(), NNEInputTensorBuffer.Num());
	check(NumFloatsWritten == NumNeuralNetInputs);
	return true;
}

