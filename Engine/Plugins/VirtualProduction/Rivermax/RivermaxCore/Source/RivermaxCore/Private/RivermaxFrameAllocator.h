// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Containers/SpscQueue.h"
#include "Delegates/DelegateCombinations.h"
#include "RivermaxOutputFrame.h"
#include "RHI.h"

namespace UE::RivermaxCore
{
	struct FRivermaxOutputVideoFrameInfo;
}


namespace UE::RivermaxCore::Private
{
	struct FRivermaxOutputFrame;

	/** Delegate triggered when data has been copied into a given frame */
	DECLARE_DELEGATE_OneParam(FOnFrameDataCopiedDelegate, const TSharedPtr<FRivermaxOutputFrame>&);

	/** Base class for frame allocation. Currently supports GPU allocation using CUDA and System memory */
	class FBaseFrameAllocator
	{
	public:
		FBaseFrameAllocator(FIntPoint FrameResolution, uint32 Stride, FOnFrameDataCopiedDelegate InOnDataCopiedDelegate);
		virtual ~FBaseFrameAllocator() = default;

		/** Allocates memory to hold FrameCount frames */
		virtual bool Allocate(int32 FrameCount) = 0;

		/** Deallocate memory */
		virtual void Deallocate() = 0;

		/** Initiate a copy into a given frame */
		virtual bool CopyData(const FRivermaxOutputVideoFrameInfo& NewFrame, const TSharedPtr<FRivermaxOutputFrame>& DestinationFrame) = 0;
		
		/** Returns allocated frame for a given index */
		const TSharedPtr<FRivermaxOutputFrame> GetFrame(int32 Index) const;

	protected:
		
		/** Returns buffer address of a given frame */
		virtual void* GetFrameAddress(int32 FrameIndex) const = 0;

	protected:

		/** List of allocated frames. */
		TArray<TSharedPtr<FRivermaxOutputFrame>> Frames;

		/** Resolution of frames to be allocated */
		FIntPoint FrameResolution = FIntPoint::ZeroValue;

		/** Stride of a line */
		uint32 Stride = 0;
		
		/** Total desired size of a frame */
		int32 DesiredFrameSize = 0;

		/** Delegate callbacked when data has been copied. Depending on allocator, memcopy can be async. */
		FOnFrameDataCopiedDelegate OnDataCopiedDelegate;
	};

	class FGPUAllocator : public FBaseFrameAllocator
	{
		using Super = FBaseFrameAllocator;

	public:
		FGPUAllocator(FIntPoint FrameResolution, uint32 Stride, FOnFrameDataCopiedDelegate InOnDataCopiedDelegate);

		//~ Begin FBaseFrameAllocator interface
		virtual bool Allocate(int32 FrameCount) override;
		virtual void Deallocate() override;
		virtual bool CopyData(const FRivermaxOutputVideoFrameInfo& NewFrame, const TSharedPtr<FRivermaxOutputFrame>& DestinationFrame) override;
	
	protected:
		virtual void* GetFrameAddress(int32 FrameIndex) const override;
		//~ End FBaseFrameAllocator interface

	private:
		/** Get mapped address in cuda space for a given buffer. Cache will be updated if not found */
		void* GetMappedAddress(const FBufferRHIRef& InBuffer);

	private:

		/** Number of frames that was allocated */
		int32 AllocatedFrameCount = 0;

		/** Allocated size of a frame */
		size_t AllocatedFrameSize = 0;

		/** Allocated memory base address used when it's time to free */
		void* CudaAllocatedMemoryBaseAddress = nullptr;

		/** Total allocated gpu memory. */
		int32 CudaAllocatedMemory = 0;

		/** Cuda stream used for our operations */
		void* GPUStream = nullptr;

		/** Map between buffer we are sending and their mapped address in gpu space */
		TMap<FBufferRHIRef, void*> BufferCudaMemoryMap;

		/** Frames that are waiting for a cuda memcopy. Will be dequeued when our cuda task has completed */
		TSpscQueue<TSharedPtr<FRivermaxOutputFrame>> PendingCopies;
	};

	class FSystemAllocator : public FBaseFrameAllocator
	{
		using Super = FBaseFrameAllocator;

	public:
		FSystemAllocator(FIntPoint FrameResolution, uint32 Stride, FOnFrameDataCopiedDelegate InOnDataCopiedDelegate);
		
		//~ Begin FBaseFrameAllocator interface
		virtual bool Allocate(int32 FrameCount) override;
		virtual void Deallocate() override;
		virtual bool CopyData(const FRivermaxOutputVideoFrameInfo& NewFrame, const TSharedPtr<FRivermaxOutputFrame>& DestinationFrame) override;
	
	protected:
		virtual void* GetFrameAddress(int32 FrameIndex) const override;
		//~ End FBaseFrameAllocator interface
	
	private:

		/** Number of frames that was allocated */
		int32 AllocatedFrameCount = 0;

		/** Size of a frame that was allocated */
		size_t AllocatedFrameSize = 0;

		/** Total memory allocated to hold all frames */
		size_t SystemAllocatedMemory = 0;

		/** Allocated memory base address used when it's time to free */
		void* SystemMemoryBaseAddress = nullptr;
	};
}


