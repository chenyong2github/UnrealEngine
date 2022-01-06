// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerGraphDataInterface.h"

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

FString UMLDeformerDataInterface::GetDisplayName() const
{
	return TEXT("ML Deformer");
}

TArray<FOptimusCDIPinDefinition> UMLDeformerDataInterface::GetPinDefinitions() const
{
	TArray<FOptimusCDIPinDefinition> Defs;
	Defs.Add({ "DebugScale", "ReadDebugScale" });
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
		Fn.Name = TEXT("ReadDebugScale");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.ValueType = FShaderValueType::Get(EShaderFundamentalType::Float);
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

BEGIN_SHADER_PARAMETER_STRUCT(FSceneDataInterfaceParameters, )
	SHADER_PARAMETER(uint32, NumVertices)
	SHADER_PARAMETER(FVector3f, VertexDeltaScale)
	SHADER_PARAMETER(FVector3f, VertexDeltaMean)
	SHADER_PARAMETER(float, VertexDeltaMultiplier)
	SHADER_PARAMETER(float, DebugScale)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float>, PositionDeltaBuffer)
	SHADER_PARAMETER_SRV(Buffer<uint>, VertexMapBuffer)
END_SHADER_PARAMETER_STRUCT()

void UMLDeformerDataInterface::GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& OutBuilder) const
{
	OutBuilder.AddNestedStruct<FSceneDataInterfaceParameters>(UID);
}

void UMLDeformerDataInterface::GetHLSL(FString& OutHLSL) const
{
	OutHLSL += TEXT("#include \"/Plugin/MLDeformer/Private/MLDeformerGraphDataInterface.ush\"\n");
}

void UMLDeformerDataInterface::GetSourceTypes(TArray<UClass*>& OutSourceTypes) const
{
	OutSourceTypes.Add(UMLDeformerComponent::StaticClass());
}

UComputeDataProvider* UMLDeformerDataInterface::CreateDataProvider(TArrayView<TObjectPtr<UObject>> InSourceObjects, uint64 InInputMask, uint64 InOutputMask) const
{
	UMLDeformerDataProvider* Provider = NewObject<UMLDeformerDataProvider>();
	if (InSourceObjects.Num() == 1)
	{
		Provider->DeformerComponent = Cast<UMLDeformerComponent>(InSourceObjects[0]);
	}
	return Provider;
}

bool UMLDeformerDataProvider::IsValid() const
{
	return
		DeformerComponent != nullptr &&
		DeformerComponent->GetDeformerAsset() != nullptr &&
		DeformerComponent->GetDeformerAsset()->GetVertexMapBuffer().ShaderResourceViewRHI != nullptr &&
		DeformerComponent->GetDeformerAsset()->GetInferenceNeuralNetwork() != nullptr &&
		DeformerComponent->GetDeformerAsset()->GetInferenceNeuralNetwork()->IsLoaded();
}

FComputeDataProviderRenderProxy* UMLDeformerDataProvider::GetRenderProxy()
{
	return new FMLDeformerDataProviderProxy(DeformerComponent);
}

FMLDeformerDataProviderProxy::FMLDeformerDataProviderProxy(UMLDeformerComponent* DeformerComponent)
{
	const UMLDeformerAsset* DeformerAsset = DeformerComponent->GetDeformerAsset();
	NeuralNetwork = DeformerAsset->GetInferenceNeuralNetwork();
	VertexMapBufferSRV = DeformerAsset->GetVertexMapBuffer().ShaderResourceViewRHI;
	VertexDeltaScale = DeformerAsset->GetVertexDeltaScale();
	VertexDeltaMean = DeformerAsset->GetVertexDeltaMean();
	bCanRunNeuralNet = NeuralNetwork ? (NeuralNetwork->GetInputTensor().Num() == static_cast<int64>(DeformerAsset->GetInputInfo().CalcNumNeuralNetInputs())) : false;

	VertexDeltaMultiplier = DeformerComponent->GetVertexDeltaMultiplier();
#if WITH_EDITORONLY_DATA
	HeatMapScale = DeformerAsset->GetVizSettings()->GetHeatMapScale();
#endif
}

void FMLDeformerDataProviderProxy::AllocateResources(FRDGBuilder& GraphBuilder)
{
	if (bCanRunNeuralNet)
	{
		check(NeuralNetwork != nullptr);
		Buffer = GraphBuilder.RegisterExternalBuffer(NeuralNetwork->GetOutputTensor().GetPooledBuffer());
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
	FSceneDataInterfaceParameters Parameters;
	FMemory::Memset(&Parameters, 0, sizeof(Parameters));
	Parameters.NumVertices = 0;
	Parameters.VertexDeltaScale = VertexDeltaScale;
	Parameters.VertexDeltaMean = VertexDeltaMean;
	Parameters.VertexDeltaMultiplier = VertexDeltaMultiplier;
	Parameters.DebugScale = HeatMapScale;
	Parameters.PositionDeltaBuffer = BufferSRV;
	Parameters.VertexMapBuffer = VertexMapBufferSRV;

	TArray<uint8> ParamData;
	ParamData.SetNum(sizeof(Parameters));
	FMemory::Memcpy(ParamData.GetData(), &Parameters, sizeof(Parameters));
	OutBindings.Structs.Add(TTuple<FString, TArray<uint8>>(UID, MoveTemp(ParamData)));
}
