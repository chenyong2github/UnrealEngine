// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	D3D12RHI.h: Public D3D RHI definitions.
=============================================================================*/

#pragma once

///////////////////////////////////////////////////////////////
// Platform agnostic defines
///////////////////////////////////////////////////////////////
#define SUB_ALLOCATED_DEFAULT_ALLOCATIONS	1

#define DEBUG_RESOURCE_STATES	0

// DX12 doesn't support higher MSAA count
#define DX_MAX_MSAA_COUNT	8

// This is a value that should be tweaked to fit the app, lower numbers will have better performance
// Titles using many terrain layers may want to set MAX_SRVS to 64 to avoid shader compilation errors. This will have a small performance hit of around 0.1%
#define MAX_SRVS		64
#define MAX_SAMPLERS	16
#define MAX_UAVS		16
#define MAX_CBS			16

// This value controls how many root constant buffers can be used per shader stage in a root signature.
// Note: Using root descriptors significantly increases the size of root signatures (each root descriptor is 2 DWORDs).
#define MAX_ROOT_CBVS	MAX_CBS

// So outside callers can override this
#ifndef USE_STATIC_ROOT_SIGNATURE
	#define USE_STATIC_ROOT_SIGNATURE 0
#endif

// How many residency packets can be in flight before the rendering thread
// blocks for them to drain. Should be ~ NumBufferedFrames * AvgNumSubmissionsPerFrame i.e.
// enough to ensure that the GPU is rarely blocked by residency work
#define RESIDENCY_PIPELINE_DEPTH	6

// This is the primary define that controls if the logic for the SubmissionGapRecorder is enabled or not in D3D12
#ifndef PLATFORM_ALLOW_D3D12_SUBMISSION_GAP_RECORDER
#define PLATFORM_ALLOW_D3D12_SUBMISSION_GAP_RECORDER 1
#endif

#if PLATFORM_ALLOW_D3D12_SUBMISSION_GAP_RECORDER
#define D3D12_SUBMISSION_GAP_RECORDER !(UE_BUILD_SHIPPING || WITH_EDITOR)
#define D3D12_SUBMISSION_GAP_RECORDER_DEBUG_INFO  !(UE_BUILD_SHIPPING || UE_BUILD_TEST || WITH_EDITOR)
#else
#define D3D12_SUBMISSION_GAP_RECORDER 0
#define D3D12_SUBMISSION_GAP_RECORDER_DEBUG_INFO  0
#endif

#if PLATFORM_WINDOWS || PLATFORM_HOLOLENS
	#define ENABLE_RESIDENCY_MANAGEMENT				1
	#define ASYNC_DEFERRED_DELETION					1
	#define PIPELINE_STATE_FILE_LOCATION			FPaths::ProjectSavedDir()
	#define USE_PIX									D3D12_PROFILING_ENABLED
#else
	#include "D3D12RHIPlatformPublic.h"
#endif


#define FD3D12_TEXTURE_DATA_PITCH_ALIGNMENT D3D12_TEXTURE_DATA_PITCH_ALIGNMENT
#define FD3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER

typedef uint16 CBVSlotMask;
static_assert(MAX_ROOT_CBVS <= MAX_CBS, "MAX_ROOT_CBVS must be <= MAX_CBS.");
static_assert((8 * sizeof(CBVSlotMask)) >= MAX_CBS, "CBVSlotMask isn't large enough to cover all CBs. Please increase the size.");
static_assert((8 * sizeof(CBVSlotMask)) >= MAX_ROOT_CBVS, "CBVSlotMask isn't large enough to cover all CBs. Please increase the size.");
static const CBVSlotMask GRootCBVSlotMask = (1 << MAX_ROOT_CBVS) - 1; // Mask for all slots that are used by root descriptors.
static const CBVSlotMask GDescriptorTableCBVSlotMask = static_cast<CBVSlotMask>(-1) & ~(GRootCBVSlotMask); // Mask for all slots that are used by a root descriptor table.

#if MAX_SRVS > 32
typedef uint64 SRVSlotMask;
#else
typedef uint32 SRVSlotMask;
#endif
static_assert((8 * sizeof(SRVSlotMask)) >= MAX_SRVS, "SRVSlotMask isn't large enough to cover all SRVs. Please increase the size.");

typedef uint16 SamplerSlotMask;
static_assert((8 * sizeof(SamplerSlotMask)) >= MAX_SAMPLERS, "SamplerSlotMask isn't large enough to cover all Samplers. Please increase the size.");

typedef uint16 UAVSlotMask;
static_assert((8 * sizeof(UAVSlotMask)) >= MAX_UAVS, "UAVSlotMask isn't large enough to cover all UAVs. Please increase the size.");

/* If the submission gap recorder code is enabled define the SubmissionGapRecorder class */
#if D3D12_SUBMISSION_GAP_RECORDER
/** Class for tracking timestamps for recording bubbles between command list submissions */
class FD3D12SubmissionGapRecorder
{
	struct FGapSpan
	{
		uint64 BeginCycles;
		uint64 DurationCycles;
	};
	struct FFrame
	{
		FFrame() { bIsValid = false; bSafeToReadOnRenderThread = false;  FrameNumber = -1; }

		TArray<FGapSpan> GapSpans;
		uint32 FrameNumber;
		uint64 TotalWaitCycles;
		uint64 StartCycles;
		uint64 EndCycles;
		bool bIsValid;
		bool bSafeToReadOnRenderThread;
	};
public:
	FD3D12SubmissionGapRecorder();

	// Submits the gap timestamps for a frame. Typically called from the RHI thread in EndFrame. Returns the total number of cycles spent waiting
	uint64 SubmitSubmissionTimestampsForFrame(uint32 FrameCounter, TArray<uint64>& PrevFrameBeginSubmissionTimestamps, TArray<uint64>& PrevFrameEndSubmissionTimestamps);

	// Adjusts a timestamp by subtracting any preceding submission gaps
	uint64 AdjustTimestampForSubmissionGaps(uint32 FrameSubmitted, uint64 Timestamp);

	// Called when we advance the frame from the render thread (in EndDrawingViewport)
	void OnRenderThreadAdvanceFrame();

	int32 GetStartFrameSlotIdx() const		{ return StartFrameSlotIdx; }
	void  SetStartFrameSlotIdx(int32 val)	{ StartFrameSlotIdx = val; }

	int32 GetEndFrameSlotIdx() const		{ return EndFrameSlotIdx; }
	void  SetEndFrameSlotIdx(int32 val)		{ EndFrameSlotIdx = val; }

	int32 GetPresentSlotIdx() const			{ return PresentSlotIdx; }
	void SetPresentSlotIdx(int32 val)		{ PresentSlotIdx = val; }

private:

	TArray<FD3D12SubmissionGapRecorder::FFrame> FrameRingbuffer;

	FCriticalSection GapSpanMutex;
	uint32 WriteIndex;
	uint32 WriteIndexRT;
	uint32 ReadIndex;
	uint64 CurrentGapSpanReadIndex;
	uint64 CurrentElapsedWaitCycles;
	uint64 LastTimestampAdjusted;
	int32  StartFrameSlotIdx;
	int32  EndFrameSlotIdx;
	int32  PresentSlotIdx;
};
#endif