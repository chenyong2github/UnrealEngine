// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraCommon.h"
#include "RHI.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "HAL/Platform.h"
#include "Misc/Build.h"
#include "Particles/ParticlePerfStats.h"

#include "NiagaraGPUProfilerInterface.h"

#if WITH_NIAGARA_GPU_PROFILER

/** Helper class to time gpu runtime cost of dispatches */
class FNiagaraGPUProfiler : public FNiagaraGPUProfilerInterface
{
	static constexpr int32 NumBufferFrames = 5;

	struct FGpuStageTimer
	{
		int32					NumDispatchGroups = 0;
		int32					NumDispatches = 0;
		FRHIPooledRenderQuery	StartQuery;
		FRHIPooledRenderQuery	EndQuery;
	};

	struct FGpuDispatchTimer
	{
		uint32									bUniqueInstance : 1;
		TWeakObjectPtr<class USceneComponent>	OwnerComponent;
		TWeakObjectPtr<class UNiagaraEmitter>	OwnerEmitter;
		FName									StageName;
		FRHIPooledRenderQuery					StartQuery;
		FRHIPooledRenderQuery					EndQuery;
	};

	struct FGpuFrameData
	{
		bool CanWrite() const { return EndQuery.GetQuery() == nullptr; }
		bool CanRead() const { return EndQuery.GetQuery() != nullptr; }

		FRHIPooledRenderQuery		EndQuery;
		FGpuStageTimer				StageTimers[ENiagaraGpuComputeTickStage::Max];
		TArray<FGpuDispatchTimer>	DispatchTimers;
	};

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnFrameResults, const FNiagaraGpuFrameResultsPtr&);

public:
	FNiagaraGPUProfiler(uintptr_t InOwnerContext);
	~FNiagaraGPUProfiler();

	void BeginFrame(FRHICommandListImmediate& RHICmdList);
	void EndFrame(FRHICommandList& RHICmdList);

	void BeginStage(FRHICommandList& RHICmdList, ENiagaraGpuComputeTickStage::Type TickStage, int32 NumDispatchGroups);
	void EndStage(FRHICommandList& RHICmdList, ENiagaraGpuComputeTickStage::Type TickStage, int32 NumDispatches);

	void BeginDispatch(FRHICommandList& RHICmdList, const struct FNiagaraGpuDispatchInstance& DispatchInstance);
	void BeginDispatch(FRHICommandList& RHICmdList, const struct FNiagaraComputeInstanceData& InstanceData, FName StageName);
	void BeginDispatch(FRHICommandList& RHICmdList, FName StageName);
	void EndDispatch(FRHICommandList& RHICmdList);

private:
	FGpuFrameData* GetReadFrame() { check(CurrentReadFrame >= 0 && CurrentReadFrame < UE_ARRAY_COUNT(GpuFrames)); return GpuFrames[CurrentReadFrame].CanRead() ? &GpuFrames[CurrentReadFrame] : nullptr; }
	FGpuFrameData* GetWriteFrame() { check(CurrentWriteFrame >= 0 && CurrentWriteFrame < UE_ARRAY_COUNT(GpuFrames)); return GpuFrames[CurrentWriteFrame].CanWrite() ? &GpuFrames[CurrentWriteFrame] : nullptr; }
	bool ProcessFrame(FRHICommandListImmediate& RHICmdList, FGpuFrameData& ReadFrame);

private:
	uintptr_t				OwnerContext = 0;

	int32					CurrentReadFrame = 0;					// Index of the next frame to read from
	int32					CurrentWriteFrame = 0;					// Index of the next frame to write into
	FGpuFrameData			GpuFrames[NumBufferFrames];

	FGpuFrameData*			ActiveWriteFrame = nullptr;				// Not null while we are generating a frame of data, otherwise null
	bool					bDispatchRecursionGuard = false;		// We don't support timing dispatches inside one another

	FRenderQueryPoolRHIRef	QueryPool;
};

#endif //WITH_NIAGARA_GPU_PROFILER
