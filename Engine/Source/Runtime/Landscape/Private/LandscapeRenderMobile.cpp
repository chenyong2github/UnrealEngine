// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
LandscapeRenderMobile.cpp: Landscape Rendering without using vertex texture fetch
=============================================================================*/

#include "LandscapeRenderMobile.h"
#include "ShaderParameterUtils.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"
#include "PrimitiveSceneInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "HAL/LowLevelMemTracker.h"
#include "MeshMaterialShader.h"

// Debug CVar for disabling the loading of landscape hole meshes
static TAutoConsoleVariable<int32> CVarMobileLandscapeHoleMesh(
	TEXT("r.Mobile.LandscapeHoleMesh"),
	1,
	TEXT("Set to 0 to skip loading of landscape hole meshes on mobile."),
	ECVF_Default);

bool FLandscapeVertexFactoryMobile::ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
{
	auto FeatureLevel = GetMaxSupportedFeatureLevel(Parameters.Platform);
	return (FeatureLevel == ERHIFeatureLevel::ES3_1) &&
		(Parameters.MaterialParameters.bIsUsedWithLandscape || Parameters.MaterialParameters.bIsSpecialEngineMaterial);
}

void FLandscapeVertexFactoryMobile::InitRHI()
{
	// list of declaration items
	FVertexDeclarationElementList Elements;

	// position decls
	Elements.Add(AccessStreamComponent(MobileData.PositionComponent,0));

	if (MobileData.LODHeightsComponent.Num())
	{
		const int32 BaseAttribute = 1;
		for(int32 Index = 0;Index < MobileData.LODHeightsComponent.Num();Index++)
		{
			Elements.Add(AccessStreamComponent(MobileData.LODHeightsComponent[Index], BaseAttribute + Index));
		}
	}

	// create the actual device decls
	InitDeclaration(Elements);
}

/** Shader parameters for use with FLandscapeVertexFactory */
class FLandscapeVertexFactoryMobileVertexShaderParameters : public FVertexFactoryShaderParameters
{
	DECLARE_INLINE_TYPE_LAYOUT(FLandscapeVertexFactoryMobileVertexShaderParameters, NonVirtual);
public:
	/**
	* Bind shader constants by name
	* @param	ParameterMap - mapping of named shader constants to indices
	*/
	void Bind(const FShaderParameterMap& ParameterMap)
	{
		LodValuesParameter.Bind(ParameterMap,TEXT("LodValues"));
		ForcedLodParameter.Bind(ParameterMap,TEXT("ForcedLod"));
		LodTessellationParameter.Bind(ParameterMap, TEXT("LodTessellationParams"));
		NeighborSectionLodParameter.Bind(ParameterMap,TEXT("NeighborSectionLod"));
		LodBiasParameter.Bind(ParameterMap,TEXT("LodBias"));
		SectionLodsParameter.Bind(ParameterMap,TEXT("SectionLods"));
	}

	void GetElementShaderBindings(
		const class FSceneInterface* Scene,
		const FSceneView* InView,
		const class FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory,
		const FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams
	) const
	{
		SCOPE_CYCLE_COUNTER(STAT_LandscapeVFDrawTimeVS);

		const FLandscapeBatchElementParams* BatchElementParams = (const FLandscapeBatchElementParams*)BatchElement.UserData;
		check(BatchElementParams);

		const FLandscapeComponentSceneProxyMobile* SceneProxy = (const FLandscapeComponentSceneProxyMobile*)BatchElementParams->SceneProxy;
		ShaderBindings.Add(Shader->GetUniformBufferParameter<FLandscapeUniformShaderParameters>(),*BatchElementParams->LandscapeUniformShaderParametersResource);

		if (LodValuesParameter.IsBound())
		{
			FVector4 LodValues(
				0.0f, // this is the mesh's LOD, ES2 always uses the LOD0 mesh
				0.0f, // unused
				(float)SceneProxy->SubsectionSizeQuads,
				1.f / (float)SceneProxy->SubsectionSizeQuads);

			ShaderBindings.Add(LodValuesParameter, LodValues);
		}

		if (LodBiasParameter.IsBound())
		{
			FVector CameraLocalPos3D = SceneProxy->WorldToLocal.TransformPosition(InView->ViewMatrices.GetViewOrigin());

			FVector4 LodBias(
				0.0f, // unused
				0.0f, // unused
				CameraLocalPos3D.X + SceneProxy->SectionBase.X,
				CameraLocalPos3D.Y + SceneProxy->SectionBase.Y
			);
			ShaderBindings.Add(LodBiasParameter, LodBias);
		}

		if (SceneProxy->bRegistered)
		{
			ShaderBindings.Add(Shader->GetUniformBufferParameter<FLandscapeSectionLODUniformParameters>(), LandscapeRenderSystems.FindChecked(SceneProxy->LandscapeKey)->UniformBuffer);
		}
		else
		{
			ShaderBindings.Add(Shader->GetUniformBufferParameter<FLandscapeSectionLODUniformParameters>(), GNullLandscapeRenderSystemResources.UniformBuffer);
		}

		if (ForcedLodParameter.IsBound())
		{
			ShaderBindings.Add(ForcedLodParameter, BatchElementParams->ForcedLOD);
		}

#if 0
		FLandscapeComponentSceneProxy::FViewCustomDataLOD* LODData = (FLandscapeComponentSceneProxy::FViewCustomDataLOD*)InView->GetCustomData(SceneProxy->GetPrimitiveSceneInfo()->GetIndex());
		int32 SubSectionIndex = BatchElementParams->SubX + BatchElementParams->SubY * SceneProxy->NumSubsections;

		if (LODData != nullptr)
		{
			SceneProxy->PostInitViewCustomData(*InView, LODData);

			if (LodTessellationParameter.IsBound())
			{
				ShaderBindings.Add(LodTessellationParameter, LODData->LodTessellationParams);
			}

			if (SectionLodsParameter.IsBound())
			{
				if (LODData->UseCombinedMeshBatch)
				{
					ShaderBindings.Add(SectionLodsParameter, LODData->ShaderCurrentLOD);
				}
				else // in non combined, only the one representing us as we'll be called 4 times (once per sub section)
				{
					check(SubSectionIndex >= 0);
					FVector4 ShaderCurrentLOD(ForceInitToZero);
					ShaderCurrentLOD.Component(SubSectionIndex) = LODData->ShaderCurrentLOD.Component(SubSectionIndex);

					ShaderBindings.Add(SectionLodsParameter, ShaderCurrentLOD);
				}
			}

			if (NeighborSectionLodParameter.IsBound())
			{
				FVector4 ShaderCurrentNeighborLOD[FLandscapeComponentSceneProxy::NEIGHBOR_COUNT] = { FVector4(ForceInitToZero), FVector4(ForceInitToZero), FVector4(ForceInitToZero), FVector4(ForceInitToZero) };

				if (LODData->UseCombinedMeshBatch)
				{
					int32 SubSectionCount = SceneProxy->NumSubsections == 1 ? 1 : FLandscapeComponentSceneProxy::MAX_SUBSECTION_COUNT;

					for (int32 NeighborSubSectionIndex = 0; NeighborSubSectionIndex < SubSectionCount; ++NeighborSubSectionIndex)
					{
						ShaderCurrentNeighborLOD[NeighborSubSectionIndex] = LODData->SubSections[NeighborSubSectionIndex].ShaderCurrentNeighborLOD;
						check(ShaderCurrentNeighborLOD[NeighborSubSectionIndex].X != -1.0f); // they should all match so only check the 1st one for simplicity
					}

					ShaderBindings.Add(NeighborSectionLodParameter, ShaderCurrentNeighborLOD);
				}
				else // in non combined, only the one representing us as we'll be called 4 times (once per sub section)
				{
					check(SubSectionIndex >= 0);
					ShaderCurrentNeighborLOD[SubSectionIndex] = LODData->SubSections[SubSectionIndex].ShaderCurrentNeighborLOD;
					check(ShaderCurrentNeighborLOD[SubSectionIndex].X != -1.0f); // they should all match so only check the 1st one for simplicity

					ShaderBindings.Add(NeighborSectionLodParameter, ShaderCurrentNeighborLOD);
				}
			}
		}
#endif
	}
protected:
	LAYOUT_FIELD(FShaderParameter, LodValuesParameter);
	LAYOUT_FIELD(FShaderParameter, ForcedLodParameter);
	LAYOUT_FIELD(FShaderParameter, LodTessellationParameter);
	LAYOUT_FIELD(FShaderParameter, NeighborSectionLodParameter);
	LAYOUT_FIELD(FShaderParameter, LodBiasParameter);
	LAYOUT_FIELD(FShaderParameter, SectionLodsParameter);
	LAYOUT_FIELD(TShaderUniformBufferParameter<FLandscapeUniformShaderParameters>, LandscapeShaderParameters);
};

/** Shader parameters for use with FLandscapeVertexFactory */
class FLandscapeVertexFactoryMobilePixelShaderParameters : public FLandscapeVertexFactoryPixelShaderParameters
{
	DECLARE_INLINE_TYPE_LAYOUT(FLandscapeVertexFactoryMobilePixelShaderParameters, NonVirtual);
public:
	/**
	* Bind shader constants by name
	* @param	ParameterMap - mapping of named shader constants to indices
	*/
	void Bind(const FShaderParameterMap& ParameterMap)
	{
		FLandscapeVertexFactoryPixelShaderParameters::Bind(ParameterMap);
		BlendableLayerMaskParameter.Bind(ParameterMap, TEXT("BlendableLayerMask"));
	}

	void GetElementShaderBindings(
		const class FSceneInterface* Scene,
		const FSceneView* InView,
		const class FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory,
		const FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams
	) const
	{
		SCOPE_CYCLE_COUNTER(STAT_LandscapeVFDrawTimePS);
		
		FLandscapeVertexFactoryPixelShaderParameters::GetElementShaderBindings(Scene, InView, Shader, InputStreamType, FeatureLevel, VertexFactory, BatchElement, ShaderBindings, VertexStreams);

		if (BlendableLayerMaskParameter.IsBound())
		{
			const FLandscapeBatchElementParams* BatchElementParams = (const FLandscapeBatchElementParams*)BatchElement.UserData;
			check(BatchElementParams);
			const FLandscapeComponentSceneProxyMobile* SceneProxy = (const FLandscapeComponentSceneProxyMobile*)BatchElementParams->SceneProxy;
			
			FVector MaskVector;
			MaskVector[0] = (SceneProxy->BlendableLayerMask & (1 << 0)) ? 1 : 0;
			MaskVector[1] = (SceneProxy->BlendableLayerMask & (1 << 1)) ? 1 : 0;
			MaskVector[2] = (SceneProxy->BlendableLayerMask & (1 << 2)) ? 1 : 0;
			ShaderBindings.Add(BlendableLayerMaskParameter, MaskVector);
		}
	}

protected:
	LAYOUT_FIELD(FShaderParameter, BlendableLayerMaskParameter);
};

/**
  * Shader parameters for use with FLandscapeFixedGridVertexFactory
  * Simple grid rendering (without dynamic lod blend) needs a simpler fixed setup.
  */
class FLandscapeFixedGridVertexFactoryMobileVertexShaderParameters : public FLandscapeVertexFactoryMobileVertexShaderParameters
{
public:
	void GetElementShaderBindings(
		const class FSceneInterface* Scene,
		const FSceneView* InView,
		const class FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory,
		const FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams
	) const
	{
		SCOPE_CYCLE_COUNTER(STAT_LandscapeVFDrawTimeVS);

		const FLandscapeBatchElementParams* BatchElementParams = (const FLandscapeBatchElementParams*)BatchElement.UserData;
		check(BatchElementParams);
		const FLandscapeComponentSceneProxyMobile* SceneProxy = (const FLandscapeComponentSceneProxyMobile*)BatchElementParams->SceneProxy;
		ShaderBindings.Add(Shader->GetUniformBufferParameter<FLandscapeUniformShaderParameters>(), *BatchElementParams->LandscapeUniformShaderParametersResource);

		if (LodValuesParameter.IsBound())
		{
			ShaderBindings.Add(LodValuesParameter, SceneProxy->GetShaderLODValues(BatchElementParams->CurrentLOD));
		}

		if (LodBiasParameter.IsBound())
		{
			ShaderBindings.Add(LodBiasParameter, FVector4(ForceInitToZero));
		}

		if (ForcedLodParameter.IsBound())
		{
			ShaderBindings.Add(ForcedLodParameter, BatchElementParams->ForcedLOD);
		}
	}
};

IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FLandscapeVertexFactoryMobile, SF_Vertex, FLandscapeVertexFactoryMobileVertexShaderParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FLandscapeVertexFactoryMobile, SF_Pixel, FLandscapeVertexFactoryMobilePixelShaderParameters);

IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FLandscapeFixedGridVertexFactoryMobile, SF_Vertex, FLandscapeFixedGridVertexFactoryMobileVertexShaderParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FLandscapeFixedGridVertexFactoryMobile, SF_Pixel, FLandscapeVertexFactoryMobilePixelShaderParameters);

IMPLEMENT_VERTEX_FACTORY_TYPE(FLandscapeVertexFactoryMobile, "/Engine/Private/LandscapeVertexFactory.ush", true, true, true, false, false);
IMPLEMENT_VERTEX_FACTORY_TYPE_EX(FLandscapeFixedGridVertexFactoryMobile, "/Engine/Private/LandscapeVertexFactory.ush", true, true, true, false, false, true, false);

void FLandscapeFixedGridVertexFactoryMobile::ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FLandscapeVertexFactoryMobile::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	OutEnvironment.SetDefine(TEXT("FIXED_GRID"), TEXT("1"));
}
	
bool FLandscapeFixedGridVertexFactoryMobile::ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
{
	return GetMaxSupportedFeatureLevel(Parameters.Platform) == ERHIFeatureLevel::ES3_1 &&
		(Parameters.MaterialParameters.bIsUsedWithLandscape || Parameters.MaterialParameters.bIsSpecialEngineMaterial);
}

/**
* Initialize the RHI for this rendering resource
*/
void FLandscapeVertexBufferMobile::InitRHI()
{
	// create a static vertex buffer
	FRHIResourceCreateInfo CreateInfo;
	void* VertexDataPtr = nullptr;
	VertexBufferRHI = RHICreateAndLockVertexBuffer(VertexData.Num(), BUF_Static, CreateInfo, VertexDataPtr);

	// Copy stored platform data and free CPU copy
	FMemory::Memcpy(VertexDataPtr, VertexData.GetData(), VertexData.Num());
	VertexData.Empty();

	RHIUnlockVertexBuffer(VertexBufferRHI);
}

struct FLandscapeMobileHoleData
{
	FRawStaticIndexBuffer16or32Interface* IndexBuffer = nullptr;
	int32 NumHoleLods;
	int32 IndexBufferSize;
	int32 MinHoleIndex;
	int32 MaxHoleIndex;

	~FLandscapeMobileHoleData()
	{
		if (IndexBuffer != nullptr)
		{
			DEC_DWORD_STAT_BY(STAT_LandscapeHoleMem, IndexBuffer->GetResourceDataSize());
			IndexBuffer->ReleaseResource();
			delete IndexBuffer;
		}
	}
};

template <typename INDEX_TYPE>
void SerializeLandscapeMobileHoleData(FMemoryArchive& Ar, FLandscapeMobileHoleData& HoleData)
{
	Ar << HoleData.MinHoleIndex;
	Ar << HoleData.MaxHoleIndex;

	TArray<INDEX_TYPE> IndexData;
	Ar << HoleData.IndexBufferSize;
	IndexData.SetNumUninitialized(HoleData.IndexBufferSize);
	Ar.Serialize(IndexData.GetData(), HoleData.IndexBufferSize * sizeof(INDEX_TYPE));

	const bool bLoadHoleMeshData = HoleData.IndexBufferSize > 0 && CVarMobileLandscapeHoleMesh.GetValueOnGameThread();
	if (bLoadHoleMeshData)
	{
		FRawStaticIndexBuffer16or32<INDEX_TYPE>* IndexBuffer = new FRawStaticIndexBuffer16or32<INDEX_TYPE>(false);
		IndexBuffer->AssignNewBuffer(IndexData);
		HoleData.IndexBuffer = IndexBuffer;
		BeginInitResource(HoleData.IndexBuffer);
		INC_DWORD_STAT_BY(STAT_LandscapeHoleMem, HoleData.IndexBuffer->GetResourceDataSize());
	}
}

/**
 * Container for FLandscapeVertexBufferMobile that we can reference from a thread-safe shared pointer
 * while ensuring the vertex buffer is always destroyed on the render thread.
 **/
struct FLandscapeMobileRenderData
{
	FLandscapeVertexBufferMobile* VertexBuffer = nullptr;
	FLandscapeMobileHoleData* HoleData = nullptr;
	FOccluderVertexArraySP OccluderVerticesSP;

	FLandscapeMobileRenderData(const TArray<uint8>& InPlatformData)
	{
		FMemoryReader MemAr(InPlatformData);

		int32 NumHoleLods;
		MemAr << NumHoleLods;
		if (NumHoleLods > 0)
		{
			HoleData = new FLandscapeMobileHoleData;
			HoleData->NumHoleLods = NumHoleLods;

			bool b16BitIndices;
			MemAr << b16BitIndices;
			if (b16BitIndices)
			{
				SerializeLandscapeMobileHoleData<uint16>(MemAr, *HoleData);
			}
			else
			{
				SerializeLandscapeMobileHoleData<uint32>(MemAr, *HoleData);
			}
		}

		{
			int32 VertexCount;
			MemAr << VertexCount;
			TArray<uint8> VertexData;
			VertexData.SetNumUninitialized(VertexCount*sizeof(FLandscapeMobileVertex));
			MemAr.Serialize(VertexData.GetData(), VertexData.Num());
			VertexBuffer = new FLandscapeVertexBufferMobile(MoveTemp(VertexData));
		}
		
		{
			int32 NumOccluderVertices;
			MemAr << NumOccluderVertices;
			if (NumOccluderVertices > 0)
			{
				OccluderVerticesSP = MakeShared<FOccluderVertexArray, ESPMode::ThreadSafe>();
				OccluderVerticesSP->SetNumUninitialized(NumOccluderVertices);
				MemAr.Serialize(OccluderVerticesSP->GetData(), NumOccluderVertices * sizeof(FVector));

				INC_DWORD_STAT_BY(STAT_LandscapeOccluderMem, OccluderVerticesSP->GetAllocatedSize());
			}
		}
	}

	~FLandscapeMobileRenderData()
	{
		// Make sure the vertex buffer is always destroyed from the render thread 
		if (VertexBuffer != nullptr)
		{
			if (IsInRenderingThread())
			{
				delete VertexBuffer;
				delete HoleData;
			}
			else
			{
				FLandscapeVertexBufferMobile* InVertexBuffer = VertexBuffer;
				FLandscapeMobileHoleData* InHoleData = HoleData;
				ENQUEUE_RENDER_COMMAND(InitCommand)(
					[InVertexBuffer, InHoleData](FRHICommandListImmediate& RHICmdList)
				{
					delete InVertexBuffer;
					delete InHoleData;
				});
			}
		}

		if (OccluderVerticesSP.IsValid())
		{
			DEC_DWORD_STAT_BY(STAT_LandscapeOccluderMem, OccluderVerticesSP->GetAllocatedSize());
		}
	}
};

FLandscapeComponentSceneProxyMobile::FLandscapeComponentSceneProxyMobile(ULandscapeComponent* InComponent)
	: FLandscapeComponentSceneProxy(InComponent)
	, MobileRenderData(InComponent->PlatformData.GetRenderData())
{
	check(InComponent);
	
	check(InComponent->MobileMaterialInterfaces.Num() > 0);
	check(InComponent->MobileWeightmapTextures.Num() > 0);

	WeightmapTextures = InComponent->MobileWeightmapTextures;
	NormalmapTexture = InComponent->MobileWeightmapTextures[0];

	BlendableLayerMask = InComponent->MobileBlendableLayerMask;

#if WITH_EDITOR
	TArray<FWeightmapLayerAllocationInfo>& LayerAllocations = InComponent->MobileWeightmapLayerAllocations.Num() ? InComponent->MobileWeightmapLayerAllocations : InComponent->GetWeightmapLayerAllocations();
	LayerColors.Empty();
	for (auto& Allocation : LayerAllocations)
	{
		if (Allocation.LayerInfo != nullptr)
		{
			LayerColors.Add(Allocation.LayerInfo->LayerUsageDebugColor);
		}
	}
#endif
}

int32 FLandscapeComponentSceneProxyMobile::CollectOccluderElements(FOccluderElementsCollector& Collector) const
{
	if (MobileRenderData->OccluderVerticesSP.IsValid() && SharedBuffers->OccluderIndicesSP.IsValid())
	{
		Collector.AddElements(MobileRenderData->OccluderVerticesSP, SharedBuffers->OccluderIndicesSP, GetLocalToWorld());
		return 1;
	}

	return 0;
}

FLandscapeComponentSceneProxyMobile::~FLandscapeComponentSceneProxyMobile()
{
	if (VertexFactory)
	{
		delete VertexFactory;
		VertexFactory = NULL;
	}
}

SIZE_T FLandscapeComponentSceneProxyMobile::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

void FLandscapeComponentSceneProxyMobile::CreateRenderThreadResources()
{
	LLM_SCOPE(ELLMTag::Landscape);

	if (IsComponentLevelVisible())
	{
		RegisterNeighbors(this);
	}
	
	auto FeatureLevel = GetScene().GetFeatureLevel();
	// Use only Index buffers
	SharedBuffers = FLandscapeComponentSceneProxy::SharedBuffersMap.FindRef(SharedBuffersKey);
	if (SharedBuffers == nullptr)
	{
		int32 NumOcclusionVertices = MobileRenderData->OccluderVerticesSP.IsValid() ? MobileRenderData->OccluderVerticesSP->Num() : 0;
				
		SharedBuffers = new FLandscapeSharedBuffers(
			SharedBuffersKey, SubsectionSizeQuads, NumSubsections,
			GetScene().GetFeatureLevel(), false, NumOcclusionVertices);

		FLandscapeComponentSceneProxy::SharedBuffersMap.Add(SharedBuffersKey, SharedBuffers);
				
		if (UseVirtualTexturing(FeatureLevel))
		{
			//todo[vt]: We will need a version of this to support XYOffsetmapTexture
			FLandscapeFixedGridVertexFactoryMobile* LandscapeVertexFactory = new FLandscapeFixedGridVertexFactoryMobile(FeatureLevel);
			LandscapeVertexFactory->MobileData.PositionComponent = FVertexStreamComponent(MobileRenderData->VertexBuffer, STRUCT_OFFSET(FLandscapeMobileVertex,Position), sizeof(FLandscapeMobileVertex), VET_UByte4N);
			for( uint32 Index = 0; Index < LANDSCAPE_MAX_ES_LOD_COMP; ++Index )
			{
				LandscapeVertexFactory->MobileData.LODHeightsComponent.Add
				(FVertexStreamComponent(MobileRenderData->VertexBuffer, STRUCT_OFFSET(FLandscapeMobileVertex,LODHeights) + sizeof(uint8) * 4 * Index, sizeof(FLandscapeMobileVertex), VET_UByte4N));
			}
			LandscapeVertexFactory->InitResource();
			SharedBuffers->FixedGridVertexFactory = LandscapeVertexFactory;
		}
	}

	//
	FixedGridVertexFactory = SharedBuffers->FixedGridVertexFactory;
	
	SharedBuffers->AddRef();

	// Init vertex buffer
	check(MobileRenderData->VertexBuffer);
	MobileRenderData->VertexBuffer->InitResource();

	FLandscapeVertexFactoryMobile* LandscapeVertexFactory = new FLandscapeVertexFactoryMobile(FeatureLevel);
	LandscapeVertexFactory->MobileData.PositionComponent = FVertexStreamComponent(MobileRenderData->VertexBuffer, STRUCT_OFFSET(FLandscapeMobileVertex,Position), sizeof(FLandscapeMobileVertex), VET_UByte4N);
	for( uint32 Index = 0; Index < LANDSCAPE_MAX_ES_LOD_COMP; ++Index )
	{
		LandscapeVertexFactory->MobileData.LODHeightsComponent.Add
			(FVertexStreamComponent(MobileRenderData->VertexBuffer, STRUCT_OFFSET(FLandscapeMobileVertex,LODHeights) + sizeof(uint8) * 4 * Index, sizeof(FLandscapeMobileVertex), VET_UByte4N));
	}

	LandscapeVertexFactory->InitResource();
	VertexFactory = LandscapeVertexFactory;

	// Assign LandscapeUniformShaderParameters
	LandscapeUniformShaderParameters.InitResource();
}

TSharedPtr<FLandscapeMobileRenderData, ESPMode::ThreadSafe> FLandscapeComponentDerivedData::GetRenderData()
{
	check(IsInGameThread());

	if (FPlatformProperties::RequiresCookedData() && CachedRenderData.IsValid())
	{
		// on device we can re-use the cached data if we are re-registering our component.
		return CachedRenderData;
	}
	else
	{
		check(CompressedLandscapeData.Num() > 0);

		FMemoryReader Ar(CompressedLandscapeData);

		// Note: change LANDSCAPE_FULL_DERIVEDDATA_VER when modifying the serialization layout
		int32 UncompressedSize;
		Ar << UncompressedSize;

		int32 CompressedSize;
		Ar << CompressedSize;

		TArray<uint8> CompressedData;
		CompressedData.Empty(CompressedSize);
		CompressedData.AddUninitialized(CompressedSize);
		Ar.Serialize(CompressedData.GetData(), CompressedSize);

		TArray<uint8> UncompressedData;
		UncompressedData.Empty(UncompressedSize);
		UncompressedData.AddUninitialized(UncompressedSize);

		verify(FCompression::UncompressMemory(NAME_Zlib, UncompressedData.GetData(), UncompressedSize, CompressedData.GetData(), CompressedSize));

		TSharedPtr<FLandscapeMobileRenderData, ESPMode::ThreadSafe> RenderData = MakeShareable(new FLandscapeMobileRenderData(MoveTemp(UncompressedData)));

		// if running on device		
		if (FPlatformProperties::RequiresCookedData())
		{
			// free the compressed data now that we have used it to create the render data.
			CompressedLandscapeData.Empty();
			// store a reference to the render data so we can use it again should the component be reregistered.
			CachedRenderData = RenderData;
		}

		return RenderData;
	}
}

void FLandscapeComponentSceneProxyMobile::ApplyMeshElementModifier(FMeshBatchElement& InOutMeshElement, int32 InLodIndex) const
{
	const bool bHoleDataExists = MobileRenderData->HoleData != nullptr && MobileRenderData->HoleData->IndexBuffer != nullptr && InLodIndex < MobileRenderData->HoleData->NumHoleLods;
	if (bHoleDataExists)
	{
		FLandscapeMobileHoleData const& HoleData = *MobileRenderData->HoleData;
		InOutMeshElement.IndexBuffer = HoleData.IndexBuffer;
		InOutMeshElement.NumPrimitives = HoleData.IndexBufferSize / 3;
		InOutMeshElement.FirstIndex = 0;
		InOutMeshElement.MinVertexIndex = HoleData.MinHoleIndex;
		InOutMeshElement.MaxVertexIndex = HoleData.MaxHoleIndex;
	}
}
