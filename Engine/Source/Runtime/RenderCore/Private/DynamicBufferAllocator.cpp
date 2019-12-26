// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
DynamicBufferAllocator.cpp: Classes for allocating transient rendering data.
==============================================================================*/

#include "DynamicBufferAllocator.h"
#include "RenderResource.h"
#include "Misc/ScopeLock.h"

int32 GMaxReadBufferRenderingBytesAllocatedPerFrame = 32 * 1024 * 1024;

FAutoConsoleVariableRef CVarMaxReadBufferRenderingBytesAllocatedPerFrame(
	TEXT("r.ReadBuffer.MaxRenderingBytesAllocatedPerFrame"),
	GMaxReadBufferRenderingBytesAllocatedPerFrame,
	TEXT("The maximum number of transient rendering read buffer bytes to allocate before we start panic logging who is doing the allocations"));

// The allocator works by looking for the first free buffer that contains the required number of elements.  There is currently no trim so buffers stay in memory.
// To avoid increasing allocation sizes over multiple frames causing severe memory bloat (i.e. 100 elements, 1001 elements) we first align the required
// number of elements to GMinReadBufferRenderingBufferSize, we then take the max(aligned num, GMinReadBufferRenderingBufferSize)
int32 GMinReadBufferRenderingBufferSize = 256 * 1024;
FAutoConsoleVariableRef CVarMinReadBufferSize(
	TEXT("r.ReadBuffer.MinSize"),
	GMinReadBufferRenderingBufferSize,
	TEXT("The minimum size (in instances) to allocate in blocks for rendering read buffers. i.e. 256*1024 = 1mb for a float buffer"));

int32 GAlignReadBufferRenderingBufferSize = 64 * 1024;
FAutoConsoleVariableRef CVarAlignReadBufferSize(
	TEXT("r.ReadBuffer.AlignSize"),
	GAlignReadBufferRenderingBufferSize,
	TEXT("The alignment size (in instances) to allocate in blocks for rendering read buffers. i.e. 64*1024 = 256k for a float buffer"));

struct FDynamicReadBufferPool
{
	/** List of vertex buffers. */
	TIndirectArray<FDynamicAllocReadBuffer> Buffers;
	/** The current buffer from which allocations are being made. */
	FDynamicAllocReadBuffer* CurrentBuffer;

	/** Default constructor. */
	FDynamicReadBufferPool()
		: CurrentBuffer(NULL)
	{
	}

	/** Destructor. */
	~FDynamicReadBufferPool()
	{
		int32 NumVertexBuffers = Buffers.Num();
		for (int32 BufferIndex = 0; BufferIndex < NumVertexBuffers; ++BufferIndex)
		{
			Buffers[BufferIndex].Release();
		}
	}

	FCriticalSection CriticalSection;
};



FGlobalDynamicReadBuffer::FGlobalDynamicReadBuffer()
	: TotalAllocatedSinceLastCommit(0)
{
	FloatBufferPool = new FDynamicReadBufferPool();
	Int32BufferPool = new FDynamicReadBufferPool();
}

FGlobalDynamicReadBuffer::~FGlobalDynamicReadBuffer()
{
	Cleanup();
}

void FGlobalDynamicReadBuffer::Cleanup()
{
	if (FloatBufferPool)
	{
		delete FloatBufferPool;
		FloatBufferPool = nullptr;
	}

	if (Int32BufferPool)
	{
		delete Int32BufferPool;
		Int32BufferPool = nullptr;
	}
}
void FGlobalDynamicReadBuffer::InitRHI()
{
}

void FGlobalDynamicReadBuffer::ReleaseRHI()
{
	Cleanup();
}

FGlobalDynamicReadBuffer::FAllocation FGlobalDynamicReadBuffer::AllocateFloat(uint32 Num)
{
	FScopeLock ScopeLock(&FloatBufferPool->CriticalSection);

	FAllocation Allocation;

	TotalAllocatedSinceLastCommit += Num;
	if (IsRenderAlarmLoggingEnabled())
	{
		UE_LOG(LogRendererCore, Warning, TEXT("FGlobalReadBuffer::AllocateFloat(%u), will have allocated %u total this frame"), Num, TotalAllocatedSinceLastCommit);
	}
	uint32 SizeInBytes = sizeof(float) * Num;
	FDynamicAllocReadBuffer* Buffer = FloatBufferPool->CurrentBuffer;
	if (Buffer == NULL || Buffer->AllocatedByteCount + SizeInBytes > Buffer->NumBytes)
	{
		// Find a buffer in the pool big enough to service the request.
		Buffer = NULL;
		for (int32 BufferIndex = 0, NumBuffers = FloatBufferPool->Buffers.Num(); BufferIndex < NumBuffers; ++BufferIndex)
		{
			FDynamicAllocReadBuffer& BufferToCheck = FloatBufferPool->Buffers[BufferIndex];
			if (BufferToCheck.AllocatedByteCount + SizeInBytes <= BufferToCheck.NumBytes)
			{
				Buffer = &BufferToCheck;
				break;
			}
		}

		// Create a new vertex buffer if needed.
		if (Buffer == NULL)
		{
			const uint32 AlignedNum = FMath::DivideAndRoundUp(Num, (uint32)GAlignReadBufferRenderingBufferSize) * GAlignReadBufferRenderingBufferSize;
			const uint32 NewBufferSize = FMath::Max(AlignedNum, (uint32)GMinReadBufferRenderingBufferSize);
			Buffer = new FDynamicAllocReadBuffer();
			FloatBufferPool->Buffers.Add(Buffer);
			Buffer->Initialize(sizeof(float), NewBufferSize, PF_R32_FLOAT, BUF_Volatile);
		}

		// Lock the buffer if needed.
		if (Buffer->MappedBuffer == NULL)
		{
			Buffer->Lock();
		}

		// Remember this buffer, we'll try to allocate out of it in the future.
		FloatBufferPool->CurrentBuffer = Buffer;
	}

	check(Buffer != NULL);
	checkf(Buffer->AllocatedByteCount + SizeInBytes <= Buffer->NumBytes, TEXT("Global dynamic read buffer float buffer allocation failed: BufferSize=%d AllocatedByteCount=%d SizeInBytes=%d"), Buffer->NumBytes, Buffer->AllocatedByteCount, SizeInBytes);
	Allocation.Buffer = Buffer->MappedBuffer + Buffer->AllocatedByteCount;
	Allocation.ReadBuffer = Buffer;
	Allocation.FirstIndex = Buffer->AllocatedByteCount;
	Buffer->AllocatedByteCount += SizeInBytes;

	return Allocation;
}

FGlobalDynamicReadBuffer::FAllocation FGlobalDynamicReadBuffer::AllocateInt32(uint32 Num)
{
	FScopeLock ScopeLock(&Int32BufferPool->CriticalSection);
	FAllocation Allocation;

	TotalAllocatedSinceLastCommit += Num;
	if (IsRenderAlarmLoggingEnabled())
	{
		UE_LOG(LogRendererCore, Warning, TEXT("FGlobalReadBuffer::AllocateInt32(%u), will have allocated %u total this frame"), Num, TotalAllocatedSinceLastCommit);
	}
	uint32 SizeInBytes = sizeof(int32) * Num;
	FDynamicAllocReadBuffer* Buffer = Int32BufferPool->CurrentBuffer;
	if (Buffer == NULL || Buffer->AllocatedByteCount + SizeInBytes > Buffer->NumBytes)
	{
		// Find a buffer in the pool big enough to service the request.
		Buffer = NULL;
		for (int32 BufferIndex = 0, NumBuffers = Int32BufferPool->Buffers.Num(); BufferIndex < NumBuffers; ++BufferIndex)
		{
			FDynamicAllocReadBuffer& BufferToCheck = Int32BufferPool->Buffers[BufferIndex];
			if (BufferToCheck.AllocatedByteCount + SizeInBytes <= BufferToCheck.NumBytes)
			{
				Buffer = &BufferToCheck;
				break;
			}
		}

		// Create a new vertex buffer if needed.
		if (Buffer == NULL)
		{
			const uint32 AlignedNum = FMath::DivideAndRoundUp(Num, (uint32)GAlignReadBufferRenderingBufferSize) * GAlignReadBufferRenderingBufferSize;
			const uint32 NewBufferSize = FMath::Max(AlignedNum, (uint32)GMinReadBufferRenderingBufferSize);
			Buffer = new FDynamicAllocReadBuffer();
			Int32BufferPool->Buffers.Add(Buffer);
			Buffer->Initialize(sizeof(int32), NewBufferSize, PF_R32_SINT, BUF_Volatile);
		}

		// Lock the buffer if needed.
		if (Buffer->MappedBuffer == NULL)
		{
			Buffer->Lock();
		}

		// Remember this buffer, we'll try to allocate out of it in the future.
		Int32BufferPool->CurrentBuffer = Buffer;
	}

	check(Buffer != NULL);
	checkf(Buffer->AllocatedByteCount + SizeInBytes <= Buffer->NumBytes, TEXT("Global dynamic read buffer int32 buffer allocation failed: BufferSize=%d AllocatedByteCount=%d SizeInBytes=%d"), Buffer->NumBytes, Buffer->AllocatedByteCount, SizeInBytes);
	Allocation.Buffer = Buffer->MappedBuffer + Buffer->AllocatedByteCount;
	Allocation.ReadBuffer = Buffer;
	Allocation.FirstIndex = Buffer->AllocatedByteCount;
	Buffer->AllocatedByteCount += SizeInBytes;

	return Allocation;
}

bool FGlobalDynamicReadBuffer::IsRenderAlarmLoggingEnabled() const
{
	return GMaxReadBufferRenderingBytesAllocatedPerFrame > 0 && TotalAllocatedSinceLastCommit >= (size_t)GMaxReadBufferRenderingBytesAllocatedPerFrame;
}

void FGlobalDynamicReadBuffer::Commit()
{
	for (int32 BufferIndex = 0, NumBuffers = FloatBufferPool->Buffers.Num(); BufferIndex < NumBuffers; ++BufferIndex)
	{
		FDynamicAllocReadBuffer& Buffer = FloatBufferPool->Buffers[BufferIndex];
		if (Buffer.MappedBuffer != NULL)
		{
			Buffer.Unlock();
		}
		else if (GGlobalBufferNumFramesUnusedThresold && !Buffer.AllocatedByteCount)
		{
			++Buffer.NumFramesUnused;
			if (Buffer.NumFramesUnused >= GGlobalBufferNumFramesUnusedThresold )
			{
				// Remove the buffer, assumes they are unordered.
				Buffer.Release();
				FloatBufferPool->Buffers.RemoveAtSwap(BufferIndex);
				--BufferIndex;
				--NumBuffers;
			}
		}
	}
	FloatBufferPool->CurrentBuffer = NULL;

	for (int32 BufferIndex = 0, NumBuffers = Int32BufferPool->Buffers.Num(); BufferIndex < NumBuffers; ++BufferIndex)
	{
		FDynamicAllocReadBuffer& Buffer = Int32BufferPool->Buffers[BufferIndex];
		if (Buffer.MappedBuffer != NULL)
		{
			Buffer.Unlock();
		}
		else if (GGlobalBufferNumFramesUnusedThresold  && !Buffer.AllocatedByteCount)
		{
			++Buffer.NumFramesUnused;
			if (Buffer.NumFramesUnused >= GGlobalBufferNumFramesUnusedThresold )
			{
				// Remove the buffer, assumes they are unordered.
				Buffer.Release();
				Int32BufferPool->Buffers.RemoveAtSwap(BufferIndex);
				--BufferIndex;
				--NumBuffers;
			}
		}
	}
	Int32BufferPool->CurrentBuffer = NULL;
	TotalAllocatedSinceLastCommit = 0;
}