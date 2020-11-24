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
#include "Materials/MaterialInstanceDynamic.h"

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

static TAutoConsoleVariable<int32> CVarRayTracingNiagaraSprites(
	TEXT("r.RayTracing.Geometry.NiagaraSprites"),
	1,
	TEXT("Include Niagara sprites in ray tracing effects (default = 1 (Niagara sprites enabled in ray tracing))"));


/** Dynamic data for sprite renderers. */
struct FNiagaraDynamicDataSprites : public FNiagaraDynamicDataBase
{
	FNiagaraDynamicDataSprites(const FNiagaraEmitterInstance* InEmitter)
		: FNiagaraDynamicDataBase(InEmitter)
	{
	}
	
	FMaterialRenderProxy* Material = nullptr;
	TArray<UNiagaraDataInterface*> DataInterfacesBound;
	TArray<UObject*> ObjectsBound;
	TArray<uint8> ParameterDataBound;
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
	, PivotInUVSpace(0.5f, 0.5f)
	, SortMode(ENiagaraSortMode::ViewDistance)
	, SubImageSize(1.0f, 1.0f)
	, bSubImageBlend(false)
	, bRemoveHMDRollInVR(false)
	, bSortOnlyWhenTranslucent(true)
	, bGpuLowLatencyTranslucency(true)
	, bEnableDistanceCulling(false)
	, MinFacingCameraBlendDistance(0.0f)
	, MaxFacingCameraBlendDistance(0.0f)
	, DistanceCullRange(0.0f, FLT_MAX)
	, MaterialParamValidMask(0)
	, RendererVisTagOffset(INDEX_NONE)
	, RendererVisibility(0)
{
	check(InProps && Emitter);

	const UNiagaraSpriteRendererProperties* Properties = CastChecked<const UNiagaraSpriteRendererProperties>(InProps);
	SourceMode = Properties->SourceMode;
	Alignment = Properties->Alignment;
	FacingMode = Properties->FacingMode;
	PivotInUVSpace = Properties->PivotInUVSpace;
	SortMode = Properties->SortMode;
	SubImageSize = Properties->SubImageSize;
	bSubImageBlend = Properties->bSubImageBlend;
	bRemoveHMDRollInVR = Properties->bRemoveHMDRollInVR;
	bSortOnlyWhenTranslucent = Properties->bSortOnlyWhenTranslucent;
	bGpuLowLatencyTranslucency = Properties->bGpuLowLatencyTranslucency && (SortMode == ENiagaraSortMode::None);
	MinFacingCameraBlendDistance = Properties->MinFacingCameraBlendDistance;
	MaxFacingCameraBlendDistance = Properties->MaxFacingCameraBlendDistance;
	RendererVisibility = Properties->RendererVisibility;

	bEnableDistanceCulling = Properties->bEnableCameraDistanceCulling;
	if (Properties->bEnableCameraDistanceCulling)
	{
		DistanceCullRange = FVector2D(Properties->MinCameraDistance, Properties->MaxCameraDistance);
	}

	// Get the offset of visibility tag in either particle data or parameter store
	RendererVisTagOffset = INDEX_NONE;
	bEnableCulling = bEnableDistanceCulling;
	if (Properties->RendererVisibilityTagBinding.CanBindToHostParameterMap())
	{
		RendererVisTagOffset = Emitter->GetRendererBoundVariables().IndexOf(Properties->RendererVisibilityTagBinding.GetParamMapBindableVariable());
		bVisTagInParamStore = true;
	}
	else
	{
		int32 FloatOffset, HalfOffset;
		const FNiagaraDataSet& Data = Emitter->GetData();
		Data.GetVariableComponentOffsets(Properties->RendererVisibilityTagBinding.GetDataSetBindableVariable(), FloatOffset, RendererVisTagOffset, HalfOffset);
		bVisTagInParamStore = false;
		bEnableCulling |= RendererVisTagOffset != INDEX_NONE;
	}

	NumCutoutVertexPerSubImage = Properties->GetNumCutoutVertexPerSubimage();
	CutoutVertexBuffer.Data = Properties->GetCutoutData();

	MaterialParamValidMask = Properties->MaterialParamValidMask;

	RendererLayoutWithCustomSort = &Properties->RendererLayoutWithCustomSort;
	RendererLayoutWithoutCustomSort = &Properties->RendererLayoutWithoutCustomSort;

	bSetAnyBoundVars = false;
	if (Emitter->GetRendererBoundVariables().IsEmpty() == false)
	{
		const TArray< const FNiagaraVariableAttributeBinding*>& VFBindings = Properties->GetAttributeBindings();
		check(VFBindings.Num() >= ENiagaraSpriteVFLayout::Type::Num);

		for (int32 i = 0; i < ENiagaraSpriteVFLayout::Type::Num; i++)
		{
			VFBoundOffsetsInParamStore[i] = INDEX_NONE;
			if (VFBindings[i] && VFBindings[i]->CanBindToHostParameterMap())
			{
				VFBoundOffsetsInParamStore[i] = Emitter->GetRendererBoundVariables().IndexOf(VFBindings[i]->GetParamMapBindableVariable());
				if (VFBoundOffsetsInParamStore[i] != INDEX_NONE)
					bSetAnyBoundVars = true;
			}
		}
	}
	else
	{
		for (int32 i = 0; i < ENiagaraSpriteVFLayout::Type::Num; i++)
		{
			VFBoundOffsetsInParamStore[i] = INDEX_NONE;
		}
	}
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

int32 FNiagaraRendererSprites::GetMaxIndirectArgs() const
{
	if (SimTarget == ENiagaraSimTarget::GPUComputeSim)
	{
		return 1;
	}

	// If we're CPU, we only need indirect args if we're using renderer visibility or distance culling
	if (bEnableDistanceCulling || (!bVisTagInParamStore && RendererVisTagOffset != INDEX_NONE))
	{
		return 1;
	}

	return 0;
}

void FNiagaraRendererSprites::CreateRenderThreadResources(NiagaraEmitterInstanceBatcher* Batcher)
{
	FNiagaraRenderer::CreateRenderThreadResources(Batcher);
	CutoutVertexBuffer.InitResource();

#if RHI_RAYTRACING
	if (IsRayTracingEnabled())
	{
		FRayTracingGeometryInitializer Initializer;
		static const FName DebugName("FNiagaraRendererSprites");
		static int32 DebugNumber = 0;
		Initializer.DebugName = FName(DebugName, DebugNumber++);
		Initializer.IndexBuffer = nullptr;
		Initializer.GeometryType = RTGT_Triangles;
		Initializer.bFastBuild = true;
		Initializer.bAllowUpdate = false;
		RayTracingGeometry.SetInitializer(Initializer);
		RayTracingGeometry.InitResource();
	}
#endif
}

FNiagaraRendererSprites::FCPUSimParticleDataAllocation FNiagaraRendererSprites::ConditionalAllocateCPUSimParticleData(FNiagaraDynamicDataSprites* DynamicDataSprites, const FNiagaraRendererLayout* RendererLayout, FGlobalDynamicReadBuffer& DynamicReadBuffer, bool bNeedsGPUVis) const
{
	FNiagaraDataBuffer* SourceParticleData = DynamicDataSprites->GetParticleDataToRender();
	check(SourceParticleData);//Can be null but should be checked before here.

	FCPUSimParticleDataAllocation CPUSimParticleDataAllocation { DynamicReadBuffer };

	if (SimTarget == ENiagaraSimTarget::CPUSim && SourceMode == ENiagaraRendererSourceDataMode::Particles)
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderSpritesCPUSimCopy);

		if (GbEnableMinimalGPUBuffers)
		{
			SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderSpritesCPUSimMemCopy);
			CPUSimParticleDataAllocation.ParticleData = TransferDataToGPU(DynamicReadBuffer, RendererLayout, SourceParticleData);
		}
		else
		{
			SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderSpritesCPUSimMemCopy);
			int32 TotalFloatSize = SourceParticleData->GetFloatBuffer().Num() / sizeof(float);
			CPUSimParticleDataAllocation.ParticleData.FloatData = DynamicReadBuffer.AllocateFloat(TotalFloatSize);
			FMemory::Memcpy(CPUSimParticleDataAllocation.ParticleData.FloatData.Buffer, SourceParticleData->GetFloatBuffer().GetData(), SourceParticleData->GetFloatBuffer().Num());
			int32 TotalHalfSize = SourceParticleData->GetHalfBuffer().Num() / sizeof(FFloat16);
			CPUSimParticleDataAllocation.ParticleData.HalfData = DynamicReadBuffer.AllocateHalf(TotalFloatSize);
			FMemory::Memcpy(CPUSimParticleDataAllocation.ParticleData.HalfData.Buffer, SourceParticleData->GetHalfBuffer().GetData(), SourceParticleData->GetHalfBuffer().Num());
		}

		if (bNeedsGPUVis)
		{
			// For CPU sims, we need to also copy off the renderer visibility tags for the sort shader
			check(!bVisTagInParamStore && RendererVisTagOffset != INDEX_NONE);
			const int32 NumInstances = SourceParticleData->GetNumInstances();
			CPUSimParticleDataAllocation.IntData = DynamicReadBuffer.AllocateInt32(NumInstances);
			int32* Dest = (int32*)CPUSimParticleDataAllocation.IntData.Buffer;
			const int32* Src = (const int32*)SourceParticleData->GetInt32Buffer().GetData();
			const uint32 IntStride = SourceParticleData->GetInt32Stride() / sizeof(uint32);
			for (int32 InstIdx = 0; InstIdx < NumInstances; ++InstIdx)
			{
				Dest[InstIdx] = Src[RendererVisTagOffset * IntStride + InstIdx];
			}
		}
	}

	return CPUSimParticleDataAllocation;
}

FNiagaraSpriteUniformBufferRef FNiagaraRendererSprites::CreatePerViewUniformBuffer(const FSceneView* View, const FSceneViewFamily& ViewFamily, const FNiagaraSceneProxy *SceneProxy, const FNiagaraRendererLayout* RendererLayout, const FNiagaraDynamicDataSprites* DynamicDataSprites) const
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


	PerViewUniformParameters.DefaultPos = bLocalSpace ? FVector4(0.0f, 0.0f, 0.0f, 1.0f) : FVector4(SceneProxy->GetLocalToWorld().GetOrigin());
	PerViewUniformParameters.DefaultSize = FVector2D(50.f, 50.0f);
	PerViewUniformParameters.DefaultUVScale = FVector2D(1.0f, 1.0f);
	PerViewUniformParameters.DefaultVelocity = FVector(0.f, 0.0f, 0.0f);
	PerViewUniformParameters.DefaultRotation =  0.0f;
	PerViewUniformParameters.DefaultColor = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	PerViewUniformParameters.DefaultMatRandom = 0.0f;
	PerViewUniformParameters.DefaultCamOffset = 0.0f;
	PerViewUniformParameters.DefaultNormAge = 0.0f;
	PerViewUniformParameters.DefaultSubImage = 0.0f;
	PerViewUniformParameters.DefaultFacing = FVector4(1.0f, 0.0f, 0.0f, 0.0f);
	PerViewUniformParameters.DefaultAlignment = FVector4(1.0f, 0.0f, 0.0f, 0.0f);
	PerViewUniformParameters.DefaultDynamicMaterialParameter0 = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	PerViewUniformParameters.DefaultDynamicMaterialParameter1 = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	PerViewUniformParameters.DefaultDynamicMaterialParameter2 = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	PerViewUniformParameters.DefaultDynamicMaterialParameter3 = FVector4(1.0f, 1.0f, 1.0f, 1.0f);

	TConstArrayView<FNiagaraRendererVariableInfo> VFVariables = RendererLayout->GetVFVariables_RenderThread();
	if (SourceMode == ENiagaraRendererSourceDataMode::Particles)
	{
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
	}
	else if (SourceMode == ENiagaraRendererSourceDataMode::Emitter) // Clear all these out because we will be using the defaults to specify them
	{
		PerViewUniformParameters.PositionDataOffset = INDEX_NONE;
		PerViewUniformParameters.VelocityDataOffset = INDEX_NONE;
		PerViewUniformParameters.RotationDataOffset = INDEX_NONE;
		PerViewUniformParameters.SizeDataOffset = INDEX_NONE;
		PerViewUniformParameters.ColorDataOffset = INDEX_NONE;
		PerViewUniformParameters.MaterialParamDataOffset = INDEX_NONE;
		PerViewUniformParameters.MaterialParam1DataOffset = INDEX_NONE;
		PerViewUniformParameters.MaterialParam2DataOffset = INDEX_NONE;
		PerViewUniformParameters.MaterialParam3DataOffset = INDEX_NONE;
		PerViewUniformParameters.SubimageDataOffset = INDEX_NONE;
		PerViewUniformParameters.FacingDataOffset = INDEX_NONE;
		PerViewUniformParameters.AlignmentDataOffset = INDEX_NONE;
		PerViewUniformParameters.CameraOffsetDataOffset = INDEX_NONE;
		PerViewUniformParameters.UVScaleDataOffset = INDEX_NONE;
		PerViewUniformParameters.NormalizedAgeDataOffset = INDEX_NONE;
		PerViewUniformParameters.MaterialRandomDataOffset = INDEX_NONE;
	}
	else
	{
		// Unsupported source data mode detected
		check(SourceMode <= ENiagaraRendererSourceDataMode::Emitter);
	}

	PerViewUniformParameters.MaterialParamValidMask = MaterialParamValidMask;
	bool bCustomAlignmentSet = false;
	bool bCustomFacingSet = false;

	if (bSetAnyBoundVars && DynamicDataSprites)
	{
		for (int32 i = 0; i < ENiagaraSpriteVFLayout::Type::Num; i++)
		{
			if (VFBoundOffsetsInParamStore[i] != INDEX_NONE && DynamicDataSprites->ParameterDataBound.IsValidIndex(VFBoundOffsetsInParamStore[i]))
			{
				switch (i)
				{
				case ENiagaraSpriteVFLayout::Type::Position:
					memcpy(&PerViewUniformParameters.DefaultPos, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(FVector));
					break;
				case ENiagaraSpriteVFLayout::Type::Color:
					memcpy(&PerViewUniformParameters.DefaultColor, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(FLinearColor));
					break;
				case ENiagaraSpriteVFLayout::Type::Velocity:
					memcpy(&PerViewUniformParameters.DefaultVelocity, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(FVector));
					break;
				case ENiagaraSpriteVFLayout::Type::Rotation:
					memcpy(&PerViewUniformParameters.DefaultRotation, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(float));
					break;
				case ENiagaraSpriteVFLayout::Type::Size:
					memcpy(&PerViewUniformParameters.DefaultSize, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(FVector2D));
					break;
				case ENiagaraSpriteVFLayout::Type::Facing:
					memcpy(&PerViewUniformParameters.DefaultFacing, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(FVector));
					bCustomFacingSet = true;
					break;
				case ENiagaraSpriteVFLayout::Type::Alignment:
					memcpy(&PerViewUniformParameters.DefaultAlignment, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(FVector));
					bCustomAlignmentSet = true;
					break;
				case ENiagaraSpriteVFLayout::Type::SubImage:
					memcpy(&PerViewUniformParameters.DefaultSubImage, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(float));
					break;
				case ENiagaraSpriteVFLayout::Type::MaterialParam0:
					memcpy(&PerViewUniformParameters.DefaultDynamicMaterialParameter0, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(FVector4));
					PerViewUniformParameters.MaterialParamValidMask |= 0x1;
					break;
				case ENiagaraSpriteVFLayout::Type::MaterialParam1:
					memcpy(&PerViewUniformParameters.DefaultDynamicMaterialParameter1, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(FVector4));
					PerViewUniformParameters.MaterialParamValidMask |= 0x2;
					break;
				case ENiagaraSpriteVFLayout::Type::MaterialParam2:
					memcpy(&PerViewUniformParameters.DefaultDynamicMaterialParameter2, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(FVector4));
					PerViewUniformParameters.MaterialParamValidMask |= 0x4;
					break;
				case ENiagaraSpriteVFLayout::Type::MaterialParam3:
					memcpy(&PerViewUniformParameters.DefaultDynamicMaterialParameter3, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(FVector4));
					PerViewUniformParameters.MaterialParamValidMask |= 0x8;
					break;
				case ENiagaraSpriteVFLayout::Type::CameraOffset:
					memcpy(&PerViewUniformParameters.DefaultCamOffset, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(float));
					break;
				case ENiagaraSpriteVFLayout::Type::UVScale:					
					memcpy(&PerViewUniformParameters.DefaultUVScale, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(FVector2D));
					break;
				case ENiagaraSpriteVFLayout::Type::MaterialRandom:	
					memcpy(&PerViewUniformParameters.DefaultMatRandom, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(float));
					break;
				case ENiagaraSpriteVFLayout::Type::CustomSorting:
					// unsupport for now...
					break;
				case ENiagaraSpriteVFLayout::Type::NormalizedAge:
					memcpy(&PerViewUniformParameters.DefaultNormAge, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(float));
					break;
				}
			}
		}			
	}

	PerViewUniformParameters.SubImageBlendMode = bSubImageBlend;

	{
		ENiagaraSpriteFacingMode ActualFacingMode = FacingMode;
		ENiagaraSpriteAlignment ActualAlignmentMode = Alignment;

		const int32 FacingOffset = SourceMode == ENiagaraRendererSourceDataMode::Particles ? PerViewUniformParameters.FacingDataOffset : VFBoundOffsetsInParamStore[ENiagaraSpriteVFLayout::Facing];
		if (FacingOffset == INDEX_NONE && FacingMode == ENiagaraSpriteFacingMode::CustomFacingVector && !bCustomFacingSet)
		{
			ActualFacingMode = ENiagaraSpriteFacingMode::FaceCamera;
		}

		const int32 AlignmentOffset = SourceMode == ENiagaraRendererSourceDataMode::Particles ? PerViewUniformParameters.AlignmentDataOffset : VFBoundOffsetsInParamStore[ENiagaraSpriteVFLayout::Alignment];
		if (AlignmentOffset == INDEX_NONE && ActualAlignmentMode == ENiagaraSpriteAlignment::CustomAlignment && !bCustomAlignmentSet)
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
	}

	return FNiagaraSpriteUniformBufferRef::CreateUniformBufferImmediate(PerViewUniformParameters, UniformBuffer_SingleFrame);
}

void FNiagaraRendererSprites::SetVertexFactoryParticleData(
	FNiagaraSpriteVertexFactory& OutVertexFactory, 
	int32& OutCulledGPUParticleCountOffset,
	FNiagaraDynamicDataSprites* DynamicDataSprites, 
	FCPUSimParticleDataAllocation& CPUSimParticleDataAllocation,
	const FSceneView* View,
	FNiagaraSpriteVFLooseParameters& VFLooseParams,
	const FNiagaraSceneProxy *SceneProxy,
	const FNiagaraRendererLayout* RendererLayout
) const
{
	NiagaraEmitterInstanceBatcher* Batcher = SceneProxy->GetBatcher();
	check(Batcher);

	OutCulledGPUParticleCountOffset = INDEX_NONE;

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

	//Sort particles if needed.
	if (SourceMode == ENiagaraRendererSourceDataMode::Particles)
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderSpritesSorting)

		FMaterialRenderProxy* MaterialRenderProxy = DynamicDataSprites->Material;
		check(MaterialRenderProxy);
		EBlendMode BlendMode = MaterialRenderProxy->GetIncompleteMaterialWithFallback(FeatureLevel).GetBlendMode();
		OutVertexFactory.SetSortedIndices(nullptr, 0xFFFFFFFF);

		const bool bHasTranslucentMaterials = IsTranslucentBlendMode(BlendMode);
		FNiagaraDataBuffer* SourceParticleData = DynamicDataSprites->GetParticleDataToRender(bHasTranslucentMaterials && bGpuLowLatencyTranslucency);
		check(SourceParticleData);//Can be null but should be checked before here.

		const int32 NumInstances = SourceParticleData->GetNumInstances();

		FNiagaraGPUSortInfo SortInfo;
		const bool bShouldCull = bEnableCulling && GNiagaraGPUCulling && FNiagaraUtilities::AllowComputeShaders(Batcher->GetShaderPlatform());
		const bool bShouldSort = SortMode != ENiagaraSortMode::None && (bHasTranslucentMaterials || !bSortOnlyWhenTranslucent);
		const bool bCustomSorting = SortMode == ENiagaraSortMode::CustomAscending || SortMode == ENiagaraSortMode::CustomDecending;
		TConstArrayView<FNiagaraRendererVariableInfo> VFVariables = RendererLayout->GetVFVariables_RenderThread();
		const FNiagaraRendererVariableInfo& SortVariable = VFVariables[bCustomSorting ? ENiagaraSpriteVFLayout::CustomSorting : ENiagaraSpriteVFLayout::Position];
		if (bShouldCull || (bShouldSort && SortVariable.GetGPUOffset() != INDEX_NONE))
		{
			SortInfo.ParticleCount = NumInstances;
			SortInfo.SortMode = SortMode;
			SortInfo.SetSortFlags(GNiagaraGPUSortingUseMaxPrecision != 0, bHasTranslucentMaterials); 
			SortInfo.SortAttributeOffset = bShouldSort ? SortVariable.GetGPUOffset() : INDEX_NONE;
			SortInfo.ViewOrigin = View->ViewMatrices.GetViewOrigin();
			SortInfo.ViewDirection = View->GetViewDirection();
			if (bLocalSpace)
			{
				SortInfo.ViewOrigin = SceneProxy->GetLocalToWorldInverse().TransformPosition(SortInfo.ViewOrigin);
				SortInfo.ViewDirection = SceneProxy->GetLocalToWorld().GetTransposed().TransformVector(SortInfo.ViewDirection);
			}

			if (bShouldCull)
			{
				SortInfo.bEnableCulling = true;
				SortInfo.CullPositionAttributeOffset = VFVariables[ENiagaraSpriteVFLayout::Position].GetGPUOffset();
				SortInfo.RendererVisTagAttributeOffset = bVisTagInParamStore ? INDEX_NONE : RendererVisTagOffset;
				SortInfo.RendererVisibility = RendererVisibility;
				SortInfo.DistanceCullRange = DistanceCullRange;

				OutCulledGPUParticleCountOffset = Batcher->GetGPUInstanceCounterManager().AcquireCulledEntry();
				SortInfo.CulledGPUParticleCountOffset = OutCulledGPUParticleCountOffset;
			}
		}

		if (SimTarget == ENiagaraSimTarget::CPUSim)
		{
			FRHIShaderResourceView* FloatSRV = CPUSimParticleDataAllocation.ParticleData.FloatData.IsValid() ? CPUSimParticleDataAllocation.ParticleData.FloatData.SRV : (FRHIShaderResourceView*)FNiagaraRenderer::GetDummyFloatBuffer();
			FRHIShaderResourceView* HalfSRV = CPUSimParticleDataAllocation.ParticleData.HalfData.IsValid() ? CPUSimParticleDataAllocation.ParticleData.HalfData.SRV : (FRHIShaderResourceView*)FNiagaraRenderer::GetDummyHalfBuffer();
			FRHIShaderResourceView* IntSRV = CPUSimParticleDataAllocation.IntData.IsValid() ? CPUSimParticleDataAllocation.IntData.SRV : (FRHIShaderResourceView*)FNiagaraRenderer::GetDummyIntBuffer();
			const uint32 ParticleFloatDataStride = GbEnableMinimalGPUBuffers ? SourceParticleData->GetNumInstances() : (SourceParticleData->GetFloatStride() / sizeof(float));
			const uint32 ParticleHalfDataStride = GbEnableMinimalGPUBuffers ? SourceParticleData->GetNumInstances() : (SourceParticleData->GetHalfStride() / sizeof(FFloat16));			
			const uint32 ParticleIntDataStride = CPUSimParticleDataAllocation.IntData.IsValid() ? NumInstances : 0; // because we copied it off

			if (bShouldCull || (SortInfo.SortMode != ENiagaraSortMode::None && SortInfo.SortAttributeOffset != INDEX_NONE))
			{
				const int32 Threshold = GNiagaraGPUSortingCPUToGPUThreshold;
				if (bShouldCull || (Threshold >= 0 && SortInfo.ParticleCount >= Threshold && FNiagaraUtilities::AllowComputeShaders(Batcher->GetShaderPlatform())))
				{
					SortInfo.ParticleCount = NumInstances;
					SortInfo.ParticleDataFloatSRV = FloatSRV;
					SortInfo.ParticleDataHalfSRV = HalfSRV;
					SortInfo.ParticleDataIntSRV = IntSRV;
					SortInfo.FloatDataStride = ParticleFloatDataStride;
					SortInfo.HalfDataStride = ParticleHalfDataStride;
					SortInfo.IntDataStride = ParticleIntDataStride;
					SortInfo.GPUParticleCountSRV = Batcher->GetGPUInstanceCounterManager().GetInstanceCountBuffer().SRV;
					SortInfo.GPUParticleCountOffset = SourceParticleData->GetGPUInstanceCountBufferOffset();
					SortInfo.RendererVisTagAttributeOffset = (bVisTagInParamStore || RendererVisTagOffset == INDEX_NONE) ? INDEX_NONE : 0; // because it's copied off
					if (Batcher->AddSortedGPUSimulation(SortInfo))
					{
						OutVertexFactory.SetSortedIndices(SortInfo.AllocationInfo.BufferSRV, SortInfo.AllocationInfo.BufferOffset);
					}
				}
				else
				{
					SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderSpritesGlobalSortCPU);

					FGlobalDynamicReadBuffer::FAllocation SortedIndices;
					SortedIndices = CPUSimParticleDataAllocation.DynamicReadBuffer.AllocateInt32(NumInstances);
					SortIndices(SortInfo, SortVariable, *SourceParticleData, SortedIndices);
					OutVertexFactory.SetSortedIndices(SortedIndices.SRV, 0);
				}
			}
			auto& ParticleData = CPUSimParticleDataAllocation.ParticleData;

			check(ParticleFloatDataStride == ParticleHalfDataStride); // sanity check for the loose params

			VFLooseParams.NiagaraFloatDataStride = ParticleFloatDataStride;
			VFLooseParams.NiagaraParticleDataFloat = FloatSRV;
			VFLooseParams.NiagaraParticleDataHalf = HalfSRV;
		}
		else // ENiagaraSimTarget::GPUSim
		{
			FRHIShaderResourceView* FloatSRV = SourceParticleData->GetGPUBufferFloat().SRV.IsValid() ? (FRHIShaderResourceView*)SourceParticleData->GetGPUBufferFloat().SRV : (FRHIShaderResourceView*)FNiagaraRenderer::GetDummyFloatBuffer();
			FRHIShaderResourceView* HalfSRV = SourceParticleData->GetGPUBufferHalf().SRV.IsValid() ? (FRHIShaderResourceView*)SourceParticleData->GetGPUBufferHalf().SRV : (FRHIShaderResourceView*)FNiagaraRenderer::GetDummyHalfBuffer();
			FRHIShaderResourceView* IntSRV = SourceParticleData->GetGPUBufferInt().SRV.IsValid() ? (FRHIShaderResourceView*)SourceParticleData->GetGPUBufferInt().SRV : (FRHIShaderResourceView*)FNiagaraRenderer::GetDummyIntBuffer();
			const uint32 ParticleFloatDataStride = SourceParticleData->GetFloatStride() / sizeof(float);
			const uint32 ParticleHalfDataStride = SourceParticleData->GetHalfStride() / sizeof(FFloat16);
			const uint32 ParticleIntDataStride = SourceParticleData->GetInt32Stride() / sizeof(int32);

			if (bShouldCull || (SortInfo.SortMode != ENiagaraSortMode::None && SortInfo.SortAttributeOffset != INDEX_NONE))
			{
				// Here we need to be conservative about the InstanceCount, since the final value is only known on the GPU after the simulation.
				SortInfo.ParticleCount = SourceParticleData->GetNumInstances();

				SortInfo.ParticleDataFloatSRV = FloatSRV;
				SortInfo.ParticleDataHalfSRV = HalfSRV;
				SortInfo.ParticleDataIntSRV = IntSRV;
				SortInfo.FloatDataStride = ParticleFloatDataStride;
				SortInfo.HalfDataStride = ParticleHalfDataStride;
				SortInfo.IntDataStride = ParticleIntDataStride;
				SortInfo.GPUParticleCountSRV = Batcher->GetGPUInstanceCounterManager().GetInstanceCountBuffer().SRV;
				SortInfo.GPUParticleCountOffset = SourceParticleData->GetGPUInstanceCountBufferOffset();
				if (Batcher->AddSortedGPUSimulation(SortInfo))
				{
					OutVertexFactory.SetSortedIndices(SortInfo.AllocationInfo.BufferSRV, SortInfo.AllocationInfo.BufferOffset);
				}
			}

			check(ParticleFloatDataStride == ParticleHalfDataStride); // sanity check for the loose params

			VFLooseParams.NiagaraFloatDataStride = ParticleFloatDataStride;
			VFLooseParams.NiagaraParticleDataFloat = FloatSRV;
			VFLooseParams.NiagaraParticleDataHalf = HalfSRV;
		}
	}
	else if (SourceMode == ENiagaraRendererSourceDataMode::Emitter)
	{
		VFLooseParams.NiagaraFloatDataStride = 0;
		VFLooseParams.NiagaraParticleDataFloat = (FRHIShaderResourceView*)FNiagaraRenderer::GetDummyFloatBuffer();
		VFLooseParams.NiagaraParticleDataHalf = (FRHIShaderResourceView*)FNiagaraRenderer::GetDummyHalfBuffer();
	}
}

void FNiagaraRendererSprites::CreateMeshBatchForView(
	const FSceneView* View, 
	const FSceneViewFamily& ViewFamily, 
	const FNiagaraSceneProxy* SceneProxy,
	int32 CulledGPUParticleCountOffset,
	FNiagaraDynamicDataSprites* DynamicDataSprites,
	FMeshBatch& MeshBatch,
	FNiagaraSpriteVFLooseParameters& VFLooseParams,
	FNiagaraMeshCollectorResourcesSprite& CollectorResources,
	const FNiagaraRendererLayout* RendererLayout
) const
{
	FNiagaraDataBuffer* SourceParticleData = DynamicDataSprites->GetParticleDataToRender();
	check(SourceParticleData);//Can be null but should be checked before here.
	int32 NumInstances = SourceMode == ENiagaraRendererSourceDataMode::Particles ? SourceParticleData->GetNumInstances() : 1;
	const bool bIsWireframe = ViewFamily.EngineShowFlags.Wireframe;

	FMaterialRenderProxy* MaterialRenderProxy = DynamicDataSprites->Material;
	check(MaterialRenderProxy);

	ENiagaraSpriteFacingMode ActualFacingMode = FacingMode;
	ENiagaraSpriteAlignment ActualAlignmentMode = Alignment;

	TConstArrayView<FNiagaraRendererVariableInfo> VFVariables = RendererLayout->GetVFVariables_RenderThread();
	{
		int32 FacingOffset = SourceMode == ENiagaraRendererSourceDataMode::Particles ? VFVariables[ENiagaraSpriteVFLayout::Facing].GetGPUOffset() : INDEX_NONE;
		if (FacingOffset == INDEX_NONE)
			FacingOffset = VFBoundOffsetsInParamStore[ENiagaraSpriteVFLayout::Facing];
		if (FacingOffset == INDEX_NONE && FacingMode == ENiagaraSpriteFacingMode::CustomFacingVector )
		{
			ActualFacingMode = ENiagaraSpriteFacingMode::FaceCamera;
		}

		int32 AlignmentOffset = SourceMode == ENiagaraRendererSourceDataMode::Particles ? VFVariables[ENiagaraSpriteVFLayout::Alignment].GetGPUOffset() : INDEX_NONE;
		if (AlignmentOffset == INDEX_NONE)
			AlignmentOffset = VFBoundOffsetsInParamStore[ENiagaraSpriteVFLayout::Alignment];
		if (AlignmentOffset == INDEX_NONE && ActualAlignmentMode == ENiagaraSpriteAlignment::CustomAlignment)
		{
			ActualAlignmentMode = ENiagaraSpriteAlignment::Unaligned;
		}

		CollectorResources.VertexFactory.SetAlignmentMode((uint32)ActualAlignmentMode);
		CollectorResources.VertexFactory.SetFacingMode((uint32)FacingMode);
	}
	CollectorResources.VertexFactory.SetParticleFactoryType(NVFT_Sprite);
	CollectorResources.VertexFactory.InitResource();
	CollectorResources.VertexFactory.SetSpriteUniformBuffer(CollectorResources.UniformBuffer);

	VFLooseParams.NumCutoutVerticesPerFrame = CollectorResources.VertexFactory.GetNumCutoutVerticesPerFrame();
	VFLooseParams.CutoutGeometry = CollectorResources.VertexFactory.GetCutoutGeometrySRV() ? CollectorResources.VertexFactory.GetCutoutGeometrySRV() : GFNiagaraNullCutoutVertexBuffer.VertexBufferSRV.GetReference();
	VFLooseParams.ParticleAlignmentMode = CollectorResources.VertexFactory.GetAlignmentMode();
	VFLooseParams.ParticleFacingMode = CollectorResources.VertexFactory.GetFacingMode();
	VFLooseParams.SortedIndices = CollectorResources.VertexFactory.GetSortedIndicesSRV() ? CollectorResources.VertexFactory.GetSortedIndicesSRV() : GFNiagaraNullSortedIndicesVertexBuffer.VertexBufferSRV.GetReference();
	VFLooseParams.SortedIndicesOffset = CollectorResources.VertexFactory.GetSortedIndicesOffset();

	const bool bGPUCulled = CulledGPUParticleCountOffset != INDEX_NONE;
	uint32 IndirectArgsOffset = INDEX_NONE;
	NiagaraEmitterInstanceBatcher* Batcher = nullptr;
	if (bGPUCulled || (SimTarget == ENiagaraSimTarget::GPUComputeSim && SourceMode == ENiagaraRendererSourceDataMode::Particles))
	{
		Batcher = SceneProxy->GetBatcher();
		check(Batcher);

		int32 CountOffset = bGPUCulled ? CulledGPUParticleCountOffset : SourceParticleData->GetGPUInstanceCountBufferOffset();
		IndirectArgsOffset = Batcher->GetGPUInstanceCounterManager().AddDrawIndirect(CountOffset, NumIndicesPerInstance, 0,
			View->IsInstancedStereoPass(), bGPUCulled);
	}

	if (IndirectArgsOffset != INDEX_NONE)
	{
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
		(SourceMode == ENiagaraRendererSourceDataMode::Particles && SourceParticleData->GetNumInstances() == 0) ||
		GbEnableNiagaraSpriteRendering == 0 ||
		!GSupportsResourceView // Current shader requires SRV to draw properly in all cases.
		)
	{
		return;
	}

	// If the visibility tag comes from a parameter map, so we can evaluate it here and just early out if it doesn't match up
	if (bVisTagInParamStore && DynamicDataSprites->ParameterDataBound.IsValidIndex(RendererVisTagOffset))
	{
		int32 VisTag = 0;
		memcpy(&VisTag, DynamicDataSprites->ParameterDataBound.GetData() + RendererVisTagOffset, sizeof(int32));
		if (RendererVisibility != VisTag)
		{
			return;
		}
	}

#if STATS
	FScopeCycleCounter EmitterStatsCounter(EmitterStatID);
#endif
	FMaterialRenderProxy* MaterialRenderProxy = DynamicDataSprites->Material;
	check(MaterialRenderProxy);
	const EBlendMode BlendMode = MaterialRenderProxy->GetIncompleteMaterialWithFallback(FeatureLevel).GetBlendMode();
	const bool bShouldSort = SortMode != ENiagaraSortMode::None && (BlendMode == BLEND_AlphaComposite || BlendMode == BLEND_AlphaHoldout || BlendMode == BLEND_Translucent || !bSortOnlyWhenTranslucent);
	const bool bNeedCustomSort = bShouldSort && (SortMode == ENiagaraSortMode::CustomAscending || SortMode == ENiagaraSortMode::CustomDecending);
	const bool bNeedsGPUVis = !bVisTagInParamStore && RendererVisTagOffset != INDEX_NONE && GNiagaraGPUCulling && FNiagaraUtilities::AllowComputeShaders(Batcher->GetShaderPlatform());
	const FNiagaraRendererLayout* RendererLayout = bNeedCustomSort ? RendererLayoutWithCustomSort : RendererLayoutWithoutCustomSort;

	FCPUSimParticleDataAllocation CPUSimParticleDataAllocation = ConditionalAllocateCPUSimParticleData(DynamicDataSprites, RendererLayout, Collector.GetDynamicReadBuffer(), bNeedsGPUVis);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			const FSceneView* View = Views[ViewIndex];
			if (View->bIsInstancedStereoEnabled && IStereoRendering::IsStereoEyeView(*View) && !IStereoRendering::IsAPrimaryView(*View))
			{
				// We don't have to generate batches for non-primary views in stereo instance rendering
				continue;
			}

			if (SourceMode == ENiagaraRendererSourceDataMode::Emitter && bEnableDistanceCulling)
			{
				FVector ViewOrigin = View->ViewMatrices.GetViewOrigin();
				FVector RefPosition = SceneProxy->GetLocalToWorld().GetOrigin();
				const int32 BoundPosOffset = VFBoundOffsetsInParamStore[ENiagaraSpriteVFLayout::Type::Position];
				if (BoundPosOffset != INDEX_NONE && DynamicDataSprites->ParameterDataBound.IsValidIndex(BoundPosOffset))
				{
					// retrieve the reference position from the parameter store
					memcpy(&RefPosition, DynamicDataSprites->ParameterDataBound.GetData() + BoundPosOffset, sizeof(FVector));
					if (bLocalSpace)
					{
						RefPosition = SceneProxy->GetLocalToWorld().TransformPosition(RefPosition);
					}
				}

				float DistSquared = FVector::DistSquared(RefPosition, ViewOrigin);
				if (DistSquared < DistanceCullRange.X * DistanceCullRange.X || DistSquared > DistanceCullRange.Y * DistanceCullRange.Y)
				{
					// Distance cull the whole emitter
					continue;
				}
			}

			FNiagaraMeshCollectorResourcesSprite& CollectorResources = Collector.AllocateOneFrameResource<FNiagaraMeshCollectorResourcesSprite>();
			FNiagaraSpriteVFLooseParameters VFLooseParams;
			int32 CulledGPUParticleCountOffset = INDEX_NONE;
			SetVertexFactoryParticleData(CollectorResources.VertexFactory, CulledGPUParticleCountOffset, DynamicDataSprites, CPUSimParticleDataAllocation, View, VFLooseParams, SceneProxy, RendererLayout);
			CollectorResources.UniformBuffer = CreatePerViewUniformBuffer(View, ViewFamily, SceneProxy, RendererLayout, DynamicDataSprites);
			FMeshBatch& MeshBatch = Collector.AllocateMesh();

			CreateMeshBatchForView(View, ViewFamily, SceneProxy, CulledGPUParticleCountOffset, DynamicDataSprites, MeshBatch, VFLooseParams, CollectorResources, RendererLayout);

			Collector.AddMesh(ViewIndex, MeshBatch);
		}
	}
}

#if RHI_RAYTRACING
void FNiagaraRendererSprites::GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context, TArray<FRayTracingInstance>& OutRayTracingInstances, const FNiagaraSceneProxy* SceneProxy)
{
	if (!CVarRayTracingNiagaraSprites.GetValueOnRenderThread())
	{
		return;
	}

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
		(SourceMode == ENiagaraRendererSourceDataMode::Particles && SourceParticleData->GetNumInstancesAllocated() == 0) ||
		(SourceMode == ENiagaraRendererSourceDataMode::Particles && SourceParticleData->GetNumInstances() == 0) ||
		GbEnableNiagaraSpriteRendering == 0 ||
		!GSupportsResourceView // Current shader requires SRV to draw properly in all cases.
		)
	{
		return;
	}

	uint32 NumInstances = SourceMode == ENiagaraRendererSourceDataMode::Particles ? SourceParticleData->GetNumInstances() : 1;

	FRayTracingInstance RayTracingInstance;
	RayTracingInstance.Geometry = &RayTracingGeometry;
	RayTracingInstance.InstanceTransforms.Add(FMatrix::Identity);

	const FNiagaraRendererLayout* RendererLayout = RendererLayoutWithCustomSort;
	const bool bNeedsGPUVis = !bVisTagInParamStore && RendererVisTagOffset != INDEX_NONE && GNiagaraGPUCulling && FNiagaraUtilities::AllowComputeShaders(Batcher->GetShaderPlatform());

	{
		// Setup material for our ray tracing instance
		FCPUSimParticleDataAllocation CPUSimParticleDataAllocation = ConditionalAllocateCPUSimParticleData(DynamicDataSprites, RendererLayout, Context.RayTracingMeshResourceCollector.GetDynamicReadBuffer(), bNeedsGPUVis);
		FNiagaraMeshCollectorResourcesSprite& CollectorResources = Context.RayTracingMeshResourceCollector.AllocateOneFrameResource<FNiagaraMeshCollectorResourcesSprite>();
		FNiagaraSpriteVFLooseParameters VFLooseParams;
		int32 CulledGPUParticleCountOffset = INDEX_NONE;
		SetVertexFactoryParticleData(CollectorResources.VertexFactory, CulledGPUParticleCountOffset, DynamicDataSprites, CPUSimParticleDataAllocation, Context.ReferenceView, VFLooseParams, SceneProxy, RendererLayout);
		CollectorResources.UniformBuffer = CreatePerViewUniformBuffer(Context.ReferenceView, Context.ReferenceViewFamily, SceneProxy, RendererLayout, DynamicDataSprites);
		FMeshBatch MeshBatch;
		CreateMeshBatchForView(Context.ReferenceView, Context.ReferenceViewFamily, SceneProxy, CulledGPUParticleCountOffset, DynamicDataSprites, MeshBatch, VFLooseParams, CollectorResources, RendererLayout);

		RayTracingInstance.Materials.Add(MeshBatch);

		// USe the internal vertex buffer only when initialized otherwise used the shared vertex buffer - needs to be updated every frame
		FRWBuffer* VertexBuffer = RayTracingDynamicVertexBuffer.NumBytes > 0 ? &RayTracingDynamicVertexBuffer : nullptr;

		// Different numbers of cutout vertices correspond to different index buffers
		// For 8 verts, use GSixTriangleParticleIndexBuffer
		// For 4 verts cutout geometry and normal particle geometry, use the typical 6 indices
		const int32 NumVerticesPerInstance = NumCutoutVertexPerSubImage == 8 ? 18 : 6;
		const int32 NumTrianglesPerInstance = NumCutoutVertexPerSubImage == 8 ? 6 : 2;

		// Update dynamic ray tracing geometry
		Context.DynamicRayTracingGeometriesToUpdate.Add(
			FRayTracingDynamicGeometryUpdateParams
			{
				RayTracingInstance.Materials,
				MeshBatch.Elements[0].NumPrimitives == 0,
				NumVerticesPerInstance* NumInstances,
				NumVerticesPerInstance* NumInstances* (uint32)sizeof(FVector),
				NumTrianglesPerInstance * NumInstances,
				&RayTracingGeometry,
				VertexBuffer,
				true
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

		FNiagaraDataBuffer* DataToRender = Emitter->GetData().GetCurrentData();
		if(SimTarget == ENiagaraSimTarget::GPUComputeSim || (DataToRender != nullptr &&  (SourceMode == ENiagaraRendererSourceDataMode::Emitter || (SourceMode == ENiagaraRendererSourceDataMode::Particles && DataToRender->GetNumInstances() > 0))))
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

		if (DynamicData)
		{
			const FNiagaraParameterStore& ParameterData = Emitter->GetRendererBoundVariables();
			DynamicData->DataInterfacesBound = ParameterData.GetDataInterfaces();
			DynamicData->ObjectsBound = ParameterData.GetUObjects();
			DynamicData->ParameterDataBound = ParameterData.GetParameterDataArray();
		}

		if (DynamicData && Properties->MaterialParameterBindings.Num() != 0)
		{
			ProcessMaterialParameterBindings(MakeArrayView(Properties->MaterialParameterBindings), Emitter, MakeArrayView(BaseMaterials_GT));
		}
	}

	return DynamicData;  // for VF that can fetch from particle data directly
}

int FNiagaraRendererSprites::GetDynamicDataSize()const
{
	uint32 Size = sizeof(FNiagaraDynamicDataSprites);
	return Size;
}

bool FNiagaraRendererSprites::IsMaterialValid(const UMaterialInterface* Mat)const
{
	return Mat && Mat->CheckMaterialUsage_Concurrent(MATUSAGE_NiagaraSprites);
}