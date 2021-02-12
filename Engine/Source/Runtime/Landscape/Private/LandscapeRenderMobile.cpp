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
		TexCoordOffsetParameter.Bind(ParameterMap,TEXT("TexCoordOffset"));
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

		if (TexCoordOffsetParameter.IsBound())
		{
			FVector CameraLocalPos3D = SceneProxy->WorldToLocal.TransformPosition(InView->ViewMatrices.GetViewOrigin());

			FVector2D TexCoordOffset(
				CameraLocalPos3D.X + SceneProxy->SectionBase.X,
				CameraLocalPos3D.Y + SceneProxy->SectionBase.Y
			);
			ShaderBindings.Add(TexCoordOffsetParameter, TexCoordOffset);
		}

		if (SceneProxy->bRegistered)
		{
			ShaderBindings.Add(Shader->GetUniformBufferParameter<FLandscapeSectionLODUniformParameters>(), LandscapeRenderSystems.FindChecked(SceneProxy->LandscapeKey)->UniformBuffer);
		}
		else
		{
			ShaderBindings.Add(Shader->GetUniformBufferParameter<FLandscapeSectionLODUniformParameters>(), GNullLandscapeRenderSystemResources.UniformBuffer);
		}
			}

protected:
	LAYOUT_FIELD(FShaderParameter, TexCoordOffsetParameter);
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
	}
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
		ShaderBindings.Add(Shader->GetUniformBufferParameter<FLandscapeUniformShaderParameters>(), *BatchElementParams->LandscapeUniformShaderParametersResource);
		ShaderBindings.Add(Shader->GetUniformBufferParameter<FLandscapeFixedGridUniformShaderParameters>(), (*BatchElementParams->FixedGridUniformShaderParameters)[BatchElementParams->CurrentLOD]);

		if (TexCoordOffsetParameter.IsBound())
		{
			ShaderBindings.Add(TexCoordOffsetParameter, FVector4(ForceInitToZero));
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

void FLandscapeVertexBufferMobile::UpdateMemoryStat(int32 Delta)
{
	INC_DWORD_STAT_BY(STAT_LandscapeVertexMem, Delta);
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

FLandscapeMobileRenderData::FLandscapeMobileRenderData(const TArray<uint8>& InPlatformData, uint8 InCurFirstLODIdx)
	: CurrentFirstLODIdx(InCurFirstLODIdx)
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
		VertexData.SetNumUninitialized(VertexCount * sizeof(FLandscapeMobileVertex));
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

FLandscapeMobileRenderData::~FLandscapeMobileRenderData()
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

FLandscapeComponentSceneProxyMobile::FLandscapeComponentSceneProxyMobile(ULandscapeComponent* InComponent)
	: FLandscapeComponentSceneProxy(InComponent)
	, MobileRenderData(InComponent->PlatformData.GetRenderData())
{
	check(InComponent);
	
	check(InComponent->MobileMaterialInterfaces.Num() > 0);
	check(InComponent->MobileWeightmapTextures.Num() > 0);

	WeightmapTextures = InComponent->MobileWeightmapTextures;
	NormalmapTexture = InComponent->MobileWeightmapTextures[0];

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
	
	// Use only index buffers from the shared buffers since the vertex buffers are unique per proxy on mobile
	SharedBuffers = FLandscapeComponentSceneProxy::SharedBuffersMap.FindRef(SharedBuffersKey);
	if (SharedBuffers == nullptr)
	{
		int32 NumOcclusionVertices = MobileRenderData->OccluderVerticesSP.IsValid() ? MobileRenderData->OccluderVerticesSP->Num() : 0;
				
		SharedBuffers = new FLandscapeSharedBuffers(
			SharedBuffersKey, SubsectionSizeQuads, NumSubsections,
			GetScene().GetFeatureLevel(), false, NumOcclusionVertices);

		FLandscapeComponentSceneProxy::SharedBuffersMap.Add(SharedBuffersKey, SharedBuffers);
	}
	SharedBuffers->AddRef();
				
	// Init vertex buffer
	{
		check(MobileRenderData->VertexBuffer);
		MobileRenderData->VertexBuffer->InitResource();

		FLandscapeVertexFactoryMobile* LandscapeVertexFactory = new FLandscapeVertexFactoryMobile(FeatureLevel);
		LandscapeVertexFactory->MobileData.PositionComponent = FVertexStreamComponent(MobileRenderData->VertexBuffer, STRUCT_OFFSET(FLandscapeMobileVertex, Position), sizeof(FLandscapeMobileVertex), VET_UByte4N);
		for (uint32 Index = 0; Index < LANDSCAPE_MAX_ES_LOD_COMP; ++Index)
		{
			LandscapeVertexFactory->MobileData.LODHeightsComponent.Add
				(FVertexStreamComponent(MobileRenderData->VertexBuffer, STRUCT_OFFSET(FLandscapeMobileVertex, LODHeights) + sizeof(uint8) * 4 * Index, sizeof(FLandscapeMobileVertex), VET_UByte4N));
		}

		LandscapeVertexFactory->InitResource();
		VertexFactory = LandscapeVertexFactory;
	}

	// Init vertex buffer for rendering to virtual texture
	if (UseVirtualTexturing(FeatureLevel))
	{
		FLandscapeFixedGridVertexFactoryMobile* LandscapeVertexFactory = new FLandscapeFixedGridVertexFactoryMobile(FeatureLevel);
		LandscapeVertexFactory->MobileData.PositionComponent = FVertexStreamComponent(MobileRenderData->VertexBuffer, STRUCT_OFFSET(FLandscapeMobileVertex, Position), sizeof(FLandscapeMobileVertex), VET_UByte4N);
		
		for (uint32 Index = 0; Index < LANDSCAPE_MAX_ES_LOD_COMP; ++Index)
		{
			LandscapeVertexFactory->MobileData.LODHeightsComponent.Add
				(FVertexStreamComponent(MobileRenderData->VertexBuffer, STRUCT_OFFSET(FLandscapeMobileVertex, LODHeights) + sizeof(uint8) * 4 * Index, sizeof(FLandscapeMobileVertex), VET_UByte4N));
		}

		LandscapeVertexFactory->InitResource();
		FixedGridVertexFactory = LandscapeVertexFactory;
	}

	// Assign LandscapeUniformShaderParameters
	LandscapeUniformShaderParameters.InitResource();

	// Create per Lod uniform buffers
	LandscapeFixedGridUniformShaderParameters.AddDefaulted(MaxLOD + 1);
	for (int32 LodIndex = 0; LodIndex <= MaxLOD; ++LodIndex)
	{
		LandscapeFixedGridUniformShaderParameters[LodIndex].InitResource();
		FLandscapeFixedGridUniformShaderParameters Parameters;
		Parameters.LodValues = FVector4(
			LodIndex,
			0.f,
			(float)((SubsectionSizeVerts >> LodIndex) - 1),
			1.f / (float)((SubsectionSizeVerts >> LodIndex) - 1));
		LandscapeFixedGridUniformShaderParameters[LodIndex].SetContents(Parameters);
	}

	MobileRenderData->bReadyForStreaming = true;
}

TSharedPtr<FLandscapeMobileRenderData, ESPMode::ThreadSafe> FLandscapeComponentDerivedData::GetRenderData()
{
	// This function is expected to be called from either the GameThread or via ParallelFor from the GameThread
	check(!IsInActualRenderingThread());

	if (FPlatformProperties::RequiresCookedData() && CachedRenderData.IsValid())
	{
		// on device we can re-use the cached data if we are re-registering our component.
		return CachedRenderData;
	}
	else
	{
		check(CompressedLandscapeData.Num() > 0);
		CachedRenderData.Reset();

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

		TSharedPtr<FLandscapeMobileRenderData, ESPMode::ThreadSafe> RenderData = MakeShareable(new FLandscapeMobileRenderData(MoveTemp(UncompressedData), (uint8)StreamingLODDataArray.Num()));

		// if running on device		
		if (FPlatformProperties::RequiresCookedData())
		{
			// free the compressed data now that we have used it to create the render data.
			CompressedLandscapeData.Empty();
		}

		// store a reference to the render data so we can use it again should the component be reregistered.
		CachedRenderData = RenderData;

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

#if PLATFORM_SUPPORTS_LANDSCAPE_VISUAL_MESH_LOD_STREAMING
uint8 FLandscapeComponentSceneProxyMobile::GetCurrentFirstLODIdx_RenderThread() const
{
	check(MobileRenderData.IsValid());
	return MobileRenderData->CurrentFirstLODIdx;
}
#endif
