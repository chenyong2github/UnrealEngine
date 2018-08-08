// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "MetalRHIPrivate.h"
#include "Misc/App.h"
#if PLATFORM_IOS
#include "IOS/IOSAppDelegate.h"
#endif
#include "Misc/ConfigCacheIni.h"
#include "HAL/PlatformFramePacer.h"
#include "Runtime/HeadMountedDisplay/Public/IHeadMountedDisplayModule.h"

#include "MetalContext.h"
#include "MetalProfiler.h"
#include "MetalCommandBuffer.h"

#include "ShaderCache.h"

int32 GMetalSupportsIntermediateBackBuffer = PLATFORM_MAC ? 1 : 0;
static FAutoConsoleVariableRef CVarMetalSupportsIntermediateBackBuffer(
	TEXT("rhi.Metal.SupportsIntermediateBackBuffer"),
	GMetalSupportsIntermediateBackBuffer,
	TEXT("When enabled (> 0) allocate an intermediate texture to use as the back-buffer & blit from there into the actual device back-buffer, thereby allowing screenshots & video capture that would otherwise be impossible as the texture required has already been released back to the OS as required by Metal's API. (Off by default (0) on iOS/tvOS but enabled (1) on Mac)"), ECVF_ReadOnly);

int32 GMetalSeparatePresentThread = 0;
static FAutoConsoleVariableRef CVarMetalSeparatePresentThread(
	TEXT("rhi.Metal.SeparatePresentThread"),
	GMetalSeparatePresentThread,
	TEXT("When enabled (> 0) requires rhi.Metal.SupportsIntermediateBackBuffer be enabled and will cause two intermediate back-buffers be allocated so that the presentation of frames to the screen can be run on a separate thread.\n")
	TEXT("This option uncouples the Render/RHI thread from calls to -[CAMetalLayer nextDrawable] and will run arbitrarily fast by rendering but not waiting to present all frames. This is equivalent to running without V-Sync, but without the screen tearing.\n")
	TEXT("On macOS 10.12 this will not be beneficial, but on later macOS versions this is the only way to ensure that we keep the CPU & GPU saturated with commands and don't ever stall waiting for V-Sync.\n")
	TEXT("On iOS/tvOS this is the only way to run without locking the CPU to V-Sync somewhere - this shouldn't be used in a shipping title without understanding the power/heat implications.\n")
	TEXT("(Off by default (0))"), ECVF_ReadOnly);

int32 GMetalNonBlockingPresent = 0;
static FAutoConsoleVariableRef CVarMetalNonBlockingPresent(
	TEXT("rhi.Metal.NonBlockingPresent"),
	GMetalNonBlockingPresent,
	TEXT("When enabled (> 0) this will force MetalRHI to query if a back-buffer is available to present and if not will skip the frame. Only functions on macOS, it is ignored on iOS/tvOS.\n")
	TEXT("(Off by default (0))"));

#if PLATFORM_MAC
static int32 GMetalCommandQueueSize = 5120; // This number is large due to texture streaming - currently each texture is its own command-buffer.
// The whole MetalRHI needs to be changed to use MTLHeaps/MTLFences & reworked so that operations with the same synchronisation requirements are collapsed into a single blit command-encoder/buffer.
#else
static int32 GMetalCommandQueueSize = 0;
#endif
static FAutoConsoleVariableRef CVarMetalCommandQueueSize(
	TEXT("rhi.Metal.CommandQueueSize"),
	GMetalCommandQueueSize,
	TEXT("The maximum number of command-buffers that can be allocated from each command-queue. (Default: 5120 Mac, 64 iOS/tvOS)"), ECVF_ReadOnly);

#if METAL_DEBUG_OPTIONS
int32 GMetalBufferScribble = 0; // Deliberately not static, see InitFrame_UniformBufferPoolCleanup
static FAutoConsoleVariableRef CVarMetalBufferScribble(
	TEXT("rhi.Metal.BufferScribble"),
	GMetalBufferScribble,
	TEXT("Debug option: when enabled will scribble over the buffer contents with a single value when releasing buffer objects, or regions thereof. (Default: 0, Off)"));

int32 GMetalBufferZeroFill = 0; // Deliberately not static
static FAutoConsoleVariableRef CVarMetalBufferZeroFill(
	TEXT("rhi.Metal.BufferZeroFill"),
	GMetalBufferZeroFill,
	TEXT("Debug option: when enabled will fill the buffer contents with 0 when allocating buffer objects, or regions thereof. (Default: 0, Off)"));

static int32 GMetalResourcePurgeOnDelete = 0;
static FAutoConsoleVariableRef CVarMetalResourcePurgeOnDelete(
	TEXT("rhi.Metal.ResourcePurgeOnDelete"),
	GMetalResourcePurgeOnDelete,
	TEXT("Debug option: when enabled all MTLResource objects will have their backing stores purged on release - any subsequent access will be invalid and cause a command-buffer failure. Useful for making intermittent resource lifetime errors more common and easier to track. (Default: 0, Off)"));

static int32 GMetalResourceDeferDeleteNumFrames = 0;
static FAutoConsoleVariableRef CVarMetalResourceDeferDeleteNumFrames(
	TEXT("rhi.Metal.ResourceDeferDeleteNumFrames"),
	GMetalResourcePurgeOnDelete,
	TEXT("Debug option: set to the number of frames that must have passed before resource free-lists are processed and resources disposed of. (Default: 0, Off)"));
#endif

#if UE_BUILD_SHIPPING || UE_BUILD_TEST
int32 GMetalRuntimeDebugLevel = 0;
#else
int32 GMetalRuntimeDebugLevel = 1;
#endif
static FAutoConsoleVariableRef CVarMetalRuntimeDebugLevel(
	TEXT("rhi.Metal.RuntimeDebugLevel"),
	GMetalRuntimeDebugLevel,
	TEXT("The level of debug validation performed by MetalRHI in addition to the underlying Metal API & validation layer.\n")
	TEXT("Each subsequent level adds more tests and reporting in addition to the previous level.\n")
	TEXT("*LEVELS >1 ARE IGNORED IN SHIPPING AND TEST BUILDS*. (Default: 1 (Debug, Development), 0 (Test, Shipping))\n")
	TEXT("\t0: Off,\n")
	TEXT("\t1: Record the debug-groups issued into a command-buffer and report them on failure,\n")
	TEXT("\t2: Enable light-weight validation of resource bindings & API usage,\n")
	TEXT("\t3: Track resources and validate lifetime on command-buffer failure,\n")
	TEXT("\t4: Reset resource bindings to simplify GPU trace debugging,\n")
	TEXT("\t5: Enable slower, more extensive validation checks for resource types & encoder usage,\n")
	TEXT("\t6: Record the draw, blit & dispatch commands issued into a command-buffer and report them on failure,\n")
	TEXT("\t7: Allow rhi.Metal.CommandBufferCommitThreshold to break command-encoders (except when MSAA is enabled),\n")
	TEXT("\t8: Wait for each command-buffer to complete immediately after submission."));

float GMetalPresentFramePacing = 0.0f;
#if !PLATFORM_MAC
static FAutoConsoleVariableRef CVarMetalPresentFramePacing(
	TEXT("rhi.Metal.PresentFramePacing"),
	GMetalPresentFramePacing,
	TEXT("Specify the desired frame rate for presentation (iOS 10.3+ only, default: 0.0f, off"));
#endif

#if PLATFORM_MAC
static ns::AutoReleased<ns::Object<id <NSObject>>> GMetalDeviceObserver;
static mtlpp::Device GetMTLDevice(uint32& DeviceIndex)
{
	SCOPED_AUTORELEASE_POOL;
	
	DeviceIndex = 0;
	
	ns::Array<mtlpp::Device> DeviceList;
	
	if (FPlatformMisc::MacOSXVersionCompare(10, 13, 4) >= 0)
	{
			DeviceList = mtlpp::Device::CopyAllDevicesWithObserver(GMetalDeviceObserver, ^(const mtlpp::Device & Device, const ns::String & Notification)
			{
				if ([Notification.GetPtr() isEqualToString:MTLDeviceWasAddedNotification])
				{
					FPlatformMisc::GPUChangeNotification(Device.GetRegistryID(), FPlatformMisc::EMacGPUNotification::Added);
				}
				else if ([Notification.GetPtr() isEqualToString:MTLDeviceRemovalRequestedNotification])
				{
					FPlatformMisc::GPUChangeNotification(Device.GetRegistryID(), FPlatformMisc::EMacGPUNotification::RemovalRequested);
				}
				else if ([Notification.GetPtr() isEqualToString:MTLDeviceWasRemovedNotification])
				{
					FPlatformMisc::GPUChangeNotification(Device.GetRegistryID(), FPlatformMisc::EMacGPUNotification::Removed);
				}
			});
	}
	else
	{
		DeviceList = mtlpp::Device::CopyAllDevices();
	}
	
	const int32 NumDevices = DeviceList.GetSize();
	
	TArray<FMacPlatformMisc::FGPUDescriptor> const& GPUs = FPlatformMisc::GetGPUDescriptors();
	check(GPUs.Num() > 0);

	// @TODO  here, GetGraphicsAdapterLuid() is used as a device index (how the function "GetGraphicsAdapter" used to work)
	//        eventually we want the HMD module to return the MTLDevice's registryID, but we cannot fully handle that until
	//        we drop support for 10.12
	//  NOTE: this means any implementation of GetGraphicsAdapterLuid() for Mac should return an index, and use -1 as a 
	//        sentinel value representing "no device" (instead of 0, which is used in the LUID case)
	int32 HmdGraphicsAdapter  = IHeadMountedDisplayModule::IsAvailable() ? (int32)IHeadMountedDisplayModule::Get().GetGraphicsAdapterLuid() : -1;
 	int32 OverrideRendererId = FPlatformMisc::GetExplicitRendererIndex();
	
	int32 ExplicitRendererId = OverrideRendererId >= 0 ? OverrideRendererId : HmdGraphicsAdapter;
	if(ExplicitRendererId < 0 && GPUs.Num() > 1 && FMacPlatformMisc::MacOSXVersionCompare(10, 11, 5) == 0)
	{
		OverrideRendererId = -1;
		bool bForceExplicitRendererId = false;
		for(uint32 i = 0; i < GPUs.Num(); i++)
		{
			FMacPlatformMisc::FGPUDescriptor const& GPU = GPUs[i];
			if((GPU.GPUVendorId == 0x10DE))
			{
				OverrideRendererId = i;
				bForceExplicitRendererId = (GPU.GPUMetalBundle && ![GPU.GPUMetalBundle isEqualToString:@"GeForceMTLDriverWeb"]);
			}
			else if(!GPU.GPUHeadless && GPU.GPUVendorId != 0x8086)
			{
				OverrideRendererId = i;
			}
		}
		if (bForceExplicitRendererId)
		{
			ExplicitRendererId = OverrideRendererId;
		}
	}
	
	mtlpp::Device SelectedDevice;
	if (ExplicitRendererId >= 0 && ExplicitRendererId < GPUs.Num())
	{
		FMacPlatformMisc::FGPUDescriptor const& GPU = GPUs[ExplicitRendererId];
		TArray<FString> NameComponents;
		FString(GPU.GPUName).TrimStart().ParseIntoArray(NameComponents, TEXT(" "));	
		for (uint32 index = 0; index < NumDevices; index++)
		{
			mtlpp::Device Device = DeviceList[index];
			
			if(MTLPP_CHECK_AVAILABLE(10.13, 11.0, 11.0) && (Device.GetRegistryID() == GPU.RegistryID))
			{
				DeviceIndex = ExplicitRendererId;
				SelectedDevice = Device;
			}
			else if(([Device.GetName().GetPtr() rangeOfString:@"Nvidia" options:NSCaseInsensitiveSearch].location != NSNotFound && GPU.GPUVendorId == 0x10DE)
			   || ([Device.GetName().GetPtr() rangeOfString:@"AMD" options:NSCaseInsensitiveSearch].location != NSNotFound && GPU.GPUVendorId == 0x1002)
			   || ([Device.GetName().GetPtr() rangeOfString:@"Intel" options:NSCaseInsensitiveSearch].location != NSNotFound && GPU.GPUVendorId == 0x8086))
			{
				bool bMatchesName = (NameComponents.Num() > 0);
				for (FString& Component : NameComponents)
				{
					bMatchesName &= FString(Device.GetName().GetPtr()).Contains(Component);
				}
				if((Device.IsHeadless() == GPU.GPUHeadless || GPU.GPUVendorId != 0x1002) && bMatchesName)
                {
					DeviceIndex = ExplicitRendererId;
					SelectedDevice = Device;
					break;
				}
			}
		}
		if(!SelectedDevice)
		{
			UE_LOG(LogMetal, Warning,  TEXT("Couldn't find Metal device to match GPU descriptor (%s) from IORegistry - using default device."), *FString(GPU.GPUName));
		}
	}
	if (SelectedDevice == nil)
	{
		TArray<FString> NameComponents;
		SelectedDevice = mtlpp::Device::CreateSystemDefaultDevice();
		bool bFoundDefault = false;
		for (uint32 i = 0; i < GPUs.Num(); i++)
		{
			FMacPlatformMisc::FGPUDescriptor const& GPU = GPUs[i];
			if(MTLPP_CHECK_AVAILABLE(10.13, 11.0, 11.0) && (SelectedDevice.GetRegistryID() == GPU.RegistryID))
			{
				DeviceIndex = i;
				bFoundDefault = true;
				break;
			}
			else if(([SelectedDevice.GetName().GetPtr() rangeOfString:@"Nvidia" options:NSCaseInsensitiveSearch].location != NSNotFound && GPU.GPUVendorId == 0x10DE)
					|| ([SelectedDevice.GetName().GetPtr() rangeOfString:@"AMD" options:NSCaseInsensitiveSearch].location != NSNotFound && GPU.GPUVendorId == 0x1002)
					|| ([SelectedDevice.GetName().GetPtr() rangeOfString:@"Intel" options:NSCaseInsensitiveSearch].location != NSNotFound && GPU.GPUVendorId == 0x8086))
			{
				NameComponents.Empty();
				bool bMatchesName = FString(GPU.GPUName).TrimStart().ParseIntoArray(NameComponents, TEXT(" ")) > 0;
				for (FString& Component : NameComponents)
				{
					bMatchesName &= FString(SelectedDevice.GetName().GetPtr()).Contains(Component);
				}
				if((SelectedDevice.IsHeadless() == GPU.GPUHeadless || GPU.GPUVendorId != 0x1002) && bMatchesName)
                {
					DeviceIndex = i;
					bFoundDefault = true;
					break;
				}
			}
		}
		if(!bFoundDefault)
		{
			UE_LOG(LogMetal, Warning,  TEXT("Couldn't find Metal device %s in GPU descriptors from IORegistry - capability reporting may be wrong."), *FString(SelectedDevice.GetName().GetPtr()));
		}
	}
	return SelectedDevice;
}

mtlpp::PrimitiveTopologyClass TranslatePrimitiveTopology(uint32 PrimitiveType)
{
	switch (PrimitiveType)
	{
		case PT_TriangleList:
		case PT_TriangleStrip:
			return mtlpp::PrimitiveTopologyClass::Triangle;
		case PT_LineList:
			return mtlpp::PrimitiveTopologyClass::Line;
		case PT_PointList:
			return mtlpp::PrimitiveTopologyClass::Point;
		case PT_1_ControlPointPatchList:
		case PT_2_ControlPointPatchList:
		case PT_3_ControlPointPatchList:
		case PT_4_ControlPointPatchList:
		case PT_5_ControlPointPatchList:
		case PT_6_ControlPointPatchList:
		case PT_7_ControlPointPatchList:
		case PT_8_ControlPointPatchList:
		case PT_9_ControlPointPatchList:
		case PT_10_ControlPointPatchList:
		case PT_11_ControlPointPatchList:
		case PT_12_ControlPointPatchList:
		case PT_13_ControlPointPatchList:
		case PT_14_ControlPointPatchList:
		case PT_15_ControlPointPatchList:
		case PT_16_ControlPointPatchList:
		case PT_17_ControlPointPatchList:
		case PT_18_ControlPointPatchList:
		case PT_19_ControlPointPatchList:
		case PT_20_ControlPointPatchList:
		case PT_21_ControlPointPatchList:
		case PT_22_ControlPointPatchList:
		case PT_23_ControlPointPatchList:
		case PT_24_ControlPointPatchList:
		case PT_25_ControlPointPatchList:
		case PT_26_ControlPointPatchList:
		case PT_27_ControlPointPatchList:
		case PT_28_ControlPointPatchList:
		case PT_29_ControlPointPatchList:
		case PT_30_ControlPointPatchList:
		case PT_31_ControlPointPatchList:
		case PT_32_ControlPointPatchList:
		{
			return mtlpp::PrimitiveTopologyClass::Triangle;
		}
		default:
			UE_LOG(LogMetal, Fatal, TEXT("Unsupported primitive topology %d"), (int32)PrimitiveType);
			return mtlpp::PrimitiveTopologyClass::Triangle;
	}
}
#endif

FMetalDeviceContext* FMetalDeviceContext::CreateDeviceContext()
{
	uint32 DeviceIndex = 0;
#if PLATFORM_IOS
	mtlpp::Device Device = mtlpp::Device([IOSAppDelegate GetDelegate].IOSView->MetalDevice);
#else
	mtlpp::Device Device = GetMTLDevice(DeviceIndex);
	if (!Device)
	{
		FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, TEXT("The graphics card in this Mac appears to erroneously report support for Metal graphics technology, which is required to run this application, but failed to create a Metal device. The application will now exit."), TEXT("Failed to initialize Metal"));
		exit(0);
	}
#endif
	
	uint32 MetalDebug = GMetalRuntimeDebugLevel;
	const bool bOverridesMetalDebug = FParse::Value( FCommandLine::Get(), TEXT( "MetalRuntimeDebugLevel=" ), MetalDebug );
	if (bOverridesMetalDebug)
	{
		GMetalRuntimeDebugLevel = MetalDebug;
	}
	
	MTLPP_VALIDATION(mtlpp::ValidatedDevice::Register(Device));
	
	FMetalCommandQueue* Queue = new FMetalCommandQueue(Device, GMetalCommandQueueSize);
	check(Queue);
	
	return new FMetalDeviceContext(Device, DeviceIndex, Queue);
}

FMetalDeviceContext::FMetalDeviceContext(mtlpp::Device MetalDevice, uint32 InDeviceIndex, FMetalCommandQueue* Queue)
: FMetalContext(MetalDevice, *Queue, true)
, DeviceIndex(InDeviceIndex)
, CaptureManager(MetalDevice.GetPtr(), *Queue)
, SceneFrameCounter(0)
, FrameCounter(0)
, ActiveContexts(1)
{
	CommandQueue.SetRuntimeDebuggingLevel(GMetalRuntimeDebugLevel);
	
	// If the separate present thread is enabled then an intermediate backbuffer is required
	check(!GMetalSeparatePresentThread || GMetalSupportsIntermediateBackBuffer);
	
	// Hook into the ios framepacer, if it's enabled for this platform.
	FrameReadyEvent = NULL;
	if( FPlatformRHIFramePacer::IsEnabled() || GMetalSeparatePresentThread )
	{
		FrameReadyEvent = FPlatformProcess::GetSynchEventFromPool();
		FPlatformRHIFramePacer::InitWithEvent( FrameReadyEvent );
		
		// A bit dirty - this allows the present frame pacing to match the CPU pacing by default unless you've overridden it with the CVar
		// In all likelihood the CVar is only useful for debugging.
		if (GMetalPresentFramePacing <= 0.0f)
		{
			FString FrameRateLockAsEnum;
			GConfig->GetString(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("FrameRateLock"), FrameRateLockAsEnum, GEngineIni);
	
			uint32 FrameRateLock = 0;
			FParse::Value(*FrameRateLockAsEnum, TEXT("PUFRL_"), FrameRateLock);
			if (FrameRateLock > 0)
			{
				GMetalPresentFramePacing = (float)FrameRateLock;
			}
		}
	}
	
	if (FParse::Param(FCommandLine::Get(), TEXT("MetalIntermediateBackBuffer")) || FParse::Param(FCommandLine::Get(), TEXT("MetalOffscreenOnly")))
	{
		GMetalSupportsIntermediateBackBuffer = 1;
	}
	
	METAL_GPUPROFILE(FMetalProfiler::CreateProfiler(this));
	
	InitFrame(true);
}

FMetalDeviceContext::~FMetalDeviceContext()
{
	SubmitCommandsHint(EMetalSubmitFlagsWaitOnCommandBuffer);
	delete &(GetCommandQueue());
	
#if PLATFORM_MAC
	if (FPlatformMisc::MacOSXVersionCompare(10, 13, 4) >= 0)
	{
		mtlpp::Device::RemoveDeviceObserver(GMetalDeviceObserver);
	}
#endif
}

void FMetalDeviceContext::Init(void)
{
	Heap.Init(GetCommandQueue());
}

void FMetalDeviceContext::BeginFrame()
{
#if ENABLE_METAL_GPUPROFILE
	FPlatformTLS::SetTlsValue(CurrentContextTLSSlot, this);
#endif
}

#if METAL_DEBUG_OPTIONS
void FMetalDeviceContext::ScribbleBuffer(FMetalBuffer& Buffer)
{
	static uint8 Fill = 0;
	if (Buffer.GetStorageMode() != mtlpp::StorageMode::Private)
	{
		FMemory::Memset(Buffer.GetContents(), Fill++, Buffer.GetLength());
#if PLATFORM_MAC
		if (Buffer.GetStorageMode() == mtlpp::StorageMode::Managed)
		{
			Buffer.DidModify(ns::Range(0, Buffer.GetLength()));
		}
#endif
	}
	else
	{
		FillBuffer(Buffer, ns::Range(0, Buffer.GetLength()), Fill++);
	}
}
#endif

void FMetalDeviceContext::ClearFreeList()
{
	static bool bSupportsHeaps = SupportsFeature(EMetalFeaturesHeaps);
	uint32 Index = 0;
	while(Index < DelayedFreeLists.Num())
	{
		FMetalDelayedFreeList* Pair = DelayedFreeLists[Index];
		if(METAL_DEBUG_OPTION(Pair->DeferCount-- <= 0 &&) Pair->IsComplete())
		{
			for( id Entry : Pair->ObjectFreeList )
			{
				[Entry release];
			}
			for ( FMetalBuffer& Buffer : Pair->UsedBuffers )
			{
#if METAL_DEBUG_OPTIONS
				if (GMetalBufferScribble)
				{
					ScribbleBuffer(Buffer);
				}
				if (GMetalResourcePurgeOnDelete && !Buffer.GetHeap() && !Buffer.GetParentBuffer())
				{
					Buffer.SetPurgeableState(mtlpp::PurgeableState::Empty);
				}
#endif
				Heap.ReleaseBuffer(Buffer);
			}
			for ( FMetalTexture& Texture : Pair->UsedTextures )
			{
				if (!(Texture.GetBuffer() || Texture.GetParentTexture()))
				{
#if METAL_DEBUG_OPTIONS
					if (GMetalResourcePurgeOnDelete && !Texture.GetHeap())
					{
						Texture.SetPurgeableState(mtlpp::PurgeableState::Empty);
					}
#endif
					Heap.ReleaseTexture(nullptr, Texture);
				}
			}
			delete Pair;
			DelayedFreeLists.RemoveAt(Index, 1, false);
		}
		else
		{
			Index++;
		}
	}
}

void FMetalDeviceContext::DrainHeap()
{
	Heap.Compact(false);
}

void FMetalDeviceContext::EndFrame()
{
	Heap.Compact(false);
	
	FlushFreeList();
	
	ClearFreeList();
	
	// A 'frame' in this context is from the beginning of encoding on the CPU
	// to the end of all rendering operations on the GPU. So the semaphore is
	// signalled when the last command buffer finishes GPU execution.
	{
		dispatch_semaphore_t CmdBufferSemaphore = CommandBufferSemaphore;
		dispatch_retain(CmdBufferSemaphore);
		
		RenderPass.AddCompletionHandler(
		[CmdBufferSemaphore](mtlpp::CommandBuffer const& cmd_buf)
		{
			dispatch_semaphore_signal(CmdBufferSemaphore);
			dispatch_release(CmdBufferSemaphore);
		});
	}
	
	if (bPresented)
	{
		FMetalGPUProfiler::IncrementFrameIndex();
		CaptureManager.PresentFrame(FrameCounter++);
		bPresented = false;
	}
	
	// Latched update of whether to use runtime debugging features
	uint32 SubmitFlags = EMetalSubmitFlagsResetState;
#if METAL_DEBUG_OPTIONS
	if (GMetalRuntimeDebugLevel != CommandQueue.GetRuntimeDebuggingLevel())
	{
		CommandQueue.SetRuntimeDebuggingLevel(GMetalRuntimeDebugLevel);
		
		// After change the debug features level wait on commit
		SubmitFlags |= EMetalSubmitFlagsWaitOnCommandBuffer;
	}
#endif
	SubmitCommandsHint((uint32)SubmitFlags);
	
	InitFrame(true);
}

void FMetalDeviceContext::BeginScene()
{
#if ENABLE_METAL_GPUPROFILE
	FPlatformTLS::SetTlsValue(CurrentContextTLSSlot, this);
#endif
	
	// Increment the frame counter. INDEX_NONE is a special value meaning "uninitialized", so if
	// we hit it just wrap around to zero.
	SceneFrameCounter++;
	if (SceneFrameCounter == INDEX_NONE)
	{
		SceneFrameCounter++;
	}
}

void FMetalDeviceContext::EndScene()
{
}

void FMetalDeviceContext::BeginDrawingViewport(FMetalViewport* Viewport)
{
#if ENABLE_METAL_GPUPROFILE
	FPlatformTLS::SetTlsValue(CurrentContextTLSSlot, this);
#endif
}

bool FMetalDeviceContext::FMetalDelayedFreeList::IsComplete() const
{
	bool bFinished = true;
	for (mtlpp::CommandBufferFence const& Fence : Fences)
	{
		bFinished &= Fence.Wait(0);

		if (!bFinished)
			break;
	}
	return bFinished;
}

void FMetalDeviceContext::FlushFreeList()
{
	static bool bSupportsHeaps = SupportsFeature(EMetalFeaturesHeaps);
	FMetalDelayedFreeList* NewList = new FMetalDelayedFreeList;
	
	// Get the committed command buffer fences and clear the array in the command-queue
	GetCommandQueue().GetCommittedCommandBufferFences(NewList->Fences);
	
	METAL_DEBUG_OPTION(NewList->DeferCount = GMetalResourceDeferDeleteNumFrames);
	FreeListMutex.Lock();
	NewList->UsedBuffers = MoveTemp(UsedBuffers);
	NewList->UsedTextures = MoveTemp(UsedTextures);
	NewList->ObjectFreeList = ObjectFreeList;
#if METAL_DEBUG_OPTIONS
	if (FrameFences.Num())
	{
		FrameFences.Empty();
	}
#endif
	ObjectFreeList.Empty(ObjectFreeList.Num());
	FreeListMutex.Unlock();
	
	DelayedFreeLists.Add(NewList);
}

void FMetalDeviceContext::EndDrawingViewport(FMetalViewport* Viewport, bool bPresent, bool bLockToVsync)
{
	// enqueue a present if desired
	static bool const bOffscreenOnly = FParse::Param(FCommandLine::Get(), TEXT("MetalOffscreenOnly"));
	if (bPresent && !bOffscreenOnly)
	{
		
#if PLATFORM_MAC
		// Handle custom present
		FRHICustomPresent* const CustomPresent = Viewport->GetCustomPresent();
		if (CustomPresent != nullptr)
		{
			int32 SyncInterval = 0;
			{
				SCOPE_CYCLE_COUNTER(STAT_MetalCustomPresentTime);
				CustomPresent->Present(SyncInterval);
			}
			
			mtlpp::CommandBuffer CurrentCommandBuffer = GetCurrentCommandBuffer();
			check(CurrentCommandBuffer);
			
			CurrentCommandBuffer.AddScheduledHandler([CustomPresent](mtlpp::CommandBuffer const&) {
				CustomPresent->PostPresent();
			});
		}
#endif
		
		RenderPass.End();
		
		mtlpp::CommandBuffer CB = GetCurrentCommandBuffer();
		FMetalGPUProfiler::RecordFrame(CB);
		
		SubmitCommandsHint(EMetalSubmitFlagsCreateCommandBuffer);
		
		Viewport->Present(GetCommandQueue(), bLockToVsync);
	}
	
	bPresented = bPresent;
	
	// We may be limiting our framerate to the display link
	if( FrameReadyEvent != nullptr && !GMetalSeparatePresentThread )
	{
		FrameReadyEvent->Wait();
	}
	
	Viewport->ReleaseDrawable();
}

void FMetalDeviceContext::ReleaseObject(id Object)
{
	if (GIsMetalInitialized) // @todo zebra: there seems to be some race condition at exit when the framerate is very low
	{
		check(Object);
		FreeListMutex.Lock();
		if(!ObjectFreeList.Contains(Object))
        {
            ObjectFreeList.Add(Object);
        }
        else
        {
            [Object release];
        }
		FreeListMutex.Unlock();
	}
}

void FMetalDeviceContext::ReleaseTexture(FMetalSurface* Surface, FMetalTexture& Texture)
{
	if (GIsMetalInitialized) // @todo zebra: there seems to be some race condition at exit when the framerate is very low
	{
		check(Surface && Texture);
		ReleaseTexture(Texture);
	}
}

void FMetalDeviceContext::ReleaseTexture(FMetalTexture& Texture)
{
	if(GIsMetalInitialized)
	{
		check(Texture);
		FreeListMutex.Lock();
		if(!UsedTextures.Contains(Texture))
		{
			UsedTextures.Add(MoveTemp(Texture));
		}
		FreeListMutex.Unlock();
	}
}

void FMetalDeviceContext::ReleaseFence(mtlpp::Fence Fence)
{
#if METAL_DEBUG_OPTIONS
	if(GetCommandList().GetCommandQueue().GetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation)
	{
		FScopeLock Lock(&FreeListMutex);
		FrameFences.Add(Fence);
	}
#endif
	
	ReleaseObject([Fence.GetPtr() retain]);
}

FMetalTexture FMetalDeviceContext::CreateTexture(FMetalSurface* Surface, mtlpp::TextureDescriptor Descriptor)
{
	FMetalTexture Tex = Heap.CreateTexture(Descriptor, Surface);
#if METAL_DEBUG_OPTIONS
	static bool bSupportsHeaps = SupportsFeature(EMetalFeaturesHeaps);
	if (GMetalResourcePurgeOnDelete && !Tex.GetHeap())
	{
		Tex.SetPurgeableState(mtlpp::PurgeableState::NonVolatile);
	}
#endif
	
	return Tex;
}

FMetalBuffer FMetalDeviceContext::CreatePooledBuffer(FMetalPooledBufferArgs const& Args)
{
	FMetalBuffer Buffer = Heap.CreateBuffer(Args.Size, BufferOffsetAlignment, GetCommandQueue().GetCompatibleResourceOptions((mtlpp::ResourceOptions)(BUFFER_CACHE_MODE | mtlpp::ResourceOptions::HazardTrackingModeUntracked | ((NSUInteger)Args.Storage << mtlpp::ResourceStorageModeShift))));
	check(Buffer && Buffer.GetPtr());
#if METAL_DEBUG_OPTIONS
	static bool bSupportsHeaps = SupportsFeature(EMetalFeaturesHeaps);
	if (GMetalResourcePurgeOnDelete && !Buffer.GetHeap())
	{
		Buffer.SetPurgeableState(mtlpp::PurgeableState::NonVolatile);
	}
	if (GMetalBufferZeroFill && Args.Storage != mtlpp::StorageMode::Private)
	{
		FMemory::Memset(Buffer.GetContents(), 0x0, Buffer.GetLength());
	}
#endif
	
	return Buffer;
}

void FMetalDeviceContext::ReleaseBuffer(FMetalBuffer& Buffer)
{
	if(GIsMetalInitialized)
	{
		check(Buffer);
		FreeListMutex.Lock();
		if(!UsedBuffers.Contains(Buffer))
		{
			UsedBuffers.Add(MoveTemp(Buffer));
		}
		FreeListMutex.Unlock();
	}
}

struct FMetalRHICommandUpdateFence final : public FRHICommand<FMetalRHICommandUpdateFence>
{
	enum EMode
	{
		Start,
		End
	};
	
	FMetalDeviceContext* Context;
	FMetalFence Fence;
	EMode Mode;
	
	FORCEINLINE_DEBUGGABLE FMetalRHICommandUpdateFence(FMetalDeviceContext* DeviceContext, FMetalFence const& InFence, EMode InMode)
	: Context(DeviceContext)
	, Fence(InFence)
	, Mode(InMode)
	{
	}
	
	void Execute(FRHICommandListBase& CmdList)
	{
		check(Context);
		if (Mode == Start)
		{
			if(!Context->GetCurrentCommandBuffer())
			{
				Context->SetParallelPassFences(Fence, nil);
				Context->InitFrame(false);
			}
			else
			{
				Context->GetCurrentRenderPass().Wait(Fence);
			}
		}
		else if (Mode == End)
		{
			Context->SetParallelPassFences(nil, Fence);
			Context->FinishFrame();
		}
	}
};

FMetalRHICommandContext* FMetalDeviceContext::AcquireContext(int32 NewIndex, int32 NewNum)
{
	FMetalRHICommandContext* Context = ParallelContexts.Pop();
	if (!Context)
	{
		FMetalContext* MetalContext = new FMetalContext(GetDevice(), GetCommandQueue(), false);
		check(MetalContext);
		
		FMetalRHICommandContext* CmdContext = static_cast<FMetalRHICommandContext*>(RHIGetDefaultContext());
		check(CmdContext);
		
		Context = new FMetalRHICommandContext(CmdContext->GetProfiler(), MetalContext);
	}
	check(Context);
	
	if(ParallelFences.Num() < NewNum)
	{
		ParallelFences.AddDefaulted(NewNum - ParallelFences.Num());
	}
	
	NSString* StartLabel = nil;
	NSString* EndLabel = nil;
#if METAL_DEBUG_OPTIONS
	StartLabel = [NSString stringWithFormat:@"Start Parallel Context Index %d Num %d", NewIndex, NewNum];
	EndLabel = [NSString stringWithFormat:@"End Parallel Context Index %d Num %d", NewIndex, NewNum];
#endif
	
	FMetalFence StartFence(NewIndex == 0 ? CommandList.GetCommandQueue().CreateFence(StartLabel) : ParallelFences[NewIndex - 1]);
	FMetalFence EndFence(CommandList.GetCommandQueue().CreateFence(EndLabel));
	ParallelFences[NewIndex] = EndFence;
	
	// Give the context the fences so that we can properly order the parallel contexts.
	Context->GetInternalContext().SetParallelPassFences(StartFence, EndFence);
	
	if (NewIndex == 0)
	{
		if (FRHICommandListExecutor::GetImmediateCommandList().Bypass() || !IsRunningRHIInSeparateThread())
		{
			FMetalRHICommandUpdateFence UpdateCommand(this, StartFence, FMetalRHICommandUpdateFence::End);
			UpdateCommand.Execute(FRHICommandListExecutor::GetImmediateCommandList());
		}
		else
		{
			new (FRHICommandListExecutor::GetImmediateCommandList().AllocCommand<FMetalRHICommandUpdateFence>()) FMetalRHICommandUpdateFence(this, StartFence, FMetalRHICommandUpdateFence::End);
		}
	}
	
	if (FRHICommandListExecutor::GetImmediateCommandList().Bypass() || !IsRunningRHIInSeparateThread())
	{
		FMetalRHICommandUpdateFence UpdateCommand(this, EndFence, FMetalRHICommandUpdateFence::Start);
		UpdateCommand.Execute(FRHICommandListExecutor::GetImmediateCommandList());
	}
	else
	{
		new (FRHICommandListExecutor::GetImmediateCommandList().AllocCommand<FMetalRHICommandUpdateFence>()) FMetalRHICommandUpdateFence(this, EndFence, FMetalRHICommandUpdateFence::Start);
	}
	
	FPlatformAtomics::InterlockedIncrement(&ActiveContexts);
	return Context;
}

void FMetalDeviceContext::ReleaseContext(FMetalRHICommandContext* Context)
{
	check(!Context->GetInternalContext().GetCurrentCommandBuffer());
	
	ParallelContexts.Push(Context);
	FPlatformAtomics::InterlockedDecrement(&ActiveContexts);
	check(ActiveContexts >= 1);
}

uint32 FMetalDeviceContext::GetNumActiveContexts(void) const
{
	return ActiveContexts;
}

uint32 FMetalDeviceContext::GetDeviceIndex(void) const
{
	return DeviceIndex;
}

#if METAL_DEBUG_OPTIONS
void FMetalDeviceContext::AddActiveBuffer(FMetalBuffer const& Buffer)
{
    if(GetCommandList().GetCommandQueue().GetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation)
    {
        FScopeLock Lock(&ActiveBuffersMutex);
        
        NSRange DestRange = NSMakeRange(Buffer.GetOffset(), Buffer.GetLength());
        TArray<NSRange>* Ranges = ActiveBuffers.Find(Buffer.GetPtr());
        if (!Ranges)
        {
            ActiveBuffers.Add(Buffer.GetPtr(), TArray<NSRange>());
            Ranges = ActiveBuffers.Find(Buffer.GetPtr());
        }
        Ranges->Add(DestRange);
    }
}

static bool operator==(NSRange const& A, NSRange const& B)
{
    return NSEqualRanges(A, B);
}

void FMetalDeviceContext::RemoveActiveBuffer(FMetalBuffer const& Buffer)
{
    if(GetCommandList().GetCommandQueue().GetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation)
    {
        FScopeLock Lock(&ActiveBuffersMutex);
        
        NSRange DestRange = NSMakeRange(Buffer.GetOffset(), Buffer.GetLength());
        TArray<NSRange>& Ranges = ActiveBuffers.FindChecked(Buffer.GetPtr());
        int32 i = Ranges.RemoveSingle(DestRange);
        check(i > 0);
    }
}

bool FMetalDeviceContext::ValidateIsInactiveBuffer(FMetalBuffer const& Buffer)
{
    if(GetCommandList().GetCommandQueue().GetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation)
    {
        FScopeLock Lock(&ActiveBuffersMutex);
        
        TArray<NSRange>* Ranges = ActiveBuffers.Find(Buffer.GetPtr());
        if (Ranges)
        {
            NSRange DestRange = NSMakeRange(Buffer.GetOffset(), Buffer.GetLength());
            for (NSRange Range : *Ranges)
            {
                if (NSIntersectionRange(Range, DestRange).length > 0)
                {
                    UE_LOG(LogMetal, Error, TEXT("ValidateIsInactiveBuffer failed on overlapping ranges ({%d, %d} vs {%d, %d}) of buffer %p."), (uint32)Range.location, (uint32)Range.length, (uint32)Buffer.GetOffset(), (uint32)Buffer.GetLength(), Buffer.GetPtr());
                    return false;
                }
            }
        }
    }
    return true;
}
#endif


#if ENABLE_METAL_GPUPROFILE
uint32 FMetalContext::CurrentContextTLSSlot = FPlatformTLS::AllocTlsSlot();
#endif

FMetalContext::FMetalContext(mtlpp::Device InDevice, FMetalCommandQueue& Queue, bool const bIsImmediate)
: Device(InDevice)
, CommandQueue(Queue)
, CommandList(Queue, bIsImmediate)
, StateCache(bIsImmediate)
, RenderPass(CommandList, StateCache)
, QueryBuffer(new FMetalQueryBufferPool(this))
, StartFence(nil)
, EndFence(nil)
{
	// create a semaphore for multi-buffering the command buffer
	CommandBufferSemaphore = dispatch_semaphore_create(FParse::Param(FCommandLine::Get(),TEXT("gpulockstep")) ? 1 : 3);
}

FMetalContext::~FMetalContext()
{
	SubmitCommandsHint(EMetalSubmitFlagsWaitOnCommandBuffer);
}

mtlpp::Device& FMetalContext::GetDevice()
{
	return Device;
}

FMetalCommandQueue& FMetalContext::GetCommandQueue()
{
	return CommandQueue;
}

FMetalCommandList& FMetalContext::GetCommandList()
{
	return CommandList;
}

mtlpp::CommandBuffer const& FMetalContext::GetCurrentCommandBuffer() const
{
	return RenderPass.GetCurrentCommandBuffer();
}

mtlpp::CommandBuffer& FMetalContext::GetCurrentCommandBuffer()
{
	return RenderPass.GetCurrentCommandBuffer();
}

void FMetalContext::InsertCommandBufferFence(FMetalCommandBufferFence& Fence, mtlpp::CommandBufferHandler Handler)
{
	check(GetCurrentCommandBuffer());
	
	RenderPass.InsertCommandBufferFence(Fence, Handler);
}

#if ENABLE_METAL_GPUPROFILE
FMetalContext* FMetalContext::GetCurrentContext()
{
	FMetalContext* Current = (FMetalContext*)FPlatformTLS::GetTlsValue(CurrentContextTLSSlot);
	check(Current);
	return Current;
}

void FMetalContext::MakeCurrent(FMetalContext* Context)
{
	FPlatformTLS::SetTlsValue(CurrentContextTLSSlot, Context);
}
#endif

void FMetalContext::SetParallelPassFences(mtlpp::Fence Start, mtlpp::Fence End)
{
	check(!StartFence.IsValid() && !EndFence.IsValid());
	StartFence = Start;
	EndFence = End;
}

void FMetalContext::InitFrame(bool const bImmediateContext)
{
#if ENABLE_METAL_GPUPROFILE
	FPlatformTLS::SetTlsValue(CurrentContextTLSSlot, this);
#endif
	
	// Reset cached state in the encoder
	StateCache.Reset();
	
	// Wait for the frame semaphore on the immediate context.
	if (bImmediateContext)
	{
		dispatch_semaphore_wait(CommandBufferSemaphore, DISPATCH_TIME_FOREVER);
	}
	
	// Reallocate if necessary to ensure >= 80% usage, otherwise we're just too wasteful
	// RenderPass.GetRingBuffer().Shrink();
	
	// Begin the render pass frame.
	RenderPass.Begin(StartFence);
	
	// Unset the start fence, the render-pass owns it and we can consider it encoded now!
	StartFence.Reset();
	
	// make sure first SetRenderTarget goes through
	StateCache.InvalidateRenderTargets();
}

void FMetalContext::FinishFrame()
{
	// Ensure that we update the end fence for parallel contexts.
	RenderPass.Update(EndFence);
	
	// Unset the end fence, the render-pass owns it and we can consider it encoded now!
	EndFence.Reset();
	
	// End the render pass
	RenderPass.End();
	
	// Issue any outstanding commands.
	SubmitCommandsHint(EMetalSubmitFlagsNone);
	
	// make sure first SetRenderTarget goes through
	StateCache.InvalidateRenderTargets();
	
#if ENABLE_METAL_GPUPROFILE
	FPlatformTLS::SetTlsValue(CurrentContextTLSSlot, nullptr);
#endif
}

void FMetalContext::SubmitCommandsHint(uint32 const Flags)
{
	// When the command-buffer is submitted for a reason other than a break of a logical command-buffer (where one high-level command-sequence becomes more than one command-buffer).
	if (!(Flags & EMetalSubmitFlagsBreakCommandBuffer))
	{
		// Release the current query buffer if there are outstanding writes so that it isn't transitioned by a future encoder that will cause a resource access conflict and lifetime error.
		GetQueryBufferPool()->ReleaseCurrentQueryBuffer();
	}
	
	RenderPass.Submit((EMetalSubmitFlags)Flags);
}

void FMetalContext::SubmitCommandBufferAndWait()
{
	// kick the whole buffer
	// Commit to hand the commandbuffer off to the gpu
	// Wait for completion as requested.
	SubmitCommandsHint((EMetalSubmitFlagsCreateCommandBuffer | EMetalSubmitFlagsBreakCommandBuffer | EMetalSubmitFlagsWaitOnCommandBuffer));
}

void FMetalContext::ResetRenderCommandEncoder()
{
	SubmitCommandsHint();
	
	StateCache.InvalidateRenderTargets();
	
	SetRenderTargetsInfo(StateCache.GetRenderTargetsInfo(), true);
}

bool FMetalContext::PrepareToDraw(uint32 PrimitiveType, EMetalIndexType IndexType)
{
	SCOPE_CYCLE_COUNTER(STAT_MetalPrepareDrawTime);
	TRefCountPtr<FMetalGraphicsPipelineState> CurrentPSO = StateCache.GetGraphicsPSO();
	check(IsValidRef(CurrentPSO));
	
	// Enforce calls to SetRenderTarget prior to issuing draw calls.
#if PLATFORM_MAC
	if(!FShaderCache::IsPredrawCall(StateCache.GetShaderCacheStateObject()))
	{
		check(StateCache.GetHasValidRenderTarget());
	}
#else
	if (!StateCache.GetHasValidRenderTarget())
	{
		return false;
	}
#endif
	
	FMetalHashedVertexDescriptor const& VertexDesc = CurrentPSO->VertexDeclaration->Layout;
	
	// Validate the vertex layout in debug mode, or when the validation layer is enabled for development builds.
	// Other builds will just crash & burn if it is incorrect.
#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
	if(CommandQueue.GetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation)
	{
		MTLVertexDescriptor* Layout = VertexDesc.VertexDesc;
		
		if(Layout && Layout.layouts)
		{
			for (uint32 i = 0; i < MaxVertexElementCount; i++)
			{
				auto Attribute = [Layout.attributes objectAtIndexedSubscript:i];
				if(Attribute && Attribute.format > MTLVertexFormatInvalid)
				{
					auto BufferLayout = [Layout.layouts objectAtIndexedSubscript:Attribute.bufferIndex];
					uint32 BufferLayoutStride = BufferLayout ? BufferLayout.stride : 0;
					
					uint32 BufferIndex = METAL_TO_UNREAL_BUFFER_INDEX(Attribute.bufferIndex);
					
					uint32 InOutMask = CurrentPSO->VertexShader->Bindings.InOutMask;
					if (InOutMask & (1 << BufferIndex))
					{
						uint64 MetalSize = StateCache.GetVertexBufferSize(BufferIndex);
						
						// If the vertex attribute is required and either no Metal buffer is bound or the size of the buffer is smaller than the stride, or the stride is explicitly specified incorrectly then the layouts don't match.
						if (BufferLayoutStride > 0 && MetalSize < BufferLayoutStride)
						{
							FString Report = FString::Printf(TEXT("Vertex Layout Mismatch: Index: %d, Len: %lld, Decl. Stride: %d"), Attribute.bufferIndex, MetalSize, BufferLayoutStride);
							UE_LOG(LogMetal, Warning, TEXT("%s"), *Report);
						}
					}
				}
			}
		}
	}
#endif
	
	// @todo Handle the editor not setting a depth-stencil target for the material editor's tiles which render to depth even when they shouldn't.
	bool const bNeedsDepthStencilWrite = (IsValidRef(CurrentPSO->PixelShader) && (CurrentPSO->PixelShader->Bindings.InOutMask & 0x8000));
	
	// @todo Improve the way we handle binding a dummy depth/stencil so we can get pure UAV raster operations...
	bool const bNeedsDepthStencilForUAVRaster = (StateCache.GetRenderTargetsInfo().NumColorRenderTargets == 0 && StateCache.GetRenderTargetsInfo().NumUAVs > 0);
	
	bool const bBindDepthStencilForWrite = bNeedsDepthStencilWrite && !StateCache.HasValidDepthStencilSurface() && !FShaderCache::IsPredrawCall(StateCache.GetShaderCacheStateObject());
	bool const bBindDepthStencilForUAVRaster = bNeedsDepthStencilForUAVRaster && !StateCache.HasValidDepthStencilSurface() && !FShaderCache::IsPredrawCall(StateCache.GetShaderCacheStateObject());
	
	if (bBindDepthStencilForWrite || bBindDepthStencilForUAVRaster)
	{
#if UE_BUILD_DEBUG
		if (bBindDepthStencilForWrite)
		{
			UE_LOG(LogMetal, Warning, TEXT("Binding a temporary depth-stencil surface as the bound shader pipeline writes to depth/stencil but no depth/stencil surface was bound!"));
		}
		else
		{
			check(bNeedsDepthStencilForUAVRaster);
			UE_LOG(LogMetal, Warning, TEXT("Binding a temporary depth-stencil surface as the bound shader pipeline needs a texture bound - even when only writing to UAVs!"));
		}
#endif
		check(StateCache.GetRenderTargetArraySize() <= 1);
		CGSize FBSize;
		if (bBindDepthStencilForWrite)
		{
			check(!bBindDepthStencilForUAVRaster);
			FBSize = StateCache.GetFrameBufferSize();
		}
		else
		{
			check(bBindDepthStencilForUAVRaster);
			FBSize = CGSizeMake(StateCache.GetViewport(0).width, StateCache.GetViewport(0).height);
		}
		
		FRHISetRenderTargetsInfo Info = StateCache.GetRenderTargetsInfo();
		
		FTexture2DRHIRef FallbackDepthStencilSurface = StateCache.CreateFallbackDepthStencilSurface(FBSize.width, FBSize.height);
		check(IsValidRef(FallbackDepthStencilSurface));
		
		if (bBindDepthStencilForWrite)
		{
			check(!bBindDepthStencilForUAVRaster);
			Info.DepthStencilRenderTarget.Texture = FallbackDepthStencilSurface;
		}
		else
		{
			check(bBindDepthStencilForUAVRaster);
			Info.DepthStencilRenderTarget = FRHIDepthRenderTargetView(FallbackDepthStencilSurface, ERenderTargetLoadAction::ELoad, ERenderTargetStoreAction::ENoAction, FExclusiveDepthStencil::DepthRead_StencilRead);
		}
		
		// Ensure that we make it a Clear/Store -> Load/Store for the colour targets or we might render incorrectly
		for (uint32 i = 0; i < Info.NumColorRenderTargets; i++)
		{
			if (Info.ColorRenderTarget[i].LoadAction != ERenderTargetLoadAction::ELoad)
			{
				check(Info.ColorRenderTarget[i].StoreAction == ERenderTargetStoreAction::EStore || Info.ColorRenderTarget[i].StoreAction == ERenderTargetStoreAction::EMultisampleResolve);
				Info.ColorRenderTarget[i].LoadAction = ERenderTargetLoadAction::ELoad;
			}
		}
		
		if (StateCache.SetRenderTargetsInfo(Info, StateCache.GetVisibilityResultsBuffer(), true))
		{
			RenderPass.RestartRenderPass(StateCache.GetRenderPassDescriptor());
		}
		
		if (bBindDepthStencilForUAVRaster)
		{
			mtlpp::ScissorRect Rect(0, 0, (NSUInteger)FBSize.width, (NSUInteger)FBSize.height);
			StateCache.SetScissorRect(false, Rect);
		}
		
		// Enforce calls to SetRenderTarget prior to issuing draw calls.
		if(!FShaderCache::IsPredrawCall(StateCache.GetShaderCacheStateObject()))
		{
			check(StateCache.GetHasValidRenderTarget());
		}
	}
	else if (!bNeedsDepthStencilWrite && !bNeedsDepthStencilForUAVRaster && StateCache.GetFallbackDepthStencilBound())
	{
		FRHISetRenderTargetsInfo Info = StateCache.GetRenderTargetsInfo();
		Info.DepthStencilRenderTarget.Texture = nullptr;
		
		RenderPass.EndRenderPass();
		
		StateCache.SetRenderTargetsActive(false);
		StateCache.SetRenderTargetsInfo(Info, StateCache.GetVisibilityResultsBuffer(), true);
		
		RenderPass.BeginRenderPass(StateCache.GetRenderPassDescriptor());
		
		// Enforce calls to SetRenderTarget prior to issuing draw calls.
		if(!FShaderCache::IsPredrawCall(StateCache.GetShaderCacheStateObject()))
		{
			check(StateCache.GetHasValidRenderTarget());
		}
	}
	
	// make sure the BSS has a valid pipeline state object
	StateCache.SetIndexType(IndexType);
	
	return true;
}

void FMetalContext::SetRenderTargetsInfo(const FRHISetRenderTargetsInfo& RenderTargetsInfo, bool const bRestart)
{
#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
	if (!CommandList.IsImmediate())
	{
		bool bClearInParallelBuffer = false;
		
		for (uint32 RenderTargetIndex = 0; RenderTargetIndex < MaxSimultaneousRenderTargets; RenderTargetIndex++)
		{
			if (RenderTargetIndex < RenderTargetsInfo.NumColorRenderTargets && RenderTargetsInfo.ColorRenderTarget[RenderTargetIndex].Texture != nullptr)
			{
				const FRHIRenderTargetView& RenderTargetView = RenderTargetsInfo.ColorRenderTarget[RenderTargetIndex];
				if(RenderTargetView.LoadAction == ERenderTargetLoadAction::EClear)
				{
					bClearInParallelBuffer = true;
				}
			}
		}
		
		if (bClearInParallelBuffer)
		{
			UE_LOG(LogMetal, Warning, TEXT("One or more render targets bound for clear during parallel encoding: this will not behave as expected because each command-buffer will clear the target of the previous contents."));
		}
		
		if (RenderTargetsInfo.DepthStencilRenderTarget.Texture != nullptr)
		{
			if(RenderTargetsInfo.DepthStencilRenderTarget.DepthLoadAction == ERenderTargetLoadAction::EClear)
			{
				UE_LOG(LogMetal, Warning, TEXT("Depth-target bound for clear during parallel encoding: this will not behave as expected because each command-buffer will clear the target of the previous contents."));
			}
			if(RenderTargetsInfo.DepthStencilRenderTarget.StencilLoadAction == ERenderTargetLoadAction::EClear)
			{
				UE_LOG(LogMetal, Warning, TEXT("Stencil-target bound for clear during parallel encoding: this will not behave as expected because each command-buffer will clear the target of the previous contents."));
			}
		}
	}
#endif
	
	bool bSet = false;
	if (IsFeatureLevelSupported( GMaxRHIShaderPlatform, ERHIFeatureLevel::ES3_1 ))
	{
		// @todo Improve the way we handle binding a dummy depth/stencil so we can get pure UAV raster operations...
		const bool bNeedsDepthStencilForUAVRaster = RenderTargetsInfo.NumColorRenderTargets == 0 && RenderTargetsInfo.NumUAVs > 0 && !RenderTargetsInfo.DepthStencilRenderTarget.Texture;

		if (bNeedsDepthStencilForUAVRaster)
		{
			FRHISetRenderTargetsInfo Info = RenderTargetsInfo;
			CGSize FBSize = CGSizeMake(StateCache.GetViewport(0).width, StateCache.GetViewport(0).height);
			FTexture2DRHIRef FallbackDepthStencilSurface = StateCache.CreateFallbackDepthStencilSurface(FBSize.width, FBSize.height);
			check(IsValidRef(FallbackDepthStencilSurface));
#if PLATFORM_MAC
			Info.DepthStencilRenderTarget = FRHIDepthRenderTargetView(FallbackDepthStencilSurface, ERenderTargetLoadAction::ENoAction, ERenderTargetStoreAction::ENoAction, FExclusiveDepthStencil::DepthRead_StencilRead);
#else
			Info.DepthStencilRenderTarget = FRHIDepthRenderTargetView(FallbackDepthStencilSurface, ERenderTargetLoadAction::EClear, ERenderTargetStoreAction::ENoAction, FExclusiveDepthStencil::DepthRead_StencilRead);
#endif

			if (QueryBuffer->GetCurrentQueryBuffer() != StateCache.GetVisibilityResultsBuffer())
			{
				RenderPass.EndRenderPass();
			}
			bSet = StateCache.SetRenderTargetsInfo(Info, QueryBuffer->GetCurrentQueryBuffer(), bRestart);
		}
		else
		{
			if (QueryBuffer->GetCurrentQueryBuffer() != StateCache.GetVisibilityResultsBuffer())
			{
				RenderPass.EndRenderPass();
			}
			bSet = StateCache.SetRenderTargetsInfo(RenderTargetsInfo, QueryBuffer->GetCurrentQueryBuffer(), bRestart);
		}
	}
	else
	{
		if (NULL != StateCache.GetVisibilityResultsBuffer())
		{
			RenderPass.EndRenderPass();
		}
		bSet = StateCache.SetRenderTargetsInfo(RenderTargetsInfo, NULL, bRestart);
	}
	
	if (bSet && StateCache.GetHasValidRenderTarget())
	{
		RenderPass.EndRenderPass();
		RenderPass.BeginRenderPass(StateCache.GetRenderPassDescriptor());
	}
}

FMetalBuffer FMetalContext::AllocateFromRingBuffer(uint32 Size, uint32 Alignment)
{
	return RenderPass.GetRingBuffer().NewBuffer(Size, Alignment);
}

void FMetalContext::DrawPrimitive(uint32 PrimitiveType, uint32 BaseVertexIndex, uint32 NumPrimitives, uint32 NumInstances)
{
	// finalize any pending state
	if(!PrepareToDraw(PrimitiveType))
	{
		return;
	}
	
	RenderPass.DrawPrimitive(PrimitiveType, BaseVertexIndex, NumPrimitives, NumInstances);
	
	if(!FShaderCache::IsPredrawCall(StateCache.GetShaderCacheStateObject()))
	{
		FShaderCache::LogDraw(StateCache.GetShaderCacheStateObject(), PrimitiveType, 0);
	}
}

void FMetalContext::DrawPrimitiveIndirect(uint32 PrimitiveType, FMetalVertexBuffer* VertexBuffer, uint32 ArgumentOffset)
{
	// finalize any pending state
	if(!PrepareToDraw(PrimitiveType))
	{
		return;
	}
	
	RenderPass.DrawPrimitiveIndirect(PrimitiveType, VertexBuffer, ArgumentOffset);
	
	if(!FShaderCache::IsPredrawCall(StateCache.GetShaderCacheStateObject()))
	{
		FShaderCache::LogDraw(StateCache.GetShaderCacheStateObject(), PrimitiveType, 0);
	}
}

void FMetalContext::DrawIndexedPrimitive(FMetalBuffer const& IndexBuffer, uint32 IndexStride, mtlpp::IndexType IndexType, uint32 PrimitiveType, int32 BaseVertexIndex, uint32 FirstInstance, uint32 NumVertices, uint32 StartIndex, uint32 NumPrimitives, uint32 NumInstances)
{
	// finalize any pending state
	if(!PrepareToDraw(PrimitiveType, GetRHIMetalIndexType(IndexType)))
	{
		return;
	}
	
	RenderPass.DrawIndexedPrimitive(IndexBuffer, IndexStride, PrimitiveType, BaseVertexIndex, FirstInstance, NumVertices, StartIndex, NumPrimitives, NumInstances);
	
	if(!FShaderCache::IsPredrawCall(StateCache.GetShaderCacheStateObject()))
	{
		FShaderCache::LogDraw(StateCache.GetShaderCacheStateObject(), PrimitiveType, IndexStride);
	}
}

void FMetalContext::DrawIndexedIndirect(FMetalIndexBuffer* IndexBuffer, uint32 PrimitiveType, FMetalStructuredBuffer* VertexBuffer, int32 DrawArgumentsIndex, uint32 NumInstances)
{
	// finalize any pending state
	if(!PrepareToDraw(PrimitiveType))
	{
		return;
	}
	
	RenderPass.DrawIndexedIndirect(IndexBuffer, PrimitiveType, VertexBuffer, DrawArgumentsIndex, NumInstances);
	
	if(!FShaderCache::IsPredrawCall(StateCache.GetShaderCacheStateObject()))
	{
		FShaderCache::LogDraw(StateCache.GetShaderCacheStateObject(), PrimitiveType, IndexBuffer->GetStride());
	}
}

void FMetalContext::DrawIndexedPrimitiveIndirect(uint32 PrimitiveType,FMetalIndexBuffer* IndexBuffer,FMetalVertexBuffer* VertexBuffer, uint32 ArgumentOffset)
{	
	// finalize any pending state
	if(!PrepareToDraw(PrimitiveType))
	{
		return;
	}
	
	RenderPass.DrawIndexedPrimitiveIndirect(PrimitiveType, IndexBuffer, VertexBuffer, ArgumentOffset);
	
	if(!FShaderCache::IsPredrawCall(StateCache.GetShaderCacheStateObject()))
	{
		FShaderCache::LogDraw(StateCache.GetShaderCacheStateObject(), PrimitiveType, IndexBuffer->GetStride());
	}
}

void FMetalContext::CopyFromTextureToBuffer(FMetalTexture const& Texture, uint32 sourceSlice, uint32 sourceLevel, mtlpp::Origin sourceOrigin, mtlpp::Size sourceSize, FMetalBuffer const& toBuffer, uint32 destinationOffset, uint32 destinationBytesPerRow, uint32 destinationBytesPerImage, mtlpp::BlitOption options)
{
	RenderPass.CopyFromTextureToBuffer(Texture, sourceSlice, sourceLevel, sourceOrigin, sourceSize, toBuffer, destinationOffset, destinationBytesPerRow, destinationBytesPerImage, options);
}

void FMetalContext::CopyFromBufferToTexture(FMetalBuffer const& Buffer, uint32 sourceOffset, uint32 sourceBytesPerRow, uint32 sourceBytesPerImage, mtlpp::Size sourceSize, FMetalTexture const& toTexture, uint32 destinationSlice, uint32 destinationLevel, mtlpp::Origin destinationOrigin, mtlpp::BlitOption options)
{
	RenderPass.CopyFromBufferToTexture(Buffer, sourceOffset, sourceBytesPerRow, sourceBytesPerImage, sourceSize, toTexture, destinationSlice, destinationLevel, destinationOrigin, options);
}

void FMetalContext::CopyFromTextureToTexture(FMetalTexture const& Texture, uint32 sourceSlice, uint32 sourceLevel, mtlpp::Origin sourceOrigin, mtlpp::Size sourceSize, FMetalTexture const& toTexture, uint32 destinationSlice, uint32 destinationLevel, mtlpp::Origin destinationOrigin)
{
	RenderPass.CopyFromTextureToTexture(Texture, sourceSlice, sourceLevel, sourceOrigin, sourceSize, toTexture, destinationSlice, destinationLevel, destinationOrigin);
}

void FMetalContext::CopyFromBufferToBuffer(FMetalBuffer const& SourceBuffer, NSUInteger SourceOffset, FMetalBuffer const& DestinationBuffer, NSUInteger DestinationOffset, NSUInteger Size)
{
	RenderPass.CopyFromBufferToBuffer(SourceBuffer, SourceOffset, DestinationBuffer, DestinationOffset, Size);
}

bool FMetalContext::AsyncCopyFromBufferToTexture(FMetalBuffer const& Buffer, uint32 sourceOffset, uint32 sourceBytesPerRow, uint32 sourceBytesPerImage, mtlpp::Size sourceSize, FMetalTexture const& toTexture, uint32 destinationSlice, uint32 destinationLevel, mtlpp::Origin destinationOrigin, mtlpp::BlitOption options)
{
	return RenderPass.AsyncCopyFromBufferToTexture(Buffer, sourceOffset, sourceBytesPerRow, sourceBytesPerImage, sourceSize, toTexture, destinationSlice, destinationLevel, destinationOrigin, options);
}

bool FMetalContext::AsyncCopyFromTextureToTexture(FMetalTexture const& Texture, uint32 sourceSlice, uint32 sourceLevel, mtlpp::Origin sourceOrigin, mtlpp::Size sourceSize, FMetalTexture const& toTexture, uint32 destinationSlice, uint32 destinationLevel, mtlpp::Origin destinationOrigin)
{
	return RenderPass.AsyncCopyFromTextureToTexture(Texture, sourceSlice, sourceLevel, sourceOrigin, sourceSize, toTexture, destinationSlice, destinationLevel, destinationOrigin);
}

void FMetalContext::AsyncCopyFromBufferToBuffer(FMetalBuffer const& SourceBuffer, NSUInteger SourceOffset, FMetalBuffer const& DestinationBuffer, NSUInteger DestinationOffset, NSUInteger Size)
{
	RenderPass.AsyncCopyFromBufferToBuffer(SourceBuffer, SourceOffset, DestinationBuffer, DestinationOffset, Size);
}

void FMetalContext::AsyncGenerateMipmapsForTexture(FMetalTexture const& Texture)
{
	RenderPass.AsyncGenerateMipmapsForTexture(Texture);
}

void FMetalContext::SubmitAsyncCommands(mtlpp::CommandBufferHandler ScheduledHandler, mtlpp::CommandBufferHandler CompletionHandler, bool const bWait)
{
	RenderPass.AddAsyncCommandBufferHandlers(ScheduledHandler, CompletionHandler);
	if (bWait)
	{
		SubmitCommandsHint((uint32)(EMetalSubmitFlagsAsyncCommandBuffer|EMetalSubmitFlagsWaitOnCommandBuffer|EMetalSubmitFlagsBreakCommandBuffer));
	}
}

void FMetalContext::SynchronizeTexture(FMetalTexture const& Texture, uint32 Slice, uint32 Level)
{
	RenderPass.SynchronizeTexture(Texture, Slice, Level);
}

void FMetalContext::SynchroniseResource(mtlpp::Resource const& Resource)
{
	RenderPass.SynchroniseResource(Resource);
}

void FMetalContext::FillBuffer(FMetalBuffer const& Buffer, ns::Range Range, uint8 Value)
{
	RenderPass.FillBuffer(Buffer, Range, Value);
}

void FMetalContext::Dispatch(uint32 ThreadGroupCountX, uint32 ThreadGroupCountY, uint32 ThreadGroupCountZ)
{
	RenderPass.Dispatch(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
}

void FMetalContext::DispatchIndirect(FMetalVertexBuffer* ArgumentBuffer, uint32 ArgumentOffset)
{
	RenderPass.DispatchIndirect(ArgumentBuffer, ArgumentOffset);
}

void FMetalContext::StartTiming(class FMetalEventNode* EventNode)
{
	mtlpp::CommandBufferHandler Handler = nil;
	
	bool const bHasCurrentCommandBuffer = GetCurrentCommandBuffer();
	
	if(EventNode)
	{
		Handler = EventNode->Start();
		
		if (bHasCurrentCommandBuffer)
		{
			RenderPass.AddCompletionHandler(Handler);
			Block_release(Handler);
		}
	}
	
	SubmitCommandsHint(EMetalSubmitFlagsCreateCommandBuffer);
	
	if (Handler != nil && !bHasCurrentCommandBuffer)
	{
		GetCurrentCommandBuffer().AddScheduledHandler(Handler);
		Block_release(Handler);
	}
}

void FMetalContext::EndTiming(class FMetalEventNode* EventNode)
{
	bool const bWait = EventNode->Wait();
	mtlpp::CommandBufferHandler Handler = EventNode->Stop();
	RenderPass.AddCompletionHandler(Handler);
	Block_release(Handler);
	
	if (!bWait)
	{
		SubmitCommandsHint(EMetalSubmitFlagsCreateCommandBuffer);
	}
	else
	{
		SubmitCommandBufferAndWait();
	}
}

#if METAL_SUPPORTS_PARALLEL_RHI_EXECUTE

class FMetalCommandContextContainer : public IRHICommandContextContainer
{
	FMetalRHICommandContext* CmdContext;
	int32 Index;
	int32 Num;
	
public:
	
	/** Custom new/delete with recycling */
	void* operator new(size_t Size);
	void operator delete(void *RawMemory);
	
	FMetalCommandContextContainer(int32 InIndex, int32 InNum)
	: CmdContext(nullptr)
	, Index(InIndex)
	, Num(InNum)
	{
		CmdContext = GetMetalDeviceContext().AcquireContext(Index, Num);
		check(CmdContext);
	}
	
	virtual ~FMetalCommandContextContainer() override
	{
		check(!CmdContext);
	}
	
	virtual IRHICommandContext* GetContext() override
	{
		check(CmdContext);
		CmdContext->GetInternalContext().InitFrame(false);
		return CmdContext;
	}
	virtual void FinishContext() override
	{
		if (CmdContext)
		{
			CmdContext->GetInternalContext().FinishFrame();
		}
	}
	virtual void SubmitAndFreeContextContainer(int32 NewIndex, int32 NewNum) override
	{
		if (CmdContext)
		{
			check(Index == NewIndex && Num == NewNum);
			check(CmdContext->GetInternalContext().GetCurrentCommandBuffer().GetPtr() == nil);
			CmdContext->GetInternalContext().GetCommandList().Submit(Index, Num);
			
			GetMetalDeviceContext().ReleaseContext(CmdContext);
			CmdContext = nullptr;
			check(!CmdContext);
		}
		delete this;
	}
};

static TLockFreeFixedSizeAllocator<sizeof(FMetalCommandContextContainer), PLATFORM_CACHE_LINE_SIZE, FThreadSafeCounter> FMetalCommandContextContainerAllocator;

void* FMetalCommandContextContainer::operator new(size_t Size)
{
	return FMemory::Malloc(Size);
}

/**
 * Custom delete
 */
void FMetalCommandContextContainer::operator delete(void *RawMemory)
{
	FMemory::Free(RawMemory);
}

IRHICommandContextContainer* FMetalDynamicRHI::RHIGetCommandContextContainer(int32 Index, int32 Num)
{
	return new FMetalCommandContextContainer(Index, Num);
}

#else

IRHICommandContextContainer* FMetalDynamicRHI::RHIGetCommandContextContainer(int32 Index, int32 Num)
{
	return nullptr;
}

#endif
