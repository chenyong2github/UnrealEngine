// Copyright Epic Games, Inc. All Rights Reserved.

#include "VertexDeltaModelDataInterface.h"

#include "Components/SkeletalMeshComponent.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "ComputeFramework/ShaderParameterMetadataBuilder.h"
#include "MLDeformerAsset.h"
#include "MLDeformerComponent.h"
#include "MLDeformerModelInstance.h"
#include "OptimusDataDomain.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderGraphResources.h"
#include "SkeletalRenderPublic.h"
#include "VertexDeltaModel.h"

BEGIN_SHADER_PARAMETER_STRUCT(FVertexDeltaModelDataInterfaceParameters,)
	MLDEFORMER_SHADER_PARAMETERS()
	SHADER_PARAMETER(FVector3f, VertexDeltaScale)
	SHADER_PARAMETER(FVector3f, VertexDeltaMean)
END_SHADER_PARAMETER_STRUCT()

MLDEFORMER_GRAPH_IMPLEMENT_BASICS(
	UVertexDeltaModelDataInterface,
	UVertexDeltaModelDataProvider,
	UE::VertexDeltaModel::FVertexDeltaModelDataProviderProxy,
	FVertexDeltaModelDataInterfaceParameters,
	TEXT("#include \"/Plugin/VertexDeltaModel/Private/VertexDeltaModelDataInterface.ush\"\n"),
	TEXT("Vertex Delta Model"))

namespace UE::VertexDeltaModel
{
	FVertexDeltaModelDataProviderProxy::FVertexDeltaModelDataProviderProxy(USkeletalMeshComponent* SkeletalMeshComponent, UMLDeformerComponent* DeformerComponent)
		: UE::MLDeformer::FMLDeformerGraphDataProviderProxy(SkeletalMeshComponent, DeformerComponent)
	{
		const UMLDeformerModel* Model = DeformerComponent->GetDeformerAsset()->GetModel();
		const UVertexDeltaModel* VertexDeltaModel = Cast<UVertexDeltaModel>(Model);
		check(VertexDeltaModel);

		VertexDeltaScale = static_cast<FVector3f>(VertexDeltaModel->GetVertexDeltaScale());
		VertexDeltaMean = static_cast<FVector3f>(VertexDeltaModel->GetVertexDeltaMean());
	}

	void FVertexDeltaModelDataProviderProxy::GatherDispatchData(FDispatchSetup const& InDispatchSetup, FCollectedDispatchData& InOutDispatchData)
	{
		MLDEFORMER_GRAPH_DISPATCH_START(FVertexDeltaModelDataInterfaceParameters, InDispatchSetup, InOutDispatchData)
		MLDEFORMER_GRAPH_DISPATCH_DEFAULT_PARAMETERS()
		Parameters->VertexDeltaScale = VertexDeltaScale;
		Parameters->VertexDeltaMean = VertexDeltaMean;
		MLDEFORMER_GRAPH_DISPATCH_END()
	}
}	// namespace UE::VertexDeltaModel
