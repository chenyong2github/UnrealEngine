// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AGXRHIPrivate.h"
#include "AGXCommandQueue.h"
#include "GPUProfiler.h"

// Stats
DECLARE_CYCLE_STAT_EXTERN(TEXT("MakeDrawable time"),STAT_AGXMakeDrawableTime,STATGROUP_AGXRHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Draw call time"),STAT_AGXDrawCallTime,STATGROUP_AGXRHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("PrepareDraw time"),STAT_AGXPrepareDrawTime,STATGROUP_AGXRHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("SwitchToRender time"),STAT_AGXSwitchToRenderTime,STATGROUP_AGXRHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("SwitchToCompute time"),STAT_AGXSwitchToComputeTime,STATGROUP_AGXRHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("SwitchToBlit time"),STAT_AGXSwitchToBlitTime,STATGROUP_AGXRHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("SwitchToAsyncBlit time"),STAT_AGXSwitchToAsyncBlitTime,STATGROUP_AGXRHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("PrepareToRender time"),STAT_AGXPrepareToRenderTime,STATGROUP_AGXRHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("PrepareToDispatch time"),STAT_AGXPrepareToDispatchTime,STATGROUP_AGXRHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("CommitRenderResourceTables time"),STAT_AGXCommitRenderResourceTablesTime,STATGROUP_AGXRHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("SetRenderState time"),STAT_AGXSetRenderStateTime,STATGROUP_AGXRHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("SetRenderPipelineState time"),STAT_AGXSetRenderPipelineStateTime,STATGROUP_AGXRHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("PipelineState time"),STAT_AGXPipelineStateTime,STATGROUP_AGXRHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Buffer Page-Off time"), STAT_AGXBufferPageOffTime, STATGROUP_AGXRHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Texture Page-Off time"), STAT_AGXTexturePageOffTime, STATGROUP_AGXRHI, );

DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Uniform Memory Allocated Per-Frame"), STAT_AGXUniformMemAlloc, STATGROUP_AGXRHI, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Uniform Memory Freed Per-Frame"), STAT_AGXUniformMemFreed, STATGROUP_AGXRHI, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Vertex Memory Allocated Per-Frame"), STAT_AGXVertexMemAlloc, STATGROUP_AGXRHI, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Vertex Memory Freed Per-Frame"), STAT_AGXVertexMemFreed, STATGROUP_AGXRHI, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Index Memory Allocated Per-Frame"), STAT_AGXIndexMemAlloc, STATGROUP_AGXRHI, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Index Memory Freed Per-Frame"), STAT_AGXIndexMemFreed, STATGROUP_AGXRHI, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Texture Memory Updated Per-Frame"), STAT_AGXTextureMemUpdate, STATGROUP_AGXRHI, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("Buffer Memory"), STAT_AGXBufferMemory, STATGROUP_AGXRHI, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("Texture Memory"), STAT_AGXTextureMemory, STATGROUP_AGXRHI, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("Heap Memory"), STAT_AGXHeapMemory, STATGROUP_AGXRHI, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("Unused Buffer Memory"), STAT_AGXBufferUnusedMemory, STATGROUP_AGXRHI, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("Unused Texture Memory"), STAT_AGXTextureUnusedMemory, STATGROUP_AGXRHI, );

DECLARE_MEMORY_STAT_EXTERN(TEXT("Uniform Memory In Flight"), STAT_AGXUniformMemoryInFlight, STATGROUP_AGXRHI, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("Allocated Uniform Pool Memory"), STAT_AGXUniformAllocatedMemory, STATGROUP_AGXRHI, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("Uniform Memory Per Frame"), STAT_AGXUniformBytesPerFrame, STATGROUP_AGXRHI, );

DECLARE_MEMORY_STAT_EXTERN(TEXT("General Frame Allocator Memory In Flight"), STAT_AGXFrameAllocatorMemoryInFlight, STATGROUP_AGXRHI, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("Allocated Frame Allocator Memory"), STAT_AGXFrameAllocatorAllocatedMemory, STATGROUP_AGXRHI, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("Frame Allocator Memory Per Frame"), STAT_AGXFrameAllocatorBytesPerFrame, STATGROUP_AGXRHI, );

DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Buffer Count"), STAT_AGXBufferCount, STATGROUP_AGXRHI, );
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Texture Count"), STAT_AGXTextureCount, STATGROUP_AGXRHI, );
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Heap Count"), STAT_AGXHeapCount, STATGROUP_AGXRHI, );
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Fence Count"), STAT_AGXFenceCount, STATGROUP_AGXRHI, );

DECLARE_CYCLE_STAT_EXTERN(TEXT("Texture Page-On time"), STAT_AGXTexturePageOnTime, STATGROUP_AGXRHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("GPU Work time"), STAT_AGXGPUWorkTime, STATGROUP_AGXRHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("GPU Idle time"), STAT_AGXGPUIdleTime, STATGROUP_AGXRHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Present time"), STAT_AGXPresentTime, STATGROUP_AGXRHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("CustomPresent time"), STAT_AGXCustomPresentTime, STATGROUP_AGXRHI, );
#if STATS
extern int64 volatile GAGXTexturePageOnTime;
extern int64 volatile GAGXGPUWorkTime;
extern int64 volatile GAGXGPUIdleTime;
extern int64 volatile GAGXPresentTime;
#endif

DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Number Command Buffers Created Per-Frame"), STAT_AGXCommandBufferCreatedPerFrame, STATGROUP_AGXRHI, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Number Command Buffers Committed Per-Frame"), STAT_AGXCommandBufferCommittedPerFrame, STATGROUP_AGXRHI, );

/** A single perf event node, which tracks information about a appBeginDrawEvent/appEndDrawEvent range. */
class FAGXEventNode : public FGPUProfilerEventNode
{
public:
	
	FAGXEventNode(FAGXContext* InContext, const TCHAR* InName, FGPUProfilerEventNode* InParent, bool bIsRoot, bool bInFullProfiling)
	: FGPUProfilerEventNode(InName, InParent)
	, StartTime(0)
	, EndTime(0)
	, Context(InContext)
	, bRoot(bIsRoot)
    , bFullProfiling(bInFullProfiling)
	{
	}
	
	virtual ~FAGXEventNode();
	
	/**
	 * Returns the time in ms that the GPU spent in this draw event.
	 * This blocks the CPU if necessary, so can cause hitching.
	 */
	virtual float GetTiming() override;
	
	virtual void StartTiming() override;
	
	virtual void StopTiming() override;
	
	mtlpp::CommandBufferHandler Start(void);
	mtlpp::CommandBufferHandler Stop(void);

	bool Wait() const { return bRoot && bFullProfiling; }
	bool IsRoot() const { return bRoot; }
	
	uint64 GetCycles() { return EndTime - StartTime; }
	
	uint64 StartTime;
	uint64 EndTime;
private:
	FAGXContext* Context;
	bool bRoot;
    bool bFullProfiling;
};

/** An entire frame of perf event nodes, including ancillary timers. */
class FAGXEventNodeFrame : public FGPUProfilerEventNodeFrame
{
public:
	FAGXEventNodeFrame(FAGXContext* InContext, bool bInFullProfiling)
	: RootNode(new FAGXEventNode(InContext, TEXT("Frame"), nullptr, true, bInFullProfiling))
    , bFullProfiling(bInFullProfiling)
	{
	}
	
	virtual ~FAGXEventNodeFrame()
	{
        if(bFullProfiling)
        {
            delete RootNode;
        }
	}
	
	/** Start this frame of per tracking */
	virtual void StartFrame() override;
	
	/** End this frame of per tracking, but do not block yet */
	virtual void EndFrame() override;
	
	/** Calculates root timing base frequency (if needed by this RHI) */
	virtual float GetRootTimingResults() override;
	
	virtual void LogDisjointQuery() override;
	
	FAGXEventNode* RootNode;
    bool bFullProfiling;
};

// This class has multiple inheritance but really FGPUTiming is a static class
class FAGXGPUTiming : public FGPUTiming
{
public:
	
	/**
	 * Constructor.
	 */
	FAGXGPUTiming()
	{
		StaticInitialize(nullptr, PlatformStaticInitialize);
	}
	
	void SetCalibrationTimestamp(uint64 GPU, uint64 CPU)
	{
		FGPUTiming::SetCalibrationTimestamp({ GPU, CPU });
	}
	
private:
	
	/**
	 * Initializes the static variables, if necessary.
	 */
	static void PlatformStaticInitialize(void* UserData)
	{
		// Are the static variables initialized?
		if ( !GAreGlobalsInitialized )
		{
			GIsSupported = true;
			SetTimingFrequency(1000 * 1000 * 1000);
			GAreGlobalsInitialized = true;
		}
	}
};

struct IAGXStatsScope
{
	FString Name;
	FString Parent;
	TArray<IAGXStatsScope*> Children;
	
	uint64 CPUStartTime;
	uint64 CPUEndTime;
	
	uint64 GPUStartTime;
	uint64 GPUEndTime;
	
	uint64 CPUThreadIndex;
	uint64 GPUThreadIndex;
	
	virtual ~IAGXStatsScope();
	
	virtual void Start(mtlpp::CommandBuffer const& Buffer) = 0;
	virtual void End(mtlpp::CommandBuffer const& Buffer) = 0;
	
	FString GetJSONRepresentation(uint32 PID);
};

struct FAGXCPUStats : public IAGXStatsScope
{
	FAGXCPUStats(FString const& Name);
	virtual ~FAGXCPUStats();
	
	void Start(void);
	void End(void);
	
	virtual void Start(mtlpp::CommandBuffer const& Buffer) final override;
	virtual void End(mtlpp::CommandBuffer const& Buffer) final override;
};

struct FAGXDisplayStats : public IAGXStatsScope
{
	FAGXDisplayStats(uint32 DisplayID, double OutputSeconds, double Duration);
	virtual ~FAGXDisplayStats();
	
	virtual void Start(mtlpp::CommandBuffer const& Buffer) final override;
	virtual void End(mtlpp::CommandBuffer const& Buffer) final override;
};

enum EMTLFenceType
{
	EMTLFenceTypeWait,
	EMTLFenceTypeUpdate,
};

struct FAGXCommandBufferStats : public IAGXStatsScope
{
	FAGXCommandBufferStats(mtlpp::CommandBuffer const& Buffer, uint64 GPUThreadIndex);
	virtual ~FAGXCommandBufferStats();
	
	virtual void Start(mtlpp::CommandBuffer const& Buffer) final override;
	virtual void End(mtlpp::CommandBuffer const& Buffer) final override;

	ns::AutoReleased<mtlpp::CommandBuffer> CmdBuffer;
};

/**
 * Simple struct to hold sortable command buffer start and end timestamps.
 */
struct FAGXCommandBufferTiming
{
	CFTimeInterval StartTime;
	CFTimeInterval EndTime;

	bool operator<(const FAGXCommandBufferTiming& RHS) const
	{
		// Sort by start time and then by length if the commandbuffer started at the same time
		if (this->StartTime < RHS.StartTime)
		{
			return true;
		}
		else if ((this->StartTime == RHS.StartTime) && (this->EndTime > RHS.EndTime))
		{
			return true;
		}
		return false;
	}
};

/**
 * Encapsulates GPU profiling logic and data.
 * There's only one global instance of this struct so it should only contain global data, nothing specific to a frame.
 */
struct FAGXGPUProfiler : public FGPUProfiler
{
	/** GPU hitch profile histories */
	TIndirectArray<FAGXEventNodeFrame> GPUHitchEventNodeFrames;
	
	FAGXGPUProfiler(FAGXContext* InContext)
	:	FGPUProfiler()
	,	Context(InContext)
	,   NumNestedFrames(0)
	{}
	
	virtual ~FAGXGPUProfiler() {}
	
	virtual FGPUProfilerEventNode* CreateEventNode(const TCHAR* InName, FGPUProfilerEventNode* InParent) override;
	
	void Cleanup();
	
	virtual void PushEvent(const TCHAR* Name, FColor Color) override;
	virtual void PopEvent() override;
	
	void BeginFrame();
	void EndFrame();
	
	// WARNING:
	// These functions MUST be called from within Metal scheduled/completion handlers
	// since they depend on libdispatch to enforce ordering.
	static void RecordFrame(TArray<FAGXCommandBufferTiming>& CommandBufferTimings, FAGXCommandBufferTiming& LastPresentBufferTiming);
	static void RecordPresent(const mtlpp::CommandBuffer& Buffer);
	// END WARNING
	
	FAGXGPUTiming TimingSupport;
	FAGXContext* Context;
	int32 NumNestedFrames;
};

class FAGXProfiler : public FAGXGPUProfiler
{
	static FAGXProfiler* Self;
public:
	FAGXProfiler(FAGXContext* InContext);
	~FAGXProfiler();
	
	static FAGXProfiler* CreateProfiler(FAGXContext* InContext);
	static FAGXProfiler* GetProfiler();
	static void DestroyProfiler();
	
	void BeginCapture(int InNumFramesToCapture = -1);
	void EndCapture();
	bool TracingEnabled() const;
	
	void BeginFrame();
	void EndFrame();
	
	void AddDisplayVBlank(uint32 DisplayID, double OutputSeconds, double OutputDuration);
	
	void EncodeDraw(FAGXCommandBufferStats* CmdBufStats, char const* DrawCall, uint32 RHIPrimitives, uint32 RHIVertices, uint32 RHIInstances);
	void EncodeBlit(FAGXCommandBufferStats* CmdBufStats, char const* DrawCall);
	void EncodeBlit(FAGXCommandBufferStats* CmdBufStats, FString DrawCall);
	void EncodeDispatch(FAGXCommandBufferStats* CmdBufStats, char const* DrawCall);
	
	FAGXCPUStats* AddCPUStat(FString const& Name);
	FAGXCommandBufferStats* AllocateCommandBuffer(mtlpp::CommandBuffer const& Buffer, uint64 GPUThreadIndex);
	void AddCommandBuffer(FAGXCommandBufferStats* CommandBuffer);
	virtual void PushEvent(const TCHAR* Name, FColor Color) final override;
	virtual void PopEvent() final override;
	
	void SaveTrace();
	
private:
	FCriticalSection Mutex;
	
	TArray<FAGXCommandBufferStats*> TracedBuffers;
	TArray<FAGXDisplayStats*> DisplayStats;
	TArray<FAGXCPUStats*> CPUStats;
	
	int32 NumFramesToCapture;
	int32 CaptureFrameNumber;
	
	bool bRequestStartCapture;
	bool bRequestStopCapture;
	bool bEnabled;
};

struct FAGXScopedCPUStats
{
	FAGXScopedCPUStats(FString const& Name)
	: Stats(nullptr)
	{
		FAGXProfiler* Profiler = FAGXProfiler::GetProfiler();
		if (Profiler)
		{
			Stats = Profiler->AddCPUStat(Name);
			if (Stats)
			{
				Stats->Start();
			}
		}
	}
	
	~FAGXScopedCPUStats()
	{
		if (Stats)
		{
			Stats->End();
		}
	}
	
	FAGXCPUStats* Stats;
};
