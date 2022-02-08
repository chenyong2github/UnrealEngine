// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerDataInterface.h"

#include "Components/SkeletalMeshComponent.h"
#include "ComputeFramework/ComputeKernelPermutationSet.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "MLDeformerAsset.h"
#include "MLDeformerComponent.h"
#include "NeuralNetwork.h"
#include "OptimusDataDomain.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderGraphResources.h"
#include "ShaderParameterMetadataBuilder.h"
#include "SkeletalRenderPublic.h"

FString UMLDeformerDataInterface::GetDisplayName() const
{
	return TEXT("ML Deformer");
}

TArray<FOptimusCDIPinDefinition> UMLDeformerDataInterface::GetPinDefinitions() const
{
	TArray<FOptimusCDIPinDefinition> Defs;
	Defs.Add({ "PositionDelta", "ReadPositionDelta", Optimus::DomainName::Vertex, "ReadNumVertices" });
	return Defs;
}

void UMLDeformerDataInterface::GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
{
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadNumVertices");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(ReturnParam);
		OutFunctions.Add(Fn);
	}
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadPositionDelta");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.ValueType = FShaderValueType::Get(EShaderFundamentalType::Float, 3);
		Fn.ParamTypes.Add(ReturnParam);
		FShaderParamTypeDefinition Param0 = {};
		Param0.ValueType = FShaderValueType::Get(EShaderFundamentalType::Uint);
		Fn.ParamTypes.Add(Param0);
		OutFunctions.Add(Fn);
	}
}

BEGIN_SHADER_PARAMETER_STRUCT(FMLDeformerDataInterfaceParameters, )
	SHADER_PARAMETER(uint32, NumVertices)
	SHADER_PARAMETER(uint32, InputStreamStart)
	SHADER_PARAMETER(FVector3f, VertexDeltaScale)
	SHADER_PARAMETER(FVector3f, VertexDeltaMean)
	SHADER_PARAMETER(float, VertexDeltaMultiplier)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float>, PositionDeltaBuffer)
	SHADER_PARAMETER_SRV(Buffer<uint>, VertexMapBuffer)
END_SHADER_PARAMETER_STRUCT()

void UMLDeformerDataInterface::GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& OutBuilder) const
{
	OutBuilder.AddNestedStruct<FMLDeformerDataInterfaceParameters>(UID);
}

void UMLDeformerDataInterface::GetHLSL(FString& OutHLSL) const
{
	OutHLSL += TEXT("#include \"/Plugin/MLDeformer/Private/MLDeformerDataInterface.ush\"\n");
}

void UMLDeformerDataInterface::GetSourceTypes(TArray<UClass*>& OutSourceTypes) const
{
	OutSourceTypes.Add(USkeletalMeshComponent::StaticClass());
}

UComputeDataProvider* UMLDeformerDataInterface::CreateDataProvider(TArrayView<TObjectPtr<UObject>> InSourceObjects, uint64 InInputMask, uint64 InOutputMask) const
{
	UMLDeformerDataProvider* Provider = NewObject<UMLDeformerDataProvider>();
	if (InSourceObjects.Num() == 1)
	{
		Provider->SkeletalMeshComponent = Cast<USkeletalMeshComponent>(InSourceObjects[0]);
	}
	return Provider;
}

bool UMLDeformerDataProvider::IsValid() const
{
	if (SkeletalMeshComponent == nullptr || SkeletalMeshComponent->MeshObject == nullptr)
	{
		return false;
	}
	
	UMLDeformerComponent* DeformerComponent = SkeletalMeshComponent->GetOwner()->FindComponentByClass<UMLDeformerComponent>();

	return
		DeformerComponent != nullptr &&
		DeformerComponent->GetDeformerAsset() != nullptr &&
		DeformerComponent->GetDeformerAsset()->GetVertexMapBuffer().ShaderResourceViewRHI != nullptr &&
		DeformerComponent->GetDeformerAsset()->GetInferenceNeuralNetwork() != nullptr &&
		DeformerComponent->GetDeformerAsset()->GetInferenceNeuralNetwork()->IsLoaded() &&
		DeformerComponent->GetDeformerInstance().GetNeuralNetworkInferenceHandle() != -1;
}

FComputeDataProviderRenderProxy* UMLDeformerDataProvider::GetRenderProxy()
{
	UMLDeformerComponent* DeformerComponent = SkeletalMeshComponent->GetOwner()->FindComponentByClass<UMLDeformerComponent>();

	return new FMLDeformerDataProviderProxy(SkeletalMeshComponent, DeformerComponent);
}

FMLDeformerDataProviderProxy::FMLDeformerDataProviderProxy(USkeletalMeshComponent* SkeletalMeshComponent, UMLDeformerComponent* DeformerComponent)
{
	SkeletalMeshObject = SkeletalMeshComponent->MeshObject;

	const UMLDeformerAsset* DeformerAsset = DeformerComponent->GetDeformerAsset();
	NeuralNetwork = DeformerAsset->GetInferenceNeuralNetwork();
	NeuralNetworkInferenceHandle = DeformerComponent->GetDeformerInstance().GetNeuralNetworkInferenceHandle();
	bCanRunNeuralNet = NeuralNetwork ? (NeuralNetwork->GetInputTensorForContext(NeuralNetworkInferenceHandle).Num() == static_cast<int64>(DeformerAsset->GetInputInfo().CalcNumNeuralNetInputs())) : false;

	VertexMapBufferSRV = DeformerAsset->GetVertexMapBuffer().ShaderResourceViewRHI;
	VertexDeltaScale = (FVector)DeformerAsset->GetVertexDeltaScale();
	VertexDeltaMean = (FVector)DeformerAsset->GetVertexDeltaMean();

	VertexDeltaMultiplier = DeformerComponent->GetVertexDeltaMultiplier();
}

void FMLDeformerDataProviderProxy::AllocateResources(FRDGBuilder& GraphBuilder)
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

void FMLDeformerDataProviderProxy::GetBindings(int32 InvocationIndex, TCHAR const* UID, FBindings& OutBindings) const
{
	const int32 SectionIdx = InvocationIndex;
	FSkeletalMeshRenderData const& SkeletalMeshRenderData = SkeletalMeshObject->GetSkeletalMeshRenderData();
	FSkeletalMeshLODRenderData const* LodRenderData = SkeletalMeshRenderData.GetPendingFirstLOD(0);
	FSkelMeshRenderSection const& RenderSection = LodRenderData->RenderSections[SectionIdx];

	FMLDeformerDataInterfaceParameters Parameters;
	FMemory::Memset(&Parameters, 0, sizeof(Parameters));
	Parameters.NumVertices = 0;
	Parameters.InputStreamStart = RenderSection.BaseVertexIndex;
	Parameters.VertexDeltaScale = (FVector3f)VertexDeltaScale;
	Parameters.VertexDeltaMean = (FVector3f)VertexDeltaMean;
	Parameters.VertexDeltaMultiplier = VertexDeltaMultiplier;
	Parameters.PositionDeltaBuffer = BufferSRV;
	Parameters.VertexMapBuffer = VertexMapBufferSRV;

	TArray<uint8> ParamData;
	ParamData.SetNum(sizeof(Parameters));
	FMemory::Memcpy(ParamData.GetData(), &Parameters, sizeof(Parameters));
	OutBindings.Structs.Add(TTuple<FString, TArray<uint8>>(UID, MoveTemp(ParamData)));
}
