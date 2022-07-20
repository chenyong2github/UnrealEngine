// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerGraphDebugDataInterface.h"
#include "MLDeformerGraphDataInterface.h"
#include "MLDeformerModelInstance.h"
#include "MLDeformerAsset.h"
#include "MLDeformerComponent.h"
#include "MLDeformerVizSettings.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "ComputeFramework/ComputeKernelPermutationSet.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "GeometryCache.h"
#include "GeometryCacheMeshData.h"
#include "GeometryCacheTrack.h"
#include "OptimusDataDomain.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderGraphResources.h"
#include "Rendering/SkeletalMeshModel.h"
#include "ShaderParameterMetadataBuilder.h"
#include "SkeletalRenderPublic.h"
#include "NeuralNetwork.h"

TArray<FOptimusCDIPinDefinition> UMLDeformerGraphDebugDataInterface::GetPinDefinitions() const
{
	TArray<FOptimusCDIPinDefinition> Defs;
	Defs.Add({ "HeatMapMode", "ReadHeatMapMode" });
	Defs.Add({ "HeatMapMax", "ReadHeatMapMax" });
	Defs.Add({ "GroundTruthLerp", "ReadGroundTruthLerp" });
	Defs.Add({ "PositionGroundTruth", "ReadPositionGroundTruth", Optimus::DomainName::Vertex, "ReadNumVertices" });
	return Defs;
}

TSubclassOf<UActorComponent> UMLDeformerGraphDebugDataInterface::GetRequiredComponentClass() const
{
	return UMLDeformerComponent::StaticClass();
}

void UMLDeformerGraphDebugDataInterface::GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
{
	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadNumVertices"))
		.AddReturnType(EShaderFundamentalType::Uint);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadHeatMapMode"))
		.AddReturnType(EShaderFundamentalType::Uint);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadHeatMapMax"))
		.AddReturnType(EShaderFundamentalType::Float);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadGroundTruthLerp"))
		.AddReturnType(EShaderFundamentalType::Float);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadPositionGroundTruth"))
		.AddReturnType(EShaderFundamentalType::Float, 3)
		.AddParam(EShaderFundamentalType::Uint);
}

BEGIN_SHADER_PARAMETER_STRUCT(FMLDeformerGraphDebugDataInterfaceParameters, )
	MLDEFORMER_DEBUG_SHADER_PARAMETERS()
END_SHADER_PARAMETER_STRUCT()

MLDEFORMER_GRAPH_IMPLEMENT_DEBUG_BASICS_WITH_PROXY(
	UMLDeformerGraphDebugDataInterface,
	UMLDeformerGraphDebugDataProvider,
	UE::MLDeformer::FMLDeformerGraphDebugDataProviderProxy,
	FMLDeformerGraphDebugDataInterfaceParameters,
	TEXT("#include \"/Plugin/MLDeformerFramework/Private/MLDeformerGraphHeatMapDataInterface.ush\"\n"),
	TEXT("ML Deformer Debug"))

bool UMLDeformerGraphDebugDataProvider::IsValid() const
{
#if WITH_EDITORONLY_DATA
	if (DeformerComponent == nullptr || DeformerComponent->GetDeformerAsset() == nullptr)
	{
		return false;
	}

	return DeformerComponent->GetModelInstance()->IsValidForDataProvider();
#else
	return false; // This data interface is only valid in editor.
#endif
}

#if WITH_EDITORONLY_DATA
namespace UE::MLDeformer
{
	FMLDeformerGraphDebugDataProviderProxy::FMLDeformerGraphDebugDataProviderProxy(UMLDeformerComponent* DeformerComponent, UMLDeformerAsset* DeformerAsset, UMLDeformerGraphDebugDataProvider* InProvider)
		: FComputeDataProviderRenderProxy()
	{
		Provider = InProvider;

		UMLDeformerModel* Model = DeformerAsset->GetModel();	
		UMLDeformerVizSettings* VizSettings = Model->GetVizSettings();
		const UMLDeformerModelInstance* ModelInstance = DeformerComponent->GetModelInstance();

		SkeletalMeshObject = ModelInstance->GetSkeletalMeshComponent()->MeshObject;
		VertexMapBufferSRV = Model->GetVertexMapBuffer().ShaderResourceViewRHI;
		HeatMapMode = (int32)VizSettings->GetHeatMapMode();
		HeatMapMax = 1.0f / FMath::Max(VizSettings->GetHeatMapMax(), 0.00001f);
		GroundTruthLerp = VizSettings->GetGroundTruthLerp();
	}

	void FMLDeformerGraphDebugDataProviderProxy::HandleZeroGroundTruthPositions()
	{
		if (GroundTruthPositions.Num() == 0)
		{	
			// We didn't get valid ground truth vertices.
			// Make non empty array for later buffer generation.
			GroundTruthPositions.Add(FVector3f::ZeroVector);

			// Silently disable relevant debug things.
			if (HeatMapMode == (int32)EMLDeformerHeatMapMode::GroundTruth)
			{
				HeatMapMode = -1;
				HeatMapMax = 0.0f;
				GroundTruthLerp = 0.0f;
			}
		}
	}

	void FMLDeformerGraphDebugDataProviderProxy::AllocateResources(FRDGBuilder& GraphBuilder)
	{
		GroundTruthBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector3f), GroundTruthPositions.Num()), TEXT("MLDeformer.GroundTruthPositions"));
		GroundTruthBufferSRV = GraphBuilder.CreateSRV(GroundTruthBuffer);
		GraphBuilder.QueueBufferUpload(GroundTruthBuffer, GroundTruthPositions.GetData(), sizeof(FVector3f) * GroundTruthPositions.Num(), ERDGInitialDataFlags::None);
	}

	void FMLDeformerGraphDebugDataProviderProxy::GatherDispatchData(FDispatchSetup const& InDispatchSetup, FCollectedDispatchData& InOutDispatchData)
	{
		MLDEFORMER_GRAPH_DISPATCH_START(FMLDeformerGraphDebugDataInterfaceParameters, InDispatchSetup, InOutDispatchData)
		MLDEFORMER_GRAPH_DISPATCH_DEFAULT_DEBUG_PARAMETERS()
		MLDEFORMER_GRAPH_DISPATCH_END()
	}
}	// namespace UE::MLDeformer
#endif // WITH_EDITORONLY_DATA

