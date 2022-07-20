// Copyright Epic Games, Inc. All Rights Reserved.

#include "LegacyVertexDeltaModelDataInterface.h"
#include "LegacyVertexDeltaModel.h"
#include "MLDeformerModelInstance.h"
#include "MLDeformerAsset.h"
#include "MLDeformerComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "ComputeFramework/ComputeKernelPermutationSet.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "OptimusDataDomain.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderGraphResources.h"
#include "ShaderParameterMetadataBuilder.h"
#include "SkeletalRenderPublic.h"

BEGIN_SHADER_PARAMETER_STRUCT(FLegacyVertexDeltaModelDataInterfaceParameters,)
	MLDEFORMER_SHADER_PARAMETERS()
	SHADER_PARAMETER(FVector3f, VertexDeltaScale)
	SHADER_PARAMETER(FVector3f, VertexDeltaMean)
END_SHADER_PARAMETER_STRUCT()

MLDEFORMER_GRAPH_IMPLEMENT_BASICS(
	ULegacyVertexDeltaModelDataInterface,
	ULegacyVertexDeltaModelDataProvider,
	UE::LegacyVertexDeltaModel::FLegacyVertexDeltaModelDataProviderProxy,
	FLegacyVertexDeltaModelDataInterfaceParameters,
	TEXT("#include \"/Plugin/LegacyVertexDeltaModel/Private/LegacyVertexDeltaModelDataInterface.ush\"\n"),
	TEXT("Legacy Vertex Delta Model"))

namespace UE::LegacyVertexDeltaModel
{
	FLegacyVertexDeltaModelDataProviderProxy::FLegacyVertexDeltaModelDataProviderProxy(UMLDeformerComponent* DeformerComponent)
		: UE::MLDeformer::FMLDeformerGraphDataProviderProxy(DeformerComponent)
	{
		const UMLDeformerModel* Model = DeformerComponent->GetDeformerAsset()->GetModel();
		const ULegacyVertexDeltaModel* VertexDeltaModel = Cast<ULegacyVertexDeltaModel>(Model);
		if (ensure(VertexDeltaModel))
		{
			VertexDeltaScale = static_cast<FVector3f>(VertexDeltaModel->GetVertexDeltaScale());
			VertexDeltaMean = static_cast<FVector3f>(VertexDeltaModel->GetVertexDeltaMean());
		}
	}

	void FLegacyVertexDeltaModelDataProviderProxy::GatherDispatchData(FDispatchSetup const& InDispatchSetup, FCollectedDispatchData& InOutDispatchData)
	{
		MLDEFORMER_GRAPH_DISPATCH_START(FLegacyVertexDeltaModelDataInterfaceParameters, InDispatchSetup, InOutDispatchData)
		MLDEFORMER_GRAPH_DISPATCH_DEFAULT_PARAMETERS()
		Parameters->VertexDeltaScale = VertexDeltaScale;
		Parameters->VertexDeltaMean = VertexDeltaMean;
		MLDEFORMER_GRAPH_DISPATCH_END()
	}
}	// namespace UE::LegacyVertexDeltaModel
