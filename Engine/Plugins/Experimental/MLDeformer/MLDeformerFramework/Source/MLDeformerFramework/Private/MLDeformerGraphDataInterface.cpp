// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerGraphDataInterface.h"

#include "Components/SkeletalMeshComponent.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "MLDeformerModel.h"
#include "MLDeformerModelInstance.h"
#include "MLDeformerAsset.h"
#include "MLDeformerComponent.h"
#include "NeuralNetwork.h"
#include "OptimusDataDomain.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderGraphResources.h"
#include "ShaderParameterMetadataBuilder.h"
#include "SkeletalRenderPublic.h"

TArray<FOptimusCDIPinDefinition> UMLDeformerGraphDataInterface::GetPinDefinitions() const
{
	TArray<FOptimusCDIPinDefinition> Defs;
	Defs.Add({ "PositionDelta", "ReadPositionDelta", Optimus::DomainName::Vertex, "ReadNumVertices" });
	return Defs;
}

TSubclassOf<UActorComponent> UMLDeformerGraphDataInterface::GetRequiredComponentClass() const
{
	return UMLDeformerComponent::StaticClass();
}

void UMLDeformerGraphDataInterface::GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
{
	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadNumVertices"))
		.AddReturnType(EShaderFundamentalType::Uint);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadPositionDelta"))
		.AddReturnType(EShaderFundamentalType::Float, 3)
		.AddParam(EShaderFundamentalType::Uint);
}

BEGIN_SHADER_PARAMETER_STRUCT(FMLDeformerGraphDataInterfaceParameters, )
	MLDEFORMER_SHADER_PARAMETERS()
END_SHADER_PARAMETER_STRUCT()

MLDEFORMER_GRAPH_IMPLEMENT_BASICS(
	UMLDeformerGraphDataInterface,
	UMLDeformerGraphDataProvider,
	UE::MLDeformer::FMLDeformerGraphDataProviderProxy,
	FMLDeformerGraphDataInterfaceParameters,
	TEXT("#include \"/Plugin/MLDeformerFramework/Private/MLDeformerGraphDataInterface.ush\"\n"),
	TEXT("ML Deformer"))


bool UMLDeformerGraphDataProvider::IsValid() const
{
	if (DeformerComponent == nullptr || DeformerComponent->GetDeformerAsset() == nullptr)
	{
		return false;
	}

	return DeformerComponent->GetModelInstance()->IsValidForDataProvider();
}

namespace UE::MLDeformer
{
	FMLDeformerGraphDataProviderProxy::FMLDeformerGraphDataProviderProxy(UMLDeformerComponent* DeformerComponent)
	{
		using namespace UE::MLDeformer;

		const UMLDeformerAsset* DeformerAsset = DeformerComponent->GetDeformerAsset();
		const UMLDeformerModel* Model = DeformerAsset->GetModel();
		const UMLDeformerModelInstance* ModelInstance = DeformerComponent->GetModelInstance();
		
		SkeletalMeshObject = ModelInstance->GetSkeletalMeshComponent()->MeshObject;
		NeuralNetwork = Model->GetNeuralNetwork();
		NeuralNetworkInferenceHandle = ModelInstance->GetNeuralNetworkInferenceHandle();
		bCanRunNeuralNet = ModelInstance->IsCompatible();
		Weight = DeformerComponent->GetWeight();
		VertexMapBufferSRV = Model->GetVertexMapBuffer().ShaderResourceViewRHI;
	}

	void FMLDeformerGraphDataProviderProxy::AllocateResources(FRDGBuilder& GraphBuilder)
	{
		if (bCanRunNeuralNet)
		{
			check(NeuralNetwork != nullptr);
			Buffer = GraphBuilder.RegisterExternalBuffer(NeuralNetwork->GetOutputTensorForContext(NeuralNetworkInferenceHandle).GetPooledBuffer());
		}
		else
		{
			// TODO: use an actual buffer that is of the right size, and filled with zero's.
			Buffer = GraphBuilder.RegisterExternalBuffer(GWhiteVertexBufferWithRDG->Buffer);
		}

		BufferSRV = GraphBuilder.CreateSRV(Buffer, PF_R32_FLOAT);
	}

	void FMLDeformerGraphDataProviderProxy::GatherDispatchData(FDispatchSetup const& InDispatchSetup, FCollectedDispatchData& InOutDispatchData)
	{
		MLDEFORMER_GRAPH_DISPATCH_START(FMLDeformerGraphDataInterfaceParameters, InDispatchSetup, InOutDispatchData)
		MLDEFORMER_GRAPH_DISPATCH_DEFAULT_PARAMETERS()
		MLDEFORMER_GRAPH_DISPATCH_END()
	}
}	// namespace UE::MLDeformer
