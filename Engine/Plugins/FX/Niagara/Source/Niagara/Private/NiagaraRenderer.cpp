// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraRenderer.h"
#include "ParticleResources.h"
#include "ParticleBeamTrailVertexFactory.h"
#include "NiagaraDataSet.h"
#include "NiagaraStats.h"
#include "NiagaraVertexFactory.h"
#include "Engine/Engine.h"
#include "DynamicBufferAllocator.h"
#include "NiagaraEmitterInstanceBatcher.h"

DECLARE_CYCLE_STAT(TEXT("Sort Particles"), STAT_NiagaraSortParticles, STATGROUP_Niagara);

DECLARE_CYCLE_STAT(TEXT("Global Float Alloc - All"), STAT_NiagaraAllocateGlobalFloatAll, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Global Float Alloc - InsideLock"), STAT_NiagaraAllocateGlobalFloatInsideLock, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Global Float Alloc - Alloc New Buffer"), STAT_NiagaraAllocateGlobalFloatAllocNew, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Global Float Alloc - Map Buffer"), STAT_NiagaraAllocateGlobalFloatMapBuffer, STATGROUP_Niagara);

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


FNiagaraRenderer::FNiagaraRenderer(ERHIFeatureLevel::Type FeatureLevel, const UNiagaraRendererProperties *InProps, const FNiagaraEmitterInstance* Emitter)
	: DynamicDataRender(nullptr)
	, CPUTimeMS(0.0f)
	, bLocalSpace(Emitter->GetCachedEmitter()->bLocalSpace)
	, bHasLights(false)
	, SimTarget(Emitter->GetCachedEmitter()->SimTarget)
	, NumIndicesPerInstance(InProps ? InProps->GetNumIndicesPerInstance() : 0)
{
#if STATS
	EmitterStatID = Emitter->GetCachedEmitter()->GetStatID(false, false);
#endif
}

void FNiagaraRenderer::Initialize(ERHIFeatureLevel::Type FeatureLevel, const UNiagaraRendererProperties *InProps, const FNiagaraEmitterInstance* Emitter)
{
	//Get our list of valid base materials. Fall back to default material if they're not valid.
	InProps->GetUsedMaterials(BaseMaterials_GT);
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
	ReleaseRenderThreadResources(nullptr);
	SetDynamicData_RenderThread(nullptr);
}

void FNiagaraRenderer::CreateRenderThreadResources(NiagaraEmitterInstanceBatcher* Batcher) 
{
	if (Batcher && SimTarget == ENiagaraSimTarget::GPUComputeSim)
	{
		Batcher->GetGPUInstanceCounterManager().IncrementMaxDrawIndirectCount();
	}
}

void FNiagaraRenderer::ReleaseRenderThreadResources(NiagaraEmitterInstanceBatcher* Batcher)
{
	if (Batcher && SimTarget == ENiagaraSimTarget::GPUComputeSim)
	{
		Batcher->GetGPUInstanceCounterManager().DecrementMaxDrawIndirectCount();
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

void FNiagaraRenderer::SortIndices(ENiagaraSortMode SortMode, int32 SortAttributeOffset, const FNiagaraDataBuffer& Buffer, const FMatrix& LocalToWorld, const FSceneView* View, FGlobalDynamicReadBuffer::FAllocation& OutIndices)const
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSortParticles);

	uint32 NumInstances = Buffer.GetNumInstances();
	check(OutIndices.ReadBuffer->NumBytes >= OutIndices.FirstIndex + NumInstances * sizeof(int32));
	check(SortMode != ENiagaraSortMode::None);
	check(SortAttributeOffset != INDEX_NONE);

	int32* RESTRICT IndexBuffer = (int32*)(OutIndices.Buffer);

	struct FParticleOrder
	{
		int32 Index;
		float Order;
	};
	FMemMark Mark(FMemStack::Get());
	FParticleOrder* RESTRICT ParticleOrder = (FParticleOrder*)FMemStack::Get().Alloc(sizeof(FParticleOrder) * NumInstances, alignof(FParticleOrder));

	auto SortAscending = [](const FParticleOrder& A, const FParticleOrder& B) { return A.Order < B.Order; };
	auto SortDescending = [](const FParticleOrder& A, const FParticleOrder& B) { return A.Order > B.Order; };

	if (SortMode == ENiagaraSortMode::ViewDepth || SortMode == ENiagaraSortMode::ViewDistance)
	{
		float* RESTRICT PositionX = (float*)Buffer.GetComponentPtrFloat(SortAttributeOffset);
		float* RESTRICT PositionY = (float*)Buffer.GetComponentPtrFloat(SortAttributeOffset + 1);
		float* RESTRICT PositionZ = (float*)Buffer.GetComponentPtrFloat(SortAttributeOffset + 2);
		auto GetPos = [&PositionX, &PositionY, &PositionZ](int32 Idx)
		{
			return FVector(PositionX[Idx], PositionY[Idx], PositionZ[Idx]);
		};

		//TODO Parallelize in batches? Move to GPU for large emitters?
		if (SortMode == ENiagaraSortMode::ViewDepth)
		{
			FMatrix ViewProjMatrix = View->ViewMatrices.GetViewProjectionMatrix();
			if (bLocalSpace)
			{
				for (uint32 i = 0; i < NumInstances; ++i)
				{
					ParticleOrder[i].Index = i;
					ParticleOrder[i].Order = ViewProjMatrix.TransformPosition(LocalToWorld.TransformPosition(GetPos(i))).W;
				}
			}
			else
			{
				for (uint32 i = 0; i < NumInstances; ++i)
				{
					ParticleOrder[i].Index = i;
					ParticleOrder[i].Order = ViewProjMatrix.TransformPosition(GetPos(i)).W;
				}
			}

			Sort(ParticleOrder, NumInstances, SortDescending);
		}
		else 
		{
			// check(SortMode == ENiagaraSortMode::ViewDistance); Should always be true because we are within this block. If code is moved or copied, please change.
			FVector ViewOrigin = View->ViewMatrices.GetViewOrigin();
			if (bLocalSpace)
			{
				for (uint32 i = 0; i < NumInstances; ++i)
				{
					ParticleOrder[i].Index = i;
					ParticleOrder[i].Order = (ViewOrigin - LocalToWorld.TransformPosition(GetPos(i))).SizeSquared();
				}
			}
			else
			{
				for (uint32 i = 0; i < NumInstances; ++i)
				{
					ParticleOrder[i].Index = i;
					ParticleOrder[i].Order = (ViewOrigin - GetPos(i)).SizeSquared();
				}
			}

			Sort(ParticleOrder, NumInstances, SortDescending);
		}
	}
	else
	{
		float* RESTRICT CustomSorting = (float*)Buffer.GetComponentPtrFloat(SortAttributeOffset);
		for (uint32 i = 0; i < NumInstances; ++i)
		{
			ParticleOrder[i].Index = i;
			ParticleOrder[i].Order = CustomSorting[i];
		}
		if (SortMode == ENiagaraSortMode::CustomAscending)
		{
			Sort(ParticleOrder, NumInstances, SortAscending);
		}
		else if (SortMode == ENiagaraSortMode::CustomDecending)
		{
			Sort(ParticleOrder, NumInstances, SortDescending);
		}
	}

	//Now transfer to the real index buffer.
	for (uint32 i = 0; i < NumInstances; ++i)
	{
		IndexBuffer[i] = ParticleOrder[i].Index;
	}
}
