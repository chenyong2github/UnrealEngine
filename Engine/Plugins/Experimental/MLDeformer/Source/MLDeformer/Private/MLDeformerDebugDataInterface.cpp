// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerDebugDataInterface.h"

#include "Components/SkeletalMeshComponent.h"
#include "ComputeFramework/ComputeKernelPermutationSet.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "GeometryCache.h"
#include "GeometryCacheMeshData.h"
#include "GeometryCacheTrack.h"
#include "MLDeformerAsset.h"
#include "MLDeformerComponent.h"
#include "OptimusDataDomain.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderGraphResources.h"
#include "Rendering/SkeletalMeshModel.h"
#include "ShaderParameterMetadataBuilder.h"
#include "SkeletalRenderPublic.h"

FString UMLDeformerDebugDataInterface::GetDisplayName() const
{
	return TEXT("ML Deformer Debug");
}

TArray<FOptimusCDIPinDefinition> UMLDeformerDebugDataInterface::GetPinDefinitions() const
{
	TArray<FOptimusCDIPinDefinition> Defs;
	Defs.Add({ "HeatMapScale", "ReadHeatMapScale" });
	Defs.Add({ "PositionGroundTruth", "ReadPositionGroundTruth", Optimus::DomainName::Vertex, "ReadNumVertices" });
	return Defs;
}

void UMLDeformerDebugDataInterface::GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
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
		Fn.Name = TEXT("ReadHeatMapScale");
		Fn.bHasReturnType = true;
		FShaderParamTypeDefinition ReturnParam = {};
		ReturnParam.ValueType = FShaderValueType::Get(EShaderFundamentalType::Float);
		Fn.ParamTypes.Add(ReturnParam);
		OutFunctions.Add(Fn);
	}
	{
		FShaderFunctionDefinition Fn;
		Fn.Name = TEXT("ReadPositionGroundTruth");
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

BEGIN_SHADER_PARAMETER_STRUCT(FMLDeformerDebugDataInterfaceParameters, )
	SHADER_PARAMETER(uint32, NumVertices)
	SHADER_PARAMETER(uint32, InputStreamStart)
	SHADER_PARAMETER(float, HeatMapScale)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float3>, PositionGroundTruthBuffer)
	SHADER_PARAMETER_SRV(Buffer<uint>, VertexMapBuffer)
END_SHADER_PARAMETER_STRUCT()

void UMLDeformerDebugDataInterface::GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& OutBuilder) const
{
	OutBuilder.AddNestedStruct<FMLDeformerDebugDataInterfaceParameters>(UID);
}

void UMLDeformerDebugDataInterface::GetHLSL(FString& OutHLSL) const
{
	OutHLSL += TEXT("#include \"/Plugin/MLDeformer/Private/MLDeformerDebugDataInterface.ush\"\n");
}

void UMLDeformerDebugDataInterface::GetSourceTypes(TArray<UClass*>& OutSourceTypes) const
{
	OutSourceTypes.Add(USkeletalMeshComponent::StaticClass());
	OutSourceTypes.Add(UMLDeformerComponent::StaticClass());
}

UComputeDataProvider* UMLDeformerDebugDataInterface::CreateDataProvider(TArrayView<TObjectPtr<UObject>> InSourceObjects, uint64 InInputMask, uint64 InOutputMask) const
{
	UMLDeformerDebugDataProvider* Provider = NewObject<UMLDeformerDebugDataProvider>();
	if (InSourceObjects.Num() == 2)
	{
		Provider->SkeletalMeshComponent = Cast<USkeletalMeshComponent>(InSourceObjects[0]);
		Provider->DeformerComponent = Cast<UMLDeformerComponent>(InSourceObjects[1]);
	}
#if WITH_EDITORONLY_DATA
	UMLDeformerAsset* DeformerAsset = Provider->DeformerComponent != nullptr ? Provider->DeformerComponent->GetDeformerAsset() : nullptr;
	if (DeformerAsset != nullptr)
	{
		TArray<FString> FailedImportedMeshNames;
		UMLDeformerAsset::GenerateMeshMappings(DeformerAsset, Provider->MeshMappings, FailedImportedMeshNames);
	}
#endif
	return Provider;
}

bool UMLDeformerDebugDataProvider::IsValid() const
{
#if WITH_EDITORONLY_DATA
	return
		SkeletalMeshComponent != nullptr &&
		SkeletalMeshComponent->MeshObject != nullptr &&
		DeformerComponent != nullptr &&
		DeformerComponent->GetDeformerAsset() != nullptr &&
		DeformerComponent->GetDeformerAsset()->GetVertexMapBuffer().ShaderResourceViewRHI != nullptr;
#else
	return false; // This data interface is only valid in editor.
#endif
}

FComputeDataProviderRenderProxy* UMLDeformerDebugDataProvider::GetRenderProxy()
{
#if WITH_EDITORONLY_DATA
	return new FMLDeformerDebugDataProviderProxy(SkeletalMeshComponent, DeformerComponent, MeshMappings);
#else
	return nullptr;
#endif
}

#if WITH_EDITORONLY_DATA

void GetGroundTruthPositions(
	int32 LODIndex,
	float SampleTime,
	UMLDeformerAsset const* DeformerAsset,
	TArray<FMLDeformerMeshMapping> const& MeshMappings,
	TArray<FVector3f>& OutPositions)
{
	if (!ensure(DeformerAsset))
	{
		return;
	}
	USkeletalMesh const* SkelMesh = DeformerAsset->GetSkeletalMesh();
	if (!ensure(SkelMesh))
	{
		return;
	}
	FSkeletalMeshModel const* ImportedModel = SkelMesh->GetImportedModel();
	if (!ensure(ImportedModel))
	{
		return;
	}
	UGeometryCache const* GeomCache = DeformerAsset->GetGeometryCache();
	if (!ensure(GeomCache))
	{
		return;
	}

	const FTransform AlignmentTransform = DeformerAsset->GetAlignmentTransform();

	FSkeletalMeshLODModel const& LODModel = ImportedModel->LODModels[LODIndex];
	TArray<FSkelMeshImportedMeshInfo> const& SkelMeshInfos = LODModel.ImportedMeshInfos;

	const uint32 NumVertices = LODModel.MaxImportVertex + 1;
	OutPositions.Reset(NumVertices);
	OutPositions.AddZeroed(NumVertices);

	// For all mesh mappings we found.
	for (int32 MeshMappingIndex = 0; MeshMappingIndex < MeshMappings.Num(); ++MeshMappingIndex)
	{
		FMLDeformerMeshMapping const& MeshMapping = MeshMappings[MeshMappingIndex];
		FSkelMeshImportedMeshInfo const& MeshInfo = SkelMeshInfos[MeshMapping.MeshIndex];
		UGeometryCacheTrack* Track = GeomCache->Tracks[MeshMapping.TrackIndex];

		FGeometryCacheMeshData GeomCacheMeshData;
		if (!Track->GetMeshDataAtTime(SampleTime, GeomCacheMeshData))
		{
			continue;
		}

		for (int32 VertexIndex = 0; VertexIndex < MeshInfo.NumVertices; ++VertexIndex)
		{
			const int32 SkinnedVertexIndex = MeshInfo.StartImportedVertex + VertexIndex;
			const int32 GeomCacheVertexIndex = MeshMapping.SkelMeshToTrackVertexMap[VertexIndex];
			if (GeomCacheVertexIndex != INDEX_NONE && GeomCacheMeshData.Positions.IsValidIndex(GeomCacheVertexIndex))
			{
				const FVector3f GeomCacheVertexPos = AlignmentTransform.TransformPosition(GeomCacheMeshData.Positions[GeomCacheVertexIndex]);
				OutPositions[SkinnedVertexIndex] = GeomCacheVertexPos;
			}
		}
	}
}

FMLDeformerDebugDataProviderProxy::FMLDeformerDebugDataProviderProxy(USkeletalMeshComponent* SkeletalMeshComponent, UMLDeformerComponent* DeformerComponent, TArray<FMLDeformerMeshMapping> const& MeshMappings)
{
	SkeletalMeshObject = SkeletalMeshComponent->MeshObject;
	
	UMLDeformerAsset const* DeformerAsset = DeformerComponent->GetDeformerAsset();
	VertexMapBufferSRV = DeformerAsset->GetVertexMapBuffer().ShaderResourceViewRHI;
	HeatMapScale = DeformerAsset->GetVizSettings()->GetHeatMapScale();

	const int32 LODIndex = 0;
	const float SampleTime = SkeletalMeshComponent->GetPosition();
	GetGroundTruthPositions(0, SampleTime, DeformerAsset, MeshMappings, GroundTruthPositions);
}

#endif // WITH_EDITORONLY_DATA

void FMLDeformerDebugDataProviderProxy::AllocateResources(FRDGBuilder& GraphBuilder)
{
	GroundTruthBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector3f), GroundTruthPositions.Num()), TEXT("MLDeformer.GroundTruthPositions"));
	GroundTruthBufferSRV = GraphBuilder.CreateSRV(GroundTruthBuffer);

	GraphBuilder.QueueBufferUpload(GroundTruthBuffer, GroundTruthPositions.GetData(), GroundTruthPositions.GetAllocatedSize(), ERDGInitialDataFlags::None);
}

void FMLDeformerDebugDataProviderProxy::GetBindings(int32 InvocationIndex, TCHAR const* UID, FBindings& OutBindings) const
{
	const int32 SectionIdx = InvocationIndex;
	FSkeletalMeshRenderData const& SkeletalMeshRenderData = SkeletalMeshObject->GetSkeletalMeshRenderData();
	FSkeletalMeshLODRenderData const* LodRenderData = SkeletalMeshRenderData.GetPendingFirstLOD(0);
	FSkelMeshRenderSection const& RenderSection = LodRenderData->RenderSections[SectionIdx];

	FMLDeformerDebugDataInterfaceParameters Parameters;
	FMemory::Memset(&Parameters, 0, sizeof(Parameters));
	Parameters.NumVertices = 0;
	Parameters.InputStreamStart = RenderSection.BaseVertexIndex;
	Parameters.HeatMapScale = HeatMapScale;
	Parameters.PositionGroundTruthBuffer = GroundTruthBufferSRV;
	Parameters.VertexMapBuffer = VertexMapBufferSRV;

	TArray<uint8> ParamData;
	ParamData.SetNum(sizeof(Parameters));
	FMemory::Memcpy(ParamData.GetData(), &Parameters, sizeof(Parameters));
	OutBindings.Structs.Add(TTuple<FString, TArray<uint8>>(UID, MoveTemp(ParamData)));
}
