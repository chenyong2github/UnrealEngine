// Copyright Epic Games, Inc. All Rights Reserved.

#include "AGXRHIPrivate.h"
#include "AGXProfiler.h"
#include "EngineGlobals.h"
#include "StaticBoundShaderState.h"
#include "HAL/FileManager.h"

DEFINE_STAT(STAT_AGXUniformMemAlloc);
DEFINE_STAT(STAT_AGXUniformMemFreed);
DEFINE_STAT(STAT_AGXVertexMemAlloc);
DEFINE_STAT(STAT_AGXVertexMemFreed);
DEFINE_STAT(STAT_AGXIndexMemAlloc);
DEFINE_STAT(STAT_AGXIndexMemFreed);
DEFINE_STAT(STAT_AGXTextureMemUpdate);

DEFINE_STAT(STAT_AGXDrawCallTime);
DEFINE_STAT(STAT_AGXPipelineStateTime);
DEFINE_STAT(STAT_AGXPrepareDrawTime);

DEFINE_STAT(STAT_AGXSwitchToRenderTime);
DEFINE_STAT(STAT_AGXSwitchToComputeTime);
DEFINE_STAT(STAT_AGXSwitchToBlitTime);
DEFINE_STAT(STAT_AGXSwitchToAsyncBlitTime);
DEFINE_STAT(STAT_AGXPrepareToRenderTime);
DEFINE_STAT(STAT_AGXPrepareToDispatchTime);
DEFINE_STAT(STAT_AGXCommitRenderResourceTablesTime);
DEFINE_STAT(STAT_AGXSetRenderStateTime);
DEFINE_STAT(STAT_AGXSetRenderPipelineStateTime);

DEFINE_STAT(STAT_AGXMakeDrawableTime);
DEFINE_STAT(STAT_AGXBufferPageOffTime);
DEFINE_STAT(STAT_AGXTexturePageOnTime);
DEFINE_STAT(STAT_AGXTexturePageOffTime);
DEFINE_STAT(STAT_AGXGPUWorkTime);
DEFINE_STAT(STAT_AGXGPUIdleTime);
DEFINE_STAT(STAT_AGXPresentTime);
DEFINE_STAT(STAT_AGXCustomPresentTime);
DEFINE_STAT(STAT_AGXCommandBufferCreatedPerFrame);
DEFINE_STAT(STAT_AGXCommandBufferCommittedPerFrame);
DEFINE_STAT(STAT_AGXBufferMemory);
DEFINE_STAT(STAT_AGXTextureMemory);
DEFINE_STAT(STAT_AGXHeapMemory);
DEFINE_STAT(STAT_AGXBufferUnusedMemory);
DEFINE_STAT(STAT_AGXTextureUnusedMemory);
DEFINE_STAT(STAT_AGXBufferCount);
DEFINE_STAT(STAT_AGXTextureCount);
DEFINE_STAT(STAT_AGXHeapCount);
DEFINE_STAT(STAT_AGXFenceCount);

DEFINE_STAT(STAT_AGXUniformMemoryInFlight);
DEFINE_STAT(STAT_AGXUniformAllocatedMemory);
DEFINE_STAT(STAT_AGXUniformBytesPerFrame);

DEFINE_STAT(STAT_AGXFrameAllocatorMemoryInFlight);
DEFINE_STAT(STAT_AGXFrameAllocatorAllocatedMemory);
DEFINE_STAT(STAT_AGXFrameAllocatorBytesPerFrame);

int64 volatile GAGXTexturePageOnTime = 0;
int64 volatile GAGXGPUWorkTime = 0;
int64 volatile GAGXGPUIdleTime = 0;
int64 volatile GAGXPresentTime = 0;

static void AGXWriteString(FArchive* OutputFile, const char* String)
{
	OutputFile->Serialize((void*)String, sizeof(ANSICHAR)*FCStringAnsi::Strlen(String));
}

FAGXEventNode::~FAGXEventNode()
{
}

float FAGXEventNode::GetTiming()
{
	return FPlatformTime::ToSeconds(EndTime - StartTime);
}

void FAGXEventNode::StartTiming()
{
	StartTime = 0;
	EndTime = 0;

	Context->StartTiming(this);
}

mtlpp::CommandBufferHandler FAGXEventNode::Start(void)
{
	mtlpp::CommandBufferHandler Block = [this](mtlpp::CommandBuffer const& CompletedBuffer)
	{
		const CFTimeInterval GpuTimeSeconds = CompletedBuffer.GetGpuStartTime();
		const double CyclesPerSecond = 1.0 / FPlatformTime::GetSecondsPerCycle();
		StartTime = GpuTimeSeconds * CyclesPerSecond;
	};
    return Block_copy(Block);
}

void FAGXEventNode::StopTiming()
{
	Context->EndTiming(this);
}

mtlpp::CommandBufferHandler FAGXEventNode::Stop(void)
{
	mtlpp::CommandBufferHandler Block = [this](mtlpp::CommandBuffer const& CompletedBuffer)
	{
		// This is still used by ProfileGPU
		const CFTimeInterval GpuTimeSeconds = CompletedBuffer.GetGpuEndTime();
		const double CyclesPerSecond = 1.0 / FPlatformTime::GetSecondsPerCycle();
		EndTime = GpuTimeSeconds * CyclesPerSecond;
		
		if(bRoot)
		{
			if(!bFullProfiling)
			{
				delete this;
			}
		}
	};
	return Block_copy(Block);
}

bool AGXGPUProfilerIsInSafeThread()
{
	return (GIsAGXInitialized && !GIsRHIInitialized) || (IsInRHIThread() || IsInActualRenderingThread());
}
	
/** Start this frame of per tracking */
void FAGXEventNodeFrame::StartFrame()
{
	RootNode->StartTiming();
}

/** End this frame of per tracking, but do not block yet */
void FAGXEventNodeFrame::EndFrame()
{
	RootNode->StopTiming();
}

/** Calculates root timing base frequency (if needed by this RHI) */
float FAGXEventNodeFrame::GetRootTimingResults()
{
	return RootNode->GetTiming();
}

void FAGXEventNodeFrame::LogDisjointQuery()
{
	
}

FGPUProfilerEventNode* FAGXGPUProfiler::CreateEventNode(const TCHAR* InName, FGPUProfilerEventNode* InParent)
{
#if ENABLE_METAL_GPUPROFILE
	FAGXEventNode* EventNode = new FAGXEventNode(FAGXContext::GetCurrentContext(), InName, InParent, false, false);
	return EventNode;
#else
	return nullptr;
#endif
}

void FAGXGPUProfiler::Cleanup()
{
	
}

void FAGXGPUProfiler::PushEvent(const TCHAR* Name, FColor Color)
{
	if(AGXGPUProfilerIsInSafeThread())
	{
		FGPUProfiler::PushEvent(Name, Color);
	}
}

void FAGXGPUProfiler::PopEvent()
{
	if(AGXGPUProfilerIsInSafeThread())
	{
		FGPUProfiler::PopEvent();
	}
}

//TGlobalResource<FVector4VertexDeclaration> GAGXVector4VertexDeclaration;
TGlobalResource<FTexture> GAGXLongTaskRT;

void FAGXGPUProfiler::BeginFrame()
{
	if(!CurrentEventNodeFrame)
	{
		// Start tracking the frame
		CurrentEventNodeFrame = new FAGXEventNodeFrame(Context, GTriggerGPUProfile);
		CurrentEventNodeFrame->StartFrame();
		
		if(GNumAlternateFrameRenderingGroups > 1)
		{
			GTriggerGPUProfile = false;
		}

		if(GTriggerGPUProfile)
		{
			bTrackingEvents = true;
			bLatchedGProfilingGPU = true;
			GTriggerGPUProfile = false;
		}
	}
	NumNestedFrames++;
}

void FAGXGPUProfiler::EndFrame()
{
	if(--NumNestedFrames == 0)
	{
		dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
#if PLATFORM_MAC
			FPlatformMisc::UpdateDriverMonitorStatistics(GetAGXDeviceContext().GetDeviceIndex());
#endif
		});
#if STATS
		SET_CYCLE_COUNTER(STAT_AGXTexturePageOnTime, GAGXTexturePageOnTime);
		GAGXTexturePageOnTime = 0;
		
		SET_CYCLE_COUNTER(STAT_AGXGPUIdleTime, GAGXGPUIdleTime);
		SET_CYCLE_COUNTER(STAT_AGXGPUWorkTime, GAGXGPUWorkTime);
		SET_CYCLE_COUNTER(STAT_AGXPresentTime, GAGXPresentTime);
#endif
		
		if(CurrentEventNodeFrame)
		{
			CurrentEventNodeFrame->EndFrame();
			
			if(bLatchedGProfilingGPU)
			{
				bTrackingEvents = false;
				bLatchedGProfilingGPU = false;
			
				UE_LOG(LogRHI, Warning, TEXT(""));
				UE_LOG(LogRHI, Warning, TEXT(""));
				CurrentEventNodeFrame->DumpEventTree();
			}
			
			delete CurrentEventNodeFrame;
			CurrentEventNodeFrame = NULL;
		}
	}
}

// WARNING:
// All these recording functions MUST be called from within scheduled/completion handlers.
// Ordering is enforced by libdispatch so calling these outside of that context WILL result in
// incorrect values.
void FAGXGPUProfiler::RecordFrame(TArray<FAGXCommandBufferTiming>& CommandBufferTimings, FAGXCommandBufferTiming& LastBufferTiming)
{
	const double CyclesPerSecond = 1.0 / FPlatformTime::GetSecondsPerCycle();

	double RunningFrameTimeSeconds = 0.0;
	uint64 FrameStartGPUCycles = 0;
	uint64 FrameEndGPUCycles = 0;

	// Sort the timings
	CommandBufferTimings.Sort();

	CFTimeInterval FirstStartTime = 0.0;

	// Add the timings excluding any overlapping time
	for (const FAGXCommandBufferTiming& Timing : CommandBufferTimings)
	{
		if (FirstStartTime == 0.0)
		{
			FirstStartTime = Timing.StartTime;
		}
		
		// Only process if the previous buffer finished before the end of this one
		if (LastBufferTiming.EndTime < Timing.EndTime)
		{
			// Check if the end of the previous buffer finished before the start of this one
			if (LastBufferTiming.EndTime > Timing.StartTime)
			{
				// Segment from end of last buffer to end of current
				RunningFrameTimeSeconds += Timing.EndTime - LastBufferTiming.EndTime;
			}
			else
			{
				// Full timing of this buffer
				RunningFrameTimeSeconds += Timing.EndTime - Timing.StartTime;
			}

			LastBufferTiming = Timing;
		}
	}
    
	FrameStartGPUCycles = FirstStartTime * CyclesPerSecond;
	FrameEndGPUCycles = LastBufferTiming.EndTime * CyclesPerSecond;
    
	uint64 FrameGPUTimeCycles = uint64(CyclesPerSecond * RunningFrameTimeSeconds);
	FPlatformAtomics::AtomicStore_Relaxed((int32*)&GGPUFrameTime, int32(FrameGPUTimeCycles));
	
#if STATS
	FPlatformAtomics::AtomicStore_Relaxed(&GAGXGPUWorkTime, FrameGPUTimeCycles);
	int64 FrameIdleTimeCycles = int64(FrameEndGPUCycles - FrameStartGPUCycles - FrameGPUTimeCycles);
	FPlatformAtomics::AtomicStore_Relaxed(&GAGXGPUIdleTime, FrameIdleTimeCycles);
#endif //STATS
}

void FAGXGPUProfiler::RecordPresent(const mtlpp::CommandBuffer& Buffer)
{
	const CFTimeInterval GpuStartTimeSeconds = Buffer.GetGpuStartTime();
	const CFTimeInterval GpuEndTimeSeconds = Buffer.GetGpuEndTime();
	const double CyclesPerSecond = 1.0 / FPlatformTime::GetSecondsPerCycle();
	uint64 StartTimeCycles = uint64(GpuStartTimeSeconds * CyclesPerSecond);
	uint64 EndTimeCycles = uint64(GpuEndTimeSeconds * CyclesPerSecond);
	int64 Time = int64(EndTimeCycles - StartTimeCycles);
	FPlatformAtomics::AtomicStore_Relaxed(&GAGXPresentTime, Time);
}
// END WARNING

IAGXStatsScope::~IAGXStatsScope()
{
	for (IAGXStatsScope* Stat : Children)
	{
		delete Stat;
	}
}

FString IAGXStatsScope::GetJSONRepresentation(uint32 Pid)
{
	FString JSONOutput;
	
	{
		if (GPUStartTime && GPUEndTime)
		{
			uint64 ChildStartCallTime = GPUStartTime;
			uint64 ChildDrawCallTime = GPUEndTime - GPUStartTime;
			
			JSONOutput += FString::Printf(TEXT("{\"pid\":%d, \"tid\":%d, \"ph\": \"X\", \"name\": \"%s\", \"ts\": %llu, \"dur\": %llu, \"args\":{\"num_child\":%u}},\n"),
				  Pid,
				  GPUThreadIndex,
				  *Name,
				  ChildStartCallTime,
				  ChildDrawCallTime,
				  Children.Num()
			  );
		}
	}
	
	if (CPUStartTime && CPUEndTime)
	{
		uint64 ChildStartCallTime = CPUStartTime;
		uint64 ChildDrawCallTime = FMath::Max(CPUEndTime - CPUStartTime, 1llu);
		
		JSONOutput += FString::Printf(TEXT("{\"pid\":%d, \"tid\":%d, \"ph\": \"X\", \"name\": \"%s\", \"ts\": %llu, \"dur\": %llu, \"args\":{\"num_child\":%u}},\n"),
			 Pid,
			 CPUThreadIndex,
			 *Name,
			 ChildStartCallTime,
			 ChildDrawCallTime,
			 Children.Num()
		);
	}
	
	return JSONOutput;
}

FAGXCommandBufferStats::FAGXCommandBufferStats(mtlpp::CommandBuffer const& Buffer, uint64 InGPUThreadIndex)
{
	CmdBuffer = Buffer;
	
	Name = FString::Printf(TEXT("CommandBuffer: %p %s"), CmdBuffer.GetPtr(), *FString(CmdBuffer.GetLabel().GetPtr()));
	
	CPUThreadIndex = FPlatformTLS::GetCurrentThreadId();
	GPUThreadIndex = InGPUThreadIndex;
	
	Start(Buffer);
}

FAGXCommandBufferStats::~FAGXCommandBufferStats()
{
}

void FAGXCommandBufferStats::Start(mtlpp::CommandBuffer const& Buffer)
{
	CPUStartTime = FPlatformTime::ToMilliseconds64(mach_absolute_time()) * 1000.0;
	CPUEndTime = 0;
	
	GPUStartTime = 0;
	GPUEndTime = 0;
}

void FAGXCommandBufferStats::End(mtlpp::CommandBuffer const& Buffer)
{
	check(Buffer.GetPtr() == CmdBuffer.GetPtr());
	
	bool const bTracing = FAGXProfiler::GetProfiler() && FAGXProfiler::GetProfiler()->TracingEnabled();
	CmdBuffer.AddCompletedHandler(^(const mtlpp::CommandBuffer & InnerBuffer) {
		const CFTimeInterval GpuTimeSeconds = InnerBuffer.GetGpuStartTime();
		GPUStartTime = GpuTimeSeconds * 1000000.0;
		
		const CFTimeInterval GpuEndTimeSeconds = InnerBuffer.GetGpuEndTime();
		GPUEndTime = GpuEndTimeSeconds * 1000000.0;
	
		if (bTracing)
		{
			FAGXProfiler::GetProfiler()->AddCommandBuffer(this);
		}
		else
		{
			delete this;
		}
	});
	
	CPUEndTime = FPlatformTime::ToMilliseconds64(mach_absolute_time()) * 1000.0;
}


#pragma mark -- FAGXProfiler
// ----------------------------------------------------------------


FAGXProfiler* FAGXProfiler::Self = nullptr;
static FAGXViewportPresentHandler PresentHandler = ^(uint32 DisplayID, double OutputSeconds, double OutputDuration){
	FAGXProfiler* Profiler = FAGXProfiler::GetProfiler();
	Profiler->AddDisplayVBlank(DisplayID, OutputSeconds, OutputDuration);
};

FAGXDisplayStats::FAGXDisplayStats(uint32 DisplayID, double OutputSeconds, double Duration)
{
	Name = TEXT("V-Blank");
	
	CPUThreadIndex = FPlatformTLS::GetCurrentThreadId();
	GPUThreadIndex = DisplayID;
	
	CPUStartTime = FPlatformTime::ToMilliseconds64(mach_absolute_time()) * 1000.0;
	CPUEndTime = CPUStartTime+1;
	
	GPUStartTime = OutputSeconds * 1000000.0;
	GPUEndTime = GPUStartTime + (Duration * 1000000.0);
}
FAGXDisplayStats::~FAGXDisplayStats()
{
}

void FAGXDisplayStats::Start(mtlpp::CommandBuffer const& Buffer)
{
}
void FAGXDisplayStats::End(mtlpp::CommandBuffer const& Buffer)
{
}

FAGXCPUStats::FAGXCPUStats(FString const& InName)
{
	Name = InName;
	
	CPUThreadIndex = 0;
	GPUThreadIndex = 0;
	
	CPUStartTime = 0;
	CPUEndTime = 0;
	
	GPUStartTime = 0;
	GPUEndTime = 0;
}
FAGXCPUStats::~FAGXCPUStats()
{
	
}

void FAGXCPUStats::Start(void)
{
	CPUThreadIndex = FPlatformTLS::GetCurrentThreadId();
	CPUStartTime = FPlatformTime::ToMilliseconds64(mach_absolute_time()) * 1000.0;
	
}
void FAGXCPUStats::End(void)
{
	CPUEndTime = FPlatformTime::ToMilliseconds64(mach_absolute_time()) * 1000.0;
}

void FAGXCPUStats::Start(mtlpp::CommandBuffer const& Buffer)
{
	
}
void FAGXCPUStats::End(mtlpp::CommandBuffer const& Buffer)
{
	
}

void FAGXProfiler::AddDisplayVBlank(uint32 DisplayID, double OutputSeconds, double OutputDuration)
{
	if (GIsRHIInitialized && bEnabled)
	{
		FScopeLock Lock(&Mutex);
		DisplayStats.Add(new FAGXDisplayStats(DisplayID, OutputSeconds, OutputDuration));
	}
}

FAGXProfiler::FAGXProfiler(FAGXContext* Context)
: FAGXGPUProfiler(Context)
, bEnabled(false)
{
	NumFramesToCapture = -1;
	CaptureFrameNumber = 0;
	
	bRequestStartCapture = false;
	bRequestStopCapture = false;
	
	if (FPlatformRHIFramePacer::IsEnabled())
	{
		FPlatformRHIFramePacer::AddHandler(PresentHandler);
	}
}

FAGXProfiler::~FAGXProfiler()
{
	check(bEnabled == false);
	if (FPlatformRHIFramePacer::IsEnabled())
	{
		FPlatformRHIFramePacer::RemoveHandler(PresentHandler);
	}
}

FAGXProfiler* FAGXProfiler::CreateProfiler(FAGXContext *InContext)
{
	if (!Self)
	{
		Self = new FAGXProfiler(InContext);
		
		int32 CaptureFrames = 0;
		if (FParse::Value(FCommandLine::Get(), TEXT("AGXProfileFrames="), CaptureFrames))
		{
			Self->BeginCapture(CaptureFrames);
		}
	}
	return Self;
}

FAGXProfiler* FAGXProfiler::GetProfiler()
{
	return Self;
}

void FAGXProfiler::DestroyProfiler()
{
	delete Self;
	Self = nullptr;
}

void FAGXProfiler::BeginCapture(int InNumFramesToCapture)
{
	check(IsInGameThread());
	
	NumFramesToCapture = InNumFramesToCapture;
	CaptureFrameNumber = 0;
	
	bRequestStartCapture = true;
}

void FAGXProfiler::EndCapture()
{
	bRequestStopCapture = true;
}

bool FAGXProfiler::TracingEnabled() const
{
	return bEnabled;
}

void FAGXProfiler::BeginFrame()
{
	if (AGXGPUProfilerIsInSafeThread())
	{
		if (bRequestStartCapture && !bEnabled)
		{
			bEnabled = true;
			bRequestStartCapture = false;
		}
	}
	
	FAGXGPUProfiler::BeginFrame();
	
	if (AGXGPUProfilerIsInSafeThread() && GetEmitDrawEvents())
	{
		PushEvent(TEXT("FRAME"), FColor(0, 255, 0, 255));
	}
}

void FAGXProfiler::EndFrame()
{
	if (AGXGPUProfilerIsInSafeThread() && GetEmitDrawEvents())
	{
		PopEvent();
	}
	
	FAGXGPUProfiler::EndFrame();
	
	if (AGXGPUProfilerIsInSafeThread() && bEnabled)
	{
		CaptureFrameNumber++;
		if (bRequestStopCapture || (NumFramesToCapture > 0 && CaptureFrameNumber >= NumFramesToCapture))
		{
			bRequestStopCapture = false;
			NumFramesToCapture = -1;
			bEnabled = false;
			SaveTrace();
		}
	}
}

void FAGXProfiler::EncodeDraw(FAGXCommandBufferStats* CmdBufStats, char const* DrawCall, uint32 RHIPrimitives, uint32 RHIVertices, uint32 RHIInstances)
{
	if (AGXGPUProfilerIsInSafeThread())
	{
		FAGXGPUProfiler::RegisterGPUWork(RHIPrimitives, RHIVertices);
	}
}

void FAGXProfiler::EncodeBlit(FAGXCommandBufferStats* CmdBufStats, char const* DrawCall)
{
	if (AGXGPUProfilerIsInSafeThread())
	{
		FAGXGPUProfiler::RegisterGPUWork(1, 1);
	}
}

void FAGXProfiler::EncodeBlit(FAGXCommandBufferStats* CmdBufStats, FString DrawCall)
{
	if (AGXGPUProfilerIsInSafeThread())
	{
		FAGXGPUProfiler::RegisterGPUWork(1, 1);
	}
}

void FAGXProfiler::EncodeDispatch(FAGXCommandBufferStats* CmdBufStats, char const* DrawCall)
{
	if (AGXGPUProfilerIsInSafeThread())
	{
		FAGXGPUProfiler::RegisterGPUWork(1, 1);
	}
}

FAGXCPUStats* FAGXProfiler::AddCPUStat(FString const& Name)
{
	if (GIsRHIInitialized && bEnabled)
	{
		FScopeLock Lock(&Mutex);
		FAGXCPUStats* Stat = new FAGXCPUStats(Name);
		CPUStats.Add(Stat);
		return Stat;
	}
	else
	{
		return nullptr;
	}
}

FAGXCommandBufferStats* FAGXProfiler::AllocateCommandBuffer(const mtlpp::CommandBuffer &Buffer, uint64 GPUThreadIndex)
{
	return new FAGXCommandBufferStats(Buffer, GPUThreadIndex);
}

void FAGXProfiler::AddCommandBuffer(FAGXCommandBufferStats *CommandBuffer)
{
	if (GIsRHIInitialized)
	{
		FScopeLock Lock(&Mutex);
		TracedBuffers.Add(CommandBuffer);
	}
	else
	{
		delete CommandBuffer;
	}
}

void FAGXProfiler::PushEvent(const TCHAR *Name, FColor Color)
{
	FAGXGPUProfiler::PushEvent(Name, Color);
}

void FAGXProfiler::PopEvent()
{
	FAGXGPUProfiler::PopEvent();
}

void FAGXProfiler::SaveTrace()
{
	Context->SubmitCommandBufferAndWait();
	{
		FScopeLock Lock(&Mutex);
		
		TSet<uint32> ThreadIDs;
		
		for (FAGXCommandBufferStats* CmdBufStats : TracedBuffers)
		{
			ThreadIDs.Add(CmdBufStats->CPUThreadIndex);
			
			for (IAGXStatsScope* ES : CmdBufStats->Children)
			{
				ThreadIDs.Add(ES->CPUThreadIndex);
				
				for (IAGXStatsScope* DS : ES->Children)
				{
					ThreadIDs.Add(DS->CPUThreadIndex);
				}
			}
		}
		
		TSet<uint32> Displays;
		for (FAGXDisplayStats* DisplayStat : DisplayStats)
		{
			ThreadIDs.Add(DisplayStat->CPUThreadIndex);
			Displays.Add(DisplayStat->GPUThreadIndex);
		}
		
		for (FAGXCPUStats* CPUStat : CPUStats)
		{
			ThreadIDs.Add(CPUStat->CPUThreadIndex);
		}
		
		FString Filename = FString::Printf(TEXT("Profile(%s)"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
		FString TracingRootPath = FPaths::ProfilingDir() + TEXT("Traces/");
		FString OutputFilename = TracingRootPath + Filename + TEXT(".json");
		
		FArchive* OutputFile = IFileManager::Get().CreateFileWriter(*OutputFilename);
		
		AGXWriteString(OutputFile, R"({"traceEvents":[)" "\n");
		
		int32 SortIndex = 0; // Lower numbers result in higher position in the visualizer.
		const uint32 Pid = FPlatformProcess::GetCurrentProcessId();
		
		for (int32 GPUIndex = 0; GPUIndex <= 0/*MaxGPUIndex*/; ++GPUIndex)
		{
			FString Output = FString::Printf(TEXT("{\"pid\":%d, \"tid\":%d, \"ph\": \"M\", \"name\": \"thread_name\", \"args\":{\"name\":\"GPU %d Command Buffers\"}},{\"pid\":%d, \"tid\":%d, \"ph\": \"M\", \"name\": \"thread_sort_index\", \"args\":{\"sort_index\": %d}},\n"),
											 Pid, GPUIndex, GPUIndex, Pid, GPUIndex, SortIndex
											 );
			
			AGXWriteString(OutputFile, TCHAR_TO_UTF8(*Output));
			SortIndex++;
			
			Output = FString::Printf(TEXT("{\"pid\":%d, \"tid\":%d, \"ph\": \"M\", \"name\": \"thread_name\", \"args\":{\"name\":\"GPU %d Operations\"}},{\"pid\":%d, \"tid\":%d, \"ph\": \"M\", \"name\": \"thread_sort_index\", \"args\":{\"sort_index\": %d}},\n"),
											 Pid, GPUIndex+SortIndex, GPUIndex, Pid, GPUIndex+SortIndex, SortIndex
											 );
			
			AGXWriteString(OutputFile, TCHAR_TO_UTF8(*Output));
			SortIndex++;
			
			Output = FString::Printf(TEXT("{\"pid\":%d, \"tid\":%d, \"ph\": \"M\", \"name\": \"thread_name\", \"args\":{\"name\":\"Render Events %d\"}},{\"pid\":%d, \"tid\":%d, \"ph\": \"M\", \"name\": \"thread_sort_index\", \"args\":{\"sort_index\": %d}},\n"),
									 Pid, GPUIndex+SortIndex, GPUIndex, Pid, GPUIndex+SortIndex, SortIndex
									 );
			
			AGXWriteString(OutputFile, TCHAR_TO_UTF8(*Output));
			SortIndex++;
			
			Output = FString::Printf(TEXT("{\"pid\":%d, \"tid\":%d, \"ph\": \"M\", \"name\": \"thread_name\", \"args\":{\"name\":\"Driver Stats %d\"}},{\"pid\":%d, \"tid\":%d, \"ph\": \"M\", \"name\": \"thread_sort_index\", \"args\":{\"sort_index\": %d}},\n"),
									 Pid, GPUIndex+SortIndex, GPUIndex, Pid, GPUIndex+SortIndex, SortIndex
									 );
			
			AGXWriteString(OutputFile, TCHAR_TO_UTF8(*Output));
			SortIndex++;
			
			for (uint32 Display : Displays)
			{
				Output = FString::Printf(TEXT("{\"pid\":%d, \"tid\":%d, \"ph\": \"M\", \"name\": \"thread_name\", \"args\":{\"name\":\"Display %d\"}},{\"pid\":%d, \"tid\":%d, \"ph\": \"M\", \"name\": \"thread_sort_index\", \"args\":{\"sort_index\": %d}},\n"),
										 Pid, Display + SortIndex, SortIndex - 3, Pid, Display + SortIndex, SortIndex
										 );
				
				AGXWriteString(OutputFile, TCHAR_TO_UTF8(*Output));
				SortIndex++;
			}
		}
		
		static const uint32 BufferSize = 128;
		char Buffer[BufferSize];
		for (uint32 CPUIndex : ThreadIDs)
		{
			bool bThreadName = false;
			pthread_t PThread = pthread_from_mach_thread_np((mach_port_t)CPUIndex);
			if (PThread)
			{
				if (!pthread_getname_np(PThread,Buffer,BufferSize))
				{
					bThreadName = true;
				}
			}
			if (!bThreadName)
			{
				sprintf(Buffer, "Thread %d", CPUIndex);
			}
			
			FString Output = FString::Printf(TEXT("{\"pid\":%d, \"tid\":%d, \"ph\": \"M\", \"name\": \"thread_name\", \"args\":{\"name\":\"%s\"}},{\"pid\":%d, \"tid\":%d, \"ph\": \"M\", \"name\": \"thread_sort_index\", \"args\":{\"sort_index\": %d}},\n"),
											 Pid, CPUIndex, UTF8_TO_TCHAR(Buffer), Pid, CPUIndex, SortIndex
											 );
			
			
			AGXWriteString(OutputFile, TCHAR_TO_UTF8(*Output));
			SortIndex++;
		}
		
		for (FAGXCommandBufferStats* CmdBufStats : TracedBuffers)
		{
			AGXWriteString(OutputFile, TCHAR_TO_UTF8(*CmdBufStats->GetJSONRepresentation(Pid)));
			
			for (IAGXStatsScope* ES : CmdBufStats->Children)
			{
				AGXWriteString(OutputFile, TCHAR_TO_UTF8(*ES->GetJSONRepresentation(Pid)));
				
				uint64 PrevTime = ES->GPUStartTime;
				for (IAGXStatsScope* DS : ES->Children)
				{
					AGXWriteString(OutputFile, TCHAR_TO_UTF8(*DS->GetJSONRepresentation(Pid)));
					if (!DS->GPUStartTime)
					{
						DS->GPUStartTime = FMath::Max(PrevTime, DS->GPUStartTime);
						DS->GPUEndTime = DS->GPUStartTime + 1llu;
						AGXWriteString(OutputFile, TCHAR_TO_UTF8(*DS->GetJSONRepresentation(Pid)));
					}
					PrevTime = DS->GPUEndTime;
				}
			}
			
			delete CmdBufStats;
		}
		TracedBuffers.Empty();
		
		for (FAGXDisplayStats* DisplayStat : DisplayStats)
		{
			DisplayStat->GPUThreadIndex += 3;
			AGXWriteString(OutputFile, TCHAR_TO_UTF8(*DisplayStat->GetJSONRepresentation(Pid)));
			delete DisplayStat;
		}
		DisplayStats.Empty();
		
		for (FAGXCPUStats* CPUStat : CPUStats)
		{
			AGXWriteString(OutputFile, TCHAR_TO_UTF8(*CPUStat->GetJSONRepresentation(Pid)));
			delete CPUStat;
		}
		CPUStats.Empty();
		
		// All done
		
		AGXWriteString(OutputFile, "{}]}");
		
		OutputFile->Close();
	}
}

static void HandleAGXProfileCommand(const TArray<FString>& Args, UWorld*, FOutputDevice& Ar)
{
	if (Args.Num() < 1)
	{
		return;
	}
	FString Param = Args[0];
	if (Param == TEXT("START"))
	{
		FAGXProfiler::GetProfiler()->BeginCapture();
	}
	else if (Param == TEXT("STOP"))
	{
		FAGXProfiler::GetProfiler()->EndCapture();
	}
	else
	{
		int32 CaptureFrames = 0;
		if (FParse::Value(*Param, TEXT("FRAMES="), CaptureFrames))
		{
			FAGXProfiler::GetProfiler()->BeginCapture(CaptureFrames);
		}
	}
}

static FAutoConsoleCommand HandleAGXProfilerCmd(
	TEXT("AGXProfiler"),
	TEXT("Starts or stops AGX profiler"),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleAGXProfileCommand)
);
