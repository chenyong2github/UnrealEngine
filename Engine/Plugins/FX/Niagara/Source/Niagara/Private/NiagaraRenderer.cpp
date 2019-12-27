// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraRenderer.h"
#include "ParticleResources.h"
#include "ParticleBeamTrailVertexFactory.h"
#include "NiagaraDataSet.h"
#include "NiagaraStats.h"
#include "NiagaraVertexFactory.h"
#include "Engine/Engine.h"
#include "DynamicBufferAllocator.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraGPUSortInfo.h"

DECLARE_CYCLE_STAT(TEXT("Sort Particles"), STAT_NiagaraSortParticles, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Global Float Alloc - All"), STAT_NiagaraAllocateGlobalFloatAll, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Global Float Alloc - InsideLock"), STAT_NiagaraAllocateGlobalFloatInsideLock, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Global Float Alloc - Alloc New Buffer"), STAT_NiagaraAllocateGlobalFloatAllocNew, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Global Float Alloc - Map Buffer"), STAT_NiagaraAllocateGlobalFloatMapBuffer, STATGROUP_Niagara);

int32 GNiagaraRadixSortThreshold = 400;
static FAutoConsoleVariableRef CVarNiagaraRadixSortThreshold(
	TEXT("Niagara.RadixSortThreshold"),
	GNiagaraRadixSortThreshold,
	TEXT("Instance count at which radix sort gets used instead of introspective sort.\n")
	TEXT("Set to  -1 to never use radixsort. (default=400)"),
	ECVF_Default
);

class FNiagaraDummyRWBufferFloat : public FRenderResource
{
public:
	FNiagaraDummyRWBufferFloat(const FString InDebugId) : DebugId(InDebugId) {}
	FString DebugId;
	FRWBuffer Buffer;

	virtual void InitRHI() override
	{
		UE_LOG(LogNiagara, Log, TEXT("FNiagaraDummyRWBufferFloat InitRHI %s"), *DebugId);
		Buffer.Initialize(sizeof(float), 1, EPixelFormat::PF_R32_FLOAT, BUF_Static, *DebugId);
	}

	virtual void ReleaseRHI() override
	{
		UE_LOG(LogNiagara, Log, TEXT("FNiagaraDummyRWBufferFloat ReleaseRHI %s"), *DebugId);
		Buffer.Release();
	}
};

class FNiagaraDummyRWBufferFloat4 : public FRenderResource
{
public:
	FNiagaraDummyRWBufferFloat4(const FString InDebugId) : DebugId(InDebugId) {}
	FString DebugId;
	FRWBuffer Buffer;

	virtual void InitRHI() override
	{
		UE_LOG(LogNiagara, Log, TEXT("FNiagaraDummyRWBufferFloat InitRHI %s"), *DebugId);
		Buffer.Initialize(sizeof(float) * 4, 1, EPixelFormat::PF_A32B32G32R32F, BUF_Static, *DebugId);
	}

	virtual void ReleaseRHI() override
	{
		UE_LOG(LogNiagara, Log, TEXT("FNiagaraDummyRWBufferFloat4 ReleaseRHI %s"), *DebugId);
		Buffer.Release();
	}
};

class FNiagaraDummyRWBufferInt : public FRenderResource
{
public:
	FNiagaraDummyRWBufferInt(const FString InDebugId) : DebugId(InDebugId) {}
	FString DebugId;
	FRWBuffer Buffer;

	virtual void InitRHI() override
	{
		UE_LOG(LogNiagara, Log, TEXT("FNiagaraDummyRWBufferInt InitRHI %s"), *DebugId);
		Buffer.Initialize(sizeof(int32), 1, EPixelFormat::PF_R32_SINT, BUF_Static, *DebugId);
	}

	virtual void ReleaseRHI() override
	{
		UE_LOG(LogNiagara, Log, TEXT("FNiagaraDummyRWBufferInt ReleaseRHI %s"), *DebugId);
		Buffer.Release();
	}
};

class FNiagaraDummyRWBufferUInt : public FRenderResource
{
public:
	FNiagaraDummyRWBufferUInt(const FString InDebugId) : DebugId(InDebugId) {}
	FString DebugId;
	FRWBuffer Buffer;

	virtual void InitRHI() override
	{
		UE_LOG(LogNiagara, Log, TEXT("FNiagaraDummyRWBufferUInt InitRHI %s"), *DebugId);
		Buffer.Initialize(sizeof(uint32), 1, EPixelFormat::PF_R32_UINT, BUF_Static, *DebugId);
	}

	virtual void ReleaseRHI() override
	{
		UE_LOG(LogNiagara, Log, TEXT("FNiagaraDummyRWBufferUInt ReleaseRHI %s"), *DebugId);
		Buffer.Release();
	}
};

FRWBuffer& FNiagaraRenderer::GetDummyFloatBuffer()
{
	check(IsInRenderingThread());
	static TGlobalResource<FNiagaraDummyRWBufferFloat> DummyFloatBuffer(TEXT("NiagaraRenderer::DummyFloat"));
	return DummyFloatBuffer.Buffer;
}

FRWBuffer& FNiagaraRenderer::GetDummyFloat4Buffer()
{
	check(IsInRenderingThread());
	static TGlobalResource<FNiagaraDummyRWBufferFloat4> GetDummyFloat4Buffer(TEXT("NiagaraRenderer::DummyFloat4"));
	return GetDummyFloat4Buffer.Buffer;
}

FRWBuffer& FNiagaraRenderer::GetDummyIntBuffer()
{
	check(IsInRenderingThread());
	static TGlobalResource<FNiagaraDummyRWBufferInt> DummyIntBuffer(TEXT("NiagaraRenderer::DummyInt"));
	return DummyIntBuffer.Buffer;
}

FRWBuffer& FNiagaraRenderer::GetDummyUIntBuffer()
{
	check(IsInRenderingThread());
	static TGlobalResource<FNiagaraDummyRWBufferUInt> DummyUIntBuffer(TEXT("NiagaraRenderer::DummyUInt"));
	return DummyUIntBuffer.Buffer;
}

bool FNiagaraRenderer::SetVertexFactoryVariable(const FNiagaraDataSet& DataSet, const FNiagaraVariable& Var, int32 VFVarOffset)
{
	int32 FloatOffset;
	int32 IntOffset;//TODO: No VF uses ints atm but it should be trivial to copy the float path if some vf should need to.
	DataSet.GetVariableComponentOffsets(Var, FloatOffset, IntOffset);
	int32 NumComponents = Var.GetSizeInBytes() / sizeof(float);

	int32 GPULocation = INDEX_NONE;
	bool bUpload = true;
	if (FloatOffset != INDEX_NONE)
	{
		if (FNiagaraRendererVariableInfo* ExistingVarInfo = VFVariables.FindByPredicate([&](const FNiagaraRendererVariableInfo& VarInfo) { return VarInfo.DatasetOffset == FloatOffset; }))
		{
			//Don't need to upload this var again if it's already been uploaded for another var info. Just point to that.
			//E.g. when custom sorting uses age.
			GPULocation = ExistingVarInfo->GPUBufferOffset;
			bUpload = false;
		}
		else
		{
			//For CPU Sims we pack just the required data tightly in a GPU buffer we upload. For GPU sims the data is there already so we just provide the real data location.
			GPULocation = SimTarget == ENiagaraSimTarget::CPUSim ? TotalVFComponents : FloatOffset;
			TotalVFComponents += NumComponents;
		}
	}

	VFVariables[VFVarOffset] = FNiagaraRendererVariableInfo(FloatOffset, GPULocation, NumComponents, bUpload);

	return FloatOffset != INDEX_NONE;
}

FGlobalDynamicReadBuffer::FAllocation FNiagaraRenderer::TransferDataToGPU(FGlobalDynamicReadBuffer& DynamicReadBuffer, FNiagaraDataBuffer* SrcData)const
{
	int32 TotalFloatSize = TotalVFComponents * SrcData->GetNumInstances();
	int32 ComponentStrideDest = SrcData->GetNumInstances() * sizeof(float);
	FGlobalDynamicReadBuffer::FAllocation Allocation = DynamicReadBuffer.AllocateFloat(TotalFloatSize);
	for (const FNiagaraRendererVariableInfo& VarInfo : VFVariables)
	{
		int32 GpuOffset = VarInfo.GetGPUOffset();
		if (GpuOffset != INDEX_NONE && VarInfo.bUpload)
		{
			for (int32 CompIdx = 0; CompIdx < VarInfo.NumComponents; ++CompIdx)
			{
				float* SrcComponent = (float*)SrcData->GetComponentPtrFloat(VarInfo.DatasetOffset + CompIdx);
				void* Dest = Allocation.Buffer + ComponentStrideDest * (GpuOffset + CompIdx);
				FMemory::Memcpy(Dest, SrcComponent, ComponentStrideDest);
			}
		}
	}
	return Allocation;
}

//////////////////////////////////////////////////////////////////////////

FNiagaraDynamicDataBase::FNiagaraDynamicDataBase(const FNiagaraEmitterInstance* InEmitter)
{
	check(InEmitter);

	FNiagaraDataSet& DataSet = InEmitter->GetData();
	SimTarget = DataSet.GetSimTarget();

	if (SimTarget == ENiagaraSimTarget::CPUSim)
	{
		//On CPU we pass through direct ptr to the most recent data buffer.
		Data.CPUParticleData = &DataSet.GetCurrentDataChecked();

		//Mark this buffer as in use by this renderer. Prevents this buffer being reused to write new simulation data while it's inuse by the renderer.
		Data.CPUParticleData->AddReadRef();
	}
	else
	{
		//On GPU we must access the correct buffer via the GPUExecContext. Probably a way to route this data better outside the dynamic data in future.
		//During simulation, the correct data buffer for rendering will be placed in the GPUContext and AddReadRef called.
		check(SimTarget == ENiagaraSimTarget::GPUComputeSim);
		Data.GPUExecContext = InEmitter->GetGPUContext();
	}
}

FNiagaraDynamicDataBase::~FNiagaraDynamicDataBase()
{
	if (SimTarget == ENiagaraSimTarget::CPUSim)
	{
		check(Data.CPUParticleData);
		//Release our ref on the buffer so it can be reused as a destination for a new simulation tick.
		Data.CPUParticleData->ReleaseReadRef();
	}
}

FNiagaraDataBuffer* FNiagaraDynamicDataBase::GetParticleDataToRender()const
{
	FNiagaraDataBuffer* Ret = nullptr;

	if (SimTarget == ENiagaraSimTarget::CPUSim)
	{
		Ret = Data.CPUParticleData;
	}
	else
	{
		Ret = Data.GPUExecContext->GetDataToRender();
	}

	checkSlow(Ret == nullptr || Ret->IsBeingRead());
	return Ret;
}

//////////////////////////////////////////////////////////////////////////


FNiagaraRenderer::FNiagaraRenderer(ERHIFeatureLevel::Type InFeatureLevel, const UNiagaraRendererProperties *InProps, const FNiagaraEmitterInstance* Emitter)
	: DynamicDataRender(nullptr)
	, bLocalSpace(Emitter->GetCachedEmitter()->bLocalSpace)
	, bHasLights(false)
	, SimTarget(Emitter->GetCachedEmitter()->SimTarget)
	, NumIndicesPerInstance(InProps ? InProps->GetNumIndicesPerInstance() : 0)
	, FeatureLevel(InFeatureLevel)
	, TotalVFComponents(0)
{
#if STATS
	EmitterStatID = Emitter->GetCachedEmitter()->GetStatID(false, false);
#endif
}

void FNiagaraRenderer::Initialize(const UNiagaraRendererProperties *InProps, const FNiagaraEmitterInstance* Emitter)
{
	//Get our list of valid base materials. Fall back to default material if they're not valid.
	InProps->GetUsedMaterials(Emitter, BaseMaterials_GT);
	for (UMaterialInterface*& Mat : BaseMaterials_GT)
	{
		if (!IsMaterialValid(Mat))
		{
			Mat = UMaterial::GetDefaultMaterial(MD_Surface);
		}
		BaseMaterialRelevance_GT |= Mat->GetRelevance(FeatureLevel);
	}
}

FNiagaraRenderer::~FNiagaraRenderer()
{
	ReleaseRenderThreadResources();
	SetDynamicData_RenderThread(nullptr);
}

void FNiagaraRenderer::CreateRenderThreadResources(NiagaraEmitterInstanceBatcher* Batcher) 
{
	if (Batcher && SimTarget == ENiagaraSimTarget::GPUComputeSim)
	{
		NumRegisteredGPURenderers = Batcher->GetGPUInstanceCounterManager().GetGPURendererCount();
		if (NumRegisteredGPURenderers)
		{
			NumRegisteredGPURenderers->Value += 1;
		}
	}
}

void FNiagaraRenderer::ReleaseRenderThreadResources()
{
	if (NumRegisteredGPURenderers)
	{
		NumRegisteredGPURenderers->Value -= 1;
		NumRegisteredGPURenderers.SafeRelease();
	}
}

FPrimitiveViewRelevance FNiagaraRenderer::GetViewRelevance(const FSceneView* View, const FNiagaraSceneProxy *SceneProxy)const
{
	FPrimitiveViewRelevance Result;
	bool bHasDynamicData = HasDynamicData();

	//Always draw so our LastRenderTime is updated. We may not have dynamic data if we're disabled from visibility culling.
	Result.bDrawRelevance =/* bHasDynamicData && */SceneProxy->IsShown(View) && View->Family->EngineShowFlags.Particles;
	Result.bShadowRelevance = bHasDynamicData && SceneProxy->IsShadowCast(View);
	Result.bDynamicRelevance = bHasDynamicData;
	if (bHasDynamicData)
	{
		Result.bOpaqueRelevance = View->Family->EngineShowFlags.Bounds;
		DynamicDataRender->GetMaterialRelevance().SetPrimitiveViewRelevance(Result);
	}

	return Result;
}

void FNiagaraRenderer::SetDynamicData_RenderThread(FNiagaraDynamicDataBase* NewDynamicData)
{
	check(IsInRenderingThread());
	if (DynamicDataRender)
	{
		delete DynamicDataRender;
		DynamicDataRender = NULL;
	}
	DynamicDataRender = NewDynamicData;
}

struct FParticleOrderAsUint
{
	uint32 OrderAsUint;
	int32 Index;

	template <bool bStrictlyPositive, bool bAscending>
	FORCEINLINE void SetAsUint(int32 InIndex, float InOrder) 
	{
		const uint32 SortKeySignBit = 0x80000000;
		uint32 InOrderAsUint = reinterpret_cast<uint32&>(InOrder);
		InOrderAsUint = (bStrictlyPositive || InOrder >= 0) ? (InOrderAsUint | SortKeySignBit) : ~InOrderAsUint;
		OrderAsUint = bAscending ? InOrderAsUint : ~InOrderAsUint;

		Index = InIndex;
	}
		
	FORCEINLINE operator uint32() const { return OrderAsUint; }
};

void FNiagaraRenderer::SortIndices(const FNiagaraGPUSortInfo& SortInfo, int32 SortVarIdx, const FNiagaraDataBuffer& Buffer, FGlobalDynamicReadBuffer::FAllocation& OutIndices)const
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSortParticles);

	uint32 NumInstances = Buffer.GetNumInstances();
	check(OutIndices.ReadBuffer->NumBytes >= OutIndices.FirstIndex + NumInstances * sizeof(int32));
	check(SortInfo.SortMode != ENiagaraSortMode::None);
	check(VFVariables.IsValidIndex(SortVarIdx));
	check(SortInfo.SortAttributeOffset != INDEX_NONE);

	const bool bUseRadixSort = GNiagaraRadixSortThreshold != -1 && (int32)NumInstances  > GNiagaraRadixSortThreshold;

	int32* RESTRICT IndexBuffer = (int32*)(OutIndices.Buffer);

	FMemMark Mark(FMemStack::Get());
	FParticleOrderAsUint* RESTRICT ParticleOrder = (FParticleOrderAsUint*)FMemStack::Get().Alloc(sizeof(FParticleOrderAsUint) * NumInstances, alignof(FParticleOrderAsUint));

	if (SortInfo.SortMode == ENiagaraSortMode::ViewDepth || SortInfo.SortMode == ENiagaraSortMode::ViewDistance)
	{
		int32 BaseCompOffset = VFVariables[SortVarIdx].DatasetOffset;
		float* RESTRICT PositionX = (float*)Buffer.GetComponentPtrFloat(BaseCompOffset);
		float* RESTRICT PositionY = (float*)Buffer.GetComponentPtrFloat(BaseCompOffset + 1);
		float* RESTRICT PositionZ = (float*)Buffer.GetComponentPtrFloat(BaseCompOffset + 2);
		auto GetPos = [&PositionX, &PositionY, &PositionZ](int32 Idx)
		{
			return FVector(PositionX[Idx], PositionY[Idx], PositionZ[Idx]);
		};

		if (SortInfo.SortMode == ENiagaraSortMode::ViewDepth)
		{
			for (uint32 i = 0; i < NumInstances; ++i)
			{
				ParticleOrder[i].SetAsUint<true, false>(i, FVector::DotProduct(GetPos(i) - SortInfo.ViewOrigin, SortInfo.ViewDirection));
			}
		}
		else
		{
			for (uint32 i = 0; i < NumInstances; ++i)
			{
				ParticleOrder[i].SetAsUint<true, false>(i, (GetPos(i) - SortInfo.ViewOrigin).SizeSquared());
			}
		}
	}
	else
	{
		float* RESTRICT CustomSorting = (float*)Buffer.GetComponentPtrFloat(VFVariables[SortVarIdx].DatasetOffset);

		if (SortInfo.SortMode == ENiagaraSortMode::CustomAscending)
		{
			for (uint32 i = 0; i < NumInstances; ++i)
			{
				ParticleOrder[i].SetAsUint<false, true>(i, CustomSorting[i]);
			}
		}
		else // ENiagaraSortMode::CustomDecending
		{
			for (uint32 i = 0; i < NumInstances; ++i)
			{
				ParticleOrder[i].SetAsUint<false, false>(i, CustomSorting[i]);
			}
		}
	}

	if (!bUseRadixSort)
	{
		Sort(ParticleOrder, NumInstances, [](const FParticleOrderAsUint& A, const FParticleOrderAsUint& B) { return A.OrderAsUint < B.OrderAsUint; });
		//Now transfer to the real index buffer.
		for (uint32 i = 0; i < NumInstances; ++i)
		{
			IndexBuffer[i] = ParticleOrder[i].Index;
		}
	}
	else
	{
		FParticleOrderAsUint* RESTRICT ParticleOrderResult = bUseRadixSort ? (FParticleOrderAsUint*)FMemStack::Get().Alloc(sizeof(FParticleOrderAsUint) * NumInstances, alignof(FParticleOrderAsUint)) : nullptr;
		RadixSort32(ParticleOrderResult, ParticleOrder, NumInstances);
		//Now transfer to the real index buffer.
		for (uint32 i = 0; i < NumInstances; ++i)
		{
			IndexBuffer[i] = ParticleOrderResult[i].Index;
		}
	}
}
