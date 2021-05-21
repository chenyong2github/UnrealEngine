// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraCommon.h"

class FNiagaraSystemInstance;
class NiagaraEmitterInstanceBatcher;
struct FNiagaraComputeExecutionContext;
class FNiagaraGPUInstanceCountManager;
class FNiagaraGPUSystemTick;

namespace ENiagaraGpuComputeTickStage
{
	enum Type
	{
		PreInitViews,
		PostInitViews,
		PostOpaqueRender,
		Max,
		First = PreInitViews,
		Last = PostOpaqueRender,
	};
};

class FNiagaraSystemGpuComputeProxy
{
	friend class NiagaraEmitterInstanceBatcher;

public:
	FNiagaraSystemGpuComputeProxy(FNiagaraSystemInstance* OwnerInstance);
	~FNiagaraSystemGpuComputeProxy();

	void AddToBatcher(NiagaraEmitterInstanceBatcher* Batcher);
	void RemoveFromBatcher(NiagaraEmitterInstanceBatcher* Batcher, bool bDeleteProxy);

	FNiagaraSystemInstanceID GetSystemInstanceID() const { return SystemInstanceID; }
	ENiagaraGpuComputeTickStage::Type GetComputeTickStage() const { return ComputeTickStage; }
	void QueueTick(const FNiagaraGPUSystemTick& Tick);
	void ReleaseTicks(FNiagaraGPUInstanceCountManager& GPUInstanceCountManager);

	bool RequiresDistanceFieldData() const { return bRequiresDistanceFieldData; }
	bool RequiresDepthBuffer() const { return bRequiresDepthBuffer; }
	bool RequiresEarlyViewData() const { return bRequiresEarlyViewData; }
	bool RequiresViewUniformBuffer() const { return bRequiresViewUniformBuffer; }

private:
	FNiagaraSystemInstance*						DebugOwnerInstance = nullptr;
	NiagaraEmitterInstanceBatcher*				DebugOwnerBatcher = nullptr;
	int32										BatcherIndex = INDEX_NONE;

	FNiagaraSystemInstanceID					SystemInstanceID = FNiagaraSystemInstanceID();
	ENiagaraGpuComputeTickStage::Type			ComputeTickStage = ENiagaraGpuComputeTickStage::PostOpaqueRender;
	uint32										bRequiresDistanceFieldData : 1;
	uint32										bRequiresDepthBuffer : 1;
	uint32										bRequiresEarlyViewData : 1;
	uint32										bRequiresViewUniformBuffer : 1;

	TArray<FNiagaraComputeExecutionContext*>	ComputeContexts;
	TArray<FNiagaraGPUSystemTick>				PendingTicks;
};
