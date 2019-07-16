// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraRendererSprites.h"
#include "ParticleResources.h"
#include "NiagaraSpriteVertexFactory.h"
#include "NiagaraDataSet.h"
#include "NiagaraStats.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraSortingGPU.h"
#include "NiagaraCutoutVertexBuffer.h"
#include "RayTracingDefinitions.h"
#include "RayTracingDynamicGeometryCollection.h"
#include "RayTracingInstance.h"

DECLARE_CYCLE_STAT(TEXT("Generate Sprite Dynamic Data [GT]"), STAT_NiagaraGenSpriteDynamicData, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Render Sprites [RT]"), STAT_NiagaraRenderSprites, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Render Sprites - CPU Sim Copy[RT]"), STAT_NiagaraRenderSpritesCPUSimCopy, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Render Sprites - CPU Sim Memcopy[RT]"), STAT_NiagaraRenderSpritesCPUSimMemCopy, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Render Sprites - Cutout[RT]"), STAT_NiagaraRenderSpritesCutout, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Render Sprites - Sorting[RT]"), STAT_NiagaraRenderSpritesSorting, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Render Sprites - GlobalSortCPU[RT]"), STAT_NiagaraRenderSpritesGlobalSortCPU, STATGROUP_Niagara);

DECLARE_CYCLE_STAT(TEXT("Genereate GPU Buffers"), STAT_NiagaraGenSpriteGpuBuffers, STATGROUP_Niagara);
DECLARE_DWORD_COUNTER_STAT(TEXT("NumSprites"), STAT_NiagaraNumSprites, STATGROUP_Niagara);

static int32 GbEnableNiagaraSpriteRendering = 1;
static FAutoConsoleVariableRef CVarEnableNiagaraSpriteRendering(
	TEXT("fx.EnableNiagaraSpriteRendering"),
	GbEnableNiagaraSpriteRendering,
	TEXT("If == 0, Niagara Sprite Renderers are disabled. \n"),
	ECVF_Default
);

/** Dynamic data for sprite renderers. */
struct FNiagaraDynamicDataSprites : public FNiagaraDynamicDataBase
{
	FNiagaraDynamicDataSprites(const FNiagaraEmitterInstance* InEmitter)
		: FNiagaraDynamicDataBase(InEmitter)
		, Material(nullptr)
	{
	}
	
	FMaterialRenderProxy* Material;
};

/* Mesh collector classes */
class FNiagaraMeshCollectorResourcesSprite : public FOneFrameResource
{
public:
	FNiagaraSpriteVertexFactory VertexFactory;
	FNiagaraSpriteUniformBufferRef UniformBuffer;

	virtual ~FNiagaraMeshCollectorResourcesSprite()
	{
		VertexFactory.ReleaseResource();
	}
};


//////////////////////////////////////////////////////////////////////////

FNiagaraRendererSprites::FNiagaraRendererSprites(ERHIFeatureLevel::Type FeatureLevel, const UNiagaraRendererProperties *InProps, const FNiagaraEmitterInstance* Emitter)
	: FNiagaraRenderer(FeatureLevel, InProps, Emitter)
	, Alignment(ENiagaraSpriteAlignment::Unaligned)
	, FacingMode(ENiagaraSpriteFacingMode::FaceCamera)
	, CustomFacingVectorMask(ForceInitToZero)
	, PivotInUVSpace(0.5f, 0.5f)
	, SortMode(ENiagaraSortMode::ViewDistance)
	, SubImageSize(1.0f, 1.0f)
	, bSubImageBlend(false)
	, bRemoveHMDRollInVR(false)
	, bSortOnlyWhenTranslucent(true)
	, MinFacingCameraBlendDistance(0.0f)
	, MaxFacingCameraBlendDistance(0.0f)
	, PositionOffset(INDEX_NONE)
	, ColorOffset(INDEX_NONE)
	, VelocityOffset(INDEX_NONE)
	, RotationOffset(INDEX_NONE)
	, SizeOffset(INDEX_NONE)
	, FacingOffset(INDEX_NONE)
	, AlignmentOffset(INDEX_NONE)
	, SubImageOffset(INDEX_NONE)
	, MaterialParamValidMask(0)
	, MaterialParamOffset(INDEX_NONE)
	, MaterialParamOffset1(INDEX_NONE)
	, MaterialParamOffset2(INDEX_NONE)
	, MaterialParamOffset3(INDEX_NONE)
	, CameraOffsetOffset(INDEX_NONE)
	, UVScaleOffset(INDEX_NONE)
	, MaterialRandomOffset(INDEX_NONE)
	, CustomSortingOffset(INDEX_NONE)
	, NormalizedAgeOffset(INDEX_NONE)
{
	check(InProps && Emitter);

	VertexFactory = new FNiagaraSpriteVertexFactory(NVFT_Sprite, FeatureLevel);

	const UNiagaraSpriteRendererProperties* Properties = CastChecked<const UNiagaraSpriteRendererProperties>(InProps);
	
	Alignment = Properties->Alignment;
	FacingMode = Properties->FacingMode;
	CustomFacingVectorMask = Properties->CustomFacingVectorMask;
	PivotInUVSpace = Properties->PivotInUVSpace;
	SortMode = Properties->SortMode;
	SubImageSize = Properties->SubImageSize;
	bSubImageBlend = Properties->bSubImageBlend;
	bRemoveHMDRollInVR = Properties->bRemoveHMDRollInVR;
	bSortOnlyWhenTranslucent = Properties->bSortOnlyWhenTranslucent;
	MinFacingCameraBlendDistance = Properties->MinFacingCameraBlendDistance;
	MaxFacingCameraBlendDistance = Properties->MaxFacingCameraBlendDistance;

	NumCutoutVertexPerSubImage = Properties->GetNumCutoutVertexPerSubimage();
	CutoutVertexBuffer.Data = Properties->GetCutoutData();

	int32 IntDummy;
	const FNiagaraDataSet& Data = Emitter->GetData();
	Data.GetVariableComponentOffsets(Properties->PositionBinding.DataSetVariable, PositionOffset, IntDummy);
	Data.GetVariableComponentOffsets(Properties->VelocityBinding.DataSetVariable, VelocityOffset, IntDummy);
	Data.GetVariableComponentOffsets(Properties->SpriteRotationBinding.DataSetVariable, RotationOffset, IntDummy);
	Data.GetVariableComponentOffsets(Properties->SpriteSizeBinding.DataSetVariable, SizeOffset, IntDummy);
	Data.GetVariableComponentOffsets(Properties->ColorBinding.DataSetVariable, ColorOffset, IntDummy);
	Data.GetVariableComponentOffsets(Properties->SpriteFacingBinding.DataSetVariable, FacingOffset, IntDummy);
	Data.GetVariableComponentOffsets(Properties->SpriteAlignmentBinding.DataSetVariable, AlignmentOffset, IntDummy);
	Data.GetVariableComponentOffsets(Properties->SubImageIndexBinding.DataSetVariable, SubImageOffset, IntDummy);
	Data.GetVariableComponentOffsets(Properties->DynamicMaterialBinding.DataSetVariable, MaterialParamOffset, IntDummy);
	Data.GetVariableComponentOffsets(Properties->DynamicMaterial1Binding.DataSetVariable, MaterialParamOffset1, IntDummy);
	Data.GetVariableComponentOffsets(Properties->DynamicMaterial2Binding.DataSetVariable, MaterialParamOffset2, IntDummy);
	Data.GetVariableComponentOffsets(Properties->DynamicMaterial3Binding.DataSetVariable, MaterialParamOffset3, IntDummy);
	Data.GetVariableComponentOffsets(Properties->CameraOffsetBinding.DataSetVariable, CameraOffsetOffset, IntDummy);
	Data.GetVariableComponentOffsets(Properties->UVScaleBinding.DataSetVariable, UVScaleOffset, IntDummy);
	Data.GetVariableComponentOffsets(Properties->NormalizedAgeBinding.DataSetVariable, NormalizedAgeOffset, IntDummy);
	Data.GetVariableComponentOffsets(Properties->MaterialRandomBinding.DataSetVariable, MaterialRandomOffset, IntDummy);
	Data.GetVariableComponentOffsets(Properties->CustomSortingBinding.DataSetVariable, CustomSortingOffset, IntDummy);

	MaterialParamValidMask = MaterialParamOffset != -1 ? 1 : 0;
	MaterialParamValidMask |= MaterialParamOffset1 != -1 ? 2 : 0;
	MaterialParamValidMask |= MaterialParamOffset2 != -1 ? 4 : 0;
	MaterialParamValidMask |= MaterialParamOffset3 != -1 ? 8 : 0;
}

FNiagaraRendererSprites::~FNiagaraRendererSprites()
{
	if ( VertexFactory != nullptr )
	{
		delete VertexFactory;
		VertexFactory = nullptr;
	}
}

void FNiagaraRendererSprites::ReleaseRenderThreadResources(NiagaraEmitterInstanceBatcher* Batcher)
{
	FNiagaraRenderer::ReleaseRenderThreadResources(Batcher);
	VertexFactory->ReleaseResource();
	WorldSpacePrimitiveUniformBuffer.ReleaseResource();

	CutoutVertexBuffer.ReleaseResource();
#if RHI_RAYTRACING
	if (IsRayTracingEnabled())
	{
		RayTracingGeometry.ReleaseResource();
		RayTracingDynamicVertexBuffer.Release();
	}
#endif
}

void FNiagaraRendererSprites::CreateRenderThreadResources(NiagaraEmitterInstanceBatcher* Batcher)
{
	FNiagaraRenderer::CreateRenderThreadResources(Batcher);
	CutoutVertexBuffer.InitResource();
	VertexFactory->InitResource();

#if RHI_RAYTRACING
	if (IsRayTracingEnabled())
	{
		RayTracingDynamicVertexBuffer.Initialize(4, 256, PF_R32_FLOAT, BUF_UnorderedAccess | BUF_ShaderResource, TEXT("RayTracingDynamicVertexBuffer"));

		FRayTracingGeometryInitializer Initializer;
		Initializer.PositionVertexBuffer = nullptr;
		Initializer.IndexBuffer = nullptr;
		Initializer.BaseVertexIndex = 0;
		Initializer.VertexBufferStride = 12;
		Initializer.VertexBufferByteOffset = 0;
		Initializer.TotalPrimitiveCount = 0;
		Initializer.VertexBufferElementType = VET_Float3;
		Initializer.GeometryType = RTGT_Triangles;
		Initializer.bFastBuild = true;
		Initializer.bAllowUpdate = false;
		RayTracingGeometry.SetInitializer(Initializer);
		RayTracingGeometry.InitResource();
	}
#endif
}

void FNiagaraRendererSprites::ConditionalInitPrimitiveUniformBuffer(const FNiagaraSceneProxy *SceneProxy) const
{
	// Update the primitive uniform buffer if needed.
	if (!WorldSpacePrimitiveUniformBuffer.IsInitialized())
	{
		FPrimitiveUniformShaderParameters PrimitiveUniformShaderParameters = GetPrimitiveUniformShaderParameters(
			FMatrix::Identity,
			FMatrix::Identity,
			SceneProxy->GetActorPosition(),
			SceneProxy->GetBounds(),
			SceneProxy->GetLocalBounds(),
			SceneProxy->ReceivesDecals(),
			false,
			false,
			SceneProxy->UseSingleSampleShadowFromStationaryLights(),
			SceneProxy->GetScene().HasPrecomputedVolumetricLightmap_RenderThread(),
			SceneProxy->DrawsVelocity(),
			SceneProxy->GetLightingChannelMask(),
			0,
			INDEX_NONE,
			INDEX_NONE,
			SceneProxy->AlwaysHasVelocity()
		);
		WorldSpacePrimitiveUniformBuffer.SetContents(PrimitiveUniformShaderParameters);
		WorldSpacePrimitiveUniformBuffer.InitResource();
	}
}

FNiagaraRendererSprites::FCPUSimParticleDataAllocation FNiagaraRendererSprites::ConditionalAllocateCPUSimParticleData(FNiagaraDynamicDataSprites* DynamicDataSprites, FGlobalDynamicReadBuffer& DynamicReadBuffer) const
{
	FNiagaraDataBuffer* SourceParticleData = DynamicDataSprites->GetParticleDataToRender();
	check(SourceParticleData);//Can be null but should be checked before here.
	int32 TotalFloatSize = SourceParticleData->GetFloatBuffer().Num() / sizeof(float);

	FCPUSimParticleDataAllocation CPUSimParticleDataAllocation { DynamicReadBuffer };

	if (SimTarget == ENiagaraSimTarget::CPUSim)
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderSpritesCPUSimCopy);
		CPUSimParticleDataAllocation.ParticleData = DynamicReadBuffer.AllocateFloat(TotalFloatSize);
		{
			SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderSpritesCPUSimMemCopy);
			FMemory::Memcpy(CPUSimParticleDataAllocation.ParticleData.Buffer, SourceParticleData->GetFloatBuffer().GetData(), SourceParticleData->GetFloatBuffer().Num());
		}
	}

	return CPUSimParticleDataAllocation;
}

FNiagaraSpriteUniformBufferRef FNiagaraRendererSprites::CreatePerViewUniformBuffer(const FSceneView* View, const FSceneViewFamily& ViewFamily, const FNiagaraSceneProxy *SceneProxy) const
{
	FNiagaraSpriteUniformParameters PerViewUniformParameters;
	PerViewUniformParameters.LocalToWorld = bLocalSpace ? SceneProxy->GetLocalToWorld() : FMatrix::Identity;//For now just handle local space like this but maybe in future have a VF variant to avoid the transform entirely?
	PerViewUniformParameters.LocalToWorldInverseTransposed = bLocalSpace ? SceneProxy->GetLocalToWorld().Inverse().GetTransposed() : FMatrix::Identity;
	PerViewUniformParameters.RotationBias = 0.0f;
	PerViewUniformParameters.RotationScale = 1.0f;
	PerViewUniformParameters.TangentSelector = FVector4(0.0f, 0.0f, 0.0f, 1.0f);
	PerViewUniformParameters.DeltaSeconds = ViewFamily.DeltaWorldTime;
	PerViewUniformParameters.NormalsType = 0.0f;
	PerViewUniformParameters.NormalsSphereCenter = FVector4(0.0f, 0.0f, 0.0f, 1.0f);
	PerViewUniformParameters.NormalsCylinderUnitDirection = FVector4(0.0f, 0.0f, 1.0f, 0.0f);
	PerViewUniformParameters.PivotOffset = PivotInUVSpace * -1.0f; // We do this because we want to slide the coordinates back since 0,0 is the upper left corner.
	PerViewUniformParameters.MacroUVParameters = FVector4(0.0f, 0.0f, 1.0f, 1.0f);
	PerViewUniformParameters.CameraFacingBlend = FVector4(0.0f, 0.0f, 0.0f, 1.0f);
	PerViewUniformParameters.RemoveHMDRoll = bRemoveHMDRollInVR;
	PerViewUniformParameters.CustomFacingVectorMask = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
	PerViewUniformParameters.SubImageSize = FVector4(SubImageSize.X, SubImageSize.Y, 1.0f / SubImageSize.X, 1.0f / SubImageSize.Y);
	PerViewUniformParameters.PositionDataOffset = PositionOffset;
	PerViewUniformParameters.VelocityDataOffset = VelocityOffset;
	PerViewUniformParameters.RotationDataOffset = RotationOffset;
	PerViewUniformParameters.SizeDataOffset = SizeOffset;
	PerViewUniformParameters.ColorDataOffset = ColorOffset;
	PerViewUniformParameters.MaterialParamValidMask = MaterialParamValidMask;
	PerViewUniformParameters.MaterialParamDataOffset = MaterialParamOffset;
	PerViewUniformParameters.MaterialParam1DataOffset = MaterialParamOffset1;
	PerViewUniformParameters.MaterialParam2DataOffset = MaterialParamOffset2;
	PerViewUniformParameters.MaterialParam3DataOffset = MaterialParamOffset3;
	PerViewUniformParameters.SubimageDataOffset = SubImageOffset;
	PerViewUniformParameters.FacingDataOffset = FacingOffset;
	PerViewUniformParameters.AlignmentDataOffset = AlignmentOffset;
	PerViewUniformParameters.SubImageBlendMode = bSubImageBlend;
	PerViewUniformParameters.CameraOffsetDataOffset = CameraOffsetOffset;
	PerViewUniformParameters.UVScaleDataOffset = UVScaleOffset;
	PerViewUniformParameters.NormalizedAgeDataOffset = NormalizedAgeOffset;
	PerViewUniformParameters.MaterialRandomDataOffset = MaterialRandomOffset;
	PerViewUniformParameters.DefaultPos = bLocalSpace ? FVector4(0.0f, 0.0f, 0.0f, 1.0f) : FVector4(SceneProxy->GetLocalToWorld().GetOrigin());

	ENiagaraSpriteFacingMode ActualFacingMode = FacingMode;
	ENiagaraSpriteAlignment ActualAlignmentMode = Alignment;

	if (FacingOffset == -1 && FacingMode == ENiagaraSpriteFacingMode::CustomFacingVector)
	{
		ActualFacingMode = ENiagaraSpriteFacingMode::FaceCamera;
	}

	if (AlignmentOffset == -1 && ActualAlignmentMode == ENiagaraSpriteAlignment::CustomAlignment)
	{
		ActualAlignmentMode = ENiagaraSpriteAlignment::Unaligned;
	}

	if (ActualFacingMode == ENiagaraSpriteFacingMode::FaceCameraDistanceBlend)
	{
		float DistanceBlendMinSq = MinFacingCameraBlendDistance * MinFacingCameraBlendDistance;
		float DistanceBlendMaxSq = MaxFacingCameraBlendDistance * MaxFacingCameraBlendDistance;
		float InvBlendRange = 1.0f / FMath::Max(DistanceBlendMaxSq - DistanceBlendMinSq, 1.0f);
		float BlendScaledMinDistance = DistanceBlendMinSq * InvBlendRange;

		PerViewUniformParameters.CameraFacingBlend.X = 1.0f;
		PerViewUniformParameters.CameraFacingBlend.Y = InvBlendRange;
		PerViewUniformParameters.CameraFacingBlend.Z = BlendScaledMinDistance;
	}

	if (ActualAlignmentMode == ENiagaraSpriteAlignment::VelocityAligned)
	{
		// velocity aligned
		PerViewUniformParameters.RotationScale = 0.0f;
		PerViewUniformParameters.TangentSelector = FVector4(0.0f, 1.0f, 0.0f, 0.0f);
	}

	if (ActualFacingMode == ENiagaraSpriteFacingMode::CustomFacingVector)
	{
		PerViewUniformParameters.CustomFacingVectorMask = CustomFacingVectorMask;
	}

	return FNiagaraSpriteUniformBufferRef::CreateUniformBufferImmediate(PerViewUniformParameters, UniformBuffer_SingleFrame);
}

void FNiagaraRendererSprites::SetVertexFactoryParticleData(
	FNiagaraSpriteVertexFactory& OutVertexFactory, 
	FNiagaraDynamicDataSprites* DynamicDataSprites, 
	FCPUSimParticleDataAllocation& CPUSimParticleDataAllocation,
	const FSceneView* View,
	const FNiagaraSceneProxy *SceneProxy) const
{
	NiagaraEmitterInstanceBatcher* Batcher = SceneProxy->GetBatcher();
	check(Batcher);

	// Cutout geometry.
	const bool bUseSubImage = SubImageSize.X != 1 || SubImageSize.Y != 1;
	const bool bUseCutout = CutoutVertexBuffer.VertexBufferRHI.IsValid();
	if (bUseCutout)
	{	// Is Accessing Properties safe here? Or should values be cached in the constructor?
		SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderSpritesCutout);
		if (bUseSubImage)
		{
			OutVertexFactory.SetCutoutParameters(NumCutoutVertexPerSubImage, CutoutVertexBuffer.VertexBufferSRV);
		}
		else // Otherwise simply replace the input stream with the single cutout geometry
		{
			OutVertexFactory.SetVertexBufferOverride(&CutoutVertexBuffer);
		}
	}

	FNiagaraDataBuffer* SourceParticleData = DynamicDataSprites->GetParticleDataToRender();
	check(SourceParticleData);//Can be null but should be checked before here.

	//Sort particles if needed.
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderSpritesSorting)


		FMaterialRenderProxy* MaterialRenderProxy = DynamicDataSprites->Material;
		check(MaterialRenderProxy);
		EBlendMode BlendMode = MaterialRenderProxy->GetMaterial(VertexFactory->GetFeatureLevel())->GetBlendMode();
		OutVertexFactory.SetSortedIndices(nullptr, 0xFFFFFFFF);

		int32 NumInstances = SourceParticleData->GetNumInstances();
		FNiagaraGPUSortInfo SortInfo;
		if (View && SortMode != ENiagaraSortMode::None && (BlendMode == BLEND_AlphaComposite || BlendMode == BLEND_AlphaHoldout|| BlendMode == BLEND_Translucent || !bSortOnlyWhenTranslucent))
		{
			SortInfo.ParticleCount = NumInstances;
			SortInfo.SortMode = SortMode;
			SortInfo.SortAttributeOffset = (SortInfo.SortMode == ENiagaraSortMode::CustomAscending || SortInfo.SortMode == ENiagaraSortMode::CustomDecending) ? CustomSortingOffset : PositionOffset;
			SortInfo.ViewOrigin = View->ViewMatrices.GetViewOrigin();
			SortInfo.ViewDirection = View->GetViewDirection();
			if (bLocalSpace)
			{
				FMatrix InvTransform = SceneProxy->GetLocalToWorld().InverseFast();
				SortInfo.ViewOrigin = InvTransform.TransformPosition(SortInfo.ViewOrigin);
				SortInfo.ViewDirection = InvTransform.TransformVector(SortInfo.ViewDirection);
			}
		};

		if (SimTarget == ENiagaraSimTarget::CPUSim)//TODO: Compute shader for sorting gpu sims and larger cpu sims.
		{
			check(CPUSimParticleDataAllocation.ParticleData.IsValid());
			if (SortInfo.SortMode != ENiagaraSortMode::None && SortInfo.SortAttributeOffset != INDEX_NONE)
			{
				if (GNiagaraGPUSorting &&
					GNiagaraGPUSortingCPUToGPUThreshold != INDEX_NONE &&
					SortInfo.ParticleCount >= GNiagaraGPUSortingCPUToGPUThreshold)
				{
					SortInfo.ParticleCount = NumInstances;
					SortInfo.ParticleDataFloatSRV = CPUSimParticleDataAllocation.ParticleData.ReadBuffer->SRV;
					SortInfo.FloatDataOffset = CPUSimParticleDataAllocation.ParticleData.FirstIndex / sizeof(float);
					SortInfo.FloatDataStride = SourceParticleData->GetFloatStride() / sizeof(float);
					const int32 IndexBufferOffset = Batcher->AddSortedGPUSimulation(SortInfo);
					if (IndexBufferOffset != INDEX_NONE)
					{
						OutVertexFactory.SetSortedIndices(Batcher->GetGPUSortedBuffer().VertexBufferSRV, IndexBufferOffset);
					}
				}
				else
				{
					SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderSpritesGlobalSortCPU);

					FGlobalDynamicReadBuffer::FAllocation SortedIndices;
					SortedIndices = CPUSimParticleDataAllocation.DynamicReadBuffer.AllocateInt32(NumInstances);
					SortIndices(SortInfo.SortMode, SortInfo.SortAttributeOffset, *SourceParticleData, SceneProxy->GetLocalToWorld(), View, SortedIndices);
					OutVertexFactory.SetSortedIndices(SortedIndices.ReadBuffer->SRV, SortedIndices.FirstIndex / sizeof(float));
				}
			}
			OutVertexFactory.SetParticleData(CPUSimParticleDataAllocation.ParticleData.ReadBuffer->SRV, CPUSimParticleDataAllocation.ParticleData.FirstIndex / sizeof(float), SourceParticleData->GetFloatStride() / sizeof(float));
		}
		else // ENiagaraSimTarget::GPUSim
		{
			if (SortInfo.SortMode != ENiagaraSortMode::None && SortInfo.SortAttributeOffset != INDEX_NONE && GNiagaraGPUSorting)
			{
				// Here we need to be conservative about the InstanceCount, since the final value is only known on the GPU after the simulation.
				SortInfo.ParticleCount = SourceParticleData->GetNumInstances();

				SortInfo.ParticleDataFloatSRV = SourceParticleData->GetGPUBufferFloat().SRV;
				SortInfo.FloatDataOffset = 0;
				SortInfo.FloatDataStride = SourceParticleData->GetFloatStride() / sizeof(float);
				SortInfo.GPUParticleCountSRV = Batcher->GetGPUInstanceCounterManager().GetInstanceCountBuffer().SRV;
				SortInfo.GPUParticleCountOffset = SourceParticleData->GetGPUInstanceCountBufferOffset();
				const int32 IndexBufferOffset = Batcher->AddSortedGPUSimulation(SortInfo);
				if (IndexBufferOffset != INDEX_NONE && SortInfo.GPUParticleCountOffset != INDEX_NONE)
				{
					OutVertexFactory.SetSortedIndices(Batcher->GetGPUSortedBuffer().VertexBufferSRV, IndexBufferOffset);
				}
			}

			if ( SourceParticleData->GetGPUBufferFloat().SRV.IsValid() )
			{
				OutVertexFactory.SetParticleData(SourceParticleData->GetGPUBufferFloat().SRV, 0, SourceParticleData->GetFloatStride() / sizeof(float));
			}
			else
			{
				OutVertexFactory.SetParticleData(FNiagaraRenderer::GetDummyFloatBuffer().SRV, 0, 0);
			}
		}
	}
}

void FNiagaraRendererSprites::CreateMeshBatchForView(
	const FSceneView* View, 
	const FSceneViewFamily& ViewFamily, 
	const FNiagaraSceneProxy *SceneProxy,
	FNiagaraDynamicDataSprites *DynamicDataSprites,
	uint32 IndirectArgsOffset,
	FMeshBatch& MeshBatch,
	FNiagaraMeshCollectorResourcesSprite& CollectorResources) const
{
	FNiagaraDataBuffer* SourceParticleData = DynamicDataSprites->GetParticleDataToRender();
	check(SourceParticleData);//Can be null but should be checked before here.
	int32 NumInstances = SourceParticleData->GetNumInstances();
	const bool bIsWireframe = ViewFamily.EngineShowFlags.Wireframe;

	FMaterialRenderProxy* MaterialRenderProxy = DynamicDataSprites->Material;
	check(MaterialRenderProxy);

	ENiagaraSpriteFacingMode ActualFacingMode = FacingMode;
	ENiagaraSpriteAlignment ActualAlignmentMode = Alignment;

	if (FacingOffset == -1 && FacingMode == ENiagaraSpriteFacingMode::CustomFacingVector)
	{
		ActualFacingMode = ENiagaraSpriteFacingMode::FaceCamera;
	}

	if (AlignmentOffset == -1 && ActualAlignmentMode == ENiagaraSpriteAlignment::CustomAlignment)
	{
		ActualAlignmentMode = ENiagaraSpriteAlignment::Unaligned;
	}

	CollectorResources.VertexFactory.SetAlignmentMode((uint32)ActualAlignmentMode);
	CollectorResources.VertexFactory.SetFacingMode((uint32)FacingMode);
	CollectorResources.VertexFactory.SetParticleFactoryType(NVFT_Sprite);
	CollectorResources.VertexFactory.InitResource();
	CollectorResources.VertexFactory.SetSpriteUniformBuffer(CollectorResources.UniformBuffer);

	FNiagaraSpriteVFLooseParameters VFLooseParams;
	VFLooseParams.NumCutoutVerticesPerFrame = CollectorResources.VertexFactory.GetNumCutoutVerticesPerFrame();
	VFLooseParams.CutoutGeometry = CollectorResources.VertexFactory.GetCutoutGeometrySRV() ? CollectorResources.VertexFactory.GetCutoutGeometrySRV() : GFNiagaraNullCutoutVertexBuffer.VertexBufferSRV.GetReference();
	VFLooseParams.NiagaraParticleDataFloat = CollectorResources.VertexFactory.GetParticleDataFloatSRV();
	VFLooseParams.NiagaraFloatDataOffset = CollectorResources.VertexFactory.GetFloatDataOffset();
	VFLooseParams.NiagaraFloatDataStride = CollectorResources.VertexFactory.GetFloatDataStride();
	VFLooseParams.ParticleAlignmentMode = CollectorResources.VertexFactory.GetAlignmentMode();
	VFLooseParams.ParticleFacingMode = CollectorResources.VertexFactory.GetFacingMode();
	VFLooseParams.SortedIndices = CollectorResources.VertexFactory.GetSortedIndicesSRV() ? CollectorResources.VertexFactory.GetSortedIndicesSRV().GetReference() : GFNiagaraNullSortedIndicesVertexBuffer.VertexBufferSRV.GetReference();
	VFLooseParams.SortedIndicesOffset = CollectorResources.VertexFactory.GetSortedIndicesOffset();
	if (IndirectArgsOffset != INDEX_NONE)
	{
		NiagaraEmitterInstanceBatcher* Batcher = SceneProxy->GetBatcher();
		check(Batcher); // Already verified at this point.
		VFLooseParams.IndirectArgsOffset = IndirectArgsOffset / sizeof(uint32);
		VFLooseParams.IndirectArgsBuffer = Batcher->GetGPUInstanceCounterManager().GetDrawIndirectBuffer().SRV;
	}
	else
	{
		VFLooseParams.IndirectArgsBuffer = GFNiagaraNullSortedIndicesVertexBuffer.VertexBufferSRV;
		VFLooseParams.IndirectArgsOffset = 0;
	}

	CollectorResources.VertexFactory.LooseParameterUniformBuffer = FNiagaraSpriteVFLooseParametersRef::CreateUniformBufferImmediate(VFLooseParams, UniformBuffer_SingleFrame);

	MeshBatch.VertexFactory = &CollectorResources.VertexFactory;
	MeshBatch.CastShadow = SceneProxy->CastsDynamicShadow();
#if RHI_RAYTRACING
	MeshBatch.CastRayTracedShadow = SceneProxy->CastsDynamicShadow();
#endif
	MeshBatch.bUseAsOccluder = false;
	MeshBatch.ReverseCulling = SceneProxy->IsLocalToWorldDeterminantNegative();
	MeshBatch.Type = PT_TriangleList;
	MeshBatch.DepthPriorityGroup = SceneProxy->GetDepthPriorityGroup(View);
	MeshBatch.bCanApplyViewModeOverrides = true;
	MeshBatch.bUseWireframeSelectionColoring = SceneProxy->IsSelected();
	MeshBatch.SegmentIndex = 0;

	if (bIsWireframe)
	{
		MeshBatch.MaterialRenderProxy = UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
	}
	else
	{
		MeshBatch.MaterialRenderProxy = MaterialRenderProxy;
	}

	FMeshBatchElement& MeshElement = MeshBatch.Elements[0];
	MeshElement.IndexBuffer = &GParticleIndexBuffer;
	MeshElement.FirstIndex = 0;
	MeshElement.NumPrimitives = NumIndicesPerInstance / 3;
	MeshElement.NumInstances = FMath::Max(0, NumInstances);	//->VertexData.Num();
	MeshElement.MinVertexIndex = 0;
	MeshElement.MaxVertexIndex = 0;// MeshElement.NumInstances * 4 - 1;
	MeshElement.PrimitiveUniformBuffer = WorldSpacePrimitiveUniformBuffer.GetUniformBufferRHI();
	if (IndirectArgsOffset != INDEX_NONE)
	{
		NiagaraEmitterInstanceBatcher* Batcher = SceneProxy->GetBatcher();
		check(Batcher); // Already verified at this point.
		MeshElement.IndirectArgsOffset = IndirectArgsOffset;
		MeshElement.IndirectArgsBuffer = Batcher->GetGPUInstanceCounterManager().GetDrawIndirectBuffer().Buffer;
		MeshElement.NumPrimitives = 0;
	}

	if (NumCutoutVertexPerSubImage == 8)
	{
		MeshElement.IndexBuffer = &GSixTriangleParticleIndexBuffer;
	}
	
	INC_DWORD_STAT_BY(STAT_NiagaraNumSprites, NumInstances);
}

void FNiagaraRendererSprites::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector, const FNiagaraSceneProxy *SceneProxy) const
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraRender);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderSprites);
	check(SceneProxy);

	SimpleTimer MeshElementsTimer;

	//check(DynamicDataRender)
	FNiagaraDynamicDataSprites *DynamicDataSprites = static_cast<FNiagaraDynamicDataSprites*>(DynamicDataRender);
	NiagaraEmitterInstanceBatcher* Batcher = SceneProxy->GetBatcher();
	if (!DynamicDataSprites || !Batcher)
	{
		return;
	}

	FNiagaraDataBuffer* SourceParticleData = DynamicDataSprites->GetParticleDataToRender();
	if (SourceParticleData == nullptr	||
		SourceParticleData->GetNumInstances() == 0 ||
		GbEnableNiagaraSpriteRendering == 0 ||
		!GSupportsResourceView // Current shader requires SRV to draw properly in all cases.
		)
	{
		return;
	}

#if STATS
	FScopeCycleCounter EmitterStatsCounter(EmitterStatID);
#endif
	
	FCPUSimParticleDataAllocation CPUSimParticleDataAllocation = ConditionalAllocateCPUSimParticleData(DynamicDataSprites, Collector.GetDynamicReadBuffer());

	ConditionalInitPrimitiveUniformBuffer(SceneProxy);

	uint32 IndirectArgsOffset = INDEX_NONE;
	if (SimTarget == ENiagaraSimTarget::GPUComputeSim)
	{
		IndirectArgsOffset = Batcher->GetGPUInstanceCounterManager().AddDrawIndirect(SourceParticleData->GetGPUInstanceCountBufferOffset(), NumIndicesPerInstance);
	}

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			const FSceneView* View = Views[ViewIndex];

			FNiagaraMeshCollectorResourcesSprite& CollectorResources = Collector.AllocateOneFrameResource<FNiagaraMeshCollectorResourcesSprite>();
			SetVertexFactoryParticleData(CollectorResources.VertexFactory, DynamicDataSprites, CPUSimParticleDataAllocation, View, SceneProxy);
			CollectorResources.UniformBuffer = CreatePerViewUniformBuffer(View, ViewFamily, SceneProxy);
			FMeshBatch& MeshBatch = Collector.AllocateMesh();
			CreateMeshBatchForView(View, ViewFamily, SceneProxy, DynamicDataSprites, IndirectArgsOffset, MeshBatch, CollectorResources);
			Collector.AddMesh(ViewIndex, MeshBatch);
		}
	}

	CPUTimeMS += MeshElementsTimer.GetElapsedMilliseconds();
}

#if RHI_RAYTRACING
void FNiagaraRendererSprites::GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context, TArray<FRayTracingInstance>& OutRayTracingInstances, const FNiagaraSceneProxy* SceneProxy)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraRender);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderSprites);
	check(SceneProxy);

	SimpleTimer MeshElementsTimer;

	FNiagaraDynamicDataSprites *DynamicDataSprites = static_cast<FNiagaraDynamicDataSprites*>(DynamicDataRender);
	NiagaraEmitterInstanceBatcher* Batcher = SceneProxy->GetBatcher();
	if (!DynamicDataSprites || !Batcher)
	{
		return;
	}

	FNiagaraDataBuffer* SourceParticleData = DynamicDataSprites->GetParticleDataToRender();
	if (SourceParticleData == nullptr	||
		SourceParticleData->GetNumInstancesAllocated() == 0 ||
		SourceParticleData->GetNumInstances() == 0 ||
		GbEnableNiagaraSpriteRendering == 0 ||
		!GSupportsResourceView // Current shader requires SRV to draw properly in all cases.
		)
	{
		return;
	}

	uint32 IndirectArgsOffset = INDEX_NONE;
	if (SimTarget == ENiagaraSimTarget::GPUComputeSim)
	{
		IndirectArgsOffset = Batcher->GetGPUInstanceCounterManager().AddDrawIndirect(SourceParticleData->GetGPUInstanceCountBufferOffset(), NumIndicesPerInstance);
	}

	FRayTracingInstance RayTracingInstance;
	RayTracingInstance.Geometry = &RayTracingGeometry;
	RayTracingInstance.InstanceTransforms.Add(FMatrix::Identity);

	{
		// Setup material for our ray tracing instance
		FCPUSimParticleDataAllocation CPUSimParticleDataAllocation = ConditionalAllocateCPUSimParticleData(DynamicDataSprites, Context.RayTracingMeshResourceCollector.GetDynamicReadBuffer());
		ConditionalInitPrimitiveUniformBuffer(SceneProxy);
		FNiagaraMeshCollectorResourcesSprite& CollectorResources = Context.RayTracingMeshResourceCollector.AllocateOneFrameResource<FNiagaraMeshCollectorResourcesSprite>();
		SetVertexFactoryParticleData(CollectorResources.VertexFactory, DynamicDataSprites, CPUSimParticleDataAllocation, Context.ReferenceView, SceneProxy);
		CollectorResources.UniformBuffer = CreatePerViewUniformBuffer(Context.ReferenceView, Context.ReferenceViewFamily, SceneProxy);
		FMeshBatch MeshBatch;
		CreateMeshBatchForView(Context.ReferenceView, Context.ReferenceViewFamily, SceneProxy, DynamicDataSprites, IndirectArgsOffset, MeshBatch, CollectorResources);

		ensureMsgf(MeshBatch.Elements[0].IndexBuffer != &GSixTriangleParticleIndexBuffer, TEXT("Cutout geometry is not supported by ray tracing"));

		RayTracingInstance.Materials.Add(MeshBatch);

		// Update dynamic ray tracing geometry
		Context.DynamicRayTracingGeometriesToUpdate.Add(
			FRayTracingDynamicGeometryUpdateParams
			{
				RayTracingInstance.Materials,
				MeshBatch.Elements[0].NumPrimitives == 0,
				6 *  SourceParticleData->GetNumInstances(),
				6 *  SourceParticleData->GetNumInstances() * (uint32)sizeof(FVector),
				2 *  SourceParticleData->GetNumInstances(),
				&RayTracingGeometry,
				&RayTracingDynamicVertexBuffer
			}
		);
	}

	RayTracingInstance.BuildInstanceMaskAndFlags();

	OutRayTracingInstances.Add(RayTracingInstance);

	CPUTimeMS += MeshElementsTimer.GetElapsedMilliseconds();
}
#endif

/** Update render data buffer from attributes */
FNiagaraDynamicDataBase *FNiagaraRendererSprites::GenerateDynamicData(const FNiagaraSceneProxy* Proxy, const UNiagaraRendererProperties* InProperties, const FNiagaraEmitterInstance* Emitter)const
{
	FNiagaraDynamicDataSprites *DynamicData = nullptr;
	const UNiagaraSpriteRendererProperties* Properties = CastChecked<const UNiagaraSpriteRendererProperties>(InProperties);

	if (Properties)
	{
		SimpleTimer VertexDataTimer;

		SCOPE_CYCLE_COUNTER(STAT_NiagaraGenSpriteDynamicData);

		FNiagaraDataSet& Data = Emitter->GetData();
		if(SimTarget == ENiagaraSimTarget::GPUComputeSim || Data.GetCurrentDataChecked().GetNumInstances() > 0)
		{
			DynamicData = new FNiagaraDynamicDataSprites(Emitter);


			//In preparation for a material override feature, we pass our material(s) and relevance in via dynamic data.
			//The renderer ensures we have the correct usage and relevance for materials in BaseMaterials_GT.
			//Any override feature must also do the same for materials that are set.
			check(BaseMaterials_GT.Num() == 1);
			check(BaseMaterials_GT[0]->CheckMaterialUsage_Concurrent(MATUSAGE_NiagaraSprites));
			DynamicData->Material = BaseMaterials_GT[0]->GetRenderProxy();
			DynamicData->SetMaterialRelevance(BaseMaterialRelevance_GT);
		}

		CPUTimeMS = VertexDataTimer.GetElapsedMilliseconds();
	}

	return DynamicData;  // for VF that can fetch from particle data directly
}

int FNiagaraRendererSprites::GetDynamicDataSize()const
{
	uint32 Size = sizeof(FNiagaraDynamicDataSprites);
	return Size;
}

void FNiagaraRendererSprites::TransformChanged()
{
	WorldSpacePrimitiveUniformBuffer.ReleaseResource();
}

bool FNiagaraRendererSprites::IsMaterialValid(UMaterialInterface* Mat)const
{
	return Mat && Mat->CheckMaterialUsage(MATUSAGE_NiagaraSprites);
}