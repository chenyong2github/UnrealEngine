// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanMemory.cpp: Vulkan memory RHI implementation.
=============================================================================*/

#include "VulkanRHIPrivate.h"
#include "VulkanMemory.h"
#include "Misc/OutputDeviceRedirector.h"
#include "HAL/PlatformStackWalk.h"
#include "VulkanLLM.h"
#include "Containers/SortedMap.h" 

// This 'frame number' should only be used for the deletion queue
uint32 GVulkanRHIDeletionFrameNumber = 0;
const uint32 NUM_FRAMES_TO_WAIT_FOR_RESOURCE_DELETE = 2;

#define UE_VK_MEMORY_MAX_SUB_ALLOCATION (64llu << 20llu) // set to 0 to disable

#define UE_VK_MEMORY_KEEP_FREELIST_SORTED					1
#define UE_VK_MEMORY_JOIN_FREELIST_ON_THE_FLY				(UE_VK_MEMORY_KEEP_FREELIST_SORTED && 1)
#define UE_VK_MEMORY_KEEP_FREELIST_SORTED_CATCHBUGS			0 // debugging
#define VULKAN_FREE_ALL_PAGES ((PLATFORM_ANDROID && !PLATFORM_LUMIN) ? 1 : 0)

#define VULKAN_LOG_MEMORY_UELOG 1 //in case of debugging, it is useful to be able to log directly to LowLevelPrintf, as this is easier to diff. Please do not delete this code.

#if VULKAN_LOG_MEMORY_UELOG
#define VULKAN_LOGMEMORY(fmt, ...) UE_LOG(LogVulkanRHI, Display, fmt, ##__VA_ARGS__)
#else
#define VULKAN_LOGMEMORY(fmt, ...) FPlatformMisc::LowLevelOutputDebugStringf(fmt TEXT("\n"), ##__VA_ARGS__)
#endif


DECLARE_STATS_GROUP(TEXT("Vulkan Memory Raw"), STATGROUP_VulkanMemoryRaw, STATCAT_Advanced);
DECLARE_MEMORY_STAT_EXTERN(TEXT("Dedicated Memory"), STAT_VulkanDedicatedMemory, STATGROUP_VulkanMemoryRaw, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("MemoryPool 0"), STAT_VulkanMemory0, STATGROUP_VulkanMemoryRaw, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("MemoryPool 1"), STAT_VulkanMemory1, STATGROUP_VulkanMemoryRaw, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("MemoryPool 2"), STAT_VulkanMemory2, STATGROUP_VulkanMemoryRaw, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("MemoryPool (remaining)"), STAT_VulkanMemoryX, STATGROUP_VulkanMemoryRaw, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("_Total Allocated"), STAT_VulkanMemoryTotal, STATGROUP_VulkanMemoryRaw, );

DEFINE_STAT(STAT_VulkanDedicatedMemory);
DEFINE_STAT(STAT_VulkanMemory0);
DEFINE_STAT(STAT_VulkanMemory1);
DEFINE_STAT(STAT_VulkanMemory2);
DEFINE_STAT(STAT_VulkanMemoryX);
DEFINE_STAT(STAT_VulkanMemoryTotal);


DECLARE_STATS_GROUP(TEXT("Vulkan Memory"), STATGROUP_VulkanMemory, STATCAT_Advanced);
DECLARE_MEMORY_STAT_EXTERN(TEXT("Unknown"), STAT_VulkanAllocation_Unknown, STATGROUP_VulkanMemory, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("UniformBuffer"), STAT_VulkanAllocation_UniformBuffer, STATGROUP_VulkanMemory, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("MultiBuffer"), STAT_VulkanAllocation_MultiBuffer, STATGROUP_VulkanMemory, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("RingBuffer"), STAT_VulkanAllocation_RingBuffer, STATGROUP_VulkanMemory, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("FrameTempBuffer"), STAT_VulkanAllocation_FrameTempBuffer, STATGROUP_VulkanMemory, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("ImageRenderTarget"), STAT_VulkanAllocation_ImageRenderTarget, STATGROUP_VulkanMemory, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("ImageOther"), STAT_VulkanAllocation_ImageOther, STATGROUP_VulkanMemory, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("BufferUAV"), STAT_VulkanAllocation_BufferUAV, STATGROUP_VulkanMemory, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("BufferStaging"), STAT_VulkanAllocation_BufferStaging, STATGROUP_VulkanMemory, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("BufferOther"), STAT_VulkanAllocation_BufferOther, STATGROUP_VulkanMemory, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("_Total"), STAT_VulkanAllocation_Allocated, STATGROUP_VulkanMemory, );

DEFINE_STAT(STAT_VulkanAllocation_UniformBuffer);
DEFINE_STAT(STAT_VulkanAllocation_MultiBuffer);
DEFINE_STAT(STAT_VulkanAllocation_RingBuffer);
DEFINE_STAT(STAT_VulkanAllocation_FrameTempBuffer);
DEFINE_STAT(STAT_VulkanAllocation_ImageRenderTarget);
DEFINE_STAT(STAT_VulkanAllocation_ImageOther);
DEFINE_STAT(STAT_VulkanAllocation_BufferUAV);
DEFINE_STAT(STAT_VulkanAllocation_BufferStaging);
DEFINE_STAT(STAT_VulkanAllocation_BufferOther);
DEFINE_STAT(STAT_VulkanAllocation_Allocated);

static int32 GVulkanMemoryBackTrace = 10;
static FAutoConsoleVariableRef CVarVulkanMemoryBackTrace(
	TEXT("r.Vulkan.MemoryBacktrace"),
	GVulkanMemoryBackTrace,
	TEXT("0: Disable, store __FILE__ and __LINE__\n")
	TEXT("N: Enable, n is # of steps to go back\n"),
	ECVF_ReadOnly
);

int32 GVulkanUseBufferBinning = 0;
static FAutoConsoleVariableRef CVarVulkanUseBufferBinning(
	TEXT("r.Vulkan.UseBufferBinning"),
	GVulkanUseBufferBinning,
	TEXT("Enable binning sub-allocations within buffers to help reduce fragmentation at the expense of higher high watermark [read-only]\n"),
	ECVF_ReadOnly
);

static int32 GVulkanFreePageForType = VULKAN_FREEPAGE_FOR_TYPE;
static FAutoConsoleVariableRef CVarVulkanFreePageForType(
	TEXT("r.Vulkan.FreePageForType"),
	GVulkanFreePageForType,
	TEXT("Enable separate free page list for images and buffers."),
	ECVF_ReadOnly
);

static int32 GVulkanFreeAllPages = VULKAN_FREE_ALL_PAGES;
static FAutoConsoleVariableRef CVarVulkanFreeAllPages(
	TEXT("r.Vulkan.FreeAllPages"),
	GVulkanFreeAllPages,
	TEXT("Enable to fully free all pages early. default on android only"),
	ECVF_ReadOnly
);

static int32 GVulkanLogEvictStatus = 0;
static FAutoConsoleVariableRef GVarVulkanLogEvictStatus(
	TEXT("r.Vulkan.LogEvictStatus"),
	GVulkanLogEvictStatus,
	TEXT("Log Eviction status every frame"),
	ECVF_RenderThreadSafe
);



int32 GVulkanEnableDedicatedImageMemory = 1;
static FAutoConsoleVariableRef CVarVulkanEnableDedicatedImageMemory(
	TEXT("r.Vulkan.EnableDedicatedImageMemory"),
	GVulkanEnableDedicatedImageMemory,
	TEXT("Enable to use Dedidcated Image memory on devices that prefer it."),
	ECVF_RenderThreadSafe
);


int32 GVulkanSingleAllocationPerResource = VULKAN_SINGLE_ALLOCATION_PER_RESOURCE;
static FAutoConsoleVariableRef CVarVulkanSingleAllocationPerResource(
	TEXT("r.Vulkan.SingleAllocationPerResource"),
	GVulkanSingleAllocationPerResource,
	TEXT("Enable to do a single allocation per resource"),
	ECVF_RenderThreadSafe
);

//debug variable to force evict one page
int32 GVulkanEvictOnePage = 0;
static FAutoConsoleVariableRef CVarVulkanEvictbLilleHestyMusOne(
	TEXT("r.Vulkan.EvictOnePageDebug"),
	GVulkanEvictOnePage,
	TEXT("Set to 1 to test evict one page to host"),
	ECVF_RenderThreadSafe
);
#if !UE_BUILD_SHIPPING
static int32 GVulkanFakeMemoryLimit = 0;
static FAutoConsoleVariableRef CVarVulkanFakeMemoryLimit(
	TEXT("r.Vulkan.FakeMemoryLimit"),
	GVulkanFakeMemoryLimit,
	TEXT("set to artificially limit to # MB. 0 is disabled"),
	ECVF_RenderThreadSafe
);
#endif

static float GVulkanEvictionLimitPercentage = 80.f;
static FAutoConsoleVariableRef CVarVulkanEvictionLimitPercentage(
	TEXT("r.Vulkan.EvictionLimitPercentage"),
	GVulkanEvictionLimitPercentage,
	TEXT("When more than x% of local memory is used, evict resources to host memory"),
	ECVF_RenderThreadSafe
);


static float GVulkanEvictionLimitPercentageReenableLimit = 75.f;
static FAutoConsoleVariableRef CVarVulkanEvictionLimitPercentageReenableLimit(
	TEXT("r.Vulkan.EvictionLimitPercentageRenableLimit"),
	GVulkanEvictionLimitPercentageReenableLimit,
	TEXT("After eviction has occurred, only start using local mem for textures after memory usage is less than this(Relative to Eviction percentage)"),
	ECVF_RenderThreadSafe
);


RENDERCORE_API	void DumpRenderTargetPoolMemory(FOutputDevice& OutputDevice);

#if (UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT)
static int32 GForceCoherent = 0;
static FAutoConsoleVariableRef CVarForceCoherentOperations(
	TEXT("r.Vulkan.ForceCoherentOperations"),
	GForceCoherent,
	TEXT("1 forces memory invalidation and flushing of coherent memory\n"),
	ECVF_ReadOnly
);
#else
constexpr int32 GForceCoherent = 0;
#endif


FVulkanTrackInfo::FVulkanTrackInfo()
	: Data(0)
	, SizeOrLine(0)
{
}

#if VULKAN_MEMORY_TRACK
#define VULKAN_FILL_TRACK_INFO(...) do{VulkanTrackFillInfo(__VA_ARGS__);}while(0)
#define VULKAN_FREE_TRACK_INFO(...) do{VulkanTrackFreeInfo(__VA_ARGS__);}while(0)
#define VULKAN_TRACK_STRING(s) VulkanTrackGetString(s)
#else
#define VULKAN_FILL_TRACK_INFO(...) do{}while(0)
#define VULKAN_FREE_TRACK_INFO(...) do{}while(0)
#define VULKAN_TRACK_STRING(s) FString("")
#endif

FString VulkanTrackGetString(FVulkanTrackInfo& Track)
{
	if (Track.SizeOrLine < 0)
	{
		const size_t STRING_SIZE = 16 * 1024;
		ANSICHAR StackTraceString[STRING_SIZE];
		uint64* Stack = (uint64*)Track.Data;
		FMemory::Memset(StackTraceString, 0, sizeof(StackTraceString));
		SIZE_T StringSize = STRING_SIZE;
		for (int32 Index = 0; Index < -Track.SizeOrLine; ++Index)
		{
			FPlatformStackWalk::ProgramCounterToHumanReadableString(Index, Stack[Index], StackTraceString, StringSize, 0);
			FCStringAnsi::Strncat(StackTraceString, LINE_TERMINATOR_ANSI, (int32)StringSize);
		}
		FString Out = FString::Printf(TEXT("\n%S\n"), StackTraceString);
		return Out;
	}
	else
	{
		return FString::Printf(TEXT("\n%S:%d\n"), (const char*)Track.Data, Track.SizeOrLine);
	}
}

void VulkanTrackFillInfo(FVulkanTrackInfo& Track, const char* File, uint32 Line)
{
	if (GVulkanMemoryBackTrace > 0)
	{
		uint64* Stack = new uint64[GVulkanMemoryBackTrace];
		int32 Depth = FPlatformStackWalk::CaptureStackBackTrace(Stack, GVulkanMemoryBackTrace);
		Track.SizeOrLine = -Depth;
		Track.Data = Stack;
	}
	else
	{
		Track.Data = (void*)File;
		Track.SizeOrLine = Line;
	}
}
void VulkanTrackFreeInfo(FVulkanTrackInfo& Track)
{
	if(Track.SizeOrLine < 0)
	{
		delete[] (uint64*)Track.Data;
	}
	Track.Data = 0;
	Track.SizeOrLine = 0;
}

namespace VulkanRHI
{
	struct FVulkanMemoryAllocation
	{
		const TCHAR* Name;
		FName ResourceName;
		void* Address;
		void* RHIResouce;
		uint32 Size;
		uint32 Width;
		uint32 Height;
		uint32 Depth;
		uint32 BytesPerPixel;
	};

	struct FVulkanMemoryBucket
	{
		TArray<FVulkanMemoryAllocation> Allocations;
	};

	struct FResourceHeapStats
	{
		uint64 BufferAllocations = 0;
		uint64 ImageAllocations = 0;
		uint64 UsedImageMemory = 0;
		uint64 UsedBufferMemory = 0;
		uint64 TotalMemory = 0;
		uint64 Pages = 0;
		uint64 ImagePages = 0;
		uint64 BufferPages = 0;
		VkMemoryPropertyFlags MemoryFlags = (VkMemoryPropertyFlags)0;

		FResourceHeapStats& operator += (const FResourceHeapStats& Other)
		{
			BufferAllocations += Other.BufferAllocations;
			ImageAllocations += Other.ImageAllocations;
			UsedImageMemory += Other.UsedImageMemory;
			UsedBufferMemory += Other.UsedBufferMemory;
			TotalMemory += Other.TotalMemory;
			Pages += Other.Pages;
			ImagePages += Other.ImagePages;
			BufferPages += Other.BufferPages;
			return *this;
		}
	};

	template<typename Callback>
	void IterateVulkanAllocations(Callback F, uint32 AllocatorIndex)
	{
		checkNoEntry();
	}

	enum
	{
		GPU_ONLY_HEAP_PAGE_SIZE = 256 * 1024 * 1024,
		STAGING_HEAP_PAGE_SIZE = 32 * 1024 * 1024,
		ANDROID_MAX_HEAP_PAGE_SIZE = 16 * 1024 * 1024,
		ANDROID_MAX_HEAP_IMAGE_PAGE_SIZE = 16 * 1024 * 1024,
		ANDROID_MAX_HEAP_BUFFER_PAGE_SIZE = 4 * 1024 * 1024,
	};


	constexpr uint32 FMemoryManager::PoolSizes[(int32)FMemoryManager::EPoolSizes::SizesCount];
	constexpr uint32 FMemoryManager::BufferSizes[(int32)FMemoryManager::EPoolSizes::SizesCount + 1];

	static FCriticalSection GResourcePageLock;
	static FCriticalSection GResourceLock;
	static FCriticalSection GStagingLock;
	static FCriticalSection GDeviceMemLock;
	static FCriticalSection GFenceLock;
	static FCriticalSection GResourceHeapLock;


	const TCHAR* VulkanAllocationTypeToString(EVulkanAllocationType Type)
	{
		switch (Type)
		{
		case EVulkanAllocationEmpty: return TEXT("Empty");
		case EVulkanAllocationPooledBuffer: return TEXT("PooledBuffer");
		case EVulkanAllocationBuffer: return TEXT("Buffer");
		case EVulkanAllocationImage: return TEXT("Image");
		case EVulkanAllocationImageDedicated: return TEXT("ImageDedicated");
		default:
			checkNoEntry();
		}
		return TEXT("");
	}
	const TCHAR* VulkanAllocationMetaTypeToString(EVulkanAllocationMetaType MetaType)
	{
		switch(MetaType)
		{
		case EVulkanAllocationMetaUnknown: return TEXT("Unknown");
		case EVulkanAllocationMetaUniformBuffer: return TEXT("UBO");
		case EVulkanAllocationMetaMultiBuffer: return TEXT("MultiBuf");
		case EVulkanAllocationMetaRingBuffer: return TEXT("RingBuf");
		case EVulkanAllocationMetaFrameTempBuffer: return TEXT("FrameTemp");
		case EVulkanAllocationMetaImageRenderTarget: return TEXT("ImageRT");
		case EVulkanAllocationMetaImageOther: return TEXT("Image");
		case EVulkanAllocationMetaBufferUAV: return TEXT("BufferUAV");
		case EVulkanAllocationMetaBufferStaging: return TEXT("BufferStg");
		case EVulkanAllocationMetaBufferOther: return TEXT("BufOthr");
		default:
			checkNoEntry();
		}
		return TEXT("");
	}

	static void DecMetaStats(EVulkanAllocationMetaType MetaType, uint32 Size)
	{
		DEC_DWORD_STAT_BY(STAT_VulkanAllocation_Allocated, Size);
		switch (MetaType)
		{
		case EVulkanAllocationMetaUniformBuffer:
			DEC_DWORD_STAT_BY(STAT_VulkanAllocation_UniformBuffer, Size);
			break;
		case EVulkanAllocationMetaMultiBuffer:
			DEC_DWORD_STAT_BY(STAT_VulkanAllocation_MultiBuffer, Size);
			break;
		case EVulkanAllocationMetaRingBuffer:
			DEC_DWORD_STAT_BY(STAT_VulkanAllocation_RingBuffer, Size);
			break;
		case EVulkanAllocationMetaFrameTempBuffer:
			DEC_DWORD_STAT_BY(STAT_VulkanAllocation_FrameTempBuffer, Size);
			break;
		case EVulkanAllocationMetaImageRenderTarget:
			DEC_DWORD_STAT_BY(STAT_VulkanAllocation_ImageRenderTarget, Size);
			break;
		case EVulkanAllocationMetaImageOther:
			DEC_DWORD_STAT_BY(STAT_VulkanAllocation_ImageOther, Size);
			break;
		case EVulkanAllocationMetaBufferUAV:
			DEC_DWORD_STAT_BY(STAT_VulkanAllocation_BufferUAV, Size);
			break;
		case EVulkanAllocationMetaBufferStaging:
			DEC_DWORD_STAT_BY(STAT_VulkanAllocation_BufferStaging, Size);
			break;
		case EVulkanAllocationMetaBufferOther:
			DEC_DWORD_STAT_BY(STAT_VulkanAllocation_BufferOther, Size);
			break;
		default:
			checkNoEntry();
		}
	}
	static void IncMetaStats(EVulkanAllocationMetaType MetaType, uint32 Size)
	{
		INC_DWORD_STAT_BY(STAT_VulkanAllocation_Allocated, Size);

		switch (MetaType)
		{
		case EVulkanAllocationMetaUniformBuffer:
			INC_DWORD_STAT_BY(STAT_VulkanAllocation_UniformBuffer, Size);
			break;
		case EVulkanAllocationMetaMultiBuffer:
			INC_DWORD_STAT_BY(STAT_VulkanAllocation_MultiBuffer, Size);
			break;
		case EVulkanAllocationMetaRingBuffer:
			INC_DWORD_STAT_BY(STAT_VulkanAllocation_RingBuffer, Size);
			break;
		case EVulkanAllocationMetaFrameTempBuffer:
			INC_DWORD_STAT_BY(STAT_VulkanAllocation_FrameTempBuffer, Size);
			break;
		case EVulkanAllocationMetaImageRenderTarget:
			INC_DWORD_STAT_BY(STAT_VulkanAllocation_ImageRenderTarget, Size);
			break;
		case EVulkanAllocationMetaImageOther:
			INC_DWORD_STAT_BY(STAT_VulkanAllocation_ImageOther, Size);
			break;
		case EVulkanAllocationMetaBufferUAV:
			INC_DWORD_STAT_BY(STAT_VulkanAllocation_BufferUAV, Size);
			break;
		case EVulkanAllocationMetaBufferStaging:
			INC_DWORD_STAT_BY(STAT_VulkanAllocation_BufferStaging, Size);
			break;
		case EVulkanAllocationMetaBufferOther:
			INC_DWORD_STAT_BY(STAT_VulkanAllocation_BufferOther, Size);
			break;
		default:
			checkNoEntry();
		}
	}

	FDeviceMemoryManager::FDeviceMemoryManager() :
		DeviceHandle(VK_NULL_HANDLE),
		bHasUnifiedMemory(false),
		Device(nullptr),
		NumAllocations(0),
		PeakNumAllocations(0)
	{
		FMemory::Memzero(MemoryProperties);
	}

	FDeviceMemoryManager::~FDeviceMemoryManager()
	{
		Deinit();
	}

	void FDeviceMemoryManager::Init(FVulkanDevice* InDevice)
	{
		check(Device == nullptr);
		Device = InDevice;
		NumAllocations = 0;
		PeakNumAllocations = 0;

		DeviceHandle = Device->GetInstanceHandle();
		VulkanRHI::vkGetPhysicalDeviceMemoryProperties(InDevice->GetPhysicalHandle(), &MemoryProperties);

		uint64 HostHeapSize = 0;
		PrimaryHostHeap = -1; // Primary

		for(uint32 i = 0; i < MemoryProperties.memoryHeapCount; ++i)
		{
			if(0 != (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT & MemoryProperties.memoryHeaps[i].flags))
			{
				if(MemoryProperties.memoryHeaps[i].size > HostHeapSize)
				{
					PrimaryHostHeap = i;
					HostHeapSize = MemoryProperties.memoryHeaps[i].size;
				}
			}
		}


		HeapInfos.AddDefaulted(MemoryProperties.memoryHeapCount);

		for (uint32 Index = 0; Index < MemoryProperties.memoryHeapCount; ++Index)
		{
			const bool bIsGPUHeap = ((MemoryProperties.memoryHeaps[Index].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) == VK_MEMORY_HEAP_DEVICE_LOCAL_BIT);
			if (bIsGPUHeap)
			{
				// Target using 95% of our budget to account for some fragmentation.
				HeapInfos[Index].TotalSize = (uint64)((float)HeapInfos[Index].TotalSize * 0.95f);
			}
		}


		SetupAndPrintMemInfo();
	}

	static FString GetMemoryPropertyFlagsString(VkMemoryPropertyFlags Flags)
	{
		FString String;
		if ((Flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
		{
			String += TEXT(" Local");
		}
		else
		{
			String += TEXT("      ");
		}

		if ((Flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
		{
			String += TEXT(" HostVisible");
		}
		else
		{
			String += TEXT("            ");
		}

		if ((Flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
		{
			String += TEXT(" HostCoherent");
		}
		else
		{
			String += TEXT("             ");
		}

		if ((Flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) == VK_MEMORY_PROPERTY_HOST_CACHED_BIT)
		{
			String += TEXT(" HostCached");
		}
		else
		{
			String += TEXT("           ");
		}

		if ((Flags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) == VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT)
		{
			String += TEXT(" Lazy");
		}
		else
		{
			String += TEXT("     ");
		}
		return String;
	};

	bool MetaTypeCanEvict(EVulkanAllocationMetaType MetaType)
	{
		switch(MetaType)
		{
			case EVulkanAllocationMetaImageOther: return true;
			default: return false;
		}
	}



	void FDeviceMemoryManager::SetupAndPrintMemInfo()
	{
		const uint32 MaxAllocations = Device->GetLimits().maxMemoryAllocationCount;
		VULKAN_LOGMEMORY(TEXT("%d Device Memory Heaps; Max memory allocations %d"), MemoryProperties.memoryHeapCount, MaxAllocations);
		for (uint32 Index = 0; Index < MemoryProperties.memoryHeapCount; ++Index)
		{
			bool bIsGPUHeap = ((MemoryProperties.memoryHeaps[Index].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) == VK_MEMORY_HEAP_DEVICE_LOCAL_BIT);
			VULKAN_LOGMEMORY(TEXT("%2d: Flags 0x%x Size %llu (%.2f MB) %s"),
				Index,
				MemoryProperties.memoryHeaps[Index].flags,
				MemoryProperties.memoryHeaps[Index].size,
				(float)((double)MemoryProperties.memoryHeaps[Index].size / 1024.0 / 1024.0),
				bIsGPUHeap ? TEXT("GPU") : TEXT(""));
			HeapInfos[Index].TotalSize = MemoryProperties.memoryHeaps[Index].size;
		}

		bHasUnifiedMemory = FVulkanPlatform::HasUnifiedMemory();
		VULKAN_LOGMEMORY(TEXT("%d Device Memory Types (%sunified)"), MemoryProperties.memoryTypeCount, bHasUnifiedMemory ? TEXT("") : TEXT("Not "));
		for (uint32 HeapIndex = 0; HeapIndex < MemoryProperties.memoryHeapCount; ++HeapIndex)
		{
			for (uint32 Index = 0; Index < MemoryProperties.memoryTypeCount; ++Index)
			{
				if(HeapIndex == MemoryProperties.memoryTypes[Index].heapIndex)
				{
					VULKAN_LOGMEMORY(TEXT("%2d: Flags 0x%05x Heap %2d %s"),
						Index,
						MemoryProperties.memoryTypes[Index].propertyFlags,
						MemoryProperties.memoryTypes[Index].heapIndex,
						*GetMemoryPropertyFlagsString(MemoryProperties.memoryTypes[Index].propertyFlags));
				}
			}
		}
		uint64 HostAllocated, HostLimit;
		GetHostMemoryStatus(&HostAllocated, &HostLimit);
		double AllocatedPercentage = 100.0 * HostAllocated / HostLimit;
		VULKAN_LOGMEMORY(TEXT("Host Allocation Percentage %6.2f%% -      %8.2fMB / %8.3fMB"), AllocatedPercentage, HostAllocated / (1024.f * 1024.f), HostLimit / (1024.f * 1024.f));



	}

	uint32 FDeviceMemoryManager::GetEvictedMemoryProperties()
	{
		if (Device->GetVendorId() == EGpuVendorId::Amd)
		{
			return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
		}
		else if (Device->GetVendorId() == EGpuVendorId::Nvidia)
		{
			return 0;
		}
		else
		{
			return 0;
		}
	}


	void FDeviceMemoryManager::Deinit()
	{
		for (int32 Index = 0; Index < HeapInfos.Num(); ++Index)
		{
			if (HeapInfos[Index].Allocations.Num())
			{
				UE_LOG(LogVulkanRHI, Warning, TEXT("Found %d unfreed allocations!"), HeapInfos[Index].Allocations.Num());
				DumpMemory();
			}
		}
		NumAllocations = 0;
	}

	bool FDeviceMemoryManager::SupportsMemoryType(VkMemoryPropertyFlags Properties) const
	{
		for (uint32 Index = 0; Index < MemoryProperties.memoryTypeCount; ++Index)
		{
			if (MemoryProperties.memoryTypes[Index].propertyFlags == Properties)
			{
				return true;
			}
		}
		return false;
	}
	void FDeviceMemoryManager::GetHostMemoryStatus(uint64* Allocated, uint64* Total) const
	{
		if(PrimaryHostHeap < 0)
		{
			*Allocated = 0;
			*Total = 1;
		}
		else
		{
			*Allocated = HeapInfos[PrimaryHostHeap].UsedSize;
			check(HeapInfos[PrimaryHostHeap].TotalSize == MemoryProperties.memoryHeaps[PrimaryHostHeap].size);
			*Total = GetBaseHeapSize(PrimaryHostHeap);
		}
	}


	bool FDeviceMemoryManager::IsHostMemory(uint32 MemoryTypeIndex) const
	{
		return 0 != (MemoryProperties.memoryTypes[MemoryTypeIndex].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	}

	FDeviceMemoryAllocation* FDeviceMemoryManager::Alloc(bool bCanFail, VkDeviceSize AllocationSize, uint32 MemoryTypeBits, VkMemoryPropertyFlags MemoryPropertyFlags, void* DedicatedAllocateInfo, float Priority, const char* File, uint32 Line)
	{
		uint32 MemoryTypeIndex = ~0;
		VERIFYVULKANRESULT(this->GetMemoryTypeFromProperties(MemoryTypeBits, MemoryPropertyFlags, &MemoryTypeIndex));
		return Alloc(bCanFail, AllocationSize, MemoryTypeIndex, DedicatedAllocateInfo, Priority, File, Line);
	}

	FDeviceMemoryAllocation* FDeviceMemoryManager::Alloc(bool bCanFail, VkDeviceSize AllocationSize, uint32 MemoryTypeIndex, void* DedicatedAllocateInfo, float Priority, const char* File, uint32 Line)
	{
		SCOPED_NAMED_EVENT(FDeviceMemoryManager_Alloc, FColor::Cyan);
		FScopeLock Lock(&GDeviceMemLock);

		check(AllocationSize > 0);
		check(MemoryTypeIndex < MemoryProperties.memoryTypeCount);

		VkMemoryAllocateInfo Info;
		ZeroVulkanStruct(Info, VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
		Info.allocationSize = AllocationSize;
		Info.memoryTypeIndex = MemoryTypeIndex;


#if VULKAN_SUPPORTS_MEMORY_PRIORITY
		VkMemoryPriorityAllocateInfoEXT Prio;
		ZeroVulkanStruct(Prio, VK_STRUCTURE_TYPE_MEMORY_PRIORITY_ALLOCATE_INFO_EXT);
		Prio.priority = Priority;
		if (Device->GetOptionalExtensions().HasMemoryPriority)
		{
			Info.pNext = &Prio;
		}
#endif

#if VULKAN_SUPPORTS_DEDICATED_ALLOCATION
		if (DedicatedAllocateInfo)
		{
			((VkMemoryDedicatedAllocateInfoKHR*)DedicatedAllocateInfo)->pNext = Info.pNext;
			Info.pNext = DedicatedAllocateInfo;
			INC_DWORD_STAT_BY(STAT_VulkanDedicatedMemory, AllocationSize);
		}
#endif
		VkDeviceMemory Handle;
		VkResult Result;

#if !UE_BUILD_SHIPPING
		if(	MemoryTypeIndex == PrimaryHostHeap && GVulkanFakeMemoryLimit && ((uint64)GVulkanFakeMemoryLimit << 20llu) < HeapInfos[PrimaryHostHeap].UsedSize )
		{
			Handle = 0;
			Result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
		}
		else
#endif
		{
			Result = VulkanRHI::vkAllocateMemory(DeviceHandle, &Info, VULKAN_CPU_ALLOCATOR, &Handle);
		}

		if (Result == VK_ERROR_OUT_OF_DEVICE_MEMORY || Result == VK_ERROR_OUT_OF_HOST_MEMORY)
		{
			if (bCanFail)
			{
				UE_LOG(LogVulkanRHI, Warning, TEXT("Failed to allocate Device Memory, Requested=%.2fKb MemTypeIndex=%d"), (float)Info.allocationSize / 1024.0f, Info.memoryTypeIndex);
				return nullptr;
			}
			const TCHAR* MemoryType = TEXT("?");
			switch (Result)
			{
			case VK_ERROR_OUT_OF_HOST_MEMORY: MemoryType = TEXT("Host"); break;
			case VK_ERROR_OUT_OF_DEVICE_MEMORY: MemoryType = TEXT("Local"); break;
			}
			DumpRenderTargetPoolMemory(*GLog);
			Device->GetMemoryManager().DumpMemory();
			GLog->PanicFlushThreadedLogs();

			UE_LOG(LogVulkanRHI, Fatal, TEXT("Out of %s Memory, Requested%.2fKB MemTypeIndex=%d\n"), MemoryType, AllocationSize, MemoryTypeIndex);
		}
		else
		{
			VERIFYVULKANRESULT(Result);
		}

		FDeviceMemoryAllocation* NewAllocation = new FDeviceMemoryAllocation;
		NewAllocation->DeviceHandle = DeviceHandle;
		NewAllocation->Handle = Handle;
		NewAllocation->Size = AllocationSize;
		NewAllocation->MemoryTypeIndex = MemoryTypeIndex;
		NewAllocation->bCanBeMapped = ((MemoryProperties.memoryTypes[MemoryTypeIndex].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		NewAllocation->bIsCoherent = ((MemoryProperties.memoryTypes[MemoryTypeIndex].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		NewAllocation->bIsCached = ((MemoryProperties.memoryTypes[MemoryTypeIndex].propertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) == VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
#if VULKAN_SUPPORTS_DEDICATED_ALLOCATION
		NewAllocation->bDedicatedMemory = DedicatedAllocateInfo != 0;
#else
		NewAllocation->bDedicatedMemory = 0;
#endif
		VULKAN_FILL_TRACK_INFO(NewAllocation->Track, File, Line);
		++NumAllocations;
		PeakNumAllocations = FMath::Max(NumAllocations, PeakNumAllocations);

		if (NumAllocations == Device->GetLimits().maxMemoryAllocationCount && !GVulkanSingleAllocationPerResource)
		{
			UE_LOG(LogVulkanRHI, Warning, TEXT("Hit Maximum # of allocations (%d) reported by device!"), NumAllocations);
		}

		uint32 HeapIndex = MemoryProperties.memoryTypes[MemoryTypeIndex].heapIndex;
		HeapInfos[HeapIndex].Allocations.Add(NewAllocation);
		HeapInfos[HeapIndex].UsedSize += AllocationSize;
		HeapInfos[HeapIndex].PeakSize = FMath::Max(HeapInfos[HeapIndex].PeakSize, HeapInfos[HeapIndex].UsedSize);

#if VULKAN_USE_LLM
		LLM_PLATFORM_SCOPE_VULKAN(ELLMTagVulkan::VulkanDriverMemoryGPU);
		LLM(FLowLevelMemTracker::Get().OnLowLevelAlloc(ELLMTracker::Platform, (void*)NewAllocation->Handle, AllocationSize, ELLMTag::GraphicsPlatform, ELLMAllocType::System));
		LLM_TRACK_VULKAN_SPARE_MEMORY_GPU((int64)AllocationSize);
#endif

		INC_DWORD_STAT(STAT_VulkanNumPhysicalMemAllocations);
		switch(MemoryTypeIndex)
		{
		case 0:  INC_DWORD_STAT_BY(STAT_VulkanMemory0, AllocationSize); break;
		case 1:  INC_DWORD_STAT_BY(STAT_VulkanMemory1, AllocationSize); break;
		case 2:  INC_DWORD_STAT_BY(STAT_VulkanMemory2, AllocationSize); break;
		default: INC_DWORD_STAT_BY(STAT_VulkanMemoryX, AllocationSize); break;
		}
		INC_DWORD_STAT_BY(STAT_VulkanMemoryTotal, AllocationSize);

		return NewAllocation;
	}

	void FDeviceMemoryManager::Free(FDeviceMemoryAllocation*& Allocation)
	{
		SCOPED_NAMED_EVENT(FDeviceMemoryManager_Free, FColor::Cyan);
		FScopeLock Lock(&GDeviceMemLock);

		check(Allocation);
		check(Allocation->Handle != VK_NULL_HANDLE);
		check(!Allocation->bFreedBySystem);
		if (Allocation->bDedicatedMemory)
		{
			DEC_DWORD_STAT_BY(STAT_VulkanDedicatedMemory, Allocation->Size);
		}
		switch (Allocation->MemoryTypeIndex)
		{
		case 0:  DEC_DWORD_STAT_BY(STAT_VulkanMemory0, Allocation->Size); break;
		case 1:  DEC_DWORD_STAT_BY(STAT_VulkanMemory1, Allocation->Size); break;
		case 2:  DEC_DWORD_STAT_BY(STAT_VulkanMemory2, Allocation->Size); break;
		default: DEC_DWORD_STAT_BY(STAT_VulkanMemoryX, Allocation->Size); break;
		}
		DEC_DWORD_STAT_BY(STAT_VulkanMemoryTotal, Allocation->Size);
		VulkanRHI::vkFreeMemory(DeviceHandle, Allocation->Handle, VULKAN_CPU_ALLOCATOR);

#if VULKAN_USE_LLM
		LLM(FLowLevelMemTracker::Get().OnLowLevelFree(ELLMTracker::Platform, (void*)Allocation->Handle, ELLMAllocType::System));
		LLM_TRACK_VULKAN_SPARE_MEMORY_GPU(-(int64)Allocation->Size);
#endif

		--NumAllocations;

		DEC_DWORD_STAT(STAT_VulkanNumPhysicalMemAllocations);

		uint32 HeapIndex = MemoryProperties.memoryTypes[Allocation->MemoryTypeIndex].heapIndex;

		HeapInfos[HeapIndex].UsedSize -= Allocation->Size;
		HeapInfos[HeapIndex].Allocations.RemoveSwap(Allocation);
		Allocation->bFreedBySystem = true;

		delete Allocation;
		Allocation = nullptr;
	}
	void FDeviceMemoryManager::GetMemoryDump(TArray<FResourceHeapStats>& OutDeviceHeapsStats)
	{
		OutDeviceHeapsStats.SetNum(0);
		for (int32 Index = 0; Index < HeapInfos.Num(); ++Index)
		{
			FResourceHeapStats Stat;
			Stat.MemoryFlags = 0;
			FHeapInfo& HeapInfo = HeapInfos[Index];
			Stat.TotalMemory = MemoryProperties.memoryHeaps[Index].size;
			for (uint32 TypeIndex = 0; TypeIndex < MemoryProperties.memoryTypeCount; ++TypeIndex)
			{
				if (MemoryProperties.memoryTypes[TypeIndex].heapIndex == Index)
				{
					Stat.MemoryFlags |= MemoryProperties.memoryTypes[TypeIndex].propertyFlags;
				}
			}

			for (int32 SubIndex = 0; SubIndex < HeapInfo.Allocations.Num(); ++SubIndex)
			{
				FDeviceMemoryAllocation* Allocation = HeapInfo.Allocations[SubIndex];
				Stat.BufferAllocations += 1;
				Stat.UsedBufferMemory += Allocation->Size;
				Stat.Pages += 1;
			}
			OutDeviceHeapsStats.Add(Stat);
		}
	}


	void FDeviceMemoryManager::DumpMemory()
	{
		VULKAN_LOGMEMORY(TEXT("/******************************************* Device Memory ********************************************\\"));
		SetupAndPrintMemInfo();
		VULKAN_LOGMEMORY(TEXT("Device Memory: %d allocations on %d heaps"), NumAllocations, HeapInfos.Num());
		for (int32 Index = 0; Index < HeapInfos.Num(); ++Index)
		{
			FHeapInfo& HeapInfo = HeapInfos[Index];
			VULKAN_LOGMEMORY(TEXT("\tHeap %d, %d allocations"), Index, HeapInfo.Allocations.Num());
			uint64 TotalSize = 0;

			if (HeapInfo.Allocations.Num() > 0)
			{
				VULKAN_LOGMEMORY(TEXT("\t\tAlloc AllocSize(MB) TotalSize(MB)    Handle"));
			}

			for (int32 SubIndex = 0; SubIndex < HeapInfo.Allocations.Num(); ++SubIndex)
			{
				FDeviceMemoryAllocation* Allocation = HeapInfo.Allocations[SubIndex];
				VULKAN_LOGMEMORY(TEXT("\t\t%5d %13.3f %13.3f %p"), SubIndex, Allocation->Size / 1024.f / 1024.f, TotalSize / 1024.0f / 1024.0f, (void*)Allocation->Handle);
				TotalSize += Allocation->Size;
			}
			VULKAN_LOGMEMORY(TEXT("\t\tTotal Allocated %.2f MB, Peak %.2f MB"), TotalSize / 1024.0f / 1024.0f, HeapInfo.PeakSize / 1024.0f / 1024.0f);
		}
#if VULKAN_OBJECT_TRACKING
		{

			TSortedMap<uint32, FVulkanMemoryBucket> AllocationBuckets;
			auto Collector = [&](const TCHAR* Name, FName ResourceName, void* Address, void* RHIRes, uint32 Width, uint32 Height, uint32 Depth, uint32 Format)
			{
				uint32 BytesPerPixel = (Format != VK_FORMAT_UNDEFINED ? GetNumBitsPerPixel((VkFormat)Format) : 8) / 8;
				uint32 Size = FPlatformMath::Max(Width,1u) * FPlatformMath::Max(Height,1u) * FPlatformMath::Max(Depth, 1u) * BytesPerPixel;
				uint32 Bucket = Size;
				if(Bucket >= (1<<20))
				{
					Bucket = (Bucket + ((1 << 20) - 1)) & ~((1 << 20)-1);
				}
				else
				{
					Bucket = (Bucket + ((1 << 10) - 1)) & ~((1 << 10)-1);
				}
				FVulkanMemoryAllocation Allocation =
				{
					Name, ResourceName, Address, RHIRes, Size, Width, Height, Depth, BytesPerPixel
				};
				FVulkanMemoryBucket& ActualBucket = AllocationBuckets.FindOrAdd(Bucket);
				ActualBucket.Allocations.Add(Allocation);
			};


			TVulkanTrackBase<FVulkanTextureBase>::CollectAll(Collector);
			TVulkanTrackBase<FVulkanResourceMultiBuffer>::CollectAll(Collector);
			for(auto& Itr : AllocationBuckets)
			{
				VULKAN_LOGMEMORY(TEXT("***** BUCKET < %d kb *****"), Itr.Key/1024);
				FVulkanMemoryBucket& B = Itr.Value;
				uint32 Size = 0;
				for (FVulkanMemoryAllocation& A : B.Allocations)
				{
					Size += A.Size;
				}
				VULKAN_LOGMEMORY(TEXT("\t\t%d / %d kb"), B.Allocations.Num(), Size / 1024);


				B.Allocations.Sort([](const FVulkanMemoryAllocation& L, const FVulkanMemoryAllocation& R)
				{
					return L.Address < R.Address;
				}
				);
				for(FVulkanMemoryAllocation& A : B.Allocations)
				{
					VULKAN_LOGMEMORY(TEXT("\t\t%p/%p %6.2fkb (%d) %5d/%5d/%5d %s ::: %s"), A.Address, A.RHIResouce, A.Size / 1024.f, A.Size, A.Width, A.Height, A.Depth, A.Name, *A.ResourceName.ToString());
				}
			}
		}
#endif

	}

	uint64 FDeviceMemoryManager::GetTotalMemory(bool bGPU) const
	{
		uint64 TotalMemory = 0;
		for (uint32 Index = 0; Index < MemoryProperties.memoryHeapCount; ++Index)
		{
			const bool bIsGPUHeap = ((MemoryProperties.memoryHeaps[Index].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) == VK_MEMORY_HEAP_DEVICE_LOCAL_BIT);

			if (bIsGPUHeap == bGPU)
			{
				TotalMemory += HeapInfos[Index].TotalSize;
			}
		}
		return TotalMemory;
	}

	FDeviceMemoryAllocation::~FDeviceMemoryAllocation()
	{
		checkf(bFreedBySystem, TEXT("Memory has to released calling FDeviceMemory::Free()!"));
	}

	void* FDeviceMemoryAllocation::Map(VkDeviceSize InSize, VkDeviceSize Offset)
	{
		check(bCanBeMapped);
		check(!MappedPointer);
		checkf(InSize == VK_WHOLE_SIZE || InSize + Offset <= Size, TEXT("Failed to Map %llu bytes, Offset %llu, AllocSize %llu bytes"), InSize, Offset, Size);

		VERIFYVULKANRESULT(VulkanRHI::vkMapMemory(DeviceHandle, Handle, Offset, InSize, 0, &MappedPointer));
		return MappedPointer;
	}

	void FDeviceMemoryAllocation::Unmap()
	{
		check(MappedPointer);
		VulkanRHI::vkUnmapMemory(DeviceHandle, Handle);
		MappedPointer = nullptr;
	}

	void FDeviceMemoryAllocation::FlushMappedMemory(VkDeviceSize InOffset, VkDeviceSize InSize)
	{
		if (!IsCoherent() || GForceCoherent != 0)
		{
			check(IsMapped());
			check(InOffset + InSize <= Size);
			VkMappedMemoryRange Range;
			ZeroVulkanStruct(Range, VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE);
			Range.memory = Handle;
			Range.offset = InOffset;
			Range.size = InSize;
			VERIFYVULKANRESULT(VulkanRHI::vkFlushMappedMemoryRanges(DeviceHandle, 1, &Range));
		}
	}

	void FDeviceMemoryAllocation::InvalidateMappedMemory(VkDeviceSize InOffset, VkDeviceSize InSize)
	{
		if (!IsCoherent() || GForceCoherent != 0)
		{
			check(IsMapped());
			check(InOffset + InSize <= Size);
			VkMappedMemoryRange Range;
			ZeroVulkanStruct(Range, VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE);
			Range.memory = Handle;
			Range.offset = InOffset;
			Range.size = InSize;
			VERIFYVULKANRESULT(VulkanRHI::vkInvalidateMappedMemoryRanges(DeviceHandle, 1, &Range));
		}
	}

	void FRange::JoinConsecutiveRanges(TArray<FRange>& Ranges)
	{
		if (Ranges.Num() > 1)
		{
#if !UE_VK_MEMORY_KEEP_FREELIST_SORTED
			Ranges.Sort();
#else
	#if UE_VK_MEMORY_KEEP_FREELIST_SORTED_CATCHBUGS
			SanityCheck(Ranges);
	#endif
#endif

#if !UE_VK_MEMORY_JOIN_FREELIST_ON_THE_FLY
			for (int32 Index = Ranges.Num() - 1; Index > 0; --Index)
			{
				FRange& Current = Ranges[Index];
				FRange& Prev = Ranges[Index - 1];
				if (Prev.Offset + Prev.Size == Current.Offset)
				{
					Prev.Size += Current.Size;
					Ranges.RemoveAt(Index, 1, false);
				}
			}
#endif
		}
	}

	int32 FRange::InsertAndTryToMerge(TArray<FRange>& Ranges, const FRange& Item, int32 ProposedIndex)
	{
#if !UE_VK_MEMORY_JOIN_FREELIST_ON_THE_FLY
		int32 Ret = Ranges.Insert(Item, ProposedIndex);
#else
		// there are four cases here
		// 1) nothing can be merged (distinct ranges)		 XXXX YYY ZZZZZ  =>   XXXX YYY ZZZZZ
		// 2) new range can be merged with the previous one: XXXXYYY  ZZZZZ  =>   XXXXXXX  ZZZZZ
		// 3) new range can be merged with the next one:     XXXX  YYYZZZZZ  =>   XXXX  ZZZZZZZZ
		// 4) new range perfectly fills the gap:             XXXXYYYYYZZZZZ  =>   XXXXXXXXXXXXXX

		// note: we can have a case where we're inserting at the beginning of the array (no previous element), but we won't have a case
		// where we're inserting at the end (no next element) - AppendAndTryToMerge() should be called instead
		checkf(Item.Offset < Ranges[ProposedIndex].Offset, TEXT("FRange::InsertAndTryToMerge() was called to append an element - internal logic error, FRange::AppendAndTryToMerge() should have been called instead."))
		int32 Ret = ProposedIndex;
		if (UNLIKELY(ProposedIndex == 0))
		{
			// only cases 1 and 3 apply
			FRange& NextRange = Ranges[Ret];

			if (UNLIKELY(NextRange.Offset == Item.Offset + Item.Size))
			{
				NextRange.Offset = Item.Offset;
				NextRange.Size += Item.Size;
			}
			else
			{
				Ret = Ranges.Insert(Item, ProposedIndex);
			}
		}
		else
		{
			// all cases apply
			FRange& NextRange = Ranges[ProposedIndex];
			FRange& PrevRange = Ranges[ProposedIndex - 1];

			// see if we can merge with previous
			if (UNLIKELY(PrevRange.Offset + PrevRange.Size == Item.Offset))
			{
				// case 2, can still end up being case 4
				PrevRange.Size += Item.Size;

				if (UNLIKELY(PrevRange.Offset + PrevRange.Size == NextRange.Offset))
				{
					// case 4
					PrevRange.Size += NextRange.Size;
					Ranges.RemoveAt(ProposedIndex);
					Ret = ProposedIndex - 1;
				}
			}
			else if (UNLIKELY(Item.Offset + Item.Size == NextRange.Offset))
			{
				// case 3
				NextRange.Offset = Item.Offset;
				NextRange.Size += Item.Size;
			}
			else
			{
				// case 1 - the new range is disjoint with both
				Ret = Ranges.Insert(Item, ProposedIndex);	// this can invalidate NextRange/PrevRange references, don't touch them after this
			}
		}
#endif

#if UE_VK_MEMORY_KEEP_FREELIST_SORTED_CATCHBUGS
		SanityCheck(Ranges);
#endif
		return Ret;
	}

	int32 FRange::AppendAndTryToMerge(TArray<FRange>& Ranges, const FRange& Item)
	{
#if !UE_VK_MEMORY_JOIN_FREELIST_ON_THE_FLY
		int32 Ret = Ranges.Add(Item);
#else
		int32 Ret = Ranges.Num() - 1;
		// we only get here when we have an element in front of us
		checkf(Ret >= 0, TEXT("FRange::AppendAndTryToMerge() was called on an empty array."));
		FRange& PrevRange = Ranges[Ret];
		if (UNLIKELY(PrevRange.Offset + PrevRange.Size == Item.Offset))
		{
			PrevRange.Size += Item.Size;
		}
		else
		{
			Ret = Ranges.Add(Item);
		}
#endif

#if UE_VK_MEMORY_KEEP_FREELIST_SORTED_CATCHBUGS
		SanityCheck(Ranges);
#endif
		return Ret;
	}

	void FRange::AllocateFromEntry(TArray<FRange>& Ranges, int32 Index, uint32 SizeToAllocate)
	{
		FRange& Entry = Ranges[Index];
		if (SizeToAllocate < Entry.Size)
		{
			// Modify current free entry in-place.
			Entry.Size -= SizeToAllocate;
			Entry.Offset += SizeToAllocate;
		}
		else
		{
			// Remove this free entry.
			Ranges.RemoveAt(Index, 1, false);
#if UE_VK_MEMORY_KEEP_FREELIST_SORTED_CATCHBUGS
			SanityCheck(Ranges);
#endif
		}
	}

	void FRange::SanityCheck(TArray<FRange>& Ranges)
	{
		if (UE_VK_MEMORY_KEEP_FREELIST_SORTED_CATCHBUGS)	// keeping the check code visible to the compiler
		{
			int32 Num = Ranges.Num();
			if (Num > 1)
			{
				for (int32 ChkIndex = 0; ChkIndex < Num - 1; ++ChkIndex)
				{
					checkf(Ranges[ChkIndex].Offset < Ranges[ChkIndex + 1].Offset, TEXT("Array is not sorted!"));
					// if we're joining on the fly, then there cannot be any adjoining ranges, so use < instead of <=
#if UE_VK_MEMORY_JOIN_FREELIST_ON_THE_FLY
					checkf(Ranges[ChkIndex].Offset + Ranges[ChkIndex].Size < Ranges[ChkIndex + 1].Offset, TEXT("Ranges are overlapping or adjoining!"));
#else
					checkf(Ranges[ChkIndex].Offset + Ranges[ChkIndex].Size <= Ranges[ChkIndex + 1].Offset, TEXT("Ranges are overlapping!"));
#endif
				}
			}
		}
	}


	int32 FRange::Add(TArray<FRange>& Ranges, const FRange & Item)
	{
#if UE_VK_MEMORY_KEEP_FREELIST_SORTED
		// find the right place to add
		int32 NumRanges = Ranges.Num();
		if (LIKELY(NumRanges <= 0))
		{
			return Ranges.Add(Item);
		}

		FRange* Data = Ranges.GetData();
		for (int32 Index = 0; Index < NumRanges; ++Index)
		{
			if (UNLIKELY(Data[Index].Offset > Item.Offset))
			{
				return InsertAndTryToMerge(Ranges, Item, Index);
			}
		}

		// if we got this far and still haven't inserted, we're a new element
		return AppendAndTryToMerge(Ranges, Item);
#else
		return Ranges.Add(Item);
#endif
	}

	VkDeviceSize FDeviceMemoryManager::GetBaseHeapSize(uint32 HeapIndex) const
	{
		VkDeviceSize HeapSize = MemoryProperties.memoryHeaps[HeapIndex].size;
#if !UE_BUILD_SHIPPING
		if(GVulkanFakeMemoryLimit && PrimaryHostHeap == HeapIndex)
		{
			HeapSize = FMath::Min<VkDeviceSize>((uint64)GVulkanFakeMemoryLimit << 20llu, HeapSize);
		}
#endif
		return HeapSize;
	}


	//Please keep -all- logic related to selecting the Page size in this function.
	uint32 FDeviceMemoryManager::GetDefaultPageSize(uint32 HeapIndex, EType Type, uint32 OverridePageSize)
	{
		VkDeviceSize HeapSize = GetBaseHeapSize(HeapIndex);
		VkDeviceSize PageSize = FMath::Min<VkDeviceSize>(HeapSize / 8, GPU_ONLY_HEAP_PAGE_SIZE);
#if PLATFORM_ANDROID && !PLATFORM_LUMIN
		PageSize = FMath::Min<VkDeviceSize>(PageSize, ANDROID_MAX_HEAP_PAGE_SIZE);
#endif
		if(OverridePageSize > 0)
		{
			PageSize = VkDeviceSize(OverridePageSize);
		}

		VkDeviceSize TargetDefaultSizeImage = ANDROID_MAX_HEAP_IMAGE_PAGE_SIZE;
		VkDeviceSize TargetPageSizeForBuffer = ANDROID_MAX_HEAP_BUFFER_PAGE_SIZE;
		VkDeviceSize DefaultPageSizeForImage = FMath::Min(TargetDefaultSizeImage, PageSize);
		VkDeviceSize DefaultPageSizeForBuffer = FMath::Min(TargetPageSizeForBuffer, PageSize);
		VkDeviceSize TargetDefaultPageSize = GVulkanFreePageForType == 0 ? PageSize : ((Type == EType::Image) ? DefaultPageSizeForImage : DefaultPageSizeForBuffer);
		return (uint32)TargetDefaultPageSize;
	}

	uint32 FDeviceMemoryManager::GetHeapIndex(uint32 MemoryTypeIndex)
	{
		return MemoryProperties.memoryTypes[MemoryTypeIndex].heapIndex;
	}




	FVulkanResourceHeap::FVulkanResourceHeap(FMemoryManager* InOwner, uint32 InMemoryTypeIndex, uint32 InOverridePageSize)
		: Owner(InOwner)
		, MemoryTypeIndex((uint16)InMemoryTypeIndex)
		, HeapIndex((uint16)InOwner->GetParent()->GetDeviceMemoryManager().GetHeapIndex(InMemoryTypeIndex))
		, bIsHostCachedSupported(false)
		, bIsLazilyAllocatedSupported(false)
		, OverridePageSize(InOverridePageSize)
		, PeakPageSize(0)
		, UsedMemory(0)
		, PageIDCounter(0)
	{
	}

	FVulkanResourceHeap::~FVulkanResourceHeap()
	{
		ReleaseFreedPages(true);
		auto DeletePages = [&](TArray<FVulkanSubresourceAllocator*>& UsedPages, const TCHAR* Name)
		{
			bool bLeak = false;
			for (int32 Index = UsedPages.Num() - 1; Index >= 0; --Index)
			{
				FVulkanSubresourceAllocator* Page = UsedPages[Index];
				bLeak |= !Page->JoinFreeBlocks();
				Owner->GetParent()->GetDeviceMemoryManager().Free(Page->MemoryAllocation);
				delete Page;
			}
			UsedPages.Reset(0);
			return bLeak;
		};
		bool bDump = false;
		bDump = bDump || DeletePages(UsedBufferPages, TEXT("Buffer"));
		bDump = bDump || DeletePages(UsedImagePages, TEXT("Image"));
		if (bDump)
		{
			Owner->GetParent()->GetMemoryManager().DumpMemory();
			GLog->Flush();
		}
		for (int32 Index = 0; Index < FreeImagePages.Num(); ++Index)
		{
			FVulkanSubresourceAllocator* Page = FreeImagePages[Index];
			Owner->GetParent()->GetDeviceMemoryManager().Free(Page->MemoryAllocation);
			delete Page;
		}
		for (int32 Index = 0; Index < FreePages.Num(); ++Index)
		{
			FVulkanSubresourceAllocator* Page = FreePages[Index];
			Owner->GetParent()->GetDeviceMemoryManager().Free(Page->MemoryAllocation);
			delete Page;
		}
	}
	void FVulkanResourceHeap::ReleasePage(FVulkanSubresourceAllocator* InPage)
	{
		Owner->UnregisterSubresourceAllocator(InPage);
		Owner->GetParent()->GetDeviceMemoryManager().Free(InPage->MemoryAllocation);
		UsedMemory -= InPage->MaxSize;
		delete InPage;

	}

	void FVulkanResourceHeap::FreePage(FVulkanSubresourceAllocator* InPage)
	{
		FScopeLock ScopeLock(&GResourceLock);
		check(InPage->JoinFreeBlocks());
		int32 Index = -1;

		InPage->FrameFreed = GFrameNumberRenderThread;
		switch(InPage->GetType())
		{
			case EVulkanAllocationBuffer:
			{
				if(UsedBufferPages.Find(InPage, Index))
				{
					UsedBufferPages.RemoveAtSwap(Index, 1, false);
				}
				else
				{
					checkNoEntry();
				}
				check(!UsedImagePages.Find(InPage, Index));

				FreePages.Add(InPage);
			}
			break;
			case EVulkanAllocationImage:
			{
				if (UsedImagePages.Find(InPage, Index))
				{
					UsedImagePages.RemoveAtSwap(Index, 1, false);
				}
				else
				{
					checkNoEntry();
				}
				check(!UsedBufferPages.Find(InPage, Index));
				if(InPage->bIsEvicting)
				{
					ReleasePage(InPage);
				}
				else
				{
					if(GVulkanFreePageForType)
					{
						FreeImagePages.Add(InPage);
					}
					else
					{
						FreePages.Add(InPage);
					}
				}
			}
			break;
			case EVulkanAllocationImageDedicated:
			{
				if (UsedDedicatedImagePages.Find(InPage, Index))
				{
					UsedDedicatedImagePages.RemoveAtSwap(Index, 1, false);
				}
				else
				{
					checkNoEntry();
				}
				ReleasePage(InPage);
			}
			break;

			case EVulkanAllocationPooledBuffer:
				checkNoEntry();
			case EVulkanAllocationEmpty:
				checkNoEntry();
			default:
				checkNoEntry();
		}
	}

	void FVulkanResourceHeap::ReleaseFreedPages(bool bImmediately)
	{
		TArray<FVulkanSubresourceAllocator*> PageToReleases; //gollummsess pagesses
		{
			FScopeLock ScopeLock(&GResourceLock);
			for (int32 Index = (bImmediately ? 0 : 1); Index < FreePages.Num(); ++Index)
			{
				FVulkanSubresourceAllocator* Page = FreePages[Index];
				if (bImmediately || Page->FrameFreed + NUM_FRAMES_TO_WAIT_BEFORE_RELEASING_TO_OS < GFrameNumberRenderThread)
				{
					PageToReleases.Add(Page);
					FreePages.RemoveAtSwap(Index, 1, false);
					break;
				}
			}
			for (int32 Index = (bImmediately ? 0 : 1); Index < FreeImagePages.Num(); ++Index)
			{
				FVulkanSubresourceAllocator* Page = FreeImagePages[Index];
				if (bImmediately || Page->FrameFreed + NUM_FRAMES_TO_WAIT_BEFORE_RELEASING_TO_OS < GFrameNumberRenderThread)
				{
					PageToReleases.Add(Page);
					FreeImagePages.RemoveAtSwap(Index, 1, false);
					break;
				}
			}
		}

		for (int32 n = 0; n < PageToReleases.Num(); ++n )
		{
			ReleasePage(PageToReleases[n]);
		}
	}
	uint64 FVulkanResourceHeap::EvictOne(FVulkanDevice& Device)
	{
		for (int32 Index = 0; Index < UsedImagePages.Num(); ++Index)
		{
			FVulkanSubresourceAllocator* Allocator = UsedImagePages[Index];
			if(!Allocator->bIsEvicting && Allocator->GetSubresourceAllocatorFlags() & VulkanAllocationFlagsCanEvict)
			{
				return Allocator->EvictToHost(Device);
			}
		}
		return 0;
	}

	void FVulkanResourceHeap::DumpMemory(FResourceHeapStats& Stats)
	{
		auto DumpPages = [&](TArray<FVulkanSubresourceAllocator*>& UsedPages, const TCHAR* TypeName, bool bIsImage)
		{
			uint64 SubAllocUsedMemory = 0;
			uint64 SubAllocAllocatedMemory = 0;
			uint32 NumSuballocations = 0;
			for (int32 Index = 0; Index < UsedPages.Num(); ++Index)
			{
				Stats.Pages++;
				Stats.TotalMemory +=UsedPages[Index]->MaxSize;
				if(bIsImage)
				{
					Stats.UsedImageMemory += UsedPages[Index]->UsedSize;
					Stats.ImageAllocations += UsedPages[Index]->NumSubAllocations;
					Stats.ImagePages++;
				}
				else
				{
					Stats.UsedBufferMemory += UsedPages[Index]->UsedSize;
					Stats.BufferAllocations += UsedPages[Index]->NumSubAllocations;
					Stats.BufferPages++;
				}

				SubAllocUsedMemory += UsedPages[Index]->UsedSize;
				SubAllocAllocatedMemory += UsedPages[Index]->MaxSize;
				NumSuballocations += UsedPages[Index]->NumSubAllocations;

				VULKAN_LOGMEMORY(TEXT("\t\t%s%d:(%6.2fmb/%6.2fmb) ID %4d %4d suballocs, %4d free chunks,DeviceMemory %p"),
					TypeName,
					Index,
					UsedPages[Index]->UsedSize / (1024.f*1024.f),
					UsedPages[Index]->MaxSize / (1024.f*1024.f),
					UsedPages[Index]->GetHandleId(),
					UsedPages[Index]->NumSubAllocations,
					UsedPages[Index]->FreeList.Num(),
					(void*)UsedPages[Index]->MemoryAllocation->GetHandle());
			}
		};

		if (GVulkanFreePageForType)
		{
			DumpPages(FreePages, TEXT("FreeBuffer"), false);
			DumpPages(FreeImagePages, TEXT("FreeImage "), true);
		}
		else
		{
			DumpPages(FreePages, TEXT("Free      "), false);
		}
		DumpPages(UsedBufferPages, TEXT("Buffer    "), false);
		DumpPages(UsedImagePages, TEXT("Image     "), true);
	}

	bool FVulkanResourceHeap::AllocateResource(FVulkanAllocation& OutAllocation, void* AllocationOwner, EType Type, uint32 Size, uint32 Alignment, bool bMapAllocation, bool bForceSeparateAllocation, EVulkanAllocationMetaType MetaType, const char* File, uint32 Line)
	{
		SCOPED_NAMED_EVENT(FResourceHeap_AllocateResource, FColor::Cyan);
		FScopeLock ScopeLock(&GResourceLock);

		FDeviceMemoryManager& DeviceMemoryManager = Owner->GetParent()->GetDeviceMemoryManager();
		uint32 PageSize = DeviceMemoryManager.GetDefaultPageSize(HeapIndex, Type, OverridePageSize);
		bool bHasUnifiedMemory = DeviceMemoryManager.HasUnifiedMemory();
		TArray<FVulkanSubresourceAllocator*>& UsedPages = (Type == EType::Image) ? UsedImagePages : UsedBufferPages;
		EVulkanAllocationType AllocationType = (Type == EType::Image) ? EVulkanAllocationImage : EVulkanAllocationBuffer;
		uint8 AllocationFlags = (!bHasUnifiedMemory && MetaTypeCanEvict(MetaType)) ? VulkanAllocationFlagsCanEvict : 0;
		if(bMapAllocation)
		{
			AllocationFlags |= VulkanAllocationFlagsMapped;
		}

		uint32 AllocationSize;

		if(GVulkanSingleAllocationPerResource)
		{
			AllocationSize =  Size;
		}
		else
		{
			if (!bForceSeparateAllocation)
			{
				if (Size < PageSize)
				{
					// Check Used pages to see if we can fit this in
					for (int32 Index = 0; Index < UsedPages.Num(); ++Index)
					{
						FVulkanSubresourceAllocator* Page = UsedPages[Index];
						if(Page->GetSubresourceAllocatorFlags() == AllocationFlags)
						{
							check(Page->MemoryAllocation->IsMapped() == bMapAllocation);
							if(Page->TryAllocate2(OutAllocation, AllocationOwner, Size, Alignment, MetaType, File, Line))
							{
								IncMetaStats(MetaType, OutAllocation.Size);
								return true;
							}
						}
					}
				}
				{
					TArray<FVulkanSubresourceAllocator*>& Pages = (Type == EType::Image && GVulkanFreePageForType) ? FreeImagePages : FreePages;
					for (int32 Index = 0; Index < Pages.Num(); ++Index)
					{
						FVulkanSubresourceAllocator* Page = Pages[Index];
						if (Page->GetSubresourceAllocatorFlags() == AllocationFlags)
						{
							check(Page->MemoryAllocation->IsMapped() == bMapAllocation);
							if(Page->TryAllocate2(OutAllocation, AllocationOwner, Size, Alignment, MetaType, File, Line))
							{
								IncMetaStats(MetaType, OutAllocation.Size);
								FreePages.RemoveSingleSwap(Page, false);
								UsedPages.Add(Page);
								return true;
							}
						}
					}
				}

				constexpr bool bUseMaxSubAllocation = UE_VK_MEMORY_MAX_SUB_ALLOCATION > 0;

				if (bUseMaxSubAllocation && Size >= UE_VK_MEMORY_MAX_SUB_ALLOCATION)
				{
					AllocationSize = Size;
				}
				else
				{
					AllocationSize = FMath::Max(Size, PageSize);
				}
			}
			else
			{
				// We get here when bForceSeparateAllocation is true, which is used for lazy allocations, since pooling those doesn't make sense.
				AllocationSize = Size;
			}
		}

		FDeviceMemoryAllocation* DeviceMemoryAllocation = DeviceMemoryManager.Alloc(true, AllocationSize, MemoryTypeIndex, nullptr, VULKAN_MEMORY_HIGHEST_PRIORITY, File, Line);
		if (!DeviceMemoryAllocation && Size != AllocationSize)
		{
			// Retry with a smaller size
			DeviceMemoryAllocation = DeviceMemoryManager.Alloc(false, Size, MemoryTypeIndex, nullptr, VULKAN_MEMORY_HIGHEST_PRIORITY, File, Line);
			if (!DeviceMemoryAllocation)
			{
				UE_LOG(LogVulkanRHI, Fatal, TEXT("Out of memory on Vulkan; MemoryTypeIndex=%d, AllocSize=%0.3fMB"), MemoryTypeIndex, (float)AllocationSize / 1048576.0f);
			}
		}
		if (bMapAllocation)
		{
			DeviceMemoryAllocation->Map(AllocationSize, 0);
		}

		++PageIDCounter;
		FVulkanSubresourceAllocator* Page = new FVulkanSubresourceAllocator(AllocationType, Owner, AllocationFlags, DeviceMemoryAllocation, MemoryTypeIndex, 0);
		Owner->RegisterSubresourceAllocator(Page);

		UsedPages.Add(Page);

		UsedMemory += AllocationSize;

		PeakPageSize = FMath::Max(PeakPageSize, AllocationSize);


		bool bOk = Page->TryAllocate2(OutAllocation, AllocationOwner, Size, Alignment, MetaType, File, Line);
		if(bOk)
		{
			IncMetaStats(MetaType, OutAllocation.Size);
		}
		return bOk;
	}

	bool FVulkanResourceHeap::AllocateDedicatedImage(FVulkanAllocation& OutAllocation, void* AllocationOwner, VkImage Image, uint32 Size, uint32 Alignment, EVulkanAllocationMetaType MetaType, const char* File, uint32 Line)
	{
#if VULKAN_SUPPORTS_DEDICATED_ALLOCATION
		FScopeLock ScopeLock(&GResourceLock);

		uint32 AllocationSize = Size;

		check(Image != VK_NULL_HANDLE);
		VkMemoryDedicatedAllocateInfoKHR DedicatedAllocInfo;
		ZeroVulkanStruct(DedicatedAllocInfo, VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR);
		DedicatedAllocInfo.image = Image;
		FDeviceMemoryAllocation* DeviceMemoryAllocation = Owner->GetParent()->GetDeviceMemoryManager().Alloc(false, AllocationSize, MemoryTypeIndex, &DedicatedAllocInfo, VULKAN_MEMORY_HIGHEST_PRIORITY, File, Line);

		++PageIDCounter;
		FVulkanSubresourceAllocator* NewPage = new FVulkanSubresourceAllocator(EVulkanAllocationImageDedicated, Owner, 0, DeviceMemoryAllocation, MemoryTypeIndex, PageIDCounter);
		Owner->RegisterSubresourceAllocator(NewPage);
		UsedDedicatedImagePages.Add(NewPage);

		UsedMemory += AllocationSize;

		PeakPageSize = FMath::Max(PeakPageSize, AllocationSize);
		return NewPage->TryAllocate2(OutAllocation, AllocationOwner, Size, Alignment, MetaType, File, Line);
#else
		checkNoEntry();
		return false;
#endif
	}

	FMemoryManager::FMemoryManager(FVulkanDevice* InDevice)
		: FDeviceChild(InDevice)
		, DeviceMemoryManager(&InDevice->GetDeviceMemoryManager())
		, AllBufferAllocationsFreeListHead(-1)
	{
	}

	FMemoryManager::~FMemoryManager()
	{
		Deinit();
	}

	void FMemoryManager::Init()
	{
		const uint32 TypeBits = (1 << DeviceMemoryManager->GetNumMemoryTypes()) - 1;

		const VkPhysicalDeviceMemoryProperties& MemoryProperties = DeviceMemoryManager->GetMemoryProperties();

		ResourceTypeHeaps.AddZeroed(MemoryProperties.memoryTypeCount);

		auto GetMemoryTypesFromProperties = [MemoryProperties](uint32 InTypeBits, VkMemoryPropertyFlags Properties, TArray<uint32>& OutTypeIndices)
		{
			// Search memtypes to find first index with those properties
			for (uint32 i = 0; i < MemoryProperties.memoryTypeCount && InTypeBits; i++)
			{
				if ((InTypeBits & 1) == 1)
				{
					// Type is available, does it match user properties?
					if ((MemoryProperties.memoryTypes[i].propertyFlags & Properties) == Properties)
					{
						OutTypeIndices.Add(i);
					}
				}
				InTypeBits >>= 1;
			}

			// No memory types matched, return failure
			return OutTypeIndices.Num() > 0;
		};

		// Setup main GPU heap
		{
			TArray<uint32> TypeIndices;
			GetMemoryTypesFromProperties(TypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, TypeIndices);
			check(TypeIndices.Num() > 0);

			for (uint32 Index = 0; Index < MemoryProperties.memoryTypeCount; ++Index)
			{
				int32 HeapIndex = MemoryProperties.memoryTypes[Index].heapIndex;
				VkDeviceSize HeapSize = MemoryProperties.memoryHeaps[HeapIndex].size;
				ResourceTypeHeaps[Index] = new FVulkanResourceHeap(this, Index);
				ResourceTypeHeaps[Index]->bIsHostCachedSupported = ((MemoryProperties.memoryTypes[Index].propertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) == VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
				ResourceTypeHeaps[Index]->bIsLazilyAllocatedSupported = ((MemoryProperties.memoryTypes[Index].propertyFlags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) == VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT);
			}
		}

		// Upload heap. Spec requires this combination to exist.
		{
			uint32 TypeIndex = 0;
			VERIFYVULKANRESULT(DeviceMemoryManager->GetMemoryTypeFromProperties(TypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &TypeIndex));
			uint64 HeapSize = MemoryProperties.memoryHeaps[MemoryProperties.memoryTypes[TypeIndex].heapIndex].size;
			ResourceTypeHeaps[TypeIndex] = new FVulkanResourceHeap(this, TypeIndex, STAGING_HEAP_PAGE_SIZE);
		}

		// Download heap. Optional type per the spec.
		{
			uint32 TypeIndex = 0;
			{
				uint32 HostVisCachedIndex = 0;
				VkResult HostCachedResult = DeviceMemoryManager->GetMemoryTypeFromProperties(TypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, &HostVisCachedIndex);
				uint32 HostVisIndex = 0;
				VkResult HostResult = DeviceMemoryManager->GetMemoryTypeFromProperties(TypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &HostVisIndex);
				if (HostCachedResult == VK_SUCCESS)
				{
					TypeIndex = HostVisCachedIndex;
				}
				else if (HostResult == VK_SUCCESS)
				{
					TypeIndex = HostVisIndex;
				}
				else
				{
					// Redundant as it would have asserted above...
					UE_LOG(LogVulkanRHI, Fatal, TEXT("No Memory Type found supporting VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT!"));
				}
			}
			uint64 HeapSize = MemoryProperties.memoryHeaps[MemoryProperties.memoryTypes[TypeIndex].heapIndex].size;
			ResourceTypeHeaps[TypeIndex] = new FVulkanResourceHeap(this, TypeIndex, STAGING_HEAP_PAGE_SIZE);
		}
	}

	void FMemoryManager::Deinit()
	{
		{
			ProcessPendingUBFreesNoLock(true);
			check(UBAllocations.PendingFree.Num() == 0);
		}
		DestroyResourceAllocations();

		for (int32 Index = 0; Index < ResourceTypeHeaps.Num(); ++Index)
		{
			delete ResourceTypeHeaps[Index];
			ResourceTypeHeaps[Index] = nullptr;
		}
		ResourceTypeHeaps.Empty(0);
	}

	void FMemoryManager::DestroyResourceAllocations()
	{
		ReleaseFreedResources(true);

		for (auto& UsedAllocations : UsedBufferAllocations)
		{
			for (int32 Index = UsedAllocations.Num() - 1; Index >= 0; --Index)
			{
				FVulkanSubresourceAllocator* BufferAllocation = UsedAllocations[Index];
				if (!BufferAllocation->JoinFreeBlocks())
				{
					UE_LOG(LogVulkanRHI, Warning, TEXT("Suballocation(s) for Buffer %p were not released.%s"), (void*)BufferAllocation->Buffer, *VULKAN_TRACK_STRING(BufferAllocation->Track));
				}

				BufferAllocation->Destroy(GetParent());
				GetParent()->GetDeviceMemoryManager().Free(BufferAllocation->MemoryAllocation);
				delete BufferAllocation;
			}
			UsedAllocations.Empty(0);
		}

		for (auto& FreeAllocations : FreeBufferAllocations)
		{
			for (int32 Index = 0; Index < FreeAllocations.Num(); ++Index)
			{
				FVulkanSubresourceAllocator* BufferAllocation = FreeAllocations[Index];
				BufferAllocation->Destroy(GetParent());
				GetParent()->GetDeviceMemoryManager().Free(BufferAllocation->MemoryAllocation);
				delete BufferAllocation;
			}
			FreeAllocations.Empty(0);
		}
	}

	void FMemoryManager::ReleaseFreedResources(bool bImmediately)
	{
		FVulkanSubresourceAllocator* BufferAllocationToRelease = nullptr;
		{
			FScopeLock ScopeLock(&GResourceHeapLock);
			for (auto& FreeAllocations : FreeBufferAllocations)
			{
				for (int32 Index = 0; Index < FreeAllocations.Num(); ++Index)
				{
					FVulkanSubresourceAllocator* BufferAllocation = FreeAllocations[Index];
					if (bImmediately || BufferAllocation->FrameFreed + NUM_FRAMES_TO_WAIT_BEFORE_RELEASING_TO_OS < GFrameNumberRenderThread)
					{
						BufferAllocationToRelease = BufferAllocation;
						FreeAllocations.RemoveAtSwap(Index, 1, false);
						break;
					}
				}
			}
		}

		if (BufferAllocationToRelease)
		{
			BufferAllocationToRelease->Destroy(GetParent());
			GetParent()->GetDeviceMemoryManager().Free(BufferAllocationToRelease->MemoryAllocation);
			delete BufferAllocationToRelease;
		}
	}

	void FMemoryManager::ReleaseFreedPages()
	{
		for(FVulkanResourceHeap* Heap : ResourceTypeHeaps)
		{
			if (Heap)
			{
				Heap->ReleaseFreedPages(false);
			}
		}
		ReleaseFreedResources(false);

		int32 PrimaryHostHeap = DeviceMemoryManager->PrimaryHostHeap;

		if((GVulkanEvictOnePage || UpdateEvictThreshold(true)) && PrimaryHostHeap >= 0)
		{
			GVulkanEvictOnePage = 0;
			FVulkanResourceHeap* Heap = ResourceTypeHeaps[PrimaryHostHeap];
			PendingEvictBytes += Heap->EvictOne(*Device);

		}

	}

	void FMemoryManager::FreeVulkanAllocationPooledBuffer(FVulkanAllocation& Allocation)
	{
		SCOPED_NAMED_EVENT(FResourceHeapManager_FreeVulkanAllocationPooledBuffer, FColor::Cyan);
		DecMetaStats(Allocation.MetaType, Allocation.Size);
		uint32 Index = Allocation.AllocatorIndex;
		AllBufferAllocations[Index]->Free(Allocation);
	}
	void FMemoryManager::FreeVulkanAllocationBuffer(FVulkanAllocation& Allocation)
	{
		SCOPED_NAMED_EVENT(FResourceHeapManager_FreeVulkanAllocationBuffer, FColor::Cyan);
		DecMetaStats(Allocation.MetaType, Allocation.Size);
		uint32 Index = Allocation.AllocatorIndex;
		AllBufferAllocations[Index]->Free(Allocation);
	}

	void FMemoryManager::FreeVulkanAllocationImage(FVulkanAllocation& Allocation)
	{
		SCOPED_NAMED_EVENT(FResourceHeapManager_FreeVulkanAllocationImage, FColor::Cyan);
		DecMetaStats(Allocation.MetaType, Allocation.Size);
		uint32 Index = Allocation.AllocatorIndex;
		AllBufferAllocations[Index]->Free(Allocation);
	}
	void FMemoryManager::FreeVulkanAllocationImageDedicated(FVulkanAllocation& Allocation)
	{
		SCOPED_NAMED_EVENT(FResourceHeapManager_FreeVulkanAllocationImageDedicated, FColor::Cyan);
		DecMetaStats(Allocation.MetaType, Allocation.Size);
		uint32 Index = Allocation.AllocatorIndex;
		AllBufferAllocations[Index]->Free(Allocation);
	}

	void FVulkanSubresourceAllocator::SetFreePending(FVulkanAllocation& Allocation)
	{
		check(Allocation.Type == Type);
		check(Allocation.AllocatorIndex == GetAllocatorIndex());
		{
			FScopeLock ScopeLock(&CS);
			FVulkanAllocationInternal& Data = InternalData[Allocation.AllocationIndex];
			Data.State = FVulkanAllocationInternal::EFREEPENDING;
		}
	}



	void FVulkanSubresourceAllocator::Free(FVulkanAllocation& Allocation)
	{
		check(Allocation.Type == Type);
		check(Allocation.AllocatorIndex == GetAllocatorIndex());

		{
			FScopeLock ScopeLock(&CS);
			FreeCalls++;
			uint32 AllocationOffset;
			uint32 AllocationSize;
			{
				FVulkanAllocationInternal& Data = InternalData[Allocation.AllocationIndex];
				AllocationOffset = Data.AllocationOffset;
				AllocationSize = Data.AllocationSize;
				MemoryUsed[Allocation.MetaType] -= AllocationSize;
				LLM_TRACK_VULKAN_HIGH_LEVEL_FREE(Data);
				LLM_TRACK_VULKAN_SPARE_MEMORY_GPU((int64)Allocation.Size);
				VULKAN_FREE_TRACK_INFO(Data.Track);
				Data.State = FVulkanAllocationInternal::EFREED;
				FreeInternalData(Allocation.AllocationIndex);
				Allocation.AllocationIndex = -1;
			}
			FRange NewFree;
			NewFree.Offset = AllocationOffset;
			NewFree.Size = AllocationSize;
			check(NewFree.Offset <= GetMaxSize());
			check(NewFree.Offset + NewFree.Size <= GetMaxSize());
			FRange::Add(FreeList, NewFree);
			UsedSize -= AllocationSize;
			NumSubAllocations--;
			check(UsedSize >= 0);
			if (JoinFreeBlocks())
			{
				FScopeLock ScopeLockResourceheap(&GResourceHeapLock);
				check(JoinFreeBlocks());
				Owner->ReleaseSubresourceAllocator(this);
			}
		}
	}

	void FMemoryManager::FreeVulkanAllocation(FVulkanAllocation& Allocation, EVulkanFreeFlags FreeFlags)
	{
		//by default, all allocations are implicitly deferred, unless manually handled.
		if(FreeFlags & EVulkanFreeFlag_DontDefer)
		{
			switch (Allocation.Type)
			{
			case EVulkanAllocationEmpty:
				break;
			case EVulkanAllocationPooledBuffer:
				FreeVulkanAllocationPooledBuffer(Allocation);
				break;
			case EVulkanAllocationBuffer:
				FreeVulkanAllocationBuffer(Allocation);
				break;
			case EVulkanAllocationImage:
				FreeVulkanAllocationImage(Allocation);
				break;
			case EVulkanAllocationImageDedicated:
				FreeVulkanAllocationImageDedicated(Allocation);
				break;
			}
			FMemory::Memzero(&Allocation, sizeof(Allocation));
			Allocation.Type = EVulkanAllocationEmpty;
		}
		else
		{

			uint32 Index = Allocation.AllocatorIndex;
			AllBufferAllocations[Index]->SetFreePending(Allocation);
			Device->GetDeferredDeletionQueue().EnqueueResourceAllocation(Allocation);
		}
		check(!Allocation.HasAllocation());
	}


	void FVulkanSubresourceAllocator::Destroy(FVulkanDevice* Device)
	{
		// Does not need to go in the deferred deletion queue
		if(Buffer != VK_NULL_HANDLE)
		{
			VulkanRHI::vkDestroyBuffer(Device->GetInstanceHandle(), Buffer, VULKAN_CPU_ALLOCATOR);
			Buffer = VK_NULL_HANDLE;
		}
	}

	FVulkanSubresourceAllocator::FVulkanSubresourceAllocator(EVulkanAllocationType InType, FMemoryManager* InOwner, uint8 InSubresourceAllocatorFlags, FDeviceMemoryAllocation* InDeviceMemoryAllocation,
		uint32 InMemoryTypeIndex, VkMemoryPropertyFlags InMemoryPropertyFlags,
		uint32 InAlignment, VkBuffer InBuffer, uint32 InBufferId, VkBufferUsageFlags InBufferUsageFlags, int32 InPoolSizeIndex)
		: Type(InType)
		, Owner(InOwner)
		, MemoryTypeIndex(InMemoryTypeIndex)
		, MemoryPropertyFlags(InMemoryPropertyFlags)
		, MemoryAllocation(InDeviceMemoryAllocation)
		, Alignment(InAlignment)
		, FrameFreed(0)
		, UsedSize(0)
		, BufferUsageFlags(InBufferUsageFlags)
		, Buffer(InBuffer)
		, BufferId(InBufferId)
		, PoolSizeIndex(InPoolSizeIndex)
		, AllocatorIndex(0xffffffff)
		, SubresourceAllocatorFlags(InSubresourceAllocatorFlags)

	{
		FMemory::Memzero(MemoryUsed);
		MaxSize = InDeviceMemoryAllocation->GetSize();

		if(InDeviceMemoryAllocation->IsMapped())
		{
			SubresourceAllocatorFlags |= VulkanAllocationFlagsMapped;
		}
		else
		{
			SubresourceAllocatorFlags &= ~VulkanAllocationFlagsMapped;
		}
		FRange FullRange;
		FullRange.Offset = 0;
		FullRange.Size = MaxSize;
		FreeList.Add(FullRange);
		VULKAN_FILL_TRACK_INFO(Track, __FILE__, __LINE__);
	}

	FVulkanSubresourceAllocator::FVulkanSubresourceAllocator(EVulkanAllocationType InType, FMemoryManager* InOwner, uint8 InSubresourceAllocatorFlags, FDeviceMemoryAllocation* InDeviceMemoryAllocation,
		uint32 InMemoryTypeIndex, uint32 BufferId)
		: Type(InType)
		, Owner(InOwner)
		, MemoryTypeIndex(InMemoryTypeIndex)
		, MemoryPropertyFlags(0)
		, MemoryAllocation(InDeviceMemoryAllocation)
		, Alignment(0)
		, FrameFreed(0)
		, UsedSize(0)
		, BufferUsageFlags(0)
		, Buffer(VK_NULL_HANDLE)
		, BufferId(BufferId)
		, PoolSizeIndex(0x7fffffff)
		, AllocatorIndex(0xffffffff)
		, SubresourceAllocatorFlags(InSubresourceAllocatorFlags)

	{
		FMemory::Memzero(MemoryUsed);
		MaxSize = InDeviceMemoryAllocation->GetSize();
		if (InDeviceMemoryAllocation->IsMapped())
		{
			SubresourceAllocatorFlags |= VulkanAllocationFlagsMapped;
		}
		else
		{
			SubresourceAllocatorFlags &= ~VulkanAllocationFlagsMapped;
		}

		FRange FullRange;
		FullRange.Offset = 0;
		FullRange.Size = MaxSize;
		FreeList.Add(FullRange);

		VULKAN_FILL_TRACK_INFO(Track, __FILE__, __LINE__);
	}
	FVulkanSubresourceAllocator::~FVulkanSubresourceAllocator()
	{
		if(!JoinFreeBlocks())
		{
			UE_LOG(LogVulkanRHI, Warning, TEXT("FVulkanSubresourceAllocator %p has unfreed %s resources %s"), (void*)this, VulkanAllocationTypeToString(Type), *VULKAN_TRACK_STRING(Track));
			uint32 LeakCount = 0;
			for(FVulkanAllocationInternal& Data : InternalData)
			{
				if(Data.State == FVulkanAllocationInternal::EALLOCATED)
				{
					UE_LOG(LogVulkanRHI, Warning, TEXT(" ** LEAK %03d [%08x-%08x] %d  %s \n%s"), LeakCount++, Data.AllocationOffset, Data.AllocationSize,  Data.Size, VulkanAllocationMetaTypeToString(Data.MetaType), *VULKAN_TRACK_STRING(Data.Track));
				}
			}
		}
		check(0 == MemoryAllocation);
		VULKAN_FREE_TRACK_INFO(Track);
	}



	bool FMemoryManager::AllocateBufferPooled(FVulkanAllocation& OutAllocation, void* AllocationOwner, uint32 Size, VkBufferUsageFlags BufferUsageFlags, VkMemoryPropertyFlags MemoryPropertyFlags, EVulkanAllocationMetaType MetaType, const char* File, uint32 Line)
	{
		SCOPED_NAMED_EVENT(FResourceHeapManager_AllocateBufferPooled, FColor::Cyan);
		check(OutAllocation.Type == EVulkanAllocationEmpty);
		const VkPhysicalDeviceLimits& Limits = Device->GetLimits();
		uint32 Alignment = 1;

		float Priority = VULKAN_MEMORY_MEDIUM_PRIORITY;

		bool bIsTexelBuffer = (BufferUsageFlags & (VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT)) != 0;
		bool bIsStorageBuffer = (BufferUsageFlags & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) != 0;
		if (bIsTexelBuffer || bIsStorageBuffer)
		{
			Alignment = FMath::Max(Alignment, bIsTexelBuffer ? (uint32)Limits.minTexelBufferOffsetAlignment : 0);
			Alignment = FMath::Max(Alignment, bIsStorageBuffer ? (uint32)Limits.minStorageBufferOffsetAlignment : 0);
		}
		else
		{
			bool bIsVertexOrIndexBuffer = (BufferUsageFlags & (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) != 0;
			if (bIsVertexOrIndexBuffer)
			{
				// No alignment restrictions on Vertex or Index buffers, can live on CPU mem
				Priority = VULKAN_MEMORY_LOW_PRIORITY;
			}
			else
			{
				// Uniform buffer
				ensure((BufferUsageFlags & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) == VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
				Alignment = FMath::Max(Alignment, (uint32)Limits.minUniformBufferOffsetAlignment);

				Priority = VULKAN_MEMORY_HIGHER_PRIORITY;
			}
		}

		if (BufferUsageFlags & (VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT))
		{
			Priority = VULKAN_MEMORY_HIGHEST_PRIORITY;
		}

		int32 PoolSize = (int32)GetPoolTypeForAlloc(Size, Alignment);
		if (PoolSize != (int32)EPoolSizes::SizesCount)
		{
			Size = PoolSizes[PoolSize];
		}

		FScopeLock ScopeLock(&GResourceHeapLock);

		for (int32 Index = 0; Index < UsedBufferAllocations[PoolSize].Num(); ++Index)
		{
			FVulkanSubresourceAllocator* SubresourceAllocator = UsedBufferAllocations[PoolSize][Index];
			if ((SubresourceAllocator->BufferUsageFlags & BufferUsageFlags) == BufferUsageFlags &&
				(SubresourceAllocator->MemoryPropertyFlags & MemoryPropertyFlags) == MemoryPropertyFlags)
			{

				if(SubresourceAllocator->TryAllocate2(OutAllocation, AllocationOwner, Size, Alignment, MetaType, File, Line))
				{
					IncMetaStats(MetaType, OutAllocation.Size);
					return true;
				}
			}
		}

		for (int32 Index = 0; Index < FreeBufferAllocations[PoolSize].Num(); ++Index)
		{
			FVulkanSubresourceAllocator* SubresourceAllocator = FreeBufferAllocations[PoolSize][Index];
			if ((SubresourceAllocator->BufferUsageFlags & BufferUsageFlags) == BufferUsageFlags &&
				(SubresourceAllocator->MemoryPropertyFlags & MemoryPropertyFlags) == MemoryPropertyFlags)
			{
				if(SubresourceAllocator->TryAllocate2(OutAllocation, AllocationOwner, Size, Alignment, MetaType, File, Line))
				{
					IncMetaStats(MetaType, OutAllocation.Size);
					FreeBufferAllocations[PoolSize].RemoveAtSwap(Index, 1, false);
					UsedBufferAllocations[PoolSize].Add(SubresourceAllocator);
					return true;
				}
			}
		}

		// New Buffer
		uint32 BufferSize = FMath::Max(Size, BufferSizes[PoolSize]);

		VkBuffer Buffer;
		VkBufferCreateInfo BufferCreateInfo;
		ZeroVulkanStruct(BufferCreateInfo, VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
		BufferCreateInfo.size = BufferSize;
		BufferCreateInfo.usage = BufferUsageFlags;
		VERIFYVULKANRESULT(VulkanRHI::vkCreateBuffer(Device->GetInstanceHandle(), &BufferCreateInfo, VULKAN_CPU_ALLOCATOR, &Buffer));

		VkMemoryRequirements MemReqs;
		VulkanRHI::vkGetBufferMemoryRequirements(Device->GetInstanceHandle(), Buffer, &MemReqs);
		Alignment = FMath::Max((uint32)MemReqs.alignment, Alignment);
		ensure(MemReqs.size >= BufferSize);

		uint32 MemoryTypeIndex;
		VERIFYVULKANRESULT(Device->GetDeviceMemoryManager().GetMemoryTypeFromProperties(MemReqs.memoryTypeBits, MemoryPropertyFlags, &MemoryTypeIndex));

		bool bHasUnifiedMemory = DeviceMemoryManager->HasUnifiedMemory();
		FDeviceMemoryAllocation* DeviceMemoryAllocation = DeviceMemoryManager->Alloc(false, MemReqs.size, MemoryTypeIndex, nullptr, Priority, File, Line);
		VERIFYVULKANRESULT(VulkanRHI::vkBindBufferMemory(Device->GetInstanceHandle(), Buffer, DeviceMemoryAllocation->GetHandle(), 0));
		uint8 AllocationFlags = 0;
		if(!bHasUnifiedMemory && MetaTypeCanEvict(MetaType))
		{
			AllocationFlags |= VulkanAllocationFlagsCanEvict;
		}
		if (DeviceMemoryAllocation->CanBeMapped())
		{
			DeviceMemoryAllocation->Map(BufferSize, 0);
		}

		uint32 BufferId = 0;
		if (UseVulkanDescriptorCache())
		{
			BufferId = ++GVulkanBufferHandleIdCounter;
		}
		FVulkanSubresourceAllocator* SubresourceAllocator = new FVulkanSubresourceAllocator(EVulkanAllocationPooledBuffer, this, AllocationFlags, DeviceMemoryAllocation, MemoryTypeIndex,
			MemoryPropertyFlags, MemReqs.alignment, Buffer, BufferId, BufferUsageFlags, PoolSize);

		RegisterSubresourceAllocator(SubresourceAllocator);
		UsedBufferAllocations[PoolSize].Add(SubresourceAllocator);

		if(SubresourceAllocator->TryAllocate2(OutAllocation, AllocationOwner, Size, Alignment, MetaType, File, Line))
		{
			IncMetaStats(MetaType, OutAllocation.Size);
			return true;
		}
		HandleOOM();
		checkNoEntry();
		return false;
	}



	void FMemoryManager::RegisterSubresourceAllocator(FVulkanSubresourceAllocator* SubresourceAllocator)
	{
		check(SubresourceAllocator->AllocatorIndex == 0xffffffff);
		if(AllBufferAllocationsFreeListHead != (PTRINT)-1)
		{
			uint32 Index = AllBufferAllocationsFreeListHead;
			AllBufferAllocationsFreeListHead = (PTRINT)AllBufferAllocations[Index];
			SubresourceAllocator->AllocatorIndex = Index;
			AllBufferAllocations[Index] = SubresourceAllocator;
		}
		else
		{
			SubresourceAllocator->AllocatorIndex = AllBufferAllocations.Num();
			AllBufferAllocations.Add(SubresourceAllocator);
		}

	}
	void FMemoryManager::UnregisterSubresourceAllocator(FVulkanSubresourceAllocator* SubresourceAllocator)
	{
		if(SubresourceAllocator->bIsEvicting)
		{
			PendingEvictBytes -= SubresourceAllocator->GetMemoryAllocation()->GetSize();
		}
		uint32 Index = SubresourceAllocator->AllocatorIndex;
		check(Index != 0xffffffff);
		AllBufferAllocations[Index] = (FVulkanSubresourceAllocator*)AllBufferAllocationsFreeListHead;
		AllBufferAllocationsFreeListHead = Index;
	}


	void FMemoryManager::ReleaseSubresourceAllocator(FVulkanSubresourceAllocator* SubresourceAllocator)
	{
		if(SubresourceAllocator->Type == EVulkanAllocationPooledBuffer)
		{
			check(SubresourceAllocator->JoinFreeBlocks());
			UsedBufferAllocations[SubresourceAllocator->PoolSizeIndex].RemoveSingleSwap(SubresourceAllocator, false);
			SubresourceAllocator->FrameFreed = GFrameNumberRenderThread;
			FreeBufferAllocations[SubresourceAllocator->PoolSizeIndex].Add(SubresourceAllocator);
		}
		else
		{
			FVulkanResourceHeap* Heap = ResourceTypeHeaps[SubresourceAllocator->MemoryTypeIndex];
			Heap->FreePage(SubresourceAllocator);
		}
	}
	//
	bool FMemoryManager::UpdateEvictThreshold(bool bLog)
	{
		uint64 HostAllocated = 0;
		uint64 HostLimit = 0;
		DeviceMemoryManager->GetHostMemoryStatus(&HostAllocated, &HostLimit);
		double AllocatedPercentage = 100.0 * (HostAllocated - PendingEvictBytes) / HostLimit;

		double EvictionLimit = GVulkanEvictionLimitPercentage;
		double EvictionLimitLowered = EvictionLimit * (GVulkanEvictionLimitPercentageReenableLimit / 100.f);
		if(bIsEvicting) //once eviction is started, further lower the limit, to avoid reclaiming memory we just free up
		{
			EvictionLimit = EvictionLimitLowered;
		}
		if(bLog && GVulkanLogEvictStatus)
		{
			FGenericPlatformMisc::LowLevelOutputDebugStringf(TEXT("EVICT STATUS %6.2f%%/%6.2f%% :: A:%8.3fMB / E:%8.3fMB / T:%8.3fMB\n"), AllocatedPercentage, EvictionLimit, HostAllocated / (1024.f*1024.f), PendingEvictBytes/ (1024.f*1024.f), HostLimit/ (1024.f*1024.f));
		}

		bIsEvicting = AllocatedPercentage > EvictionLimit;
		return bIsEvicting;
	}

	bool FMemoryManager::AllocateImageMemory(FVulkanAllocation& OutAllocation, void* AllocationOwner, const VkMemoryRequirements& MemoryReqs, VkMemoryPropertyFlags MemoryPropertyFlags, EVulkanAllocationMetaType MetaType, const char* File, uint32 Line)
	{
		bool bHasUnifiedMemory = DeviceMemoryManager->HasUnifiedMemory();
		bool bCanEvict = MetaTypeCanEvict(MetaType);
		if(!bHasUnifiedMemory && bCanEvict && MemoryPropertyFlags == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT && UpdateEvictThreshold(false))
		{
			MemoryPropertyFlags = DeviceMemoryManager->GetEvictedMemoryProperties();
		}
		bool bMapped = (MemoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
		uint32 TypeIndex = 0;

		if (DeviceMemoryManager->GetMemoryTypeFromProperties(MemoryReqs.memoryTypeBits, MemoryPropertyFlags, &TypeIndex) != VK_SUCCESS)
		{
			if ((MemoryPropertyFlags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) == VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT)
			{
				// If lazy allocations are not supported, we can fall back to real allocations.
				MemoryPropertyFlags = MemoryPropertyFlags & ~VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
				VERIFYVULKANRESULT(DeviceMemoryManager->GetMemoryTypeFromProperties(MemoryReqs.memoryTypeBits, MemoryPropertyFlags, &TypeIndex));
			}
			else
			{
				UE_LOG(LogVulkanRHI, Fatal, TEXT("Cannot find memory type for MemSize %d, MemPropTypeBits %u, MemPropertyFlags %u, %s(%d)"), (uint32)MemoryReqs.size, (uint32)MemoryReqs.memoryTypeBits, (uint32)MemoryPropertyFlags, ANSI_TO_TCHAR(File), Line);
			}
		}
		if (!ResourceTypeHeaps[TypeIndex])
		{
			UE_LOG(LogVulkanRHI, Fatal, TEXT("Missing memory type index %d, MemSize %d, MemPropTypeBits %u, MemPropertyFlags %u, %s(%d)"), TypeIndex, (uint32)MemoryReqs.size, (uint32)MemoryReqs.memoryTypeBits, (uint32)MemoryPropertyFlags, ANSI_TO_TCHAR(File), Line);
		}
		const bool bForceSeparateAllocation = (MemoryPropertyFlags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) == VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
		if(!ResourceTypeHeaps[TypeIndex]->AllocateResource(OutAllocation, AllocationOwner, EType::Image, MemoryReqs.size, MemoryReqs.alignment, bMapped, bForceSeparateAllocation, MetaType, File, Line))
		{
			VERIFYVULKANRESULT(DeviceMemoryManager->GetMemoryTypeFromPropertiesExcluding(MemoryReqs.memoryTypeBits, MemoryPropertyFlags, TypeIndex, &TypeIndex));
			bMapped = (MemoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
			if(!ResourceTypeHeaps[TypeIndex]->AllocateResource(OutAllocation, AllocationOwner, EType::Image, MemoryReqs.size, MemoryReqs.alignment, bMapped, bForceSeparateAllocation, MetaType, File, Line))
			{
				DumpMemory();
				UE_LOG(LogVulkanRHI, Fatal, TEXT("Out Of Memory, trying to allocate %d bytes\n"), MemoryReqs.size);
				return false;
			}
		}
		return true;
	}

	bool FMemoryManager::AllocateBufferMemory(FVulkanAllocation& OutAllocation, void* AllocationOwner, const VkMemoryRequirements& MemoryReqs, VkMemoryPropertyFlags MemoryPropertyFlags, EVulkanAllocationMetaType MetaType, const char* File, uint32 Line)
	{
		SCOPED_NAMED_EVENT(FResourceHeapManager_AllocateBufferMemory, FColor::Cyan);
		uint32 TypeIndex = 0;
		VkResult Result = DeviceMemoryManager->GetMemoryTypeFromProperties(MemoryReqs.memoryTypeBits, MemoryPropertyFlags, &TypeIndex);
		bool bMapped = (MemoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
		if ((Result != VK_SUCCESS) || !ResourceTypeHeaps[TypeIndex])
		{
			if ((MemoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) == VK_MEMORY_PROPERTY_HOST_CACHED_BIT)
			{
				// Try non-cached flag
				MemoryPropertyFlags = MemoryPropertyFlags & ~VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
			}

			if ((MemoryPropertyFlags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) == VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT)
			{
				// Try non-lazy flag
				MemoryPropertyFlags = MemoryPropertyFlags & ~VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
			}

			// Try another heap type
			uint32 OriginalTypeIndex = TypeIndex;
			if (DeviceMemoryManager->GetMemoryTypeFromPropertiesExcluding(MemoryReqs.memoryTypeBits, MemoryPropertyFlags, (Result == VK_SUCCESS) ? TypeIndex : (uint32)-1, &TypeIndex) != VK_SUCCESS)
			{
				UE_LOG(LogVulkanRHI, Fatal, TEXT("Unable to find alternate type for index %d, MemSize %d, MemPropTypeBits %u, MemPropertyFlags %u, %s(%d)"),
					OriginalTypeIndex, (uint32)MemoryReqs.size, (uint32)MemoryReqs.memoryTypeBits, (uint32)MemoryPropertyFlags, ANSI_TO_TCHAR(File), Line);
			}
			if (!ResourceTypeHeaps[TypeIndex])
			{
				DumpMemory();
				UE_LOG(LogVulkanRHI, Fatal, TEXT("Missing memory type index %d (originally requested %d), MemSize %d, MemPropTypeBits %u, MemPropertyFlags %u, %s(%d)"), TypeIndex, OriginalTypeIndex, (uint32)MemoryReqs.size, (uint32)MemoryReqs.memoryTypeBits, (uint32)MemoryPropertyFlags, ANSI_TO_TCHAR(File), Line);
			}
		}

		if(!ResourceTypeHeaps[TypeIndex]->AllocateResource(OutAllocation, AllocationOwner, EType::Buffer, MemoryReqs.size, MemoryReqs.alignment, bMapped, false, MetaType, File, Line))
		{
			// Try another memory type if the allocation failed
			VERIFYVULKANRESULT(DeviceMemoryManager->GetMemoryTypeFromPropertiesExcluding(MemoryReqs.memoryTypeBits, MemoryPropertyFlags, TypeIndex, &TypeIndex));
			bMapped = (MemoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
			if (!ResourceTypeHeaps[TypeIndex])
			{
				UE_LOG(LogVulkanRHI, Fatal, TEXT("Missing memory type index %d, MemSize %d, MemPropTypeBits %u, MemPropertyFlags %u, %s(%d)"), TypeIndex, (uint32)MemoryReqs.size, (uint32)MemoryReqs.memoryTypeBits, (uint32)MemoryPropertyFlags, ANSI_TO_TCHAR(File), Line);
			}
			if(!ResourceTypeHeaps[TypeIndex]->AllocateResource(OutAllocation, AllocationOwner, EType::Buffer, MemoryReqs.size, MemoryReqs.alignment, bMapped, false, MetaType, File, Line))
			{
				DumpMemory();
				UE_LOG(LogVulkanRHI, Fatal, TEXT("Out Of Memory, trying to allocate %d bytes\n"), MemoryReqs.size);
				return false;
			}
		}
		return true;
	}

	bool FMemoryManager::AllocateDedicatedImageMemory(FVulkanAllocation& OutAllocation, void* AllocationOwner, VkImage Image, const VkMemoryRequirements& MemoryReqs, VkMemoryPropertyFlags MemoryPropertyFlags, EVulkanAllocationMetaType MetaType, const char* File, uint32 Line)
	{
#if VULKAN_SUPPORTS_DEDICATED_ALLOCATION
		SCOPED_NAMED_EVENT(FVulkanMemoryManager_AllocateDedicatedImageMemory, FColor::Cyan);
		VkImageMemoryRequirementsInfo2KHR ImageMemoryReqs2;
		ZeroVulkanStruct(ImageMemoryReqs2, VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2_KHR);
		ImageMemoryReqs2.image = Image;

		VkMemoryDedicatedRequirementsKHR DedMemoryReqs;
		ZeroVulkanStruct(DedMemoryReqs, VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS_KHR);

		VkMemoryRequirements2KHR MemoryReqs2;
		ZeroVulkanStruct(MemoryReqs2, VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2_KHR);
		MemoryReqs2.pNext = &DedMemoryReqs;

		VulkanRHI::vkGetImageMemoryRequirements2KHR(Device->GetInstanceHandle(), &ImageMemoryReqs2, &MemoryReqs2);

		bool bUseDedicated = DedMemoryReqs.prefersDedicatedAllocation != VK_FALSE || DedMemoryReqs.requiresDedicatedAllocation != VK_FALSE;		
		if (bUseDedicated)
		{
			uint32 TypeIndex = 0;
			VERIFYVULKANRESULT(DeviceMemoryManager->GetMemoryTypeFromProperties(MemoryReqs.memoryTypeBits, MemoryPropertyFlags, &TypeIndex));
			ensure((MemoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
			if (!ResourceTypeHeaps[TypeIndex])
			{
				UE_LOG(LogVulkanRHI, Fatal, TEXT("Missing memory type index %d, MemSize %d, MemPropTypeBits %u, MemPropertyFlags %u, %s(%d)"), TypeIndex, (uint32)MemoryReqs.size, (uint32)MemoryReqs.memoryTypeBits, (uint32)MemoryPropertyFlags, ANSI_TO_TCHAR(File), Line);
			}
			if(!ResourceTypeHeaps[TypeIndex]->AllocateDedicatedImage(OutAllocation, AllocationOwner, Image, MemoryReqs.size, MemoryReqs.alignment, MetaType, File, Line))
			{
				VERIFYVULKANRESULT(DeviceMemoryManager->GetMemoryTypeFromPropertiesExcluding(MemoryReqs.memoryTypeBits, MemoryPropertyFlags, TypeIndex, &TypeIndex));
				ensure((MemoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
				return ResourceTypeHeaps[TypeIndex]->AllocateDedicatedImage(OutAllocation, AllocationOwner, Image, MemoryReqs.size, MemoryReqs.alignment, MetaType, File, Line);
			}
			return true;
		}
		else
		{
			return AllocateImageMemory(OutAllocation, AllocationOwner, MemoryReqs, MemoryPropertyFlags, MetaType, File, Line);
		}
#else
		checkNoEntry();
		return false;
#endif
	}


	void FMemoryManager::DumpMemory()
	{
		FScopeLock ScopeLock(&GResourceHeapLock);
		Device->GetDeviceMemoryManager().DumpMemory();
		VULKAN_LOGMEMORY(TEXT("/******************************************* FMemoryManager ********************************************\\"));
		VULKAN_LOGMEMORY(TEXT("HEAP DUMP"));

		const VkPhysicalDeviceMemoryProperties& MemoryProperties = DeviceMemoryManager->GetMemoryProperties();

		TArray<FResourceHeapStats> Summary;
		TArray<FResourceHeapStats> HeapSummary;
		HeapSummary.SetNum(MemoryProperties.memoryHeapCount);
		for(uint32 Index = 0; Index < MemoryProperties.memoryHeapCount; ++Index)
		{
			HeapSummary[Index].MemoryFlags = 0;
			for (uint32 TypeIndex = 0; TypeIndex < MemoryProperties.memoryTypeCount; ++TypeIndex)
			{
				if(MemoryProperties.memoryTypes[TypeIndex].heapIndex == Index)
				{
					HeapSummary[Index].MemoryFlags |= MemoryProperties.memoryTypes[TypeIndex].propertyFlags; //since it can be different, just set to the bitwise or of all flags.
				}
			}
		}

		uint32 NumSmallAllocators = UE_ARRAY_COUNT(UsedBufferAllocations);
		uint32 NumResourceHeaps = ResourceTypeHeaps.Num();
		Summary.SetNum(NumResourceHeaps + NumSmallAllocators * 2);


		for (int32 Index = 0; Index < ResourceTypeHeaps.Num(); ++Index)
		{
			if (ResourceTypeHeaps[Index])
			{
				VULKAN_LOGMEMORY(TEXT("Heap %d, Memory Type Index %d"), Index, ResourceTypeHeaps[Index]->MemoryTypeIndex);
				Summary[Index].MemoryFlags = MemoryProperties.memoryTypes[ResourceTypeHeaps[Index]->MemoryTypeIndex].propertyFlags;
				ResourceTypeHeaps[Index]->DumpMemory(Summary[Index]);
				uint32 MemoryTypeIndex = ResourceTypeHeaps[Index]->MemoryTypeIndex;
				uint32 HeapIndex = MemoryProperties.memoryTypes[MemoryTypeIndex].heapIndex;
				HeapSummary[HeapIndex] += Summary[Index];
			}
			else
			{
				VULKAN_LOGMEMORY(TEXT("Heap %d, NOT USED"), Index);
			}
		}

		VULKAN_LOGMEMORY(TEXT("BUFFER DUMP"));
		uint64 UsedBinnedTotal = 0;
		uint64 AllocBinnedTotal = 0;
		uint64 UsedLargeTotal = 0;
		uint64 AllocLargeTotal = 0;
		for (int32 PoolSizeIndex = 0; PoolSizeIndex < UE_ARRAY_COUNT(UsedBufferAllocations); PoolSizeIndex++)
		{
			FResourceHeapStats& StatsLocal = Summary[NumResourceHeaps + PoolSizeIndex];
			FResourceHeapStats& StatsHost = Summary[NumResourceHeaps + NumSmallAllocators + PoolSizeIndex];
			TArray<FVulkanSubresourceAllocator*>& UsedAllocations = UsedBufferAllocations[PoolSizeIndex];
			TArray<FVulkanSubresourceAllocator*>& FreeAllocations = FreeBufferAllocations[PoolSizeIndex];
			if (PoolSizeIndex == (int32)EPoolSizes::SizesCount)
			{
				VULKAN_LOGMEMORY(TEXT("Buffer of large size Allocations: %d Used / %d Free"), UsedAllocations.Num(), FreeAllocations.Num());
			}
			else
			{
				VULKAN_LOGMEMORY(TEXT("Buffer of %d size Allocations: %d Used / %d Free"), PoolSizes[PoolSizeIndex], UsedAllocations.Num(), FreeAllocations.Num());
			}
			//Stats.Pages += UsedAllocations.Num() + FreeAllocations.Num();
			//Stats.BufferPages += UsedAllocations.Num();
			for (int32 Index = 0; Index < FreeAllocations.Num(); ++Index)
			{
				FVulkanSubresourceAllocator* BA = FreeAllocations[Index];
				if(BA->MemoryPropertyFlags & (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
				{
					StatsLocal.Pages += 1;
					StatsLocal.BufferPages += 1;
					StatsLocal.TotalMemory += BA->MaxSize;
					StatsLocal.MemoryFlags |= BA->MemoryPropertyFlags;
				}
				else
				{
					StatsHost.Pages += 1;
					StatsHost.BufferPages += 1;
					StatsHost.TotalMemory += BA->MaxSize;
					StatsHost.MemoryFlags |= BA->MemoryPropertyFlags;
				}

				uint32 HeapIndex = MemoryProperties.memoryTypes[BA->MemoryTypeIndex].heapIndex;
				FResourceHeapStats& HeapStats = HeapSummary[HeapIndex];
				HeapStats.Pages += 1;
				HeapStats.BufferPages += 1;
				HeapStats.TotalMemory += BA->MaxSize;
			}

			if (UsedAllocations.Num() > 0)
			{
				uint64 _UsedBinnedTotal = 0;
				uint64 _AllocBinnedTotal = 0;
				uint64 _UsedLargeTotal = 0;
				uint64 _AllocLargeTotal = 0;

				VULKAN_LOGMEMORY(TEXT("Index  BufferHandle       DeviceMemoryHandle MemFlags BufferFlags #Suballocs #FreeChunks UsedSize/MaxSize"));
				for (int32 Index = 0; Index < UsedAllocations.Num(); ++Index)
				{
					FVulkanSubresourceAllocator* BA = UsedAllocations[Index];
					VULKAN_LOGMEMORY(TEXT("%6d 0x%016llx 0x%016llx 0x%06x 0x%08x %6d   %6d        %d/%d"), Index, (void*)BA->Buffer, (void*)BA->MemoryAllocation->GetHandle(), BA->MemoryPropertyFlags, BA->BufferUsageFlags, BA->NumSubAllocations, BA->FreeList.Num(), BA->UsedSize, BA->MaxSize);

					if (PoolSizeIndex == (int32)EPoolSizes::SizesCount)
					{
						_UsedLargeTotal += BA->UsedSize;
						_AllocLargeTotal += BA->MaxSize;
						UsedLargeTotal += BA->UsedSize;
						AllocLargeTotal += BA->MaxSize;
					}
					else
					{
						_UsedBinnedTotal += BA->UsedSize;
						_AllocBinnedTotal += BA->MaxSize;
						UsedBinnedTotal += BA->UsedSize;
						AllocBinnedTotal += BA->MaxSize;
					}

					if (BA->MemoryPropertyFlags & (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
					{
						StatsLocal.Pages += 1;
						StatsLocal.BufferPages += 1;
						StatsLocal.UsedBufferMemory += BA->UsedSize;
						StatsLocal.TotalMemory += BA->MaxSize;
						StatsLocal.BufferAllocations += BA->NumSubAllocations;
						StatsLocal.MemoryFlags |= BA->MemoryPropertyFlags;
					}
					else
					{
						StatsHost.Pages += 1;
						StatsHost.BufferPages += 1;
						StatsHost.UsedBufferMemory += BA->UsedSize;
						StatsHost.TotalMemory += BA->MaxSize;
						StatsHost.BufferAllocations += BA->NumSubAllocations;
						StatsHost.MemoryFlags |= BA->MemoryPropertyFlags;
					}
					uint32 HeapIndex = MemoryProperties.memoryTypes[BA->MemoryTypeIndex].heapIndex;
					FResourceHeapStats& HeapStats = HeapSummary[HeapIndex];
					HeapStats.Pages += 1;
					HeapStats.BufferPages += 1;
					HeapStats.UsedBufferMemory += BA->UsedSize;
					HeapStats.TotalMemory += BA->MaxSize;
					HeapStats.BufferAllocations += BA->NumSubAllocations;
				}

				if (PoolSizeIndex == (int32)EPoolSizes::SizesCount)
				{
					VULKAN_LOGMEMORY(TEXT(" Large Alloc Used/Max %d/%d %6.2f%%"), _UsedLargeTotal, _AllocLargeTotal, 100.0f * (float)_UsedLargeTotal / (float)_AllocLargeTotal);
				}
				else
				{
					VULKAN_LOGMEMORY(TEXT(" Binned [%d] Alloc Used/Max %d/%d %6.2f%%"), PoolSizes[PoolSizeIndex], _UsedBinnedTotal, _AllocBinnedTotal, 100.0f * (float)_UsedBinnedTotal / (float)_AllocBinnedTotal);
				}
			}
		}

		VULKAN_LOGMEMORY(TEXT("::Totals::"));
		VULKAN_LOGMEMORY(TEXT("Large Alloc Used/Max %d/%d %.2f%%"), UsedLargeTotal, AllocLargeTotal, 100.0f * AllocLargeTotal > 0 ? (float)UsedLargeTotal / (float)AllocLargeTotal : 0.0f);
		VULKAN_LOGMEMORY(TEXT("Binned Alloc Used/Max %d/%d %.2f%%"), UsedBinnedTotal, AllocBinnedTotal, AllocBinnedTotal > 0 ? 100.0f * (float)UsedBinnedTotal / (float)AllocBinnedTotal : 0.0f);


		auto WriteLogLine = [](const FString& Name, FResourceHeapStats& Stat)
		{
			uint64 FreeMemory = Stat.TotalMemory - Stat.UsedBufferMemory - Stat.UsedImageMemory;
			FString HostString = GetMemoryPropertyFlagsString(Stat.MemoryFlags);
			VULKAN_LOGMEMORY(TEXT("\t\t%-25s  |%8.2fmb / %8.2fmb / %8.2fmb / %8.2fmb | %10d %10d | %6d %6d %6d | %05x | %s"),
				*Name,
				Stat.UsedBufferMemory / (1024.f * 1024.f),
				Stat.UsedImageMemory / (1024.f * 1024.f),
				FreeMemory / (1024.f * 1024.f),
				Stat.TotalMemory / (1024.f * 1024.f),
				Stat.BufferAllocations,
				Stat.ImageAllocations,
				Stat.Pages,
				Stat.BufferPages,
				Stat.ImagePages,
				Stat.MemoryFlags,
				*HostString);
		};

		FResourceHeapStats Total;
		FResourceHeapStats TotalHost;
		FResourceHeapStats TotalLocal;
		FResourceHeapStats Staging;
		TArray<FResourceHeapStats> DeviceHeaps;
		Device->GetStagingManager().GetMemoryDump(Staging);
		Device->GetDeviceMemoryManager().GetMemoryDump(DeviceHeaps);



		VULKAN_LOGMEMORY(TEXT("SUMMARY"));
		VULKAN_LOGMEMORY(TEXT("\t\tDevice Heaps               |    Memory       -           FreeMem      TotlMem |  Allocs     -         |  Allocs              | Flags | Type   "));
#define VULKAN_LOGMEMORY_PAD TEXT("\t\t--------------------------------------------------------------------------------------------------------------------------------------")
		VULKAN_LOGMEMORY(VULKAN_LOGMEMORY_PAD);
		for (int32 Index = 0; Index < DeviceHeaps.Num(); ++Index)
		{
			FResourceHeapStats& Stat = DeviceHeaps[Index];
			WriteLogLine(FString::Printf(TEXT("Device Heap %d"), Index), Stat);
		}
		VULKAN_LOGMEMORY(VULKAN_LOGMEMORY_PAD);
		VULKAN_LOGMEMORY(VULKAN_LOGMEMORY_PAD);
		VULKAN_LOGMEMORY(TEXT("\t\tAllocators                 |    BufMem       ImgMem      FreeMem      TotlMem |  BufAllocs  ImgAllocs |  Pages BufPgs ImgPgs | Flags | Type   "));
		VULKAN_LOGMEMORY(VULKAN_LOGMEMORY_PAD);



		for (int32 Index = 0; Index < Summary.Num(); ++Index)
		{
			FResourceHeapStats& Stat = Summary[Index];
			Total += Stat;
			if(Stat.MemoryFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
			{
				TotalLocal += Stat;
				TotalLocal.MemoryFlags |= Stat.MemoryFlags;
			}
			else
			{
				TotalHost += Stat;
				TotalHost.MemoryFlags |= Stat.MemoryFlags;
			}
			if(Index >= (int)NumResourceHeaps)
			{
				int PoolSizeIndex = (Index - NumResourceHeaps) % NumSmallAllocators;
				uint32 PoolSize = PoolSizeIndex >= (int32)EPoolSizes::SizesCount ? -1 : PoolSizes[PoolSizeIndex];
				if(0 == PoolSizeIndex)
				{
					VULKAN_LOGMEMORY(VULKAN_LOGMEMORY_PAD);
				}
				WriteLogLine(FString::Printf(TEXT("Pool %d"), PoolSize), Stat);
			}
			else
			{
				WriteLogLine(FString::Printf(TEXT("Heap %d"), Index), Stat);
			}
		}
		VULKAN_LOGMEMORY(VULKAN_LOGMEMORY_PAD);
		WriteLogLine(TEXT("TotalHost"), TotalHost);
		WriteLogLine(TEXT("TotalLocal"), TotalLocal);
		WriteLogLine(TEXT("Total"), Total);
		VULKAN_LOGMEMORY(VULKAN_LOGMEMORY_PAD);
		for(int32 Index = 0; Index < HeapSummary.Num(); ++Index)
		{
			FResourceHeapStats& Stat = HeapSummary[Index];
			//for the heaps, show -actual- max size, not reserved.
			Stat.TotalMemory = MemoryProperties.memoryHeaps[Index].size;
			WriteLogLine(FString::Printf(TEXT("Allocated Device Heap %d"), Index), Stat);
		}
		VULKAN_LOGMEMORY(VULKAN_LOGMEMORY_PAD);
		VULKAN_LOGMEMORY(TEXT("\t\tSubsystems                 |    BufMem       ImgMem      FreeMem      TotlMem |  BufAllocs  ImgAllocs |  Pages BufPgs ImgPgs | Flags | Type   "));
		VULKAN_LOGMEMORY(VULKAN_LOGMEMORY_PAD);
		WriteLogLine(TEXT("Staging"), Staging);
		VULKAN_LOGMEMORY(VULKAN_LOGMEMORY_PAD);

		VULKAN_LOGMEMORY(TEXT("\n\nSubAllocator Dump\n\n"));
		auto WriteLogLineSubAllocator = [](const FString& Name, const FString& MemoryString, FVulkanSubresourceAllocator& Allocator)
		{
			TArrayView<uint32> MemoryUsed = Allocator.GetMemoryUsed();
			uint32 NumAllocations = Allocator.GetNumSubAllocations();
			uint32 TotalMemory = Allocator.GetMaxSize();
			uint32 TotalUsed = 0;
			for(uint32 Used : MemoryUsed)
			{
				TotalUsed += Used;
			}
			uint64 Free= TotalMemory - TotalUsed;
			//%8.2fmb / %8.2fmb / %8.2fmb / %8.2fmb | %10d %10d | %6d %6d %6d | %05x | %s"),
			VULKAN_LOGMEMORY(TEXT("\t\t%-25s  | %12d | %8.2fmb / %8.2fmb / %8.2fmb | %8.2fmb / %8.2fmb / %8.2fmb / %8.2fmb | %8.2fmb / %8.2fmb | %8.2fmb / %8.2fmb / %8.2fmb | %s"),
				*Name,
				NumAllocations,
				TotalUsed / (1024.f * 1024.f),
				Free / (1024.f * 1024.f),
				TotalMemory / (1024.f * 1024.f),
				MemoryUsed[EVulkanAllocationMetaUnknown] / (1024.f * 1024.f),
				MemoryUsed[EVulkanAllocationMetaUniformBuffer] / (1024.f * 1024.f),
				MemoryUsed[EVulkanAllocationMetaMultiBuffer] / (1024.f * 1024.f),
				MemoryUsed[EVulkanAllocationMetaFrameTempBuffer] / (1024.f * 1024.f),
				MemoryUsed[EVulkanAllocationMetaImageRenderTarget] / (1024.f * 1024.f),
				MemoryUsed[EVulkanAllocationMetaImageOther] / (1024.f * 1024.f),
				MemoryUsed[EVulkanAllocationMetaBufferUAV] / (1024.f * 1024.f),
				MemoryUsed[EVulkanAllocationMetaBufferStaging] / (1024.f * 1024.f),
				MemoryUsed[EVulkanAllocationMetaBufferOther] / (1024.f * 1024.f),
				*MemoryString
			);
		};
		auto DumpAllocatorRange = [&](FString Name, TArray<FVulkanSubresourceAllocator*>& Allocators)
		{
			for (FVulkanSubresourceAllocator* Allocator : Allocators)
			{
				VkMemoryPropertyFlags Flags = Allocator->MemoryPropertyFlags;
				if(!Flags)
				{
					Flags = MemoryProperties.memoryTypes[Allocator->MemoryTypeIndex].propertyFlags;
				}
				FString MemoryString = GetMemoryPropertyFlagsString(Flags);
				WriteLogLineSubAllocator(Name, MemoryString, *Allocator);
			}


		};

#define VULKAN_LOGMEMORY_PAD2 TEXT("\t\t-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------")
		VULKAN_LOGMEMORY(VULKAN_LOGMEMORY_PAD2);
		VULKAN_LOGMEMORY(TEXT("\t\t%-25s  | %12s | %10s / %10s / %10s | %10s / %10s / %10s / %10s | %10s / %10s | %10s / %10s / %10s |"),
			TEXT(""),
			TEXT("Count"),
			TEXT("Used"),
			TEXT("Free"),
			TEXT("Total"),
			VulkanAllocationMetaTypeToString(EVulkanAllocationMetaUnknown),
			VulkanAllocationMetaTypeToString(EVulkanAllocationMetaUniformBuffer),
			VulkanAllocationMetaTypeToString(EVulkanAllocationMetaMultiBuffer),
			VulkanAllocationMetaTypeToString(EVulkanAllocationMetaFrameTempBuffer),
			VulkanAllocationMetaTypeToString(EVulkanAllocationMetaImageRenderTarget),
			VulkanAllocationMetaTypeToString(EVulkanAllocationMetaImageOther),
			VulkanAllocationMetaTypeToString(EVulkanAllocationMetaBufferUAV),
			VulkanAllocationMetaTypeToString(EVulkanAllocationMetaBufferUAV),
			VulkanAllocationMetaTypeToString(EVulkanAllocationMetaBufferStaging),
			VulkanAllocationMetaTypeToString(EVulkanAllocationMetaBufferOther)
		);

		VULKAN_LOGMEMORY(VULKAN_LOGMEMORY_PAD2);
		for (int32 Index = 0; Index < ResourceTypeHeaps.Num(); ++Index)
		{
			if (ResourceTypeHeaps[Index])
			{
				//VULKAN_LOGMEMORY(TEXT("Heap %d, Memory Type Index %d"), Index), ResourceTypeHeaps[Index]->MemoryTypeIndex);
				DumpAllocatorRange(FString::Printf(TEXT("UsedBufferPages %d"), Index ), ResourceTypeHeaps[Index]->UsedBufferPages);
				DumpAllocatorRange(FString::Printf(TEXT("UsedImagePages %d"), Index ), ResourceTypeHeaps[Index]->UsedImagePages);
				DumpAllocatorRange(FString::Printf(TEXT("FreeImagePages %d"), Index), ResourceTypeHeaps[Index]->FreeImagePages);
				DumpAllocatorRange(FString::Printf(TEXT("FreePages %d"), Index ), ResourceTypeHeaps[Index]->FreePages);
				DumpAllocatorRange(FString::Printf(TEXT("UsedDedicatedImagePages %d"), Index ), ResourceTypeHeaps[Index]->UsedDedicatedImagePages);

			}
		}

		VULKAN_LOGMEMORY(VULKAN_LOGMEMORY_PAD2);
		for (int32 PoolSizeIndex = 0; PoolSizeIndex < UE_ARRAY_COUNT(UsedBufferAllocations); PoolSizeIndex++)
		{
			FString NameUsed = FString::Printf(TEXT("PoolUsed %d"), PoolSizeIndex);
			FString NameFree = FString::Printf(TEXT("PoolFree %d"), PoolSizeIndex);
			TArray<FVulkanSubresourceAllocator*>& UsedAllocations = UsedBufferAllocations[PoolSizeIndex];
			TArray<FVulkanSubresourceAllocator*>& FreeAllocations = FreeBufferAllocations[PoolSizeIndex];
			DumpAllocatorRange(NameUsed, UsedAllocations);
			DumpAllocatorRange(NameFree, FreeAllocations);
		}
		VULKAN_LOGMEMORY(VULKAN_LOGMEMORY_PAD2);

		GLog->PanicFlushThreadedLogs();


#undef VULKAN_LOGMEMORY_PAD
#undef VULKAN_LOGMEMORY_PAD2
	}


	void FMemoryManager::HandleOOM(bool bCanResume, VkResult Result, uint64 AllocationSize, uint32 MemoryTypeIndex)
	{
		if(!bCanResume)
		{
			const TCHAR* MemoryType = TEXT("?");
			switch(Result)
			{
			case VK_ERROR_OUT_OF_HOST_MEMORY: MemoryType = TEXT("Host"); break;
			case VK_ERROR_OUT_OF_DEVICE_MEMORY: MemoryType = TEXT("Local"); break;
			}
			DumpMemory();
			GLog->PanicFlushThreadedLogs();
			DumpRenderTargetPoolMemory(*GLog);
			GLog->PanicFlushThreadedLogs();

			UE_LOG(LogVulkanRHI, Fatal, TEXT("Out of %s Memory, Requested%.2fKB MemTypeIndex=%d\n"), MemoryType, AllocationSize, MemoryTypeIndex);
		}
	}

	void FMemoryManager::AllocUniformBuffer(FVulkanAllocation& OutAllocation, uint32 Size, const void* Contents)
	{
		if(!AllocateBufferPooled(OutAllocation, nullptr, Size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, EVulkanAllocationMetaUniformBuffer, __FILE__, __LINE__))
		{
			HandleOOM(false);
		}
		FMemory::Memcpy(OutAllocation.GetMappedPointer(Device), Contents, Size);
		OutAllocation.FlushMappedMemory(Device);
	}
	void FMemoryManager::FreeUniformBuffer(FVulkanAllocation& InAllocation)
	{
		if(InAllocation.HasAllocation())
		{
			FScopeLock ScopeLock(&UBAllocations.CS);
			ProcessPendingUBFreesNoLock(false);
			FUBPendingFree& Pending = UBAllocations.PendingFree.AddDefaulted_GetRef();
			Pending.Frame = GFrameNumberRenderThread;
			Pending.Allocation.Swap(InAllocation);
			UBAllocations.Peak = FMath::Max(UBAllocations.Peak, (uint32)UBAllocations.PendingFree.Num());
		}
	}

	void FMemoryManager::ProcessPendingUBFreesNoLock(bool bForce)
	{
		// this keeps an frame number of the first frame when we can expect to delete things, updated in the loop if any pending allocations are left
		static uint32 GFrameNumberRenderThread_WhenWeCanDelete = 0;

		if (UNLIKELY(bForce))
		{
			int32 NumAlloc = UBAllocations.PendingFree.Num();
			for (int32 Index = 0; Index < NumAlloc; ++Index)
			{
				FUBPendingFree& Alloc = UBAllocations.PendingFree[Index];
				FreeVulkanAllocation(Alloc.Allocation, EVulkanFreeFlag_DontDefer);
			}
			UBAllocations.PendingFree.Empty();

			// invalidate the value
			GFrameNumberRenderThread_WhenWeCanDelete = 0;
		}
		else
		{
			if (LIKELY(GFrameNumberRenderThread < GFrameNumberRenderThread_WhenWeCanDelete))
			{
				// too early
				return;
			}

			// making use of the fact that we always add to the end of the array, so allocations are sorted by frame ascending
			int32 OldestFrameToKeep = GFrameNumberRenderThread - VulkanRHI::NUM_FRAMES_TO_WAIT_BEFORE_RELEASING_TO_OS;
			int32 NumAlloc = UBAllocations.PendingFree.Num();
			int32 Index = 0;
			for (; Index < NumAlloc; ++Index)
			{
				FUBPendingFree& Alloc = UBAllocations.PendingFree[Index];
				if (LIKELY(Alloc.Frame < OldestFrameToKeep))
				{
					FreeVulkanAllocation(Alloc.Allocation, EVulkanFreeFlag_DontDefer);
				}
				else
				{
					// calculate when we will be able to delete the oldest allocation
					GFrameNumberRenderThread_WhenWeCanDelete = Alloc.Frame + VulkanRHI::NUM_FRAMES_TO_WAIT_BEFORE_RELEASING_TO_OS + 1;
					break;
				}
			}

			int32 ElementsLeft = NumAlloc - Index;
			if (ElementsLeft > 0 && ElementsLeft != NumAlloc)
			{
				// FUBPendingFree is POD because it is stored in a TArray
				FMemory::Memmove(UBAllocations.PendingFree.GetData(), UBAllocations.PendingFree.GetData() + Index, ElementsLeft * sizeof(FUBPendingFree));
				for(int32 EndIndex = ElementsLeft; EndIndex < UBAllocations.PendingFree.Num(); ++EndIndex)
				{
					auto& E = UBAllocations.PendingFree[EndIndex];
					if(E.Allocation.HasAllocation())
					{
						E.Allocation.Disown();
					}
				}
			}
			UBAllocations.PendingFree.SetNum(NumAlloc - Index, false);
		}
	}

	void FMemoryManager::ProcessPendingUBFrees(bool bForce)
	{
		FScopeLock ScopeLock(&UBAllocations.CS);
		ProcessPendingUBFreesNoLock(bForce);
	}

	bool FVulkanSubresourceAllocator::JoinFreeBlocks()
	{
		FScopeLock ScopeLock(&CS);
#if !UE_VK_MEMORY_JOIN_FREELIST_ON_THE_FLY
		FRange::JoinConsecutiveRanges(FreeList);
#endif

		if (FreeList.Num() == 1)
		{
			if (NumSubAllocations == 0)
			{
				check(UsedSize == 0);
				checkf(FreeList[0].Offset == 0 && FreeList[0].Size == MaxSize, TEXT("Resource Suballocation leak, should have %d free, only have %d; missing %d bytes"), MaxSize, FreeList[0].Size, MaxSize - FreeList[0].Size);
				return true;
			}
		}
		return false;
	}

	FVulkanAllocation::FVulkanAllocation()
	{
	}
	FVulkanAllocation::~FVulkanAllocation()
	{
		check(!HasAllocation());
	}


	void FVulkanAllocationInternal::Init(const FVulkanAllocation& Alloc, void* InAllocationOwner, uint32 InAllocationOffset, uint32 InAllocationSize)
	{
		check(State == EUNUSED);
		State = EALLOCATED;
		Type = Alloc.Type;
		MetaType = Alloc.MetaType;

		Size = Alloc.Size;
		AllocationSize = InAllocationSize;
		AllocationOffset = InAllocationOffset;
		AllocationOwner = InAllocationOwner;
	}

	void FVulkanAllocation::Init(EVulkanAllocationType InType, EVulkanAllocationMetaType InMetaType, uint64 Handle, uint32 InSize, uint32 InAlignedOffset, uint32 InAllocatorIndex, uint32 InAllocationIndex, uint32 BufferId)
	{
		check(!HasAllocation());
		bHasOwnership = 1;
		Type = InType;
		MetaType = InMetaType;
		Size = InSize;
		Offset = InAlignedOffset;
		check(InAllocatorIndex < (1<<ALLOCATOR_INDEX_BITS));
		check(InAllocationIndex < (1<<ALLOCATION_INDEX_BITS));
		AllocatorIndex = InAllocatorIndex;
		AllocationIndex = InAllocationIndex;
		VulkanHandle = Handle;
		HandleId = BufferId;
	}

	void FVulkanAllocation::Free(FVulkanDevice& Device)
	{
		if(HasAllocation())
		{
			Device.GetMemoryManager().FreeVulkanAllocation(*this);
			check(EVulkanAllocationEmpty != Type);

		}
	}
	void FVulkanAllocation::Swap(FVulkanAllocation& Other)
	{
		FMemory::Memswap(this, &Other, sizeof(*this));
	}

	void FVulkanAllocation::Reference(const FVulkanAllocation& Other)
	{
		FMemory::Memcpy(*this, Other);
		bHasOwnership = 0;
	}
	bool FVulkanAllocation::HasAllocation()
	{
		return Type != EVulkanAllocationEmpty && bHasOwnership;
	}

	void FVulkanAllocation::Disown()
	{
		check(bHasOwnership);
		bHasOwnership = 0;
	}
	void FVulkanAllocation::Own()
	{
		check(!bHasOwnership);
		bHasOwnership = 1;
	}
	bool FVulkanAllocation::IsValid() const
	{
		return Size != 0;
	}
	void* FVulkanAllocation::GetMappedPointer(FVulkanDevice* Device)
	{
		FVulkanSubresourceAllocator* Allocator = GetSubresourceAllocator(Device);
		uint8* pMappedPointer = (uint8*)Allocator->GetMappedPointer();
		check(pMappedPointer);
		return Offset + pMappedPointer;
	}

	void FVulkanAllocation::FlushMappedMemory(FVulkanDevice* Device)
	{
		FVulkanSubresourceAllocator* Allocator = GetSubresourceAllocator(Device);
		Allocator->Flush(Offset, Size);
	}

	void FVulkanAllocation::InvalidateMappedMemory(FVulkanDevice* Device)
	{
		FVulkanSubresourceAllocator* Allocator = GetSubresourceAllocator(Device);
		Allocator->Invalidate(Offset, Size);
	}

	VkBuffer FVulkanAllocation::GetBufferHandle() const
	{
		return (VkBuffer)VulkanHandle;
	}
	uint32 FVulkanAllocation::GetBufferAlignment(FVulkanDevice* Device) const
	{
		FVulkanSubresourceAllocator* Allocator = GetSubresourceAllocator(Device);
		return Allocator->GetAlignment();
	}
	VkDeviceMemory FVulkanAllocation::GetDeviceMemoryHandle(FVulkanDevice* Device) const
	{
		FVulkanSubresourceAllocator* Allocator = GetSubresourceAllocator(Device);
		return Allocator->GetMemoryAllocation()->GetHandle();
	}

	void FVulkanAllocation::BindBuffer(FVulkanDevice* Device, VkBuffer Buffer)
	{
		VkResult Result = VulkanRHI::vkBindBufferMemory(Device->GetInstanceHandle(), Buffer, GetDeviceMemoryHandle(Device), Offset);
		if (Result == VK_ERROR_OUT_OF_DEVICE_MEMORY || Result == VK_ERROR_OUT_OF_HOST_MEMORY)
		{
			Device->GetMemoryManager().DumpMemory();
		}
		VERIFYVULKANRESULT(Result);
	}
	void FVulkanAllocation::BindImage(FVulkanDevice* Device, VkImage Image)
	{
		VkResult Result = VulkanRHI::vkBindImageMemory(Device->GetInstanceHandle(), Image, GetDeviceMemoryHandle(Device), Offset);
		if (Result == VK_ERROR_OUT_OF_DEVICE_MEMORY || Result == VK_ERROR_OUT_OF_HOST_MEMORY)
		{
			Device->GetMemoryManager().DumpMemory();
		}
		VERIFYVULKANRESULT(Result);
	}
	FVulkanSubresourceAllocator* FVulkanAllocation::GetSubresourceAllocator(FVulkanDevice* Device) const
	{
		switch(Type)
		{
		case EVulkanAllocationEmpty:
			return 0;
			break;
		case EVulkanAllocationPooledBuffer:
		case EVulkanAllocationBuffer:
		case EVulkanAllocationImage:
		case EVulkanAllocationImageDedicated:
			return Device->GetMemoryManager().AllBufferAllocations[AllocatorIndex];
		break;
		default:
			check(0);
		}
		return 0;
	}

	void FVulkanSubresourceAllocator::FreeInternalData(int32 Index)
	{
		check(InternalData[Index].State == FVulkanAllocationInternal::EUNUSED || InternalData[Index].State ==FVulkanAllocationInternal::EFREED);
		check(InternalData[Index].NextFree == -1);
		InternalData[Index].NextFree = InternalFreeList;
		InternalFreeList = Index;
		InternalData[Index].State  = FVulkanAllocationInternal::EUNUSED;
	}



	int32 FVulkanSubresourceAllocator::AllocateInternalData()
	{
		int32 FreeListHead = InternalFreeList;
		if(FreeListHead < 0)
		{
			int32 Result = InternalData.AddZeroed(1);
			InternalData[Result].NextFree = -1;
			return Result;

		}
		else
		{
			InternalFreeList = InternalData[FreeListHead].NextFree;
			InternalData[FreeListHead].NextFree = -1;
			return FreeListHead;
		}
	}



	bool FVulkanSubresourceAllocator::TryAllocate2(FVulkanAllocation& OutAllocation, void* AllocationOwner, uint32 InSize, uint32 InAlignment, EVulkanAllocationMetaType InMetaType, const char* File, uint32 Line)
	{
		FScopeLock ScopeLock(&CS);
		if(bIsEvicting)
		{
			return false;
		}
		InAlignment = FMath::Max(InAlignment, Alignment);
		for (int32 Index = 0; Index < FreeList.Num(); ++Index)
		{
			FRange& Entry = FreeList[Index];
			uint32 AllocatedOffset = Entry.Offset;
			uint32 AlignedOffset = Align(Entry.Offset, InAlignment);
			uint32 AlignmentAdjustment = AlignedOffset - Entry.Offset;
			uint32 AllocatedSize = AlignmentAdjustment + InSize;
			if (AllocatedSize <= Entry.Size)
			{
				FRange::AllocateFromEntry(FreeList, Index, AllocatedSize);

				UsedSize += AllocatedSize;
				int32 ExtraOffset = AllocateInternalData();
				OutAllocation.Init(Type, InMetaType, (uint64)Buffer, InSize, AlignedOffset, GetAllocatorIndex(), ExtraOffset, BufferId);
				MemoryUsed[InMetaType] += AllocatedSize;
				static uint32 UIDCounter = 0;
				UIDCounter++;
				InternalData[ExtraOffset].Init(OutAllocation, AllocationOwner, AllocatedOffset, AllocatedSize);
				VULKAN_FILL_TRACK_INFO(InternalData[ExtraOffset].Track, File, Line);
				AllocCalls++;
				NumSubAllocations++;

				LLM_TRACK_VULKAN_HIGH_LEVEL_ALLOC(InternalData[ExtraOffset], OutAllocation.Size);
				LLM_TRACK_VULKAN_SPARE_MEMORY_GPU(-(int64)OutAllocation.Size);
				return true;
			}
		}
		return false;
	}

	void FVulkanSubresourceAllocator::Flush(VkDeviceSize Offset, VkDeviceSize AllocationSize)
	{
		MemoryAllocation->FlushMappedMemory(Offset, AllocationSize);
	}
	void FVulkanSubresourceAllocator::Invalidate(VkDeviceSize Offset, VkDeviceSize AllocationSize)
	{
		MemoryAllocation->InvalidateMappedMemory(Offset, AllocationSize);
	}

	TArrayView<uint32> FVulkanSubresourceAllocator::GetMemoryUsed()
	{
		return MemoryUsed;

	}
	uint32 FVulkanSubresourceAllocator::GetNumSubAllocations()
	{
		return NumSubAllocations;

	}


	uint64 FVulkanSubresourceAllocator::EvictToHost(FVulkanDevice& Device)
	{
		FScopeLock ScopeLock(&CS);
		bIsEvicting = true;
		for(FVulkanAllocationInternal& Alloc: InternalData)
		{
			if(Alloc.State == FVulkanAllocationInternal::EALLOCATED)
			{
				switch(Alloc.MetaType)
				{
				case EVulkanAllocationMetaImageOther:
				{
					FVulkanTextureBase* Texture= (FVulkanTextureBase*)Alloc.AllocationOwner;
					Texture->Evict(Device);
				}
				break;
				default:
					//right now only there is only support for evicting non-rt images
					checkNoEntry();

				}
				//check(Alloc.State != FVulkanAllocationInternal::EALLOCATED);
			}

		}
		return MemoryAllocation->GetSize();
	}

	FStagingBuffer::FStagingBuffer(FVulkanDevice* InDevice)
		: Device(InDevice)
		, Buffer(VK_NULL_HANDLE)
		, MemoryReadFlags(VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
		, BufferSize(0)
	{
	}
	VkBuffer FStagingBuffer::GetHandle() const
	{
		return Buffer;
	}

	FStagingBuffer::~FStagingBuffer()
	{
		Device->GetMemoryManager().FreeVulkanAllocation(Allocation);
	}


	void* FStagingBuffer::GetMappedPointer()
	{
		return Allocation.GetMappedPointer(Device);
	}

	uint32 FStagingBuffer::GetSize() const
	{
		return BufferSize;
	}

	VkDeviceMemory FStagingBuffer::GetDeviceMemoryHandle() const
	{
		return Allocation.GetDeviceMemoryHandle(Device);
	}

	void FStagingBuffer::FlushMappedMemory()
	{
		Allocation.FlushMappedMemory(Device);
	}

	void FStagingBuffer::InvalidateMappedMemory()
	{
		Allocation. InvalidateMappedMemory(Device);
	}


	void FStagingBuffer::Destroy()
	{
		//// Does not need to go in the deferred deletion queue
		VulkanRHI::vkDestroyBuffer(Device->GetInstanceHandle(), Buffer, VULKAN_CPU_ALLOCATOR);
		Buffer = VK_NULL_HANDLE;
		Device->GetMemoryManager().FreeVulkanAllocation(Allocation);
	}

	FStagingManager::~FStagingManager()
	{
		check(UsedStagingBuffers.Num() == 0);
		check(PendingFreeStagingBuffers.Num() == 0);
		check(FreeStagingBuffers.Num() == 0);
	}

	void FStagingManager::Deinit()
	{
		ProcessPendingFree(true, true);

		check(UsedStagingBuffers.Num() == 0);
		check(PendingFreeStagingBuffers.Num() == 0);
		check(FreeStagingBuffers.Num() == 0);
	}

	FStagingBuffer* FStagingManager::AcquireBuffer(uint32 Size, VkBufferUsageFlags InUsageFlags, VkMemoryPropertyFlagBits InMemoryReadFlags)
	{
#if VULKAN_ENABLE_AGGRESSIVE_STATS
		SCOPE_CYCLE_COUNTER(STAT_VulkanStagingBuffer);
#endif
		LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanStagingBuffers);
		if (InMemoryReadFlags == VK_MEMORY_PROPERTY_HOST_CACHED_BIT)
		{
			uint64 NonCoherentAtomSize = (uint64)Device->GetLimits().nonCoherentAtomSize;
			Size = AlignArbitrary(Size, NonCoherentAtomSize);
		}

		// Add both source and dest flags
		if ((InUsageFlags & (VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)) != 0)
		{
			InUsageFlags |= (VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
		}

		//#todo-rco: Better locking!
		{
			FScopeLock Lock(&GStagingLock);
			for (int32 Index = 0; Index < FreeStagingBuffers.Num(); ++Index)
			{
				FFreeEntry& FreeBuffer = FreeStagingBuffers[Index];
				if (FreeBuffer.StagingBuffer->GetSize() == Size && FreeBuffer.StagingBuffer->MemoryReadFlags == InMemoryReadFlags)
				{
					FStagingBuffer* Buffer = FreeBuffer.StagingBuffer;
					FreeStagingBuffers.RemoveAtSwap(Index, 1, false);
					UsedStagingBuffers.Add(Buffer);
					VULKAN_FILL_TRACK_INFO(Buffer->Track, __FILE__, __LINE__);
					return Buffer;
				}
			}
		}

		FStagingBuffer* StagingBuffer = new FStagingBuffer(Device);

		VkBufferCreateInfo StagingBufferCreateInfo;
		ZeroVulkanStruct(StagingBufferCreateInfo, VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
		StagingBufferCreateInfo.size = Size;
		StagingBufferCreateInfo.usage = InUsageFlags;

		VkDevice VulkanDevice = Device->GetInstanceHandle();

		VERIFYVULKANRESULT(VulkanRHI::vkCreateBuffer(VulkanDevice, &StagingBufferCreateInfo, VULKAN_CPU_ALLOCATOR, &StagingBuffer->Buffer));

		VkMemoryRequirements MemReqs;
		VulkanRHI::vkGetBufferMemoryRequirements(VulkanDevice, StagingBuffer->Buffer, &MemReqs);
		ensure(MemReqs.size >= Size);

		// Set minimum alignment to 16 bytes, as some buffers are used with CPU SIMD instructions
		MemReqs.alignment = FMath::Max<VkDeviceSize>(16, MemReqs.alignment);
		static const bool bIsAmd = Device->GetDeviceProperties().vendorID == 0x1002;
		if (InMemoryReadFlags == VK_MEMORY_PROPERTY_HOST_CACHED_BIT || bIsAmd)
		{
			uint64 NonCoherentAtomSize = (uint64)Device->GetLimits().nonCoherentAtomSize;
			MemReqs.alignment = AlignArbitrary(MemReqs.alignment, NonCoherentAtomSize);
		}

		VkMemoryPropertyFlags readTypeFlags = InMemoryReadFlags;
		if(!Device->GetMemoryManager().AllocateBufferMemory(StagingBuffer->Allocation, StagingBuffer, MemReqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | readTypeFlags, EVulkanAllocationMetaBufferStaging, __FILE__, __LINE__))
		{
			check(0);
		}
		StagingBuffer->MemoryReadFlags = InMemoryReadFlags;
		StagingBuffer->BufferSize = Size;
		StagingBuffer->Allocation.BindBuffer(Device, StagingBuffer->Buffer);
		//StagingBuffer->ResourceAllocation->BindBuffer(Device, StagingBuffer->Buffer);

		{
			FScopeLock Lock(&GStagingLock);
			UsedStagingBuffers.Add(StagingBuffer);
			UsedMemory += StagingBuffer->GetSize();
			PeakUsedMemory = FMath::Max(UsedMemory, PeakUsedMemory);
		}

		VULKAN_FILL_TRACK_INFO(StagingBuffer->Track, __FILE__, __LINE__);
		return StagingBuffer;
	}

	inline FStagingManager::FPendingItemsPerCmdBuffer* FStagingManager::FindOrAdd(FVulkanCmdBuffer* CmdBuffer)
	{
		for (int32 Index = 0; Index < PendingFreeStagingBuffers.Num(); ++Index)
		{
			if (PendingFreeStagingBuffers[Index].CmdBuffer == CmdBuffer)
			{
				return &PendingFreeStagingBuffers[Index];
			}
		}

		FPendingItemsPerCmdBuffer* New = new(PendingFreeStagingBuffers) FPendingItemsPerCmdBuffer;
		New->CmdBuffer = CmdBuffer;
		return New;
	}

	inline FStagingManager::FPendingItemsPerCmdBuffer::FPendingItems* FStagingManager::FPendingItemsPerCmdBuffer::FindOrAddItemsForFence(uint64 Fence)
	{
		for (int32 Index = 0; Index < PendingItems.Num(); ++Index)
		{
			if (PendingItems[Index].FenceCounter == Fence)
			{
				return &PendingItems[Index];
			}
		}

		FPendingItems* New = new(PendingItems) FPendingItems;
		New->FenceCounter = Fence;
		return New;
	}

	void FStagingManager::ReleaseBuffer(FVulkanCmdBuffer* CmdBuffer, FStagingBuffer*& StagingBuffer)
	{
#if VULKAN_ENABLE_AGGRESSIVE_STATS
		SCOPE_CYCLE_COUNTER(STAT_VulkanStagingBuffer);
#endif

		FScopeLock Lock(&GStagingLock);
		UsedStagingBuffers.RemoveSingleSwap(StagingBuffer, false);

		if (CmdBuffer)
		{
			FPendingItemsPerCmdBuffer* ItemsForCmdBuffer = FindOrAdd(CmdBuffer);
			FPendingItemsPerCmdBuffer::FPendingItems* ItemsForFence = ItemsForCmdBuffer->FindOrAddItemsForFence(CmdBuffer->GetFenceSignaledCounterA());
			check(StagingBuffer);
			ItemsForFence->Resources.Add(StagingBuffer);
		}
		else
		{
			FreeStagingBuffers.Add({StagingBuffer, GFrameNumberRenderThread});
		}
		StagingBuffer = nullptr;
	}

	void FStagingManager::GetMemoryDump(FResourceHeapStats& Stats)
	{
		for (int32 Index = 0; Index < UsedStagingBuffers.Num(); ++Index)
		{
			FStagingBuffer* Buffer = UsedStagingBuffers[Index];
			Stats.BufferAllocations += 1;
			Stats.UsedBufferMemory += Buffer->BufferSize;
			Stats.TotalMemory += Buffer->BufferSize;
			Stats.MemoryFlags |= Buffer->MemoryReadFlags|VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
		}
		for (int32 Index = 0; Index < PendingFreeStagingBuffers.Num(); ++Index)
		{
			FPendingItemsPerCmdBuffer& ItemPerCmdBuffer = PendingFreeStagingBuffers[Index];
			for (int32 FenceIndex = 0; FenceIndex < ItemPerCmdBuffer.PendingItems.Num(); ++FenceIndex)
			{
				FPendingItemsPerCmdBuffer::FPendingItems& ItemsPerFence = ItemPerCmdBuffer.PendingItems[FenceIndex];
				for (int32 BufferIndex = 0; BufferIndex < ItemsPerFence.Resources.Num(); ++BufferIndex)
				{
					FStagingBuffer* Buffer = ItemsPerFence.Resources[BufferIndex];
					Stats.BufferAllocations += 1;
					Stats.UsedBufferMemory += Buffer->BufferSize;
					Stats.TotalMemory += Buffer->BufferSize;
					Stats.MemoryFlags |= Buffer->MemoryReadFlags|VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
				}
			}
		}
		for (int32 Index = 0; Index < FreeStagingBuffers.Num(); ++Index)
		{
			FFreeEntry& Entry = FreeStagingBuffers[Index];
			Stats.BufferAllocations += 1;
			Stats.TotalMemory += Entry.StagingBuffer->BufferSize;
			Stats.MemoryFlags |= Entry.StagingBuffer->MemoryReadFlags|VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
		}
	}

	void FStagingManager::DumpMemory()
	{
		VULKAN_LOGMEMORY(TEXT("/******************************************* STAGING *******************************************\\"));
		VULKAN_LOGMEMORY(TEXT("StagingManager %d Used %d Pending Free %d Free"), UsedStagingBuffers.Num(), PendingFreeStagingBuffers.Num(), FreeStagingBuffers.Num());
		VULKAN_LOGMEMORY(TEXT("Used   BufferHandle       ResourceAllocation Size"));
		for (int32 Index = 0; Index < UsedStagingBuffers.Num(); ++Index)
		{
			FStagingBuffer* Buffer = UsedStagingBuffers[Index];
			VULKAN_LOGMEMORY(TEXT("%6d 0x%016llx 0x%016llx %6d"), Index, (void*)Buffer->GetHandle(), (void*)Buffer->Allocation.GetBufferHandle(), Buffer->BufferSize);
		}

		VULKAN_LOGMEMORY(TEXT("Pending CmdBuffer   Fence   BufferHandle    ResourceAllocation Size"));
		for (int32 Index = 0; Index < PendingFreeStagingBuffers.Num(); ++Index)
		{
			FPendingItemsPerCmdBuffer& ItemPerCmdBuffer = PendingFreeStagingBuffers[Index];
			VULKAN_LOGMEMORY(TEXT("%6d %p"), Index, (void*)ItemPerCmdBuffer.CmdBuffer->GetHandle());
			for (int32 FenceIndex = 0; FenceIndex < ItemPerCmdBuffer.PendingItems.Num(); ++FenceIndex)
			{
				FPendingItemsPerCmdBuffer::FPendingItems& ItemsPerFence = ItemPerCmdBuffer.PendingItems[FenceIndex];
				VULKAN_LOGMEMORY(TEXT("         Fence %p"), (void*)ItemsPerFence.FenceCounter);
				for (int32 BufferIndex = 0; BufferIndex < ItemsPerFence.Resources.Num(); ++BufferIndex)
				{
					FStagingBuffer* Buffer = ItemsPerFence.Resources[BufferIndex];
					VULKAN_LOGMEMORY(TEXT("                   0x%016llx 0x%016llx %6d"), (void*)Buffer->GetHandle(), (void*)Buffer->Allocation.GetBufferHandle(), Buffer->BufferSize);
				}
			}
		}

		VULKAN_LOGMEMORY(TEXT("Free   BufferHandle     ResourceAllocation Size"));
		for (int32 Index = 0; Index < FreeStagingBuffers.Num(); ++Index)
		{
			FFreeEntry& Entry = FreeStagingBuffers[Index];
			VULKAN_LOGMEMORY(TEXT("%6d 0x%016llx 0x%016llx %6d"), Index, (void*)Entry.StagingBuffer->GetHandle(), (void*)Entry.StagingBuffer->Allocation.GetBufferHandle(), Entry.StagingBuffer->BufferSize);
		}
	}


	void FStagingManager::ProcessPendingFreeNoLock(bool bImmediately, bool bFreeToOS)
	{
		int32 NumOriginalFreeBuffers = FreeStagingBuffers.Num();
		for (int32 Index = PendingFreeStagingBuffers.Num() - 1; Index >= 0; --Index)
		{
			FPendingItemsPerCmdBuffer& EntriesPerCmdBuffer = PendingFreeStagingBuffers[Index];
			for (int32 FenceIndex = EntriesPerCmdBuffer.PendingItems.Num() - 1; FenceIndex >= 0; --FenceIndex)
			{
				FPendingItemsPerCmdBuffer::FPendingItems& PendingItems = EntriesPerCmdBuffer.PendingItems[FenceIndex];
				if (bImmediately || PendingItems.FenceCounter < EntriesPerCmdBuffer.CmdBuffer->GetFenceSignaledCounterB())
				{
					for (int32 ResourceIndex = 0; ResourceIndex < PendingItems.Resources.Num(); ++ResourceIndex)
					{
						check(PendingItems.Resources[ResourceIndex]);
						FreeStagingBuffers.Add({PendingItems.Resources[ResourceIndex], GFrameNumberRenderThread});
					}

					EntriesPerCmdBuffer.PendingItems.RemoveAtSwap(FenceIndex, 1, false);
				}
			}

			if (EntriesPerCmdBuffer.PendingItems.Num() == 0)
			{
				PendingFreeStagingBuffers.RemoveAtSwap(Index, 1, false);
			}
		}

		if (bFreeToOS)
		{
			int32 NumFreeBuffers = bImmediately ? FreeStagingBuffers.Num() : NumOriginalFreeBuffers;
			for (int32 Index = NumFreeBuffers - 1; Index >= 0; --Index)
			{
				FFreeEntry& Entry = FreeStagingBuffers[Index];
				if (bImmediately || Entry.FrameNumber + NUM_FRAMES_TO_WAIT_BEFORE_RELEASING_TO_OS < GFrameNumberRenderThread)
				{
					UsedMemory -= Entry.StagingBuffer->GetSize();
					Entry.StagingBuffer->Destroy();
					delete Entry.StagingBuffer;
					FreeStagingBuffers.RemoveAtSwap(Index, 1, false);
				}
			}
		}
	}

	void FStagingManager::ProcessPendingFree(bool bImmediately, bool bFreeToOS)
	{
#if VULKAN_ENABLE_AGGRESSIVE_STATS
		SCOPE_CYCLE_COUNTER(STAT_VulkanStagingBuffer);
#endif

		FScopeLock Lock(&GStagingLock);
		ProcessPendingFreeNoLock(bImmediately, bFreeToOS);
	}

	FFence::FFence(FVulkanDevice* InDevice, FFenceManager* InOwner, bool bCreateSignaled)
		: State(bCreateSignaled ? FFence::EState::Signaled : FFence::EState::NotReady)
		, Owner(InOwner)
	{
		VkFenceCreateInfo Info;
		ZeroVulkanStruct(Info, VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
		Info.flags = bCreateSignaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0;
		VERIFYVULKANRESULT(VulkanRHI::vkCreateFence(InDevice->GetInstanceHandle(), &Info, VULKAN_CPU_ALLOCATOR, &Handle));
	}

	FFence::~FFence()
	{
		checkf(Handle == VK_NULL_HANDLE, TEXT("Didn't get properly destroyed by FFenceManager!"));
	}

	FFenceManager::~FFenceManager()
	{
		ensure(UsedFences.Num() == 0);
	}

	inline void FFenceManager::DestroyFence(FFence* Fence)
	{
		// Does not need to go in the deferred deletion queue
		VulkanRHI::vkDestroyFence(Device->GetInstanceHandle(), Fence->GetHandle(), VULKAN_CPU_ALLOCATOR);
		Fence->Handle = VK_NULL_HANDLE;
		delete Fence;
	}

	void FFenceManager::Init(FVulkanDevice* InDevice)
	{
		Device = InDevice;
	}

	void FFenceManager::Deinit()
	{
		FScopeLock Lock(&GFenceLock);
		ensureMsgf(UsedFences.Num() == 0, TEXT("No all fences are done!"));
		VkDevice DeviceHandle = Device->GetInstanceHandle();
		for (FFence* Fence : FreeFences)
		{
			DestroyFence(Fence);
		}
	}

	FFence* FFenceManager::AllocateFence(bool bCreateSignaled)
	{
		FScopeLock Lock(&GFenceLock);
		if (FreeFences.Num() != 0)
		{
			FFence* Fence = FreeFences[0];
			FreeFences.RemoveAtSwap(0, 1, false);
			UsedFences.Add(Fence);

			if (bCreateSignaled)
			{
				Fence->State = FFence::EState::Signaled;
			}
			return Fence;
		}

		FFence* NewFence = new FFence(Device, this, bCreateSignaled);
		UsedFences.Add(NewFence);
		return NewFence;
	}

	// Sets it to nullptr
	void FFenceManager::ReleaseFence(FFence*& Fence)
	{
		FScopeLock Lock(&GFenceLock);
		ResetFence(Fence);
		UsedFences.RemoveSingleSwap(Fence, false);
#if VULKAN_REUSE_FENCES
		FreeFences.Add(Fence);
#else
		DestroyFence(Fence);
#endif
		Fence = nullptr;
	}

	void FFenceManager::WaitAndReleaseFence(FFence*& Fence, uint64 TimeInNanoseconds)
	{
		FScopeLock Lock(&GFenceLock);
		if (!Fence->IsSignaled())
		{
			WaitForFence(Fence, TimeInNanoseconds);
		}

		ResetFence(Fence);
		UsedFences.RemoveSingleSwap(Fence, false);
		FreeFences.Add(Fence);
		Fence = nullptr;
	}

	bool FFenceManager::CheckFenceState(FFence* Fence)
	{
		check(UsedFences.Contains(Fence));
		check(Fence->State == FFence::EState::NotReady);
		VkResult Result = VulkanRHI::vkGetFenceStatus(Device->GetInstanceHandle(), Fence->Handle);
		switch (Result)
		{
		case VK_SUCCESS:
			Fence->State = FFence::EState::Signaled;
			return true;

		case VK_NOT_READY:
			break;

		default:
			VERIFYVULKANRESULT(Result);
			break;
		}

		return false;
	}

	bool FFenceManager::WaitForFence(FFence* Fence, uint64 TimeInNanoseconds)
	{
#if VULKAN_ENABLE_AGGRESSIVE_STATS
		SCOPE_CYCLE_COUNTER(STAT_VulkanWaitFence);
#endif

		check(UsedFences.Contains(Fence));
		check(Fence->State == FFence::EState::NotReady);
		VkResult Result = VulkanRHI::vkWaitForFences(Device->GetInstanceHandle(), 1, &Fence->Handle, true, TimeInNanoseconds);
		switch (Result)
		{
		case VK_SUCCESS:
			Fence->State = FFence::EState::Signaled;
			return true;
		case VK_TIMEOUT:
			break;
		default:
			VERIFYVULKANRESULT(Result);
			break;
		}

		return false;
	}

	void FFenceManager::ResetFence(FFence* Fence)
	{
		if (Fence->State != FFence::EState::NotReady)
		{
			VERIFYVULKANRESULT(VulkanRHI::vkResetFences(Device->GetInstanceHandle(), 1, &Fence->Handle));
			Fence->State = FFence::EState::NotReady;
		}
	}


	FGPUEvent::FGPUEvent(FVulkanDevice* InDevice)
		: FDeviceChild(InDevice)
	{
		VkEventCreateInfo Info;
		ZeroVulkanStruct(Info, VK_STRUCTURE_TYPE_EVENT_CREATE_INFO);
		VERIFYVULKANRESULT(VulkanRHI::vkCreateEvent(InDevice->GetInstanceHandle(), &Info, VULKAN_CPU_ALLOCATOR, &Handle));
	}

	FGPUEvent::~FGPUEvent()
	{
		Device->GetDeferredDeletionQueue().EnqueueResource(VulkanRHI::FDeferredDeletionQueue2::EType::Event, Handle);
	}



	FDeferredDeletionQueue2::FDeferredDeletionQueue2(FVulkanDevice* InDevice)
		: FDeviceChild(InDevice)
	{
	}

	FDeferredDeletionQueue2::~FDeferredDeletionQueue2()
	{
		check(Entries.Num() == 0);
	}

	void FDeferredDeletionQueue2::EnqueueGenericResource(EType Type, uint64 Handle)
	{
		FVulkanQueue* Queue = Device->GetGraphicsQueue();

		FEntry Entry;
		Entry.StructureType = Type;
		Queue->GetLastSubmittedInfo(Entry.CmdBuffer, Entry.FenceCounter);
		Entry.FrameNumber = GVulkanRHIDeletionFrameNumber;

		Entry.Handle = Handle;
		{
			FScopeLock ScopeLock(&CS);

#if VULKAN_HAS_DEBUGGING_ENABLED
			FEntry* ExistingEntry = Entries.FindByPredicate([&](const FEntry& InEntry)
				{
					return InEntry.Handle == Entry.Handle;
				});
			checkf(ExistingEntry == nullptr, TEXT("Attempt to double-delete resource, FDeferredDeletionQueue2::EType: %d, Handle: %llu"), (int32)Type, Handle);
#endif

			Entries.Add(Entry);
		}
	}

	void FDeferredDeletionQueue2::EnqueueResourceAllocation(FVulkanAllocation& Allocation)
	{
		if (!Allocation.HasAllocation())
		{
			return;
		}
		Allocation.Disown();
		FVulkanQueue* Queue = Device->GetGraphicsQueue();

		FEntry Entry;
		Entry.StructureType = EType::ResourceAllocation;
		Queue->GetLastSubmittedInfo(Entry.CmdBuffer, Entry.FenceCounter);
		Entry.FrameNumber = GVulkanRHIDeletionFrameNumber;

		Entry.Handle = VK_NULL_HANDLE;
		Entry.Allocation = Allocation;

		{
			FScopeLock ScopeLock(&CS);

			Entries.Add(Entry);
		}
		check(!Allocation.HasAllocation());
	}


	void FDeferredDeletionQueue2::ReleaseResources(bool bDeleteImmediately)
	{
#if VULKAN_ENABLE_AGGRESSIVE_STATS
		SCOPE_CYCLE_COUNTER(STAT_VulkanDeletionQueue);
#endif
		FScopeLock ScopeLock(&CS);

		VkDevice DeviceHandle = Device->GetInstanceHandle();

		// Traverse list backwards so the swap switches to elements already tested
		for (int32 Index = Entries.Num() - 1; Index >= 0; --Index)
		{
			FEntry* Entry = &Entries[Index];
			// #todo-rco: Had to add this check, we were getting null CmdBuffers on the first frame, or before first frame maybe
			if (bDeleteImmediately ||
				(GVulkanRHIDeletionFrameNumber > Entry->FrameNumber + NUM_FRAMES_TO_WAIT_FOR_RESOURCE_DELETE &&
					(Entry->CmdBuffer == nullptr || Entry->FenceCounter < Entry->CmdBuffer->GetFenceSignaledCounterC()))
				)
			{
				switch (Entry->StructureType)
				{
#define VKSWITCH(Type, ...)	case EType::Type: __VA_ARGS__; VulkanRHI::vkDestroy##Type(DeviceHandle, (Vk##Type)Entry->Handle, VULKAN_CPU_ALLOCATOR); break
					VKSWITCH(RenderPass);
					VKSWITCH(Buffer);
					VKSWITCH(BufferView);
					VKSWITCH(Image);
					VKSWITCH(ImageView);
					VKSWITCH(Pipeline, DEC_DWORD_STAT(STAT_VulkanNumPSOs));
					VKSWITCH(PipelineLayout);
					VKSWITCH(Framebuffer);
					VKSWITCH(DescriptorSetLayout);
					VKSWITCH(Sampler);
					VKSWITCH(Semaphore);
					VKSWITCH(ShaderModule);
					VKSWITCH(Event);
#undef VKSWITCH
				case EType::ResourceAllocation:
				{
					FVulkanAllocation Allocation = Entry->Allocation;
					Allocation.Own();
					Device->GetMemoryManager().FreeVulkanAllocation(Allocation, EVulkanFreeFlag_DontDefer);
					break;
				}


				default:
					check(0);
					break;
				}
				Entries.RemoveAtSwap(Index, 1, false);
			}
		}
	}




	void FDeferredDeletionQueue2::OnCmdBufferDeleted(FVulkanCmdBuffer* DeletedCmdBuffer)
	{
		FScopeLock ScopeLock(&CS);
		for (int32 Index = 0; Index < Entries.Num(); ++Index)
		{
			FEntry& Entry = Entries[Index];
			if (Entry.CmdBuffer == DeletedCmdBuffer)
			{
				Entry.CmdBuffer = nullptr;
			}
		}
	}

	FTempFrameAllocationBuffer::FTempFrameAllocationBuffer(FVulkanDevice* InDevice)
		: FDeviceChild(InDevice)
		, BufferIndex(0)
	{
		for (int32 Index = 0; Index < NUM_BUFFERS; ++Index)
		{
			INC_MEMORY_STAT_BY(STAT_VulkanTempFrameAllocationBuffer, ALLOCATION_SIZE);
			Entries[Index].InitBuffer(Device, ALLOCATION_SIZE);
		}
	}

	FTempFrameAllocationBuffer::~FTempFrameAllocationBuffer()
	{
		Destroy();
	}

	void FTempFrameAllocationBuffer::FFrameEntry::InitBuffer(FVulkanDevice* InDevice, uint32 InSize)
	{
		LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanFrameTemp);
		Size = InSize;
		PeakUsed = 0;
		FMemoryManager& ResourceHeapManager = InDevice->GetMemoryManager();
		check(Allocation.Type == EVulkanAllocationEmpty);
		if(ResourceHeapManager.AllocateBufferPooled(Allocation, nullptr, InSize,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
			VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			EVulkanAllocationMetaFrameTempBuffer,
			__FILE__, __LINE__))
		{
			MappedData = (uint8*)Allocation.GetMappedPointer(InDevice);
			CurrentData = MappedData;
		}
		else
		{
			ResourceHeapManager.HandleOOM(true);
		}
	}

	void FTempFrameAllocationBuffer::Destroy()
	{
		FMemoryManager& MemoryManager = Device->GetMemoryManager();
		for (int32 Index = 0; Index < NUM_BUFFERS; ++Index)
		{
			MemoryManager.FreeVulkanAllocation(Entries[Index].Allocation); ;
		}
	}

	bool FTempFrameAllocationBuffer::FFrameEntry::TryAlloc(uint32 InSize, uint32 InAlignment, FTempAllocInfo& OutInfo)
	{
		uint8* AlignedData = (uint8*)Align((uintptr_t)CurrentData, (uintptr_t)InAlignment);
		if (AlignedData + InSize <= MappedData + Size)
		{
			OutInfo.Data = AlignedData;
			OutInfo.Allocation.Reference(Allocation);
			OutInfo.CurrentOffset = (uint32)(AlignedData - MappedData);
			OutInfo.Size = InSize;
			CurrentData = AlignedData + InSize;
			PeakUsed = FMath::Max(PeakUsed, (uint32)(CurrentData - MappedData));
			return true;
		}

		return false;
	}

	void FTempFrameAllocationBuffer::Alloc(uint32 InSize, uint32 InAlignment, FTempAllocInfo& OutInfo)
	{
		FScopeLock ScopeLock(&CS);

		if (Entries[BufferIndex].TryAlloc(InSize, InAlignment, OutInfo))
		{
			return;
		}

		// Couldn't fit in the current buffers; allocate a new bigger one and schedule the current one for deletion
		uint32 NewSize = Align(ALLOCATION_SIZE + InSize + InAlignment, ALLOCATION_SIZE);
		DEC_MEMORY_STAT_BY(STAT_VulkanTempFrameAllocationBuffer, Entries[BufferIndex].Allocation.Size);
		INC_MEMORY_STAT_BY(STAT_VulkanTempFrameAllocationBuffer, NewSize);
		FVulkanAllocation& PendingDelete = Entries[BufferIndex].PendingDeletionList.AddDefaulted_GetRef();
		PendingDelete.Swap(Entries[BufferIndex].Allocation);
		Entries[BufferIndex].InitBuffer(Device, NewSize);
		if (!Entries[BufferIndex].TryAlloc(InSize, InAlignment, OutInfo))
		{
			checkf(0, TEXT("Internal Error trying to allocate %d Align %d on TempFrameBuffer, size %d"), InSize, InAlignment, NewSize);
		}
	}

	void FTempFrameAllocationBuffer::Reset()
	{
		FScopeLock ScopeLock(&CS);
		BufferIndex = (BufferIndex + 1) % NUM_BUFFERS;
		Entries[BufferIndex].Reset(Device);
	}

	void FTempFrameAllocationBuffer::FFrameEntry::Reset(FVulkanDevice* InDevice)
	{
		CurrentData = MappedData;
		FMemoryManager& MemoryManager = InDevice->GetMemoryManager();
		for(FVulkanAllocation& Alloc : PendingDeletionList)
		{
			if(Alloc.HasAllocation())
			{
				MemoryManager.FreeVulkanAllocation(Alloc);
			}
			check(!Alloc.HasAllocation());

		}
		PendingDeletionList.SetNum(0);
	}

	FSemaphore::FSemaphore(FVulkanDevice& InDevice) :
		Device(InDevice),
		SemaphoreHandle(VK_NULL_HANDLE),
		bExternallyOwned(false)
	{
		// Create semaphore
		VkSemaphoreCreateInfo CreateInfo;
		ZeroVulkanStruct(CreateInfo, VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
		VERIFYVULKANRESULT(VulkanRHI::vkCreateSemaphore(Device.GetInstanceHandle(), &CreateInfo, VULKAN_CPU_ALLOCATOR, &SemaphoreHandle));
	}

	FSemaphore::FSemaphore(FVulkanDevice& InDevice, const VkSemaphore& InExternalSemaphore) :
		Device(InDevice),
		SemaphoreHandle(InExternalSemaphore),
		bExternallyOwned(true)
	{}

	FSemaphore::~FSemaphore()
	{
		check(SemaphoreHandle != VK_NULL_HANDLE);
		if (!bExternallyOwned)
		{
			Device.GetDeferredDeletionQueue().EnqueueResource(VulkanRHI::FDeferredDeletionQueue2::EType::Semaphore, SemaphoreHandle);
		}
		SemaphoreHandle = VK_NULL_HANDLE;
	}
}


#if VULKAN_CUSTOM_MEMORY_MANAGER_ENABLED
namespace VulkanRHI
{
	VkAllocationCallbacks GAllocationCallbacks;
}
static FCriticalSection GMemMgrCS;
static FVulkanCustomMemManager GVulkanInstrumentedMemMgr;
//VkAllocationCallbacks GDescriptorAllocationCallbacks;


FVulkanCustomMemManager::FVulkanCustomMemManager()
{
	VulkanRHI::GAllocationCallbacks.pUserData = nullptr;
	VulkanRHI::GAllocationCallbacks.pfnAllocation = (PFN_vkAllocationFunction)&FVulkanCustomMemManager::Alloc;
	VulkanRHI::GAllocationCallbacks.pfnReallocation = (PFN_vkReallocationFunction)&FVulkanCustomMemManager::Realloc;
	VulkanRHI::GAllocationCallbacks.pfnFree = (PFN_vkFreeFunction)&FVulkanCustomMemManager::Free;
	VulkanRHI::GAllocationCallbacks.pfnInternalAllocation = (PFN_vkInternalAllocationNotification)&FVulkanCustomMemManager::InternalAllocationNotification;
	VulkanRHI::GAllocationCallbacks.pfnInternalFree = (PFN_vkInternalFreeNotification)&FVulkanCustomMemManager::InternalFreeNotification;
}

inline FVulkanCustomMemManager::FType& FVulkanCustomMemManager::GetType(void* UserData, VkSystemAllocationScope AllocScope)
{
	check(AllocScope < VK_SYSTEM_ALLOCATION_SCOPE_RANGE_SIZE);
	return GVulkanInstrumentedMemMgr.Types[AllocScope];
}

void* FVulkanCustomMemManager::Alloc(void* UserData, size_t Size, size_t Alignment, VkSystemAllocationScope AllocScope)
{
	check(AllocScope < VK_SYSTEM_ALLOCATION_SCOPE_RANGE_SIZE);
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanDriverMemoryCPU);
	FScopeLock Lock(&GMemMgrCS);
	void* Data = FMemory::Malloc(Size, Alignment);
	FType& Type = GetType(UserData, AllocScope);
	Type.MaxAllocSize = FMath::Max(Type.MaxAllocSize, Size);
	Type.UsedMemory += Size;
	Type.Allocs.Add(Data, Size);
	return Data;
}

void FVulkanCustomMemManager::Free(void* UserData, void* Mem)
{
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanDriverMemoryCPU);
	FScopeLock Lock(&GMemMgrCS);
	FMemory::Free(Mem);
	for (int32 Index = 0; Index < GVulkanInstrumentedMemMgr.Types.Num(); ++Index)
	{
		FType& Type = GVulkanInstrumentedMemMgr.Types[Index];
		size_t* Found = Type.Allocs.Find(Mem);
		if (Found)
		{
			Type.UsedMemory -= *Found;
			break;
		}
	}
}

void* FVulkanCustomMemManager::Realloc(void* UserData, void* Original, size_t Size, size_t Alignment, VkSystemAllocationScope AllocScope)
{
	check(AllocScope < VK_SYSTEM_ALLOCATION_SCOPE_RANGE_SIZE);
	LLM_SCOPE_VULKAN(ELLMTagVulkan::VulkanDriverMemoryCPU);
	FScopeLock Lock(&GMemMgrCS);
	void* Data = FMemory::Realloc(Original, Size, Alignment);
	FType& Type = GetType(UserData, AllocScope);
	size_t OldSize = Original ? Type.Allocs.FindAndRemoveChecked(Original) : 0;
	Type.UsedMemory -= OldSize;
	Type.Allocs.Add(Data, Size);
	Type.UsedMemory += Size;
	Type.MaxAllocSize = FMath::Max(Type.MaxAllocSize, Size);
	return Data;
}

void FVulkanCustomMemManager::InternalAllocationNotification(void* UserData, size_t Size, VkInternalAllocationType AllocationType, VkSystemAllocationScope AllocScope)
{
	check(AllocScope < VK_SYSTEM_ALLOCATION_SCOPE_RANGE_SIZE);
}

void FVulkanCustomMemManager::InternalFreeNotification(void* UserData, size_t Size, VkInternalAllocationType AllocationType, VkSystemAllocationScope AllocScope)
{
	check(AllocScope < VK_SYSTEM_ALLOCATION_SCOPE_RANGE_SIZE);
}
#endif


VkResult FDeviceMemoryManager::GetMemoryTypeFromProperties(uint32 TypeBits, VkMemoryPropertyFlags Properties, uint32* OutTypeIndex)
{
	//#todo-rco: Might need to revisit based on https://gitlab.khronos.org/vulkan/vulkan/merge_requests/1165
	// Search memtypes to find first index with those properties
	for (uint32 i = 0; i < MemoryProperties.memoryTypeCount && TypeBits; i++)
	{
		if ((TypeBits & 1) == 1)
		{
			// Type is available, does it match user properties?
			if ((MemoryProperties.memoryTypes[i].propertyFlags & Properties) == Properties)
			{
				*OutTypeIndex = i;
				return VK_SUCCESS;
			}
		}
		TypeBits >>= 1;
	}

	// No memory types matched, return failure
	return VK_ERROR_FEATURE_NOT_PRESENT;
}

VkResult FDeviceMemoryManager::GetMemoryTypeFromPropertiesExcluding(uint32 TypeBits, VkMemoryPropertyFlags Properties, uint32 ExcludeTypeIndex, uint32* OutTypeIndex)
{
	// Search memtypes to find first index with those properties
	for (uint32 i = 0; i < MemoryProperties.memoryTypeCount && TypeBits; i++)
	{
		if ((TypeBits & 1) == 1)
		{
			// Type is available, does it match user properties?
			if ((MemoryProperties.memoryTypes[i].propertyFlags & Properties) == Properties && ExcludeTypeIndex != i)
			{
				*OutTypeIndex = i;
				return VK_SUCCESS;
			}
		}
		TypeBits >>= 1;
	}

	// No memory types matched, return failure
	return VK_ERROR_FEATURE_NOT_PRESENT;
}

const VkPhysicalDeviceMemoryProperties& FDeviceMemoryManager::GetMemoryProperties() const
{
	return MemoryProperties;
}
