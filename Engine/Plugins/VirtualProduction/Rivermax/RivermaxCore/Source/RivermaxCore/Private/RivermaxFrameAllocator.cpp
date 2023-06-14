// Copyright Epic Games, Inc. All Rights Reserved.

#include "RivermaxFrameAllocator.h"

#include "CudaModule.h"
#include "RivermaxLog.h"
#include "ID3D12DynamicRHI.h"
#include "IRivermaxOutputStream.h"

namespace UE::RivermaxCore::Private
{
	bool FGPUAllocator::Allocate(int32 FrameCount)
	{
		// Allocate a single memory space that will contain all frame buffers
		TRACE_CPUPROFILER_EVENT_SCOPE(Rmax::GPUAllocation);

		FCUDAModule& CudaModule = FModuleManager::GetModuleChecked<FCUDAModule>("CUDA");
		CudaModule.DriverAPI()->cuCtxPushCurrent(CudaModule.GetCudaContext());

		// Todo: Add support for mgpu. For now, this will not work unless the memcpy does implicitely a cross gpu transfer.
		const int GPUIndex = CudaModule.GetCudaDeviceIndex();
		CUdevice CudaDevice;
		CUresult Status = CudaModule.DriverAPI()->cuDeviceGet(&CudaDevice, GPUIndex);
		if (Status != CUDA_SUCCESS)
		{
			UE_LOG(LogRivermax, Warning, TEXT("Can't allocate GPUMemory. Failed to get a Cuda device for GPU %d. Status: %d"), GPUIndex, Status);
			return false;
		}

		CUmemAllocationProp AllocationProperties = {};
		AllocationProperties.type = CU_MEM_ALLOCATION_TYPE_PINNED;
		AllocationProperties.allocFlags.gpuDirectRDMACapable = 1;
		AllocationProperties.allocFlags.usage = 0;
		AllocationProperties.location.type = CU_MEM_LOCATION_TYPE_DEVICE;
		AllocationProperties.location.id = CudaDevice;

		// Get memory granularity required for cuda device. We need to align allocation with this.
		size_t Granularity;
		Status = CudaModule.DriverAPI()->cuMemGetAllocationGranularity(&Granularity, &AllocationProperties, CU_MEM_ALLOC_GRANULARITY_RECOMMENDED);
		if (Status != CUDA_SUCCESS)
		{
			UE_LOG(LogRivermax, Warning, TEXT("Can't allocate GPUMemory. Failed to get allocation granularity. Status: %d"), Status);
			return false;
		}

		// Cuda requires allocated memory to be aligned with a certain granularity
		// We align each frame size to the desired granularity and multiply that by number of buffer
		// This causes more memory to be allocated but doing a single allocation fails rmax stream creation
		const size_t CudaAlignedFrameSize = (DesiredFrameSize % Granularity) ? DesiredFrameSize + (Granularity - (DesiredFrameSize % Granularity)) : DesiredFrameSize;
		const size_t TotalCudaAllocSize = CudaAlignedFrameSize * FrameCount;

		// Reserve contiguous memory to contain required number of buffers. 
		CUdeviceptr CudaBaseAddress;
		constexpr CUdeviceptr InitialAddress = 0;
		constexpr int32 Flags = 0;
		Status = CudaModule.DriverAPI()->cuMemAddressReserve(&CudaBaseAddress, TotalCudaAllocSize, Granularity, InitialAddress, Flags);
		if (Status != CUDA_SUCCESS)
		{
			UE_LOG(LogRivermax, Warning, TEXT("Can't allocate GPUMemory. Failed to reserve memory for %d bytes. Status: %d"), TotalCudaAllocSize, Status);
			return false;
		}

		// Make the allocation on device memory
		CUmemGenericAllocationHandle Handle;
		Status = CudaModule.DriverAPI()->cuMemCreate(&Handle, TotalCudaAllocSize, &AllocationProperties, Flags);
		if (Status != CUDA_SUCCESS)
		{
			UE_LOG(LogRivermax, Warning, TEXT("Can't allocate GPUMemory. Failed to create memory on device. Status: %d"), Status);
			return false;
		}

		bool bExit = false;
		constexpr int32 Offset = 0;
		Status = CudaModule.DriverAPI()->cuMemMap(CudaBaseAddress, TotalCudaAllocSize, Offset, Handle, Flags);
		if (Status != CUDA_SUCCESS)
		{
			UE_LOG(LogRivermax, Warning, TEXT("Can't allocate GPUMemory. Failed to map memory. Status: %d"), Status);
			// Need to release handle no matter what
			bExit = true;
		}

		// Cache to know we need to unmap/deallocate even if it fails down the road
		CudaAllocatedMemory = TotalCudaAllocSize;
		CudaAllocatedMemoryBaseAddress = reinterpret_cast<void*>(CudaBaseAddress);

		Status = CudaModule.DriverAPI()->cuMemRelease(Handle);
		if (Status != CUDA_SUCCESS)
		{
			UE_LOG(LogRivermax, Warning, TEXT("Can't allocate GPUMemory. Failed to release handle. Status: %d"), Status);
			return false;
		}

		if (bExit)
		{
			return false;
		}

		// Setup access description.
		CUmemAccessDesc MemoryAccessDescription = {};
		MemoryAccessDescription.flags = CU_MEM_ACCESS_FLAGS_PROT_READWRITE;
		MemoryAccessDescription.location.type = CU_MEM_LOCATION_TYPE_DEVICE;
		MemoryAccessDescription.location.id = CudaDevice;
		constexpr int32 Count = 1;
		Status = CudaModule.DriverAPI()->cuMemSetAccess(CudaBaseAddress, TotalCudaAllocSize, &MemoryAccessDescription, Count);
		if (Status != CUDA_SUCCESS)
		{
			UE_LOG(LogRivermax, Warning, TEXT("Can't initialize output to use GPUDirect. Failed to configure memory access. Status: %d"), Status);
			return false;
		}

		CUstream CudaStream;
		Status = CudaModule.DriverAPI()->cuStreamCreate(&CudaStream, CU_STREAM_NON_BLOCKING);
		if (Status != CUDA_SUCCESS)
		{
			UE_LOG(LogRivermax, Warning, TEXT("Can't initialize output to use GPUDirect. Failed to create its stream. Status: %d"), Status);
			return false;
		}

		GPUStream = CudaStream;

		Status = CudaModule.DriverAPI()->cuCtxSynchronize();
		if (Status != CUDA_SUCCESS)
		{
			UE_LOG(LogRivermax, Warning, TEXT("Can't initialize output to use GPUDirect. Failed to synchronize context. Status: %d"), Status);
			return false;
		}

		AllocatedFrameCount = FrameCount;
		AllocatedFrameSize = CudaAlignedFrameSize;

		for (int32 Index = 0; Index < AllocatedFrameCount; ++Index)
		{
			TSharedPtr<FRivermaxOutputFrame> Frame = MakeShared<FRivermaxOutputFrame>(Index);
			Frame->VideoBuffer = GetFrameAddress(Index);
			Frames.Add(MoveTemp(Frame));
		}

		CudaModule.DriverAPI()->cuCtxPopCurrent(nullptr);

		return true;
	}

	void FGPUAllocator::Deallocate()
	{
		if (CudaAllocatedMemory > 0)
		{
			FCUDAModule& CudaModule = FModuleManager::GetModuleChecked<FCUDAModule>("CUDA");
			CudaModule.DriverAPI()->cuCtxPushCurrent(CudaModule.GetCudaContext());

			for (const TPair<FBufferRHIRef, void*>& Entry : BufferCudaMemoryMap)
			{
				if (Entry.Value)
				{
					CudaModule.DriverAPI()->cuMemFree(reinterpret_cast<CUdeviceptr>(Entry.Value));
				}
			}
			BufferCudaMemoryMap.Empty();

			const CUdeviceptr BaseAddress = reinterpret_cast<CUdeviceptr>(CudaAllocatedMemoryBaseAddress);
			CUresult Status = CudaModule.DriverAPI()->cuMemUnmap(BaseAddress, CudaAllocatedMemory);
			if (Status != CUDA_SUCCESS)
			{
				UE_LOG(LogRivermax, Warning, TEXT("Failed to unmap cuda memory. Status: %d"), Status);
			}

			Status = CudaModule.DriverAPI()->cuMemAddressFree(BaseAddress, CudaAllocatedMemory);
			if (Status != CUDA_SUCCESS)
			{
				UE_LOG(LogRivermax, Warning, TEXT("Failed to free cuda memory. Status: %d"), Status);
			}

			Status = CudaModule.DriverAPI()->cuStreamDestroy(reinterpret_cast<CUstream>(GPUStream));
			if (Status != CUDA_SUCCESS)
			{
				UE_LOG(LogRivermax, Warning, TEXT("Failed to destroy cuda stream. Status: %d"), Status);
			}
			GPUStream = nullptr;
			AllocatedFrameCount = 0;
			Frames.Empty();

			CudaModule.DriverAPI()->cuCtxPopCurrent(nullptr);
		}
	}

	void* FGPUAllocator::GetFrameAddress(int32 FrameIndex) const
	{
		if (FrameIndex >= 0 && FrameIndex < AllocatedFrameCount)
		{
			return reinterpret_cast<uint8*>(CudaAllocatedMemoryBaseAddress) + (FrameIndex * AllocatedFrameSize);
		}

		return nullptr;
	}

	bool FGPUAllocator::CopyData(const FRivermaxOutputVideoFrameInfo& NewFrame, const TSharedPtr<FRivermaxOutputFrame>& DestinationFrame)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Rmax::GPUCopyStart);
		check(NewFrame.GPUBuffer);
		{
			FCUDAModule& CudaModule = FModuleManager::GetModuleChecked<FCUDAModule>("CUDA");
			CUresult Result = CudaModule.DriverAPI()->cuCtxPushCurrent(CudaModule.GetCudaContext());

			void* MappedPointer = GetMappedAddress(NewFrame.GPUBuffer);
			if (MappedPointer == nullptr)
			{
				UE_LOG(LogRivermax, Error, TEXT("Failed to find a mapped memory address for captured buffer. Stopping capture."));
				return false;
			}

			const CUdeviceptr CudaMemoryPointer = reinterpret_cast<CUdeviceptr>(MappedPointer);
			Result = CudaModule.DriverAPI()->cuMemcpyDtoDAsync(reinterpret_cast<CUdeviceptr>(DestinationFrame->VideoBuffer), CudaMemoryPointer, DesiredFrameSize, reinterpret_cast<CUstream>(GPUStream));
			if (Result != CUDA_SUCCESS)
			{
				UE_LOG(LogRivermax, Error, TEXT("Failed to copy captured bufer to cuda memory. Stopping capture. Error: %d"), Result);
				return false;
			}

			// Callback called by Cuda when stream work has completed on cuda engine (MemCpy -> Callback)
			// Once Memcpy has been done, we know we can mark that memory as available to be sent. 

			auto CudaCallback = [](void* userData)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(Rmax::GPUCopyDone);

				FGPUAllocator* Allocator = reinterpret_cast<FGPUAllocator*>(userData);
				TOptional<TSharedPtr<FRivermaxOutputFrame>> FrameCopied = Allocator->PendingCopies.Dequeue();
				if (FrameCopied.IsSet())
				{
					FrameCopied.GetValue()->bIsVideoBufferReady = true;
					Allocator->OnDataCopiedDelegate.ExecuteIfBound(FrameCopied.GetValue());
				}
			};

			// Add pending frame for cuda callback 
			PendingCopies.Enqueue(DestinationFrame);

			// Schedule a callback to make the frame available
			CudaModule.DriverAPI()->cuLaunchHostFunc(reinterpret_cast<CUstream>(GPUStream), CudaCallback, this);

			FCUDAModule::CUDA().cuCtxPopCurrent(nullptr);

			return true;
		}

		return false;
	}

	FGPUAllocator::FGPUAllocator(FIntPoint InFrameResolution, uint32 InStride, FOnFrameDataCopiedDelegate InOnDataCopiedDelegate)
		: Super(InFrameResolution, InStride, InOnDataCopiedDelegate)
	{

	}

	void* FGPUAllocator::GetMappedAddress(const FBufferRHIRef& InBuffer)
	{
		// If we are here, d3d12 had to have been validated
		const ERHIInterfaceType RHIType = RHIGetInterfaceType();
		check(RHIType == ERHIInterfaceType::D3D12);

		//Do we already have a mapped address for this buffer
		if (BufferCudaMemoryMap.Find((InBuffer)) == nullptr)
		{
			int64 BufferMemorySize = 0;
			CUexternalMemory MappedExternalMemory = nullptr;
			HANDLE D3D12BufferHandle = 0;
			CUDA_EXTERNAL_MEMORY_HANDLE_DESC CudaExtMemHandleDesc = {};

			// Create shared handle for our buffer
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(Rmax_D3D12CreateSharedHandle);

				ID3D12Resource* NativeD3D12Resource = GetID3D12DynamicRHI()->RHIGetResource(InBuffer);
				BufferMemorySize = GetID3D12DynamicRHI()->RHIGetResourceMemorySize(InBuffer);

				TRefCountPtr<ID3D12Device> OwnerDevice;
				HRESULT QueryResult;
				if ((QueryResult = NativeD3D12Resource->GetDevice(IID_PPV_ARGS(OwnerDevice.GetInitReference()))) != S_OK)
				{
					UE_LOG(LogRivermax, Error, TEXT("Failed to get D3D12 device for captured buffer ressource: %d)"), QueryResult);
					return nullptr;
				}

				if ((QueryResult = OwnerDevice->CreateSharedHandle(NativeD3D12Resource, NULL, GENERIC_ALL, NULL, &D3D12BufferHandle)) != S_OK)
				{
					UE_LOG(LogRivermax, Error, TEXT("Failed to create shared handle for captured buffer ressource: %d"), QueryResult);
					return nullptr;
				}

				CudaExtMemHandleDesc.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE;
				CudaExtMemHandleDesc.handle.win32.name = nullptr;
				CudaExtMemHandleDesc.handle.win32.handle = D3D12BufferHandle;
				CudaExtMemHandleDesc.size = BufferMemorySize;
				CudaExtMemHandleDesc.flags |= CUDA_EXTERNAL_MEMORY_DEDICATED;
			}

			{
				TRACE_CPUPROFILER_EVENT_SCOPE(Rmax_CudaImportMemory);

				const CUresult Result = FCUDAModule::CUDA().cuImportExternalMemory(&MappedExternalMemory, &CudaExtMemHandleDesc);

				if (D3D12BufferHandle)
				{
					CloseHandle(D3D12BufferHandle);
				}

				if (Result != CUDA_SUCCESS)
				{
					UE_LOG(LogRivermax, Error, TEXT("Failed to import shared buffer. Error: %d"), Result);
					return nullptr;
				}
			}

			{
				TRACE_CPUPROFILER_EVENT_SCOPE(Rmax_MapCudaMemory);

				CUDA_EXTERNAL_MEMORY_BUFFER_DESC BufferDescription = {};
				BufferDescription.offset = 0;
				BufferDescription.size = BufferMemorySize;
				CUdeviceptr NewMemory;
				const CUresult Result = FCUDAModule::CUDA().cuExternalMemoryGetMappedBuffer(&NewMemory, MappedExternalMemory, &BufferDescription);
				if (Result != CUDA_SUCCESS || NewMemory == 0)
				{
					UE_LOG(LogRivermax, Error, TEXT("Failed to get shared buffer mapped memory. Error: %d"), Result);
					return nullptr;
				}

				BufferCudaMemoryMap.Add(InBuffer, reinterpret_cast<void*>(NewMemory));
			}
		}

		// At this point, we have the mapped buffer in cuda space and we can use it to schedule a memcpy on cuda engine.
		return BufferCudaMemoryMap[InBuffer];
	}

	bool FSystemAllocator::Allocate(int32 FrameCount)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Rmax::SystemAllocation);

		constexpr uint32 CacheLineSize = PLATFORM_CACHE_LINE_SIZE;

		AllocatedFrameSize = Align(DesiredFrameSize, CacheLineSize);
		const size_t NeededSystemMemory = Align(AllocatedFrameSize * FrameCount, CacheLineSize);
		SystemMemoryBaseAddress = FMemory::Malloc(AllocatedFrameSize * FrameCount, CacheLineSize);

		if (SystemMemoryBaseAddress)
		{
			AllocatedFrameCount = FrameCount;

			for (int32 Index = 0; Index < AllocatedFrameCount; ++Index)
			{
				TSharedPtr<FRivermaxOutputFrame> Frame = MakeShared<FRivermaxOutputFrame>(Index);
				Frame->VideoBuffer = GetFrameAddress(Index);
				Frames.Add(MoveTemp(Frame));
			}

			return true;
		}

		return false;
	}

	void FSystemAllocator::Deallocate()
	{
		if (SystemMemoryBaseAddress)
		{
			FMemory::Free(SystemMemoryBaseAddress);
			SystemMemoryBaseAddress = nullptr;
			AllocatedFrameCount = 0;
			Frames.Empty();
		}
	}

	void* FSystemAllocator::GetFrameAddress(int32 FrameIndex) const
	{
		if (FrameIndex >= 0 && FrameIndex < AllocatedFrameCount)
		{
			return reinterpret_cast<uint8*>(SystemMemoryBaseAddress) + (FrameIndex * AllocatedFrameSize);
		}

		return nullptr;
	}

	FSystemAllocator::FSystemAllocator(FIntPoint InFrameResolution, uint32 InStride, FOnFrameDataCopiedDelegate InOnDataCopiedDelegate)
		: Super(InFrameResolution, InStride, InOnDataCopiedDelegate)
	{

	}

	bool FSystemAllocator::CopyData(const FRivermaxOutputVideoFrameInfo& NewFrame, const TSharedPtr<FRivermaxOutputFrame>& DestinationFrame)
	{
		FMemory::Memcpy(DestinationFrame->VideoBuffer, NewFrame.VideoBuffer, DesiredFrameSize);

		DestinationFrame->bIsVideoBufferReady = true;
		OnDataCopiedDelegate.ExecuteIfBound(DestinationFrame);

		return true;
	}

	FBaseFrameAllocator::FBaseFrameAllocator(FIntPoint InFrameResolution, uint32 InStride, FOnFrameDataCopiedDelegate InOnDataCopiedDelegate)
		: FrameResolution(InFrameResolution)
		, Stride(InStride)
		, DesiredFrameSize(FrameResolution.Y * Stride)
		, OnDataCopiedDelegate(MoveTemp(InOnDataCopiedDelegate))
	{

	}

	const TSharedPtr<FRivermaxOutputFrame> FBaseFrameAllocator::GetFrame(int32 Index) const
	{
		if (Frames.IsValidIndex(Index))
		{
			return Frames[Index];
		}
		return nullptr;
	}
}

