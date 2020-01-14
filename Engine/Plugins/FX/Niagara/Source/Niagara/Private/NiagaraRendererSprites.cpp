// Copyright Epic Games, Inc. All Rights Reserved.

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

int32 GbEnableMinimalGPUBuffers = 1;
static FAutoConsoleVariableRef CVarbEnableMinimalGPUBuffers(
	TEXT("fx.EnableMinimalGPUBuffers"),
	GbEnableMinimalGPUBuffers,
	TEXT("If > 0 we use new code to pass the gpu only data the VF actuially uses for redering, rather than the whole partilce buffer. \n"),
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

namespace ENiagaraSpriteVFLayout
{
	enum Type
	{
		Position, 
		Color, 
		Velocity, 
		Rotation, 
		Size, 
		Facing, 
		Alignment, 
		SubImage, 
		MaterialParam0, 
		MaterialParam1,
		MaterialParam2,
		MaterialParam3,
		CameraOffset, 
		UVScale, 
		MaterialRandom, 
		CustomSorting, 
		NormalizedAge, 

		Num,
	};
};

FNiagaraRendererSprites::FNiagaraRendererSprites(ERHIFeatureLevel::Type FeatureLevel, const UNiagaraRendererProperties *InProps, const FNiagaraEmitterInstance* Emitter)
	: FNiagaraRenderer(FeatureLevel, InProps, Emitter)
	, Alignment(ENiagaraSpriteAlignment::Unaligned)
	, FacingMode(ENiagaraSpriteFacingMode::FaceCamera)
	, PivotInUVSpace(0.5f, 0.5f)
	, SortMode(ENiagaraSortMode::ViewDistance)
	, SubImageSize(1.0f, 1.0f)
	, bSubImageBlend(false)
	, bRemoveHMDRollInVR(false)
	, bSortOnlyWhenTranslucent(true)
	, MinFacingCameraBlendDistance(0.0f)
	, MaxFacingCameraBlendDistance(0.0f)
	, MaterialParamValidMask(0)
{
	check(InProps && Emitter);

	const UNiagaraSpriteRendererProperties* Properties = CastChecked<const UNiagaraSpriteRendererProperties>(InProps);
	
	Alignment = Properties->Alignment;
	FacingMode = Properties->FacingMode;
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

	const FNiagaraDataSet& Data = Emitter->GetData();

	TotalVFComponents = 0;
	VFVariables.SetNum(ENiagaraSpriteVFLayout::Num);
	SetVertexFactoryVariable(Data, Properties->PositionBinding.DataSetVariable, ENiagaraSpriteVFLayout::Position);
	SetVertexFactoryVariable(Data, Properties->VelocityBinding.DataSetVariable, ENiagaraSpriteVFLayout::Velocity);
	SetVertexFactoryVariable(Data, Properties->ColorBinding.DataSetVariable, ENiagaraSpriteVFLayout::Color);
	SetVertexFactoryVariable(Data, Properties->SpriteRotationBinding.DataSetVariable, ENiagaraSpriteVFLayout::Rotation);
	SetVertexFactoryVariable(Data, Properties->SpriteSizeBinding.DataSetVariable, ENiagaraSpriteVFLayout::Size);
	SetVertexFactoryVariable(Data, Properties->SpriteFacingBinding.DataSetVariable, ENiagaraSpriteVFLayout::Facing);
	SetVertexFactoryVariable(Data, Properties->SpriteAlignmentBinding.DataSetVariable, ENiagaraSpriteVFLayout::Alignment);
	SetVertexFactoryVariable(Data, Properties->SubImageIndexBinding.DataSetVariable, ENiagaraSpriteVFLayout::SubImage);
	SetVertexFactoryVariable(Data, Properties->CameraOffsetBinding.DataSetVariable, ENiagaraSpriteVFLayout::CameraOffset);
	SetVertexFactoryVariable(Data, Properties->UVScaleBinding.DataSetVariable, ENiagaraSpriteVFLayout::UVScale);
	SetVertexFactoryVariable(Data, Properties->NormalizedAgeBinding.DataSetVariable, ENiagaraSpriteVFLayout::NormalizedAge);
	SetVertexFactoryVariable(Data, Properties->MaterialRandomBinding.DataSetVariable, ENiagaraSpriteVFLayout::MaterialRandom);
	SetVertexFactoryVariable(Data, Properties->CustomSortingBinding.DataSetVariable, ENiagaraSpriteVFLayout::CustomSorting);
	MaterialParamValidMask |= SetVertexFactoryVariable(Data, Properties->DynamicMaterialBinding.DataSetVariable, ENiagaraSpriteVFLayout::MaterialParam0) ? 0x1 : 0;
	MaterialParamValidMask |= SetVertexFactoryVariable(Data, Properties->DynamicMaterial1Binding.DataSetVariable, ENiagaraSpriteVFLayout::MaterialParam1) ? 0x2 : 0;
	MaterialParamValidMask |= SetVertexFactoryVariable(Data, Properties->DynamicMaterial2Binding.DataSetVariable, ENiagaraSpriteVFLayout::MaterialParam2) ? 0x4 : 0;
	MaterialParamValidMask |= SetVertexFactoryVariable(Data, Properties->DynamicMaterial3Binding.DataSetVariable, ENiagaraSpriteVFLayout::MaterialParam3) ? 0x8 : 0;
}

FNiagaraRendererSprites::~FNiagaraRendererSprites()
{
}

void FNiagaraRendererSprites::ReleaseRenderThreadResources()
{
	FNiagaraRenderer::ReleaseRenderThreadResources();

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

#if RHI_RAYTRACING
	if (IsRayTracingEnabled())
	{
		RayTracingDynamicVertexBuffer.Initialize(4, 256, PF_R32_FLOAT, BUF_UnorderedAccess | BUF_ShaderResource, TEXT("RayTracingDynamicVertexBuffer"));

		FRayTracingGeometryInitializer Initializer;
		Initializer.IndexBuffer = nullptr;
		Initializer.GeometryType = RTGT_Triangles;
		Initializer.bFastBuild = true;
		Initializer.bAllowUpdate = false;
		RayTracingGeometry.SetInitializer(Initializer);
		RayTracingGeometry.InitResource();
	}
#endif
}

FNiagaraRendererSprites::FCPUSimParticleDataAllocation FNiagaraRendererSprites::ConditionalAllocateCPUSimParticleData(FNiagaraDynamicDataSprites* DynamicDataSprites, FGlobalDynamicReadBuffer& DynamicReadBuffer) const
{
	FNiagaraDataBuffer* SourceParticleData = DynamicDataSprites->GetParticleDataToRender();
	check(SourceParticleData);//Can be null but should be checked before here.

	FCPUSimParticleDataAllocation CPUSimParticleDataAllocation { DynamicReadBuffer };

	if (SimTarget == ENiagaraSimTarget::CPUSim)
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderSpritesCPUSimCopy);

		if (GbEnableMinimalGPUBuffers)
		{
			SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderSpritesCPUSimMemCopy);
			CPUSimParticleDataAllocation.ParticleData = TransferDataToGPU(DynamicReadBuffer, SourceParticleData);
		}
		else
		{
			int32 TotalFloatSize = SourceParticleData->GetFloatBuffer().Num() / sizeof(float);
			CPUSimParticleDataAllocation.ParticleData = DynamicReadBuffer.AllocateFloat(TotalFloatSize);
			SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderSpritesCPUSimMemCopy);			
			FMemory::Memcpy(CPUSimParticleDataAllocation.ParticleData.Buffer, SourceParticleData->GetFloatBuffer().GetData(), SourceParticleData->GetFloatBuffer().Num());
		}
	}

	return CPUSimParticleDataAllocation;
}

FNiagaraSpriteUniformBufferRef FNiagaraRendererSprites::CreatePerViewUniformBuffer(const FSceneView* View, const FSceneViewFamily& ViewFamily, const FNiagaraSceneProxy *SceneProxy) const
{
	FNiagaraSpriteUniformParameters PerViewUniformParameters;
	FMemory::Memzero(&PerViewUniformParameters,sizeof(PerViewUniformParameters)); // Clear unset bytes

	PerViewUniformParameters.bLocalSpace = bLocalSpace;
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
	PerViewUniformParameters.SubImageSize = FVector4(SubImageSize.X, SubImageSize.Y, 1.0f / SubImageSize.X, 1.0f / SubImageSize.Y);

	PerViewUniformParameters.PositionDataOffset = VFVariables[ENiagaraSpriteVFLayout::Position].GetGPUOffset();
	PerViewUniformParameters.VelocityDataOffset = VFVariables[ENiagaraSpriteVFLayout::Velocity].GetGPUOffset();
	PerViewUniformParameters.RotationDataOffset = VFVariables[ENiagaraSpriteVFLayout::Rotation].GetGPUOffset();
	PerViewUniformParameters.SizeDataOffset = VFVariables[ENiagaraSpriteVFLayout::Size].GetGPUOffset();
	PerViewUniformParameters.ColorDataOffset = VFVariables[ENiagaraSpriteVFLayout::Color].GetGPUOffset();
	PerViewUniformParameters.MaterialParamDataOffset = VFVariables[ENiagaraSpriteVFLayout::MaterialParam0].GetGPUOffset();
	PerViewUniformParameters.MaterialParam1DataOffset = VFVariables[ENiagaraSpriteVFLayout::MaterialParam1].GetGPUOffset();
	PerViewUniformParameters.MaterialParam2DataOffset = VFVariables[ENiagaraSpriteVFLayout::MaterialParam2].GetGPUOffset();
	PerViewUniformParameters.MaterialParam3DataOffset = VFVariables[ENiagaraSpriteVFLayout::MaterialParam3].GetGPUOffset();
	PerViewUniformParameters.SubimageDataOffset = VFVariables[ENiagaraSpriteVFLayout::SubImage].GetGPUOffset();
	PerViewUniformParameters.FacingDataOffset = VFVariables[ENiagaraSpriteVFLayout::Facing].GetGPUOffset();
	PerViewUniformParameters.AlignmentDataOffset = VFVariables[ENiagaraSpriteVFLayout::Alignment].GetGPUOffset();
	PerViewUniformParameters.CameraOffsetDataOffset = VFVariables[ENiagaraSpriteVFLayout::CameraOffset].GetGPUOffset();
	PerViewUniformParameters.UVScaleDataOffset = VFVariables[ENiagaraSpriteVFLayout::UVScale].GetGPUOffset();
	PerViewUniformParameters.NormalizedAgeDataOffset = VFVariables[ENiagaraSpriteVFLayout::NormalizedAge].GetGPUOffset();
	PerViewUniformParameters.MaterialRandomDataOffset = VFVariables[ENiagaraSpriteVFLayout::MaterialRandom].GetGPUOffset();

	PerViewUniformParameters.SubImageBlendMode = bSubImageBlend;
	PerViewUniformParameters.MaterialParamValidMask = MaterialParamValidMask;
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
		EBlendMode BlendMode = MaterialRenderProxy->GetMaterial(FeatureLevel)->GetBlendMode();
		OutVertexFactory.SetSortedIndices(nullptr, 0xFFFFFFFF);

		int32 NumInstances = SourceParticleData->GetNumInstances();
		FNiagaraGPUSortInfo SortInfo;
		SortInfo.SortAttributeOffset = INDEX_NONE;
		int32 SortingVarIdx = INDEX_NONE;
		bool bShouldSort = SortMode != ENiagaraSortMode::None && (BlendMode == BLEND_AlphaComposite || BlendMode == BLEND_AlphaHoldout || BlendMode == BLEND_Translucent || !bSortOnlyWhenTranslucent);
		if (bShouldSort)
		{
			SortInfo.ParticleCount = NumInstances;
			SortInfo.SortMode = SortMode;
			if (SortInfo.SortMode == ENiagaraSortMode::CustomAscending || SortInfo.SortMode == ENiagaraSortMode::CustomDecending)
			{
				SortingVarIdx = ENiagaraSpriteVFLayout::CustomSorting;	
				SortInfo.ViewOrigin.Set(0, 0, 0);
				SortInfo.ViewDirection.Set(0, 0, 1);
			}
			else
			{
				SortingVarIdx = ENiagaraSpriteVFLayout::Position;
				SortInfo.ViewOrigin = View->ViewMatrices.GetViewOrigin();
				SortInfo.ViewDirection = View->GetViewDirection();
				if (bLocalSpace)
				{
					SortInfo.ViewOrigin = SceneProxy->GetLocalToWorldInverse().TransformPosition(SortInfo.ViewOrigin);
					SortInfo.ViewDirection = SceneProxy->GetLocalToWorld().GetTransposed().TransformVector(SortInfo.ViewDirection);
				}
			}

			SortInfo.SortAttributeOffset = VFVariables[SortingVarIdx].GetGPUOffset();
		}


		if (SimTarget == ENiagaraSimTarget::CPUSim)//TODO: Compute shader for sorting gpu sims and larger cpu sims.
		{
			int32 ParticleStrideInFloats = GbEnableMinimalGPUBuffers ? SourceParticleData->GetNumInstances() : SourceParticleData->GetFloatStride() / sizeof(float);
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
					SortInfo.FloatDataStride = ParticleStrideInFloats;
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
					SortIndices(SortInfo, SortingVarIdx, *SourceParticleData, SortedIndices);
					OutVertexFactory.SetSortedIndices(SortedIndices.ReadBuffer->SRV, SortedIndices.FirstIndex / sizeof(float));
				}
			}
			OutVertexFactory.SetParticleData(CPUSimParticleDataAllocation.ParticleData.ReadBuffer->SRV, CPUSimParticleDataAllocation.ParticleData.FirstIndex / sizeof(float), ParticleStrideInFloats);
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
	MeshElement.PrimitiveUniformBuffer = IsMotionBlurEnabled() ? SceneProxy->GetUniformBuffer() : SceneProxy->GetUniformBufferNoVelocity();
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
	PARTICLE_PERF_STAT_CYCLES(SceneProxy->PerfAsset, GetDynamicMeshElements);
	check(SceneProxy);

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
	FMaterialRenderProxy* MaterialRenderProxy = DynamicDataSprites->Material;
	check(MaterialRenderProxy);
	EBlendMode BlendMode = MaterialRenderProxy->GetMaterial(FeatureLevel)->GetBlendMode();
	bool bShouldSort = SortMode != ENiagaraSortMode::None && (BlendMode == BLEND_AlphaComposite || BlendMode == BLEND_AlphaHoldout || BlendMode == BLEND_Translucent || !bSortOnlyWhenTranslucent);
	bool bNeedCustomSort = bShouldSort && (SortMode == ENiagaraSortMode::CustomAscending || SortMode == ENiagaraSortMode::CustomDecending);
	//Disable the upload of sorting data if we're using a material that doesn't need it.
	//TODO: we can probably reinit the GPU layout info entirely to remove custom sorting from the buffer but for now just skip the upload if it's not needed.
	VFVariables[ENiagaraSpriteVFLayout::CustomSorting].bUpload &= bNeedCustomSort;

	FCPUSimParticleDataAllocation CPUSimParticleDataAllocation = ConditionalAllocateCPUSimParticleData(DynamicDataSprites, Collector.GetDynamicReadBuffer());

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
}

#if RHI_RAYTRACING
void FNiagaraRendererSprites::GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context, TArray<FRayTracingInstance>& OutRayTracingInstances, const FNiagaraSceneProxy* SceneProxy)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraRender);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderSprites);
	check(SceneProxy);

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
}
#endif

/** Update render data buffer from attributes */
FNiagaraDynamicDataBase *FNiagaraRendererSprites::GenerateDynamicData(const FNiagaraSceneProxy* Proxy, const UNiagaraRendererProperties* InProperties, const FNiagaraEmitterInstance* Emitter)const
{
	FNiagaraDynamicDataSprites *DynamicData = nullptr;
	const UNiagaraSpriteRendererProperties* Properties = CastChecked<const UNiagaraSpriteRendererProperties>(InProperties);

	if (Properties)
	{
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
	}

	return DynamicData;  // for VF that can fetch from particle data directly
}

int FNiagaraRendererSprites::GetDynamicDataSize()const
{
	uint32 Size = sizeof(FNiagaraDynamicDataSprites);
	return Size;
}

bool FNiagaraRendererSprites::IsMaterialValid(UMaterialInterface* Mat)const
{
	return Mat && Mat->CheckMaterialUsage(MATUSAGE_NiagaraSprites);
}