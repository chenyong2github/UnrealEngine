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
#include "ShaderCompilerCore.h"
#include "ShaderParameterMetadataBuilder.h"
#include "SkeletalRenderPublic.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MLDeformerGraphDataInterface)

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

FString UMLDeformerGraphDataInterface::GetDisplayName() const
{
	return TEXT("ML Deformer");
}

void UMLDeformerGraphDataInterface::GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& InOutBuilder, FShaderParametersMetadataAllocations& InOutAllocations) const \
{
	InOutBuilder.AddNestedStruct<FMLDeformerGraphDataInterfaceParameters>(UID);
}

TCHAR const* UMLDeformerGraphDataInterface::TemplateFilePath = TEXT("/Plugin/MLDeformerFramework/Private/MLDeformerGraphDataInterface.ush");

TCHAR const* UMLDeformerGraphDataInterface::GetShaderVirtualPath() const
{
	return TemplateFilePath;
}

void UMLDeformerGraphDataInterface::GetShaderHash(FString& InOutKey) const
{
	GetShaderFileHash(TemplateFilePath, EShaderPlatform::SP_PCD3D_SM5).AppendString(InOutKey);
}

void UMLDeformerGraphDataInterface::GetHLSL(FString& OutHLSL, FString const& InDataInterfaceName) const
{
	TMap<FString, FStringFormatArg> TemplateArgs =
	{
		{ TEXT("DataInterfaceName"), InDataInterfaceName },
	};

	FString TemplateFile;
	LoadShaderSourceFile(TemplateFilePath, EShaderPlatform::SP_PCD3D_SM5, &TemplateFile, nullptr);
	OutHLSL += FString::Format(*TemplateFile, TemplateArgs);
}

UComputeDataProvider* UMLDeformerGraphDataInterface::CreateDataProvider(TObjectPtr<UObject> InBinding, uint64 InInputMask, uint64 InOutputMask) const
{
	UMLDeformerGraphDataProvider* Provider = NewObject<UMLDeformerGraphDataProvider>();
	Provider->DeformerComponent = Cast<UMLDeformerComponent>(InBinding);
	return Provider;
}

FComputeDataProviderRenderProxy* UMLDeformerGraphDataProvider::GetRenderProxy()
{
	return new UE::MLDeformer::FMLDeformerGraphDataProviderProxy(DeformerComponent);
}

namespace UE::MLDeformer
{
	FMLDeformerGraphDataProviderProxy::FMLDeformerGraphDataProviderProxy(UMLDeformerComponent* DeformerComponent)
	{
		using namespace UE::MLDeformer;

		const UMLDeformerAsset* DeformerAsset = DeformerComponent != nullptr ? DeformerComponent->GetDeformerAsset() : nullptr;
		const UMLDeformerModel* Model = DeformerAsset != nullptr ? DeformerAsset->GetModel() : nullptr;
		const UMLDeformerModelInstance* ModelInstance = DeformerComponent != nullptr ? DeformerComponent->GetModelInstance() : nullptr;
		if (Model != nullptr && ModelInstance != nullptr)
		{
			SkeletalMeshObject = ModelInstance->GetSkeletalMeshComponent()->MeshObject;
			NeuralNetwork = Model->GetNeuralNetwork();
			NeuralNetworkInferenceHandle = ModelInstance->GetNeuralNetworkInferenceHandle();
			bCanRunNeuralNet = ModelInstance->IsCompatible() && Model->IsNeuralNetworkOnGPU() && DeformerComponent->GetModelInstance()->IsValidForDataProvider();
			Weight = DeformerComponent->GetWeight();
			VertexMapBufferSRV = Model->GetVertexMapBuffer().ShaderResourceViewRHI;
		}
	}

	bool FMLDeformerGraphDataProviderProxy::IsValid(FValidationData const& InValidationData) const
	{
		if (InValidationData.ParameterStructSize != sizeof(FMLDeformerGraphDataInterfaceParameters))
		{
			return false;
		}
		if (!bCanRunNeuralNet || SkeletalMeshObject == nullptr || NeuralNetwork == nullptr || VertexMapBufferSRV == nullptr)
		{
			return false;
		}

		return true;
	}

	void FMLDeformerGraphDataProviderProxy::AllocateResources(FRDGBuilder& GraphBuilder)
	{
		Buffer = GraphBuilder.RegisterExternalBuffer(NeuralNetwork->GetOutputTensorForContext(NeuralNetworkInferenceHandle).GetPooledBuffer());
		BufferSRV = GraphBuilder.CreateSRV(Buffer, PF_R32_FLOAT);
	}

	void FMLDeformerGraphDataProviderProxy::GatherDispatchData(FDispatchData const& InDispatchData)
	{
		MLDEFORMER_GRAPH_DISPATCH_START(FMLDeformerGraphDataInterfaceParameters, InDispatchData)
		MLDEFORMER_GRAPH_DISPATCH_DEFAULT_PARAMETERS()
		MLDEFORMER_GRAPH_DISPATCH_END()
	}
}	// namespace UE::MLDeformer
