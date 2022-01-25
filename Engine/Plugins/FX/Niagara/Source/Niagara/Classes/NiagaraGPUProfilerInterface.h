// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraCommon.h"
#include "NiagaraSystemGpuComputeProxy.h"

//////////////////////////////////////////////////////////////////////////
// Public API for tracking GPU time when the profiler is enabled
struct NIAGARA_API FNiagaraGpuProfileScope
{
#if WITH_NIAGARA_GPU_PROFILER
	FNiagaraGpuProfileScope(FRHICommandList& RHICmdList, const class FNiagaraGpuComputeDispatchInterface* ComputeDispatchInterface, FName StageName);
	FNiagaraGpuProfileScope(FRHICommandList& RHICmdList, const class FNiagaraGpuComputeDispatchInterface* ComputeDispatchInterface, const struct FNiagaraGpuDispatchInstance& DispatchInstance);
	FNiagaraGpuProfileScope(FRHICommandList& RHICmdList, const struct FNiagaraDataInterfaceArgs& Context, FName StageName);
	FNiagaraGpuProfileScope(FRHICommandList& RHICmdList, const struct FNiagaraDataInterfaceSetArgs& Context, FName StageName);
	FNiagaraGpuProfileScope(FRHICommandList& RHICmdList, const struct FNiagaraDataInterfaceStageArgs& Context, FName StageName);
	~FNiagaraGpuProfileScope();

private:
	FRHICommandList& RHICmdList;
	class FNiagaraGPUProfiler* GPUProfiler = nullptr;
#else
	FNiagaraGpuProfileScope(FRHICommandList& RHICmdList, const class FNiagaraGpuComputeDispatchInterface* ComputeDispatchInterface, FName StageName) {}
	FNiagaraGpuProfileScope(FRHICommandList& RHICmdList, const class FNiagaraGpuComputeDispatchInterface* ComputeDispatchInterface, const struct FNiagaraGpuDispatchInstance& DispatchInstance) {}
	FNiagaraGpuProfileScope(FRHICommandList& RHICmdList, const struct FNiagaraDataInterfaceArgs& Context, FName StageName) {}
	FNiagaraGpuProfileScope(FRHICommandList& RHICmdList, const struct FNiagaraDataInterfaceSetArgs& Context, FName StageName) {}
	FNiagaraGpuProfileScope(FRHICommandList& RHICmdList, const struct FNiagaraDataInterfaceStageArgs& Context, FName StageName) {}
	~FNiagaraGpuProfileScope() {}
#endif
};

#if WITH_NIAGARA_GPU_PROFILER
//////////////////////////////////////////////////////////////////////////
/** Results generated when the frame is ready and sent to that game thread */
struct FNiagaraGpuFrameResults : public TSharedFromThis<FNiagaraGpuFrameResults, ESPMode::ThreadSafe>
{
	struct FStageResults
	{
		int32	NumDispatchGroups = 0;
		int32	NumDispatches = 0;
		uint64	DurationMicroseconds = 0;
	};

	struct FDispatchResults
	{
		uint32									bUniqueInstance : 1;		// Set only once for all dispatches from an instance across all ticks
		TWeakObjectPtr<class USceneComponent>	OwnerComponent;				// Optional pointer back to owning Component
		TWeakObjectPtr<class UNiagaraEmitter>	OwnerEmitter;				// Optional pointer back to owning Emitter
		FName									StageName;					// Generally the simulation stage but may be a DataInterface name
		uint64									DurationMicroseconds;		// Duration in microseconds of the dispatch
	};

	uintptr_t					OwnerContext = 0;
	FStageResults				StageResults[ENiagaraGpuComputeTickStage::Max];
	TArray<FDispatchResults>	DispatchResults;
};

using FNiagaraGpuFrameResultsPtr = TSharedPtr<FNiagaraGpuFrameResults, ESPMode::ThreadSafe>;

//////////////////////////////////////////////////////////////////////////
// Allows various systems to listen to profiler results
struct NIAGARA_API FNiagaraGpuProfilerListener
{
	FNiagaraGpuProfilerListener();
	~FNiagaraGpuProfilerListener();

	void SetEnabled(bool bEnabled);
	void SetHandler(TFunction<void(const FNiagaraGpuFrameResultsPtr&)> Function);
	bool IsEnabled() const { return bEnabled; }

private:
	bool			bEnabled = false;
	FDelegateHandle	GameThreadHandler;
};

//////////////////////////////////////////////////////////////////////////
/** Public API to Niagara GPU Profiling. */
class FNiagaraGPUProfilerInterface
{
	friend FNiagaraGpuProfilerListener;
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnFrameResults, const FNiagaraGpuFrameResultsPtr&);

public:
	static FOnFrameResults& GetOnFrameResults_GameThread() { check(IsInGameThread()); return OnFrameResults_GameThread; }
	static FOnFrameResults& GetOnFrameResults_RenderThread() { check(IsInRenderingThread()); return OnFrameResults_RenderThread; }

protected:
	void PostResults(const FNiagaraGpuFrameResultsPtr& FrameResults);

protected:
	static std::atomic<int>	NumReaders;
	static FOnFrameResults	OnFrameResults_GameThread;
	static FOnFrameResults	OnFrameResults_RenderThread;
};

#endif //WITH_NIAGARA_GPU_PROFILER

