// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraRendererRibbons.h"
#include "ParticleResources.h"
#include "NiagaraRibbonVertexFactory.h"
#include "NiagaraDataSet.h"
#include "NiagaraStats.h"
#include "RayTracingDefinitions.h"
#include "RayTracingDynamicGeometryCollection.h"
#include "RayTracingInstance.h"

DECLARE_CYCLE_STAT(TEXT("Generate Ribbon Vertex Data [GT]"), STAT_NiagaraGenRibbonVertexData, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Render Ribbons [RT]"), STAT_NiagaraRenderRibbons, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Render Ribbons - CPU Sim Copy[RT]"), STAT_NiagaraRenderRibbonsCPUSimCopy, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Render Ribbons - CPU Sim Memcopy[RT]"), STAT_NiagaraRenderRibbonsCPUSimMemCopy, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Genereate GPU Buffers"), STAT_NiagaraGenRibbonGpuBuffers, STATGROUP_Niagara);

float GNiagaraRibbonTessellationAngle = 15.f * (2.f * PI) / 360.f; // Every 15 degrees
static FAutoConsoleVariableRef CVarNiagaraRibbonTessellationAngle(
	TEXT("Niagara.Ribbon.Tessellation.MinAngle"),
	GNiagaraRibbonTessellationAngle,
	TEXT("Ribbon segment angle to tesselate in radian. (default=15 degrees)"),
	ECVF_Scalability
);

int32 GNiagaraRibbonMaxTessellation = 16;
static FAutoConsoleVariableRef CVarNiagaraRibbonMaxTessellation(
	TEXT("Niagara.Ribbon.Tessellation.MaxInterp"),
	GNiagaraRibbonMaxTessellation,
	TEXT("When TessellationAngle is > 0, this is the maximum tesselation factor. \n")
	TEXT("Higher values allow more evenly divided tesselation. \n")
	TEXT("When TessellationAngle is 0, this is the actually tesselation factor (default=16)."),
	ECVF_Scalability
);

float GNiagaraRibbonTessellationScreenPercentage = 0.002f;
static FAutoConsoleVariableRef CVarNiagaraRibbonTessellationScreenPercentage(
	TEXT("Niagara.Ribbon.Tessellation.MaxErrorScreenPercentage"),
	GNiagaraRibbonTessellationScreenPercentage,
	TEXT("Screen percentage used to compute the tessellation factor. \n")
	TEXT("Smaller values will generate more tessellation, up to max tesselltion. (default=0.002)"),
	ECVF_Scalability
);

float GNiagaraRibbonTessellationMinDisplacementError = 0.5f;
static FAutoConsoleVariableRef CVarNiagaraRibbonTessellationMinDisplacementError(
	TEXT("Niagara.Ribbon.Tessellation.MinAbsoluteError"),
	GNiagaraRibbonTessellationMinDisplacementError,
	TEXT("Minimum absolute world size error when tessellating. \n")
	TEXT("Prevent over tessellating when distance gets really small. (default=0.5)"),
	ECVF_Scalability
);

float GNiagaraRibbonMinSegmentLength = 1.f;
static FAutoConsoleVariableRef CVarNiagaraRibbonMinSegmentLength(
	TEXT("Niagara.Ribbon.MinSegmentLength"),
	GNiagaraRibbonMinSegmentLength,
	TEXT("Min length of niagara ribbon segments. (default=1)"),
	ECVF_Scalability
);

static int32 GbEnableNiagaraRibbonRendering = 1;
static FAutoConsoleVariableRef CVarEnableNiagaraRibbonRendering(
	TEXT("fx.EnableNiagaraRibbonRendering"),
	GbEnableNiagaraRibbonRendering,
	TEXT("If == 0, Niagara Ribbon Renderers are disabled. \n"),
	ECVF_Default
);

// max absolute error 9.0x10^-3
// Eberly's polynomial degree 1 - respect bounds
// input [-1, 1] and output [0, PI]
FORCEINLINE float AcosFast(float InX)
{
	float X = FMath::Abs(InX);
	float Res = -0.156583f * X + (0.5 * PI);
	Res *= sqrt(FMath::Max(0.f, 1.0f - X));
	return (InX >= 0) ? Res : PI - Res;
}

struct FNiagaraDynamicDataRibbon : public FNiagaraDynamicDataBase
{
	FNiagaraDynamicDataRibbon(const FNiagaraEmitterInstance* InEmitter)
		: FNiagaraDynamicDataBase(InEmitter)
		, Material(nullptr)
	{
	}

	/** Material to use passed to the Renderer. */
	FMaterialRenderProxy* Material;

	// The list of all segments, each one connecting SortedIndices[SegmentId] to SortedIndices[SegmentId + 1].
	// We use this format because the final index buffer gets generated based on view sorting and InterpCount.
	TArray<int32> SegmentData;
	TArray<int32> SortedIndices;
	TArray<FVector4> TangentAndDistances;
	TArray<uint32> MultiRibbonIndices;
	TArray<float> PackedPerRibbonDataByIndex;

	// start and end world space position of the ribbon, to figure out draw direction
	FVector StartPos;
	FVector EndPos;

	void PackPerRibbonData(float U0Scale, float U0Offset, float U1Scale, float U1Offset, uint32 NumSegments, uint32 FirstParticleId)
	{
		const float OneOverNumSegments = 1 / (float)FMath::Max<uint32>(1, NumSegments);
		PackedPerRibbonDataByIndex.Add(U0Scale);
		PackedPerRibbonDataByIndex.Add(U0Offset);
		PackedPerRibbonDataByIndex.Add(U1Scale);
		PackedPerRibbonDataByIndex.Add(U1Offset);
		PackedPerRibbonDataByIndex.Add(OneOverNumSegments);
		PackedPerRibbonDataByIndex.Add(*(float*)&FirstParticleId);
	}
};

class FNiagaraMeshCollectorResourcesRibbon : public FOneFrameResource
{
public:
	FNiagaraRibbonVertexFactory VertexFactory;
	FNiagaraRibbonUniformBufferRef UniformBuffer;

	virtual ~FNiagaraMeshCollectorResourcesRibbon()
	{
		VertexFactory.ReleaseResource();
	}
};

namespace ENiagaraRibbonVFLayout
{
	enum Type
	{
		Position,
		Velocity,
		Color,
		Width,
		Twist,
		Facing,
		NormalizedAge,
		MaterialRandom,
		MaterialParam0,
		MaterialParam1,
		MaterialParam2,
		MaterialParam3,
		Num,
	};
};


FNiagaraRendererRibbons::FNiagaraRendererRibbons(ERHIFeatureLevel::Type FeatureLevel, const UNiagaraRendererProperties *InProps, const FNiagaraEmitterInstance* Emitter)
	: FNiagaraRenderer(FeatureLevel, InProps, Emitter)
	, FacingMode(ENiagaraRibbonFacingMode::Screen)
	, UV0TilingDistance(0.0f)
	, UV0Scale(FVector2D(1.0f, 1.0f))
	, UV0AgeOffsetMode(ENiagaraRibbonAgeOffsetMode::Scale)
	, UV1TilingDistance(0.0f)
	, UV1Scale(FVector2D(1.0f, 1.0f))
	, UV1AgeOffsetMode(ENiagaraRibbonAgeOffsetMode::Scale)
	, TessellationMode(ENiagaraRibbonTessellationMode::Automatic)
	, CustomCurveTension(0.f)
	, CustomTessellationFactor(16)
	, bCustomUseConstantFactor(false)
	, CustomTessellationMinAngle(15.f * PI / 180.f)
	, bCustomUseScreenSpace(true)
{
	const UNiagaraRibbonRendererProperties* Properties = CastChecked<const UNiagaraRibbonRendererProperties>(InProps);
	const FNiagaraDataSet& Data = Emitter->GetData();

	FacingMode = Properties->FacingMode;
	UV0TilingDistance = Properties->UV0TilingDistance;
	UV0Scale = Properties->UV0Scale;
	UV0Offset = Properties->UV0Offset;
	UV0AgeOffsetMode = Properties->UV0AgeOffsetMode;
	UV1TilingDistance = Properties->UV1TilingDistance;
	UV1Scale = Properties->UV1Scale;
	UV1Offset = Properties->UV1Offset;
	UV1AgeOffsetMode = Properties->UV1AgeOffsetMode;
	DrawDirection = Properties->DrawDirection;
	TessellationMode = Properties->TessellationMode;
	CustomCurveTension = FMath::Clamp<float>(Properties->CurveTension, 0.f, 0.9999f);
	CustomTessellationFactor = Properties->TessellationFactor;
	bCustomUseConstantFactor = Properties->bUseConstantFactor;
	CustomTessellationMinAngle = Properties->TessellationAngle > 0.f && Properties->TessellationAngle < 1.f ? 1.f : Properties->TessellationAngle;
	CustomTessellationMinAngle *= PI / 180.f;
	bCustomUseScreenSpace = Properties->bScreenSpaceTessellation;

	TotalVFComponents = 0;
	VFVariables.SetNum(ENiagaraRibbonVFLayout::Num);
	// required attributes
	SetVertexFactoryVariable(Data, Properties->PositionBinding.DataSetVariable, ENiagaraRibbonVFLayout::Position);
	SetVertexFactoryVariable(Data, Properties->VelocityBinding.DataSetVariable, ENiagaraRibbonVFLayout::Velocity);
	SetVertexFactoryVariable(Data, Properties->ColorBinding.DataSetVariable, ENiagaraRibbonVFLayout::Color);

	// optional attributes
	SetVertexFactoryVariable(Data, Properties->RibbonWidthBinding.DataSetVariable, ENiagaraRibbonVFLayout::Width);
	SetVertexFactoryVariable(Data, Properties->RibbonTwistBinding.DataSetVariable, ENiagaraRibbonVFLayout::Twist);
	SetVertexFactoryVariable(Data, Properties->RibbonFacingBinding.DataSetVariable, ENiagaraRibbonVFLayout::Facing);
	SetVertexFactoryVariable(Data, Properties->NormalizedAgeBinding.DataSetVariable, ENiagaraRibbonVFLayout::NormalizedAge);
	SetVertexFactoryVariable(Data, Properties->MaterialRandomBinding.DataSetVariable, ENiagaraRibbonVFLayout::MaterialRandom);

	MaterialParamValidMask = 0;
	MaterialParamValidMask |= SetVertexFactoryVariable(Data, Properties->DynamicMaterialBinding.DataSetVariable, ENiagaraRibbonVFLayout::MaterialParam0) ? 1 : 0;
	MaterialParamValidMask |= SetVertexFactoryVariable(Data, Properties->DynamicMaterial1Binding.DataSetVariable, ENiagaraRibbonVFLayout::MaterialParam1) ? 2 : 0;
	MaterialParamValidMask |= SetVertexFactoryVariable(Data, Properties->DynamicMaterial2Binding.DataSetVariable, ENiagaraRibbonVFLayout::MaterialParam2) ? 4 : 0;
	MaterialParamValidMask |= SetVertexFactoryVariable(Data, Properties->DynamicMaterial3Binding.DataSetVariable, ENiagaraRibbonVFLayout::MaterialParam3) ? 8 : 0;
}

FNiagaraRendererRibbons::~FNiagaraRendererRibbons()
{
}

void FNiagaraRendererRibbons::ReleaseRenderThreadResources()
{
	FNiagaraRenderer::ReleaseRenderThreadResources();
#if RHI_RAYTRACING
	if (IsRayTracingEnabled())
	{
		RayTracingGeometry.ReleaseResource();
		RayTracingDynamicVertexBuffer.Release();
	}
#endif
}

// FPrimitiveSceneProxy interface.
void FNiagaraRendererRibbons::CreateRenderThreadResources(NiagaraEmitterInstanceBatcher* Batcher)
{
	FNiagaraRenderer::CreateRenderThreadResources(Batcher);
#if RHI_RAYTRACING
	if (IsRayTracingEnabled())
	{
		RayTracingDynamicVertexBuffer.Initialize(4, 256, PF_R32_FLOAT, BUF_UnorderedAccess | BUF_ShaderResource, TEXT("RayTracingDynamicVertexBuffer"));

		FRayTracingGeometryInitializer Initializer;
		Initializer.IndexBuffer = nullptr;
		Initializer.TotalPrimitiveCount = 0;
		Initializer.GeometryType = RTGT_Triangles;
		Initializer.bFastBuild = true;
		Initializer.bAllowUpdate = false;
		RayTracingGeometry.SetInitializer(Initializer);
		RayTracingGeometry.InitResource();
	}
#endif
}

void FNiagaraRendererRibbons::GenerateIndexBuffer(uint16* OutIndices, uint16& outVertexCount, const TArray<int32>& SegmentData, int32 InterpCount, bool bInvertOrder)
{
	outVertexCount = 0;
	auto AddTriangleIndices = [&outVertexCount, &OutIndices, InterpCount](int32 SegmentIndex)
	{
		for (int32 SubSegmentIndex = 0; SubSegmentIndex < InterpCount; ++SubSegmentIndex)
		{
			const uint16 BaseVertexIndex = (int16)(SegmentIndex * InterpCount + SubSegmentIndex) * 2;
			OutIndices[0] = BaseVertexIndex + 0;
			OutIndices[1] = BaseVertexIndex + 1;
			OutIndices[2] = BaseVertexIndex + 2;
			OutIndices[3] = BaseVertexIndex + 1;
			OutIndices[4] = BaseVertexIndex + 3;
			OutIndices[5] = BaseVertexIndex + 2;

			outVertexCount = FMath::Max<uint16>(outVertexCount, BaseVertexIndex + 4u);
			OutIndices += 6;
		}
	};

	// If per view sorting is required, generate sort keys and sort segment indices.
	if (!bInvertOrder)
	{
		for (int32 SegmentDataIndex = 0; SegmentDataIndex < SegmentData.Num(); ++SegmentDataIndex)
		{
			AddTriangleIndices(SegmentData[SegmentDataIndex]);
		}
	}
	else
	{
		for (int32 SegmentDataIndex = SegmentData.Num() - 1; SegmentDataIndex >= 0; --SegmentDataIndex)
		{
			AddTriangleIndices(SegmentData[SegmentDataIndex]);
		}
	}
}

void FNiagaraRendererRibbons::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector, const FNiagaraSceneProxy *SceneProxy) const
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraRender);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderRibbons);
	PARTICLE_PERF_STAT_CYCLES(SceneProxy->PerfAsset, GetDynamicMeshElements);

	FNiagaraDynamicDataRibbon *DynamicDataRibbon = static_cast<FNiagaraDynamicDataRibbon*>(DynamicDataRender);
	if (!DynamicDataRibbon)
	{
		return;
	}

	FNiagaraDataBuffer* SourceParticleData = DynamicDataRibbon->GetParticleDataToRender();
	if (!SourceParticleData ||
		SourceParticleData->GetNumInstances() < 2 ||
		DynamicDataRibbon->SegmentData.Num() == 0 ||
		GbEnableNiagaraRibbonRendering == 0 ||
		!GSupportsResourceView // Current shader requires SRV to draw properly in all cases.
		)
	{
		return;
	}

#if STATS
	FScopeCycleCounter EmitterStatsCounter(EmitterStatID);
#endif

	// Compute the per-view uniform buffers.
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			const FSceneView* View = Views[ViewIndex];
			check(View);

			FMeshBatch& MeshBatch = Collector.AllocateMesh();

			FGlobalDynamicIndexBuffer::FAllocation DynamicIndexAllocation;
			FNiagaraMeshCollectorResourcesRibbon& CollectorResources = Collector.AllocateOneFrameResource<FNiagaraMeshCollectorResourcesRibbon>();

			uint16 VertexCount;
			uint32 NumPrimitives;
			CreatePerViewResources(View, ViewFamily, SceneProxy, Collector,
				VertexCount, NumPrimitives, CollectorResources.UniformBuffer, DynamicIndexAllocation);

			SetupMeshBatchAndCollectorResourceForView(
				View, ViewFamily, SceneProxy, Collector,
				DynamicDataRibbon, NumPrimitives, DynamicIndexAllocation, MeshBatch, CollectorResources);

			Collector.AddMesh(ViewIndex, MeshBatch);
		}
	}
}

int FNiagaraRendererRibbons::GetDynamicDataSize()const
{
	uint32 Size = sizeof(FNiagaraDynamicDataRibbon);
	if (DynamicDataRender)
	{
		FNiagaraDynamicDataRibbon* RibbonDynamicData = static_cast<FNiagaraDynamicDataRibbon*>(DynamicDataRender);
		Size += RibbonDynamicData->SegmentData.GetAllocatedSize();
		Size += RibbonDynamicData->SortedIndices.GetAllocatedSize();
		Size += RibbonDynamicData->TangentAndDistances.GetAllocatedSize();
		Size += RibbonDynamicData->MultiRibbonIndices.GetAllocatedSize();
		Size += RibbonDynamicData->PackedPerRibbonDataByIndex.GetAllocatedSize();
	}

	return Size;
}

void CalculateUVScaleAndOffsets(
	const FNiagaraDataSetAccessor<float>& SortKeyData, const TArray<int32>& RibbonIndices,
	bool bSortKeyIsAge, int32 StartIndex, int32 EndIndex, int32 NumSegments,
	float InUTilingDistance, float InUScale, float InUOffset, ENiagaraRibbonAgeOffsetMode InAgeOffsetMode,
	float& OutUScale, float& OutUOffset)
{
	if (EndIndex - StartIndex > 0 && bSortKeyIsAge && InUTilingDistance == 0)
	{
		float AgeUScale;
		float AgeUOffset;
		if (InAgeOffsetMode == ENiagaraRibbonAgeOffsetMode::Scale)
		{
			// In scale mode we scale and offset the UVs so that no part of the texture is clipped. In order to prevent
			// clipping at the ends we'll have to move the UVs in up to the size of a single segment of the ribbon since
			// that's the distance we'll need to to smoothly interpolate when a new segment is added, or when an old segment
			// is removed.  We calculate the end offset when the end of the ribbon is within a single time step of 0 or 1
			// which is then normalized to the range of a single segment.  We can then calculate how many segments we actually
			// have to draw the scaled ribbon, and can offset the start by the correctly scaled offset.
			float FirstAge = SortKeyData[RibbonIndices[StartIndex]];
			float SecondAge = SortKeyData[RibbonIndices[StartIndex + 1]];
			float SecondToLastAge = SortKeyData[RibbonIndices[EndIndex - 1]];
			float LastAge = SortKeyData[RibbonIndices[EndIndex]];

			float StartTimeStep = SecondAge - FirstAge;
			float StartTimeOffset = FirstAge < StartTimeStep ? StartTimeStep - FirstAge : 0;
			float StartSegmentOffset = StartTimeOffset / StartTimeStep;

			float EndTimeStep = LastAge - SecondToLastAge;
			float EndTimeOffset = 1 - LastAge < EndTimeStep ? EndTimeStep - (1 - LastAge) : 0;
			float EndSegmentOffset = EndTimeOffset / EndTimeStep;

			float AvailableSegments = NumSegments - (StartSegmentOffset + EndSegmentOffset);
			AgeUScale = NumSegments / AvailableSegments;
			AgeUOffset = -((StartSegmentOffset / NumSegments) * AgeUScale);
		}
		else
		{
			float FirstAge = SortKeyData[RibbonIndices[StartIndex]];
			float LastAge = SortKeyData[RibbonIndices[EndIndex]];

			AgeUScale = LastAge - FirstAge;
			AgeUOffset = FirstAge;
		}

		OutUScale = AgeUScale * InUScale;
		OutUOffset = (AgeUOffset * InUScale) + InUOffset;
	}
	else
	{
		OutUScale = InUScale;
		OutUOffset = InUOffset;
	}
}

FNiagaraDynamicDataBase* FNiagaraRendererRibbons::GenerateDynamicData(const FNiagaraSceneProxy* Proxy, const UNiagaraRendererProperties* InProperties, const FNiagaraEmitterInstance* Emitter)const
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraGenRibbonVertexData);

	if (SimTarget == ENiagaraSimTarget::GPUComputeSim)
	{
		return nullptr;
	}

	FNiagaraDataSet& Data = Emitter->GetData();
	const UNiagaraRibbonRendererProperties* Properties = CastChecked<const UNiagaraRibbonRendererProperties>(InProperties);

	bool bSortKeyIsAge = false;
	FNiagaraDataSetAccessor<float> SortKeyData(Data, Properties->RibbonLinkOrderBinding.DataSetVariable);
	if (SortKeyData.IsValid() == false)
	{
		SortKeyData = FNiagaraDataSetAccessor<float>(Data, Properties->NormalizedAgeBinding.DataSetVariable);
		bSortKeyIsAge = true;
	}

	FNiagaraDataSetAccessor<FVector> PosData(Data, Properties->PositionBinding.DataSetVariable);
	FNiagaraDataSetAccessor<float> SizeData(Data, Properties->RibbonWidthBinding.DataSetVariable);
	FNiagaraDataSetAccessor<float> TwistData(Data, Properties->RibbonTwistBinding.DataSetVariable);
	FNiagaraDataSetAccessor<FVector> FacingData(Data, Properties->RibbonFacingBinding.DataSetVariable);
	FNiagaraDataSetAccessor<FVector4> MaterialParamData(Data, Properties->DynamicMaterialBinding.DataSetVariable);
	FNiagaraDataSetAccessor<FVector4> MaterialParam1Data(Data, Properties->DynamicMaterial1Binding.DataSetVariable);
	FNiagaraDataSetAccessor<FVector4> MaterialParam2Data(Data, Properties->DynamicMaterial2Binding.DataSetVariable);
	FNiagaraDataSetAccessor<FVector4> MaterialParam3Data(Data, Properties->DynamicMaterial3Binding.DataSetVariable);

	FNiagaraDataSetAccessor<int32> RibbonIdData;
	FNiagaraDataSetAccessor<FNiagaraID> RibbonFullIDData;

	FNiagaraDataBuffer* DataToRender = Emitter->GetData().GetCurrentData();
	if (DataToRender == nullptr || DataToRender->GetNumInstances() < 2 || !PosData.IsValid() || !SortKeyData.IsValid())
	{
		return nullptr;
	}

	FNiagaraDynamicDataRibbon* DynamicData = new FNiagaraDynamicDataRibbon(Emitter);

	//In preparation for a material override feature, we pass our material(s) and relevance in via dynamic data.
	//The renderer ensures we have the correct usage and relevance for materials in BaseMaterials_GT.
	//Any override feature must also do the same for materials that are set.
	check(BaseMaterials_GT.Num() == 1);
	check(BaseMaterials_GT[0]->CheckMaterialUsage_Concurrent(MATUSAGE_NiagaraRibbons));
	DynamicData->Material = BaseMaterials_GT[0]->GetRenderProxy();
	DynamicData->SetMaterialRelevance(BaseMaterialRelevance_GT);

	TArray<int32>& SegmentData = DynamicData->SegmentData;
	float TotalSegmentLength = 0;
	// weighted sum based on the segment length :
	float AverageSegmentLength = 0;
	float AverageSegmentAngle = 0;
	float AverageTwistAngle = 0;
	float AverageWidth = 0;

	if (Properties->RibbonIdBinding.DataSetVariable.GetType() == FNiagaraTypeDefinition::GetIDDef())
	{
		RibbonFullIDData.Create(&Data, Properties->RibbonIdBinding.DataSetVariable);
		RibbonFullIDData.InitForAccess();
	}
	else
	{
		RibbonIdData.Create(&Data, Properties->RibbonIdBinding.DataSetVariable);
		RibbonIdData.InitForAccess();
	}

	bool bFullIDs = RibbonFullIDData.IsValid();
	bool bSimpleIDs = !bFullIDs && RibbonIdData.IsValid();
	bool bMultiRibbons = bFullIDs || bSimpleIDs;
	bool bHasTwist = TwistData.IsValid() && SizeData.IsValid();

	auto AddRibbonVerts = [&](TArray<int32>& RibbonIndices, uint32 RibbonIndex)
	{
		const int32 StartIndex = DynamicData->SortedIndices.Num();

		float TotalDistance = 0.0f;

		const FVector FirstPos = PosData[RibbonIndices[0]];
		FVector CurrPos = FirstPos;
		FVector LastToCurrVec = FVector::ZeroVector;
		float LastToCurrSize = 0;
		float LastTwist = 0;
		float LastWidth = 0;

		// Find the first position with enough distance.
		int32 CurrentIndex = 1;
		while (CurrentIndex < RibbonIndices.Num())
		{
			const int32 CurrentDataIndex = RibbonIndices[CurrentIndex];
			CurrPos = PosData[CurrentDataIndex];
			LastToCurrVec = CurrPos - FirstPos;
			LastToCurrSize = LastToCurrVec.Size();
			if (bHasTwist)
			{
				LastTwist = TwistData[CurrentDataIndex];
				LastWidth = SizeData[CurrentDataIndex];
			}

			// Find the first segment, or unique segment
			if (LastToCurrSize > GNiagaraRibbonMinSegmentLength)
			{
				// Normalize LastToCurrVec
				LastToCurrVec *= 1.f / LastToCurrSize;

				// Add the first point. Tangent follows first segment.
				DynamicData->SortedIndices.Add(RibbonIndices[0]);
				DynamicData->TangentAndDistances.Add(FVector4(LastToCurrVec.X, LastToCurrVec.Y, LastToCurrVec.Z, 0));
				DynamicData->MultiRibbonIndices.Add(RibbonIndex);
				break;
			}
			else
			{
				LastToCurrSize = 0; // Ensure that the segment gets ignored if too small
				++CurrentIndex;
			}
		}

		// Now iterate on all other points, to proceed each particle connected to 2 segments.
		int32 NextIndex = CurrentIndex + 1;
		while (NextIndex < RibbonIndices.Num())
		{
			const int32 NextDataIndex = RibbonIndices[NextIndex];
			const FVector NextPos = PosData[NextDataIndex];
			FVector CurrToNextVec = NextPos - CurrPos;
			const float CurrToNextSize = CurrToNextVec.Size();

			float NextTwist = 0;
			float NextWidth = 0;
			if (bHasTwist)
			{
				NextTwist = TwistData[NextDataIndex];
				NextWidth = SizeData[NextDataIndex];
			}

			// It the next is far enough, or the last element
			if (CurrToNextSize > GNiagaraRibbonMinSegmentLength || NextIndex == RibbonIndices.Num() - 1)
			{
				// Normalize CurrToNextVec
				CurrToNextVec *= 1.f / FMath::Max(GNiagaraRibbonMinSegmentLength, CurrToNextSize);
				const FVector Tangent = (1.f - CustomCurveTension) * (LastToCurrVec + CurrToNextVec).GetSafeNormal();

				// Update the distance for CurrentIndex.
				TotalDistance += LastToCurrSize;

				// Add the current point, which tangent is computed from neighbors
				DynamicData->SortedIndices.Add(RibbonIndices[CurrentIndex]);
				DynamicData->TangentAndDistances.Add(FVector4(Tangent.X, Tangent.Y, Tangent.Z, TotalDistance));
				DynamicData->MultiRibbonIndices.Add(RibbonIndex);

				// Assumed equal to dot(Tangent, CurrToNextVec)
				TotalSegmentLength += CurrToNextSize;
				AverageSegmentLength += CurrToNextSize * CurrToNextSize;
				AverageSegmentAngle += CurrToNextSize * AcosFast(FVector::DotProduct(LastToCurrVec, CurrToNextVec));
				AverageTwistAngle += FMath::Abs(NextTwist - LastTwist) * CurrToNextSize;
				AverageWidth += LastWidth * CurrToNextSize;

				// Move to next segment.
				CurrentIndex = NextIndex;
				CurrPos = NextPos;
				LastToCurrVec = CurrToNextVec;
				LastToCurrSize = CurrToNextSize;
				LastTwist = NextTwist;
				LastWidth = NextWidth;
			}

			// Try next if there is one.
			++NextIndex;
		}

		// Close the last point and segment if there was at least 2.
		if (LastToCurrSize > 0)
		{
			// Update the distance for CurrentIndex.
			TotalDistance += LastToCurrSize;

			// Add the last point, which tangent follows the last segment.
			DynamicData->SortedIndices.Add(RibbonIndices[CurrentIndex]);
			DynamicData->TangentAndDistances.Add(FVector4(LastToCurrVec.X, LastToCurrVec.Y, LastToCurrVec.Z, TotalDistance));
			DynamicData->MultiRibbonIndices.Add(RibbonIndex);
		}

		const int32 EndIndex = DynamicData->SortedIndices.Num() - 1;
		const int32 NumSegments = EndIndex - StartIndex;

		if (NumSegments > 0)
		{
			// Update the tangents for the first and last vertex, apply a reflect vector logic so that the initial and final curvature is continuous.
			if (NumSegments > 1)
			{
				FVector& FirstTangent = reinterpret_cast<FVector&>(DynamicData->TangentAndDistances[StartIndex]);
				FVector& NextToFirstTangent = reinterpret_cast<FVector&>(DynamicData->TangentAndDistances[StartIndex + 1]);
				FirstTangent = (2.f * FVector::DotProduct(FirstTangent, NextToFirstTangent)) * FirstTangent - NextToFirstTangent;

				FVector& LastTangent = reinterpret_cast<FVector&>(DynamicData->TangentAndDistances[EndIndex]);
				FVector& PrevToLastTangent = reinterpret_cast<FVector&>(DynamicData->TangentAndDistances[EndIndex - 1]);
				LastTangent = (2.f * FVector::DotProduct(LastTangent, PrevToLastTangent)) * LastTangent - PrevToLastTangent;
			}

			// Add segment data
			for (int32 SegmentIndex = StartIndex; SegmentIndex < EndIndex; ++SegmentIndex)
			{
				SegmentData.Add(SegmentIndex);
			}

			float U0Offset, U0Scale, U1Offset, U1Scale;

			CalculateUVScaleAndOffsets(SortKeyData, DynamicData->SortedIndices, bSortKeyIsAge, StartIndex, DynamicData->SortedIndices.Num() - 1, NumSegments,
				Properties->UV0TilingDistance, Properties->UV0Scale.X, Properties->UV0Offset.X, Properties->UV0AgeOffsetMode, U0Scale, U0Offset);
			CalculateUVScaleAndOffsets(SortKeyData, DynamicData->SortedIndices, bSortKeyIsAge, StartIndex, DynamicData->SortedIndices.Num() - 1, NumSegments,
				Properties->UV1TilingDistance, Properties->UV1Scale.X, Properties->UV1Offset.X, Properties->UV1AgeOffsetMode, U1Scale, U1Offset);

			DynamicData->PackPerRibbonData(U0Scale, U0Offset, U1Scale, U1Offset, NumSegments, StartIndex);
		}
	};

	// store the start and end positions for the ribbon for draw distance flipping 
	DynamicData->StartPos = PosData[0];
	DynamicData->EndPos = PosData[Data.GetCurrentDataChecked().GetNumInstances() - 1];

	//TODO: Move sorting to share code with sprite and mesh sorting and support the custom sorting key.
	int32 TotalIndices = Data.GetCurrentDataChecked().GetNumInstances();

	if (!bMultiRibbons)
	{
		TArray<int32> SortedIndices;
		for (int32 i = 0; i < TotalIndices; ++i)
		{
			SortedIndices.Add(i);
		}

		SortedIndices.Sort([&SortKeyData](const int32& A, const int32& B) {	return (SortKeyData[A] < SortKeyData[B]); });

		AddRibbonVerts(SortedIndices, 0);
	}
	else
	{
		if (bFullIDs)
		{
			TMap<FNiagaraID, TArray<int32>> MultiRibbonSortedIndices;

			for (int32 i = 0; i < TotalIndices; ++i)
			{
				TArray<int32>& Indices = MultiRibbonSortedIndices.FindOrAdd(RibbonFullIDData[i]);
				Indices.Add(i);
			}

			uint32 RibbonIndex = 0;
			for (TPair<FNiagaraID, TArray<int32>>& Pair : MultiRibbonSortedIndices)
			{
				TArray<int32>& SortedIndices = Pair.Value;
				SortedIndices.Sort([&SortKeyData](const int32& A, const int32& B) {	return (SortKeyData[A] < SortKeyData[B]); });
				AddRibbonVerts(SortedIndices, RibbonIndex);

				RibbonIndex++;
			};
		}
		else
		{
			//TODO: Remove simple ID path
			check(bSimpleIDs);

			TMap<int32, TArray<int32>> MultiRibbonSortedIndices;

			for (int32 i = 0; i < TotalIndices; ++i)
			{
				TArray<int32>& Indices = MultiRibbonSortedIndices.FindOrAdd(RibbonIdData[i]);
				Indices.Add(i);
			}

			uint32 RibbonIndex = 0;
			for (TPair<int32, TArray<int32>>& Pair : MultiRibbonSortedIndices)
			{
				TArray<int32>& SortedIndices = Pair.Value;
				SortedIndices.Sort([&SortKeyData](const int32& A, const int32& B) {	return (SortKeyData[A] < SortKeyData[B]); });
				AddRibbonVerts(SortedIndices, RibbonIndex);
				RibbonIndex++;
			};
		}
	}

	if (TotalSegmentLength > 0)
	{
		// Blend the result between the last frame tessellation factors and the current frame base on the total length of all segments.
		// This is only used to increase the tessellation value of the current frame data to prevent glitches where tessellation is significantly changin between frames.
		const float OneOverTotalSegmentLength = 1.f / FMath::Max(1.f, TotalSegmentLength);
		const float AveragingFactor = TessellationTotalSegmentLength / (TotalSegmentLength + TessellationTotalSegmentLength);
		TessellationTotalSegmentLength = TotalSegmentLength;

		AverageSegmentAngle *= OneOverTotalSegmentLength;
		AverageSegmentLength *= OneOverTotalSegmentLength;
		const float AverageSegmentCurvature = AverageSegmentLength / (FMath::Max(SMALL_NUMBER, FMath::Abs(FMath::Sin(AverageSegmentAngle))));

		TessellationAngle = FMath::Lerp<float>(AverageSegmentAngle, FMath::Max(TessellationAngle, AverageSegmentAngle), AveragingFactor);
		TessellationCurvature = FMath::Lerp<float>(AverageSegmentCurvature, FMath::Max(TessellationCurvature, AverageSegmentCurvature), AveragingFactor);

		if (bHasTwist)
		{
			AverageTwistAngle *= OneOverTotalSegmentLength;
			AverageWidth *= OneOverTotalSegmentLength;

			TessellationTwistAngle = FMath::Lerp<float>(AverageTwistAngle, FMath::Max(TessellationTwistAngle, AverageTwistAngle), AveragingFactor);
			TessellationTwistCurvature = FMath::Lerp<float>(AverageWidth, FMath::Max(TessellationTwistCurvature, AverageWidth), AveragingFactor);
		}
	}
	else // Reset the metrics when the ribbons are reset.
	{
		TessellationAngle = 0;
		TessellationCurvature = 0;
		TessellationTwistAngle = 0;
		TessellationTwistCurvature = 0;
		TessellationTotalSegmentLength = 0;
	}

	return DynamicData;
}

void FNiagaraRendererRibbons::AddDynamicParam(TArray<FNiagaraRibbonVertexDynamicParameter>& ParamData, const FVector4& DynamicParam)
{
	FNiagaraRibbonVertexDynamicParameter Param;
	Param.DynamicValue[0] = DynamicParam.X;
	Param.DynamicValue[1] = DynamicParam.Y;
	Param.DynamicValue[2] = DynamicParam.Z;
	Param.DynamicValue[3] = DynamicParam.W;
	ParamData.Add(Param);
}

bool FNiagaraRendererRibbons::IsMaterialValid(UMaterialInterface* Mat)const
{
	return Mat && Mat->CheckMaterialUsage(MATUSAGE_NiagaraRibbons);
}


void FNiagaraRendererRibbons::SetupMeshBatchAndCollectorResourceForView(
	const FSceneView* View,
	const FSceneViewFamily& ViewFamily,
	const FNiagaraSceneProxy* SceneProxy,
	FMeshElementCollector& Collector,
	struct FNiagaraDynamicDataRibbon* DynamicDataRibbon,
	uint32 NumPrimitives,
	const FGlobalDynamicIndexBuffer::FAllocation& IndexAllocation,
	FMeshBatch& MeshBatch,
	class FNiagaraMeshCollectorResourcesRibbon& CollectorResources) const
{
	const bool bIsWireframe = ViewFamily.EngineShowFlags.Wireframe;
	FMaterialRenderProxy* MaterialRenderProxy = DynamicDataRibbon->Material;
	check(MaterialRenderProxy);


	FNiagaraDataBuffer* SourceParticleData = DynamicDataRibbon->GetParticleDataToRender();
	check(SourceParticleData); // if this is nullptr, should already be early out before entering this function

	FCPUSimParticleDataAllocation CPUSimParticleDataAllocation = AllocateParticleDataIfCPUSim(DynamicDataRibbon, Collector.GetDynamicReadBuffer());
	auto& ParticleData = CPUSimParticleDataAllocation.ParticleData;

	int32 ParticleDataStride = GbEnableMinimalGPUBuffers ? SourceParticleData->GetNumInstances() : SourceParticleData->GetFloatStride() / sizeof(float);
	CollectorResources.VertexFactory.SetParticleData(ParticleData.ReadBuffer->SRV, ParticleData.FirstIndex / sizeof(float), ParticleDataStride);

	// TODO: need to make these two a global alloc buffer as well, not recreate
				// pass in the sorted indices so the VS can fetch the particle data in order
	FReadBuffer SortedIndicesBuffer;
	SortedIndicesBuffer.Initialize(sizeof(int32), DynamicDataRibbon->SortedIndices.Num(), EPixelFormat::PF_R32_SINT, BUF_Volatile);
	void *IndexPtr = RHILockVertexBuffer(SortedIndicesBuffer.Buffer, 0, DynamicDataRibbon->SortedIndices.Num() * sizeof(int32), RLM_WriteOnly);
	FMemory::Memcpy(IndexPtr, DynamicDataRibbon->SortedIndices.GetData(), DynamicDataRibbon->SortedIndices.Num() * sizeof(int32));
	RHIUnlockVertexBuffer(SortedIndicesBuffer.Buffer);
	CollectorResources.VertexFactory.SetSortedIndices(SortedIndicesBuffer.Buffer, SortedIndicesBuffer.SRV, 0);
	// pass in the CPU generated total segment distance (for tiling distance modes); needs to be a buffer so we can fetch them in the correct order based on Draw Direction (front->back or back->front)
	//	otherwise UVs will pop when draw direction changes based on camera view point
	FReadBuffer TangentsAndDistancesBuffer;
	TangentsAndDistancesBuffer.Initialize(sizeof(FVector4), DynamicDataRibbon->TangentAndDistances.Num(), EPixelFormat::PF_A32B32G32R32F, BUF_Volatile);
	void *TangentsAndDistancesPtr = RHILockVertexBuffer(TangentsAndDistancesBuffer.Buffer, 0, DynamicDataRibbon->TangentAndDistances.Num() * sizeof(FVector4), RLM_WriteOnly);
	FMemory::Memcpy(TangentsAndDistancesPtr, DynamicDataRibbon->TangentAndDistances.GetData(), DynamicDataRibbon->TangentAndDistances.Num() * sizeof(FVector4));
	RHIUnlockVertexBuffer(TangentsAndDistancesBuffer.Buffer);
	CollectorResources.VertexFactory.SetTangentAndDistances(TangentsAndDistancesBuffer.Buffer, TangentsAndDistancesBuffer.SRV);
	// Copy a buffer which has the per particle multi ribbon index.
	FReadBuffer MultiRibbonIndicesBuffer;
	MultiRibbonIndicesBuffer.Initialize(sizeof(uint32), DynamicDataRibbon->MultiRibbonIndices.Num(), EPixelFormat::PF_R32_UINT, BUF_Volatile);
	void* MultiRibbonIndexPtr = RHILockVertexBuffer(MultiRibbonIndicesBuffer.Buffer, 0, DynamicDataRibbon->MultiRibbonIndices.Num() * sizeof(uint32), RLM_WriteOnly);
	FMemory::Memcpy(MultiRibbonIndexPtr, DynamicDataRibbon->MultiRibbonIndices.GetData(), DynamicDataRibbon->MultiRibbonIndices.Num() * sizeof(uint32));
	RHIUnlockVertexBuffer(MultiRibbonIndicesBuffer.Buffer);
	CollectorResources.VertexFactory.SetMultiRibbonIndicesSRV(MultiRibbonIndicesBuffer.Buffer, MultiRibbonIndicesBuffer.SRV);
	// Copy the packed u data for stable age based uv generation.
	FReadBuffer PackedPerRibbonDataByIndexBuffer;
	PackedPerRibbonDataByIndexBuffer.Initialize(sizeof(float), DynamicDataRibbon->PackedPerRibbonDataByIndex.Num(), EPixelFormat::PF_R32_FLOAT, BUF_Volatile);
	void *PackedPerRibbonDataByIndexPtr = RHILockVertexBuffer(PackedPerRibbonDataByIndexBuffer.Buffer, 0, DynamicDataRibbon->PackedPerRibbonDataByIndex.Num() * sizeof(float), RLM_WriteOnly);
	FMemory::Memcpy(PackedPerRibbonDataByIndexPtr, DynamicDataRibbon->PackedPerRibbonDataByIndex.GetData(), DynamicDataRibbon->PackedPerRibbonDataByIndex.Num() * sizeof(float));
	RHIUnlockVertexBuffer(PackedPerRibbonDataByIndexBuffer.Buffer);
	CollectorResources.VertexFactory.SetPackedPerRibbonDataByIndexSRV(PackedPerRibbonDataByIndexBuffer.Buffer, PackedPerRibbonDataByIndexBuffer.SRV);

	FNiagaraRibbonVFLooseParameters VFLooseParams;
	VFLooseParams.SortedIndices = SortedIndicesBuffer.SRV;
	VFLooseParams.TangentsAndDistances = TangentsAndDistancesBuffer.SRV;
	VFLooseParams.MultiRibbonIndices = MultiRibbonIndicesBuffer.SRV;
	VFLooseParams.PackedPerRibbonDataByIndex = PackedPerRibbonDataByIndexBuffer.SRV;
	VFLooseParams.NiagaraParticleDataFloat = CollectorResources.VertexFactory.GetParticleDataFloatSRV();
	VFLooseParams.NiagaraFloatDataOffset = CollectorResources.VertexFactory.GetFloatDataOffset();
	VFLooseParams.NiagaraFloatDataStride = CollectorResources.VertexFactory.GetFloatDataStride();
	VFLooseParams.SortedIndicesOffset = CollectorResources.VertexFactory.GetSortedIndicesOffset();
	VFLooseParams.FacingMode = CollectorResources.VertexFactory.GetFacingMode();

	// Collector.AllocateOneFrameResource uses default ctor, initialize the vertex factory
	CollectorResources.VertexFactory.SetParticleFactoryType(NVFT_Ribbon);
	CollectorResources.VertexFactory.LooseParameterUniformBuffer = FNiagaraRibbonVFLooseParametersRef::CreateUniformBufferImmediate(VFLooseParams, UniformBuffer_SingleFrame);
	CollectorResources.VertexFactory.InitResource();
	CollectorResources.VertexFactory.SetRibbonUniformBuffer(CollectorResources.UniformBuffer);
	CollectorResources.VertexFactory.SetFacingMode(static_cast<uint32>(FacingMode));


	MeshBatch.VertexFactory = &CollectorResources.VertexFactory;
	MeshBatch.CastShadow = SceneProxy->CastsDynamicShadow();
#if RHI_RAYTRACING
	MeshBatch.CastRayTracedShadow = SceneProxy->CastsDynamicShadow();
#endif
	MeshBatch.bUseAsOccluder = false;
	MeshBatch.ReverseCulling = SceneProxy->IsLocalToWorldDeterminantNegative();
	MeshBatch.bDisableBackfaceCulling = true;
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
	MeshElement.IndexBuffer = IndexAllocation.IndexBuffer;
	MeshElement.FirstIndex = IndexAllocation.FirstIndex;
	MeshElement.NumPrimitives = NumPrimitives;
	check(MeshElement.NumPrimitives > 0);
	MeshElement.NumInstances = 1;
	MeshElement.MinVertexIndex = 0;
	MeshElement.MaxVertexIndex = 0;
	MeshElement.PrimitiveUniformBuffer = SceneProxy->GetUniformBuffer();

}

FNiagaraRendererRibbons::FCPUSimParticleDataAllocation FNiagaraRendererRibbons::AllocateParticleDataIfCPUSim(FNiagaraDynamicDataRibbon* DynamicDataRibbon, FGlobalDynamicReadBuffer& DynamicReadBuffer) const
{
	FNiagaraDataBuffer* SourceParticleData = DynamicDataRibbon->GetParticleDataToRender();
	check(SourceParticleData);//Can be null but should be checked before here.
	int32 TotalFloatSize = SourceParticleData->GetFloatBuffer().Num() / sizeof(float);

	FCPUSimParticleDataAllocation CPUSimParticleDataAllocation{ DynamicReadBuffer };

	bool bShouldDoFacing = FacingMode == ENiagaraRibbonFacingMode::Custom || FacingMode == ENiagaraRibbonFacingMode::CustomSideVector;
	VFVariables[ENiagaraRibbonVFLayout::Facing].bUpload = bShouldDoFacing;

	if (SimTarget == ENiagaraSimTarget::CPUSim)
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderRibbonsCPUSimMemCopy);
		if (GbEnableMinimalGPUBuffers)
		{
			CPUSimParticleDataAllocation.ParticleData = TransferDataToGPU(DynamicReadBuffer, SourceParticleData);
		}
		else
		{
			CPUSimParticleDataAllocation.ParticleData = DynamicReadBuffer.AllocateFloat(TotalFloatSize);
			FMemory::Memcpy(CPUSimParticleDataAllocation.ParticleData.Buffer, SourceParticleData->GetFloatBuffer().GetData(), SourceParticleData->GetFloatBuffer().Num());
		}
	}

	return CPUSimParticleDataAllocation;
}

void FNiagaraRendererRibbons::CreatePerViewResources(
	const FSceneView* View, const FSceneViewFamily& ViewFamily, const FNiagaraSceneProxy* SceneProxy, FMeshElementCollector& Collector,
	uint16& OutVertexCount, uint32& OutNumberOfPrimitives, FNiagaraRibbonUniformBufferRef& OutUniformBuffer, FGlobalDynamicIndexBuffer::FAllocation& InOutIndexAllocation) const
{
	FNiagaraDynamicDataRibbon* DynamicDataRibbon = static_cast<FNiagaraDynamicDataRibbon*>(DynamicDataRender);
	FNiagaraDataBuffer* SourceParticleData = DynamicDataRibbon->GetParticleDataToRender();
	check(DynamicDataRibbon);
	check(SourceParticleData);

	bool bUseConstantFactor = false;
	int32 TessellationFactor = GNiagaraRibbonMaxTessellation;
	float TessellationMinAngle = GNiagaraRibbonTessellationAngle;
	float ScreenPercentage = GNiagaraRibbonTessellationScreenPercentage;
	switch (TessellationMode)

	{
	case ENiagaraRibbonTessellationMode::Automatic:
		break;
	case ENiagaraRibbonTessellationMode::Custom:
		TessellationFactor = FMath::Min<int32>(TessellationFactor, CustomTessellationFactor); // Don't allow factors bigger than the platform limit.
		bUseConstantFactor = bCustomUseConstantFactor;
		bUseConstantFactor = bCustomUseConstantFactor;
		TessellationMinAngle = CustomTessellationMinAngle;
		ScreenPercentage = bCustomUseScreenSpace && !bUseConstantFactor ? GNiagaraRibbonTessellationScreenPercentage : 0.f;
		break;
	case ENiagaraRibbonTessellationMode::Disabled:
		TessellationFactor = 1;
		break;
	default:
		break;
	}

	const FVector ViewOriginForDistanceCulling = View->ViewMatrices.GetViewOrigin();

	int32 SegmentTessellation = 1;
	int32 NumSegments = DynamicDataRibbon->SegmentData.Num();
	if (TessellationFactor > 1 && TessellationCurvature > SMALL_NUMBER && ViewFamily.GetFeatureLevel() == ERHIFeatureLevel::SM5)
	{
		const float MinTesselation = [&]
		{
			if (TessellationMinAngle == 0.f || bUseConstantFactor)
			{
				return static_cast<float>(TessellationFactor);
			}
			else
			{
				return FMath::Max<float>(1.f, FMath::Max(TessellationTwistAngle, TessellationAngle) / FMath::Max<float>(SMALL_NUMBER, TessellationMinAngle));
			}
		}();
		const float MAX_CURVATURE_FACTOR = 0.002f; // This will clamp the curvature to around 2.5 km and avoid numerical issues.
		const float ViewDistance = SceneProxy->GetBounds().ComputeSquaredDistanceFromBoxToPoint(ViewOriginForDistanceCulling);
		const float MaxDisplacementError = FMath::Max(GNiagaraRibbonTessellationMinDisplacementError, ScreenPercentage * FMath::Sqrt(ViewDistance) / View->LODDistanceFactor);
		float Tess = TessellationAngle / FMath::Max(MAX_CURVATURE_FACTOR, AcosFast(TessellationCurvature / (TessellationCurvature + MaxDisplacementError)));
		// FMath::RoundUpToPowerOfTwo ? This could avoid vertices moving around as tesselation increases

		if (TessellationTwistAngle > 0 && TessellationTwistCurvature > 0)
		{
			const float TwistTess = TessellationTwistAngle / FMath::Max(MAX_CURVATURE_FACTOR, AcosFast(TessellationTwistCurvature / (TessellationTwistCurvature + MaxDisplacementError)));
			Tess = FMath::Max(TwistTess, Tess);
		}
		SegmentTessellation = FMath::Clamp<int32>(FMath::RoundToInt(Tess), FMath::RoundToInt(MinTesselation), TessellationFactor);
		NumSegments *= SegmentTessellation;
	}


	OutNumberOfPrimitives = NumSegments * 2;

	// Figure out whether start is closer to the view plane than end
	// TODO : This doesn't work with multi-ribbons.
	const float StartDist = FVector::DotProduct(View->GetViewDirection(), DynamicDataRibbon->StartPos - ViewOriginForDistanceCulling);
	const float EndDist = FVector::DotProduct(View->GetViewDirection(), DynamicDataRibbon->EndPos - ViewOriginForDistanceCulling);
	const bool bInvertOrder = ((StartDist > EndDist) && DrawDirection == ENiagaraRibbonDrawDirection::BackToFront)
		|| ((StartDist < EndDist) && DrawDirection == ENiagaraRibbonDrawDirection::FrontToBack);

	// Copy the index data over.
	FGlobalDynamicIndexBuffer& DynamicIndexBuffer = Collector.GetDynamicIndexBuffer();

	const uint32 IndicesPerPrimitive = 3;

	// the extra plus 3 is for safe padding, the actual size is `NumberOfPrimitives * IndicesPerPrimitive`
	InOutIndexAllocation = DynamicIndexBuffer.Allocate(OutNumberOfPrimitives * IndicesPerPrimitive, sizeof(uint16));

	GenerateIndexBuffer((uint16*)InOutIndexAllocation.Buffer, OutVertexCount, DynamicDataRibbon->SegmentData, SegmentTessellation, bInvertOrder);

	FNiagaraRibbonUniformParameters PerViewUniformParameters;
	FMemory::Memzero(&PerViewUniformParameters,sizeof(PerViewUniformParameters)); // Clear unset bytes

	PerViewUniformParameters.bLocalSpace = bLocalSpace;
	PerViewUniformParameters.DeltaSeconds = ViewFamily.DeltaWorldTime;
	PerViewUniformParameters.CameraUp = View->GetViewUp(); // FVector4(0.0f, 0.0f, 1.0f, 0.0f);
	PerViewUniformParameters.CameraRight = View->GetViewRight();//	FVector4(1.0f, 0.0f, 0.0f, 0.0f);
	PerViewUniformParameters.ScreenAlignment = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
	PerViewUniformParameters.TotalNumInstances = SourceParticleData->GetNumInstances();
	PerViewUniformParameters.InterpCount = SegmentTessellation;
	PerViewUniformParameters.OneOverInterpCount = 1.f / (float)SegmentTessellation;

	PerViewUniformParameters.PositionDataOffset = VFVariables[ENiagaraRibbonVFLayout::Position].GetGPUOffset();
	PerViewUniformParameters.VelocityDataOffset = VFVariables[ENiagaraRibbonVFLayout::Velocity].GetGPUOffset();
	PerViewUniformParameters.ColorDataOffset = VFVariables[ENiagaraRibbonVFLayout::Color].GetGPUOffset();
	PerViewUniformParameters.WidthDataOffset = VFVariables[ENiagaraRibbonVFLayout::Width].GetGPUOffset();
	PerViewUniformParameters.TwistDataOffset = VFVariables[ENiagaraRibbonVFLayout::Twist].GetGPUOffset();
	PerViewUniformParameters.NormalizedAgeDataOffset = VFVariables[ENiagaraRibbonVFLayout::NormalizedAge].GetGPUOffset();
	PerViewUniformParameters.MaterialRandomDataOffset = VFVariables[ENiagaraRibbonVFLayout::MaterialRandom].GetGPUOffset();
	PerViewUniformParameters.MaterialParamDataOffset = VFVariables[ENiagaraRibbonVFLayout::MaterialParam0].GetGPUOffset();
	PerViewUniformParameters.MaterialParam1DataOffset = VFVariables[ENiagaraRibbonVFLayout::MaterialParam1].GetGPUOffset();
	PerViewUniformParameters.MaterialParam2DataOffset = VFVariables[ENiagaraRibbonVFLayout::MaterialParam2].GetGPUOffset();
	PerViewUniformParameters.MaterialParam3DataOffset = VFVariables[ENiagaraRibbonVFLayout::MaterialParam3].GetGPUOffset();

	PerViewUniformParameters.MaterialParamValidMask = MaterialParamValidMask;

	bool bShouldDoFacing = FacingMode == ENiagaraRibbonFacingMode::Custom || FacingMode == ENiagaraRibbonFacingMode::CustomSideVector;
	PerViewUniformParameters.FacingDataOffset = bShouldDoFacing ? VFVariables[ENiagaraRibbonVFLayout::Facing].GetGPUOffset() : -1;
	PerViewUniformParameters.OneOverUV0TilingDistance = UV0TilingDistance ? 1.f / (UV0TilingDistance) : 0.f;
	PerViewUniformParameters.OneOverUV1TilingDistance = UV1TilingDistance ? 1.f / (UV1TilingDistance) : 0.f;
	PerViewUniformParameters.PackedVData = FVector4(UV0Scale.Y, UV0Offset.Y, UV1Scale.Y, UV1Offset.Y);

	PerViewUniformParameters.OneOverUV0TilingDistance = UV0TilingDistance ? 1.f / (UV0TilingDistance) : 0.f;
	PerViewUniformParameters.OneOverUV1TilingDistance = UV1TilingDistance ? 1.f / (UV1TilingDistance) : 0.f;
	PerViewUniformParameters.PackedVData = FVector4(UV0Scale.Y, UV0Offset.Y, UV1Scale.Y, UV1Offset.Y);

	OutUniformBuffer = FNiagaraRibbonUniformBufferRef::CreateUniformBufferImmediate(PerViewUniformParameters, UniformBuffer_SingleFrame);

}

#if RHI_RAYTRACING
void FNiagaraRendererRibbons::GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context, TArray<FRayTracingInstance>& OutRayTracingInstances, const FNiagaraSceneProxy* SceneProxy)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraRender);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderRibbons);
	check(SceneProxy);

	FNiagaraDynamicDataRibbon *DynamicDataRibbon = static_cast<FNiagaraDynamicDataRibbon*>(DynamicDataRender);
	NiagaraEmitterInstanceBatcher* Batcher = SceneProxy->GetBatcher();
	if (!DynamicDataRibbon || !Batcher)
	{
		return;
	}

	if (!DynamicDataRibbon->SortedIndices.Num())
	{
		return;
	}

	FNiagaraDataBuffer* SourceParticleData = DynamicDataRibbon->GetParticleDataToRender();
	if (SourceParticleData == nullptr ||
		SourceParticleData->GetNumInstancesAllocated() == 0 ||
		SourceParticleData->GetNumInstances() == 0 ||
		GbEnableNiagaraRibbonRendering == 0 ||
		!GSupportsResourceView // Current shader requires SRV to draw properly in all cases.
		)
	{
		return;
	}

	FRayTracingInstance RayTracingInstance;
	RayTracingInstance.Geometry = &RayTracingGeometry;
	RayTracingInstance.InstanceTransforms.Add(FMatrix::Identity);

	{
		auto& View = Context.ReferenceView;
		auto& ViewFamily = Context.ReferenceViewFamily;
		// Setup material for our ray tracing instance
		FNiagaraMeshCollectorResourcesRibbon& CollectorResources = Context.RayTracingMeshResourceCollector.AllocateOneFrameResource<FNiagaraMeshCollectorResourcesRibbon>();

		FGlobalDynamicIndexBuffer::FAllocation DynamicIndexAllocation;
		uint16 VertexCount;

		uint32 NumPrimitives;
		CreatePerViewResources(Context.ReferenceView, Context.ReferenceViewFamily, SceneProxy, Context.RayTracingMeshResourceCollector,
			VertexCount, NumPrimitives, CollectorResources.UniformBuffer, DynamicIndexAllocation);

		RayTracingGeometry.Initializer.IndexBuffer = DynamicIndexAllocation.IndexBuffer->IndexBufferRHI;
		RayTracingGeometry.Initializer.IndexBufferOffset = DynamicIndexAllocation.FirstIndex * sizeof(uint16);


		FMeshBatch MeshBatch;

		SetupMeshBatchAndCollectorResourceForView(
			Context.ReferenceView, Context.ReferenceViewFamily, SceneProxy, Context.RayTracingMeshResourceCollector,
			DynamicDataRibbon, NumPrimitives, DynamicIndexAllocation, MeshBatch, CollectorResources);

		RayTracingInstance.Materials.Add(MeshBatch);

		Context.DynamicRayTracingGeometriesToUpdate.Add(
			FRayTracingDynamicGeometryUpdateParams
			{
				RayTracingInstance.Materials,
				false,
				VertexCount,
				VertexCount * (uint32)sizeof(FVector),
				MeshBatch.Elements[0].NumPrimitives,
				&RayTracingGeometry,
				&RayTracingDynamicVertexBuffer,
			}
		);
	}

	RayTracingInstance.BuildInstanceMaskAndFlags();

	OutRayTracingInstances.Add(RayTracingInstance);
}
#endif