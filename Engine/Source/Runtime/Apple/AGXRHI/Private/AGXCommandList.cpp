// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXCommandList.cpp: AGX RHI command buffer list wrapper.
=============================================================================*/

#include "AGXRHIPrivate.h"
#include "AGXShaderTypes.h"
#include "AGXGraphicsPipelineState.h"
#include "AGXCommandList.h"
#include "AGXCommandQueue.h"
#include "AGXProfiler.h"
#include "AGXCommandEncoder.h"
#include "ns.hpp"

#pragma mark - Public C++ Boilerplate -

#if PLATFORM_IOS
extern bool GIsSuspended;
#endif
extern int32 GAGXDebugOpsCount;

FAGXCommandList::FAGXCommandList(FAGXCommandQueue& InCommandQueue, bool const bInImmediate)
: CommandQueue(InCommandQueue)
, Index(0)
, Num(0)
, bImmediate(bInImmediate)
{
}

FAGXCommandList::~FAGXCommandList(void)
{
}
	
#pragma mark - Public Command List Mutators -

extern CORE_API bool GIsGPUCrashed;
static void ReportMetalCommandBufferFailure(mtlpp::CommandBuffer const& CompletedBuffer, TCHAR const* ErrorType, bool bDoCheck=true)
{
	GIsGPUCrashed = true;
	
	NSString* Label = CompletedBuffer.GetLabel();
	int32 Code = CompletedBuffer.GetError().GetCode();
	NSString* Domain = CompletedBuffer.GetError().GetDomain();
	NSString* ErrorDesc = CompletedBuffer.GetError().GetLocalizedDescription();
	NSString* FailureDesc = CompletedBuffer.GetError().GetLocalizedFailureReason();
	NSString* RecoveryDesc = CompletedBuffer.GetError().GetLocalizedRecoverySuggestion();
	
	FString LabelString = Label ? FString(Label) : FString(TEXT("Unknown"));
	FString DomainString = Domain ? FString(Domain) : FString(TEXT("Unknown"));
	FString ErrorString = ErrorDesc ? FString(ErrorDesc) : FString(TEXT("Unknown"));
	FString FailureString = FailureDesc ? FString(FailureDesc) : FString(TEXT("Unknown"));
	FString RecoveryString = RecoveryDesc ? FString(RecoveryDesc) : FString(TEXT("Unknown"));
	
	NSString* Desc = CompletedBuffer.GetPtr().debugDescription;
	UE_LOG(LogAGX, Warning, TEXT("%s"), *FString(Desc));
	
#if PLATFORM_IOS
    if (bDoCheck && !GIsSuspended && !GIsRenderingThreadSuspended)
#endif
    {
#if PLATFORM_IOS
        UE_LOG(LogAGX, Warning, TEXT("Command Buffer %s Failed with %s Error! Error Domain: %s Code: %d Description %s %s %s"), *LabelString, ErrorType, *DomainString, Code, *ErrorString, *FailureString, *RecoveryString);
        FIOSPlatformMisc::GPUAssert();
#else
		UE_LOG(LogAGX, Fatal, TEXT("Command Buffer %s Failed with %s Error! Error Domain: %s Code: %d Description %s %s %s"), *LabelString, ErrorType, *DomainString, Code, *ErrorString, *FailureString, *RecoveryString);
#endif
    }
}

static __attribute__ ((optnone)) void MetalCommandBufferFailureInternal(mtlpp::CommandBuffer const& CompletedBuffer)
{
	ReportMetalCommandBufferFailure(CompletedBuffer, TEXT("Internal"));
}

static __attribute__ ((optnone)) void MetalCommandBufferFailureTimeout(mtlpp::CommandBuffer const& CompletedBuffer)
{
    ReportMetalCommandBufferFailure(CompletedBuffer, TEXT("Timeout"), PLATFORM_IOS);
}

static __attribute__ ((optnone)) void MetalCommandBufferFailurePageFault(mtlpp::CommandBuffer const& CompletedBuffer)
{
	ReportMetalCommandBufferFailure(CompletedBuffer, TEXT("PageFault"));
}

static __attribute__ ((optnone)) void MetalCommandBufferFailureBlacklisted(mtlpp::CommandBuffer const& CompletedBuffer)
{
	ReportMetalCommandBufferFailure(CompletedBuffer, TEXT("Blacklisted"));
}

static __attribute__ ((optnone)) void MetalCommandBufferFailureNotPermitted(mtlpp::CommandBuffer const& CompletedBuffer)
{
	// when iOS goes into the background, it can get a delayed NotPermitted error, so we can't crash in this case, just allow it to not be submitted
	ReportMetalCommandBufferFailure(CompletedBuffer, TEXT("NotPermitted"), !PLATFORM_IOS);
}

static __attribute__ ((optnone)) void MetalCommandBufferFailureOutOfMemory(mtlpp::CommandBuffer const& CompletedBuffer)
{
	ReportMetalCommandBufferFailure(CompletedBuffer, TEXT("OutOfMemory"));
}

static __attribute__ ((optnone)) void MetalCommandBufferFailureInvalidResource(mtlpp::CommandBuffer const& CompletedBuffer)
{
	ReportMetalCommandBufferFailure(CompletedBuffer, TEXT("InvalidResource"));
}

static void HandleMetalCommandBufferError(mtlpp::CommandBuffer const& CompletedBuffer)
{
	MTLCommandBufferError Code = (MTLCommandBufferError)CompletedBuffer.GetError().GetCode();
	switch(Code)
	{
		case MTLCommandBufferErrorInternal:
			MetalCommandBufferFailureInternal(CompletedBuffer);
			break;
		case MTLCommandBufferErrorTimeout:
			MetalCommandBufferFailureTimeout(CompletedBuffer);
			break;
		case MTLCommandBufferErrorPageFault:
			MetalCommandBufferFailurePageFault(CompletedBuffer);
			break;
		case MTLCommandBufferErrorBlacklisted:
			MetalCommandBufferFailureBlacklisted(CompletedBuffer);
			break;
		case MTLCommandBufferErrorNotPermitted:
			MetalCommandBufferFailureNotPermitted(CompletedBuffer);
			break;
		case MTLCommandBufferErrorOutOfMemory:
			MetalCommandBufferFailureOutOfMemory(CompletedBuffer);
			break;
		case MTLCommandBufferErrorInvalidResource:
			MetalCommandBufferFailureInvalidResource(CompletedBuffer);
			break;
		case MTLCommandBufferErrorNone:
			// No error
			break;
		default:
			ReportMetalCommandBufferFailure(CompletedBuffer, TEXT("Unknown"));
			break;
	}
}

static __attribute__ ((optnone)) void HandleAMDMetalCommandBufferError(mtlpp::CommandBuffer const& CompletedBuffer)
{
	HandleMetalCommandBufferError(CompletedBuffer);
}

static __attribute__ ((optnone)) void HandleNVIDIAMetalCommandBufferError(mtlpp::CommandBuffer const& CompletedBuffer)
{
	HandleMetalCommandBufferError(CompletedBuffer);
}

static __attribute__ ((optnone)) void HandleIntelMetalCommandBufferError(mtlpp::CommandBuffer const& CompletedBuffer)
{
	HandleMetalCommandBufferError(CompletedBuffer);
}

void FAGXCommandList::HandleMetalCommandBufferFailure(mtlpp::CommandBuffer const& CompletedBuffer)
{
	if (CompletedBuffer.GetError().GetDomain() == MTLCommandBufferErrorDomain || [CompletedBuffer.GetError().GetDomain() isEqualToString:MTLCommandBufferErrorDomain])
	{
		if (GRHIVendorId && IsRHIDeviceAMD())
		{
			HandleAMDMetalCommandBufferError(CompletedBuffer);
		}
		else if (GRHIVendorId && IsRHIDeviceNVIDIA())
		{
			HandleNVIDIAMetalCommandBufferError(CompletedBuffer);
		}
		else if (GRHIVendorId && IsRHIDeviceIntel())
		{
			HandleIntelMetalCommandBufferError(CompletedBuffer);
		}
		else
		{
			HandleMetalCommandBufferError(CompletedBuffer);
		}
	}
	else
	{
		ReportMetalCommandBufferFailure(CompletedBuffer, TEXT("Unknown"));
	}
}

void FAGXCommandList::SetParallelIndex(uint32 InIndex, uint32 InNum)
{
	if (!IsImmediate())
	{
		Index = InIndex;
		Num = InNum;
	}
}

void FAGXCommandList::Commit(mtlpp::CommandBuffer& Buffer, TArray<ns::Object<mtlpp::CommandBufferHandler>> CompletionHandlers, bool const bWait, bool const bIsLastCommandBuffer)
{
	check(Buffer);

	// The lifetime of this array is per frame
	if (!FrameCommitedBufferTimings.IsValid())
	{
		FrameCommitedBufferTimings = MakeShared<TArray<FAGXCommandBufferTiming>, ESPMode::ThreadSafe>();
	}

	// The lifetime of this should be for the entire game
	if (!LastCompletedBufferTiming.IsValid())
	{
		LastCompletedBufferTiming = MakeShared<FAGXCommandBufferTiming, ESPMode::ThreadSafe>();
	}

	Buffer.AddCompletedHandler([CompletionHandlers, FrameCommitedBufferTimingsLocal = FrameCommitedBufferTimings, LastCompletedBufferTimingLocal = LastCompletedBufferTiming](mtlpp::CommandBuffer const& CompletedBuffer)
	{
		if (CompletedBuffer.GetStatus() == mtlpp::CommandBufferStatus::Error)
		{
			HandleMetalCommandBufferFailure(CompletedBuffer);
		}
		if (CompletionHandlers.Num())
		{
			for (ns::Object<mtlpp::CommandBufferHandler> Handler : CompletionHandlers)
			{
				Handler.GetPtr()(CompletedBuffer);
			}
		}

		if (CompletedBuffer.GetStatus() == mtlpp::CommandBufferStatus::Completed)
		{
			FrameCommitedBufferTimingsLocal->Add({CompletedBuffer.GetGpuStartTime(), CompletedBuffer.GetGpuEndTime()});
		}

		// If this is the last reference, then it is the last command buffer to return, so record the frame
		if (FrameCommitedBufferTimingsLocal.IsUnique())
		{
			FAGXGPUProfiler::RecordFrame(*FrameCommitedBufferTimingsLocal, *LastCompletedBufferTimingLocal);
		}
	});

	// If bIsLastCommandBuffer is set then this is the end of the "frame".
	if (bIsLastCommandBuffer)
	{
		FrameCommitedBufferTimings = MakeShared<TArray<FAGXCommandBufferTiming>, ESPMode::ThreadSafe>();
	}

	if (bImmediate)
	{
		CommandQueue.CommitCommandBuffer(Buffer);
		if (bWait)
		{
			Buffer.WaitUntilCompleted();
		}
	}
	else
	{
		check(!bWait);
		SubmittedBuffers.Add(Buffer);
	}
}

void FAGXCommandList::Submit(uint32 InIndex, uint32 Count)
{
	// Only deferred contexts should call Submit, the immediate context commits directly to the command-queue.
	check(!bImmediate);

	// Command queue takes ownership of the array
	CommandQueue.SubmitCommandBuffers(SubmittedBuffers, InIndex, Count);
	SubmittedBuffers.Empty();
}
