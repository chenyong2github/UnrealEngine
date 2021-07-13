// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
D3D12Util.h: D3D RHI utility implementation.
=============================================================================*/

#include "D3D12RHIPrivate.h"
#include "EngineModule.h"
#include "RendererInterface.h"
#include "CoreGlobals.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Windows/WindowsPlatformCrashContext.h"
#include "HAL/ExceptionHandling.h"

#define D3DERR(x) case x: ErrorCodeText = TEXT(#x); break;
#define LOCTEXT_NAMESPACE "Developer.MessageLog"

#ifndef _FACD3D 
#define _FACD3D  0x876
#endif	//_FACD3D 
#ifndef MAKE_D3DHRESULT
#define _FACD3D  0x876
#define MAKE_D3DHRESULT( code )  MAKE_HRESULT( 1, _FACD3D, code )
#endif	//MAKE_D3DHRESULT

#if WITH_D3DX_LIBS
#ifndef D3DERR_INVALIDCALL
#define D3DERR_INVALIDCALL MAKE_D3DHRESULT(2156)
#endif//D3DERR_INVALIDCALL
#ifndef D3DERR_WASSTILLDRAWING
#define D3DERR_WASSTILLDRAWING MAKE_D3DHRESULT(540)
#endif//D3DERR_WASSTILLDRAWING
#endif

// GPU crashes are nonfatal on windows/nonshipping so as not to interfere with GPU crash dump processing
#if PLATFORM_WINDOWS || PLATFORM_HOLOLENS || !UE_BUILD_SHIPPING
  #define D3D12RHI_GPU_CRASH_LOG_VERBOSITY Error
#else
  #define D3D12RHI_GPU_CRASH_LOG_VERBOSITY Fatal
#endif

extern bool D3D12RHI_ShouldCreateWithD3DDebug();

static FString GetUniqueName()
{
	static int64 ID = 0;
	const int64 UniqueID = FPlatformAtomics::InterlockedIncrement(&ID);
	const FString UniqueName = FString::Printf(TEXT("D3D12RHIObjectUniqueName%lld"), UniqueID);
	return UniqueName;
}

void SetName(ID3D12Object* const Object, const TCHAR* const Name)
{
#if NAME_OBJECTS
	if (Object && Name)
	{
		VERIFYD3D12RESULT(Object->SetName(Name));
	}
	else if (Object)
	{
		VERIFYD3D12RESULT(Object->SetName(*GetUniqueName()));
	}
#else
	UNREFERENCED_PARAMETER(Object);
	UNREFERENCED_PARAMETER(Name);
#endif
}

void SetName(FD3D12Resource* const Resource, const TCHAR* const Name)
{
#if NAME_OBJECTS
	// Special case for FD3D12Resources because we also store the name as a member in FD3D12Resource
	if (Resource && Name)
	{
		Resource->SetName(Name);
	}
	else if (Resource)
	{
		Resource->SetName(*GetUniqueName());
	}
#else
	UNREFERENCED_PARAMETER(Resource);
	UNREFERENCED_PARAMETER(Name);
#endif
}

static FString GetD3D12DeviceHungErrorString(HRESULT ErrorCode)
{
	FString ErrorCodeText;

	switch (ErrorCode)
	{
		D3DERR(DXGI_ERROR_DEVICE_HUNG)
		D3DERR(DXGI_ERROR_DEVICE_REMOVED)
		D3DERR(DXGI_ERROR_DEVICE_RESET)
		D3DERR(DXGI_ERROR_DRIVER_INTERNAL_ERROR)
		D3DERR(DXGI_ERROR_INVALID_CALL)
		default:
			ErrorCodeText = FString::Printf(TEXT("%08X"), (int32)ErrorCode);
	}

	return ErrorCodeText;
}

static FString GetD3D12ErrorString(HRESULT ErrorCode, ID3D12Device* Device)
{
	FString ErrorCodeText;

	switch (ErrorCode)
	{
		D3DERR(S_OK);
		D3DERR(D3D11_ERROR_FILE_NOT_FOUND)
		D3DERR(D3D11_ERROR_TOO_MANY_UNIQUE_STATE_OBJECTS)
#if WITH_D3DX_LIBS
		D3DERR(D3DERR_INVALIDCALL)
		D3DERR(D3DERR_WASSTILLDRAWING)
#endif	//WITH_D3DX_LIBS
		D3DERR(E_FAIL)
		D3DERR(E_INVALIDARG)
		D3DERR(E_OUTOFMEMORY)
		D3DERR(DXGI_ERROR_INVALID_CALL)
		D3DERR(E_NOINTERFACE)
		D3DERR(DXGI_ERROR_DEVICE_REMOVED)
#if PLATFORM_WINDOWS
		EMBED_DXGI_ERROR_LIST(D3DERR, )
#endif
		default:
			ErrorCodeText = FString::Printf(TEXT("%08X"), (int32)ErrorCode);
	}

	if (ErrorCode == DXGI_ERROR_DEVICE_REMOVED && Device)
	{
		HRESULT hResDeviceRemoved = Device->GetDeviceRemovedReason();
		ErrorCodeText += FString(TEXT(" with Reason: ")) + GetD3D12DeviceHungErrorString(hResDeviceRemoved);
	}

	return ErrorCodeText;
}

/** Build string name of command queue type */
static const TCHAR* GetD3DCommandQueueTypeName(ED3D12CommandQueueType QueueType)
{
	switch (QueueType)
	{
	case ED3D12CommandQueueType::Default:	 return TEXT("3D");
	case ED3D12CommandQueueType::Async:		 return TEXT("Compute");
	case ED3D12CommandQueueType::Copy:		 return TEXT("Copy");
	}

	return nullptr;
}

#undef D3DERR

namespace D3D12RHI
{
	const TCHAR* GetD3D12TextureFormatString(DXGI_FORMAT TextureFormat)
	{
		static const TCHAR* EmptyString = TEXT("");
		const TCHAR* TextureFormatText = EmptyString;
#define D3DFORMATCASE(x) case x: TextureFormatText = TEXT(#x); break;
		switch (TextureFormat)
		{
			D3DFORMATCASE(DXGI_FORMAT_R8G8B8A8_UNORM)
				D3DFORMATCASE(DXGI_FORMAT_B8G8R8A8_UNORM)
				D3DFORMATCASE(DXGI_FORMAT_B8G8R8X8_UNORM)
				D3DFORMATCASE(DXGI_FORMAT_BC1_UNORM)
				D3DFORMATCASE(DXGI_FORMAT_BC2_UNORM)
				D3DFORMATCASE(DXGI_FORMAT_BC3_UNORM)
				D3DFORMATCASE(DXGI_FORMAT_BC4_UNORM)
				D3DFORMATCASE(DXGI_FORMAT_R16G16B16A16_FLOAT)
				D3DFORMATCASE(DXGI_FORMAT_R32G32B32A32_FLOAT)
				D3DFORMATCASE(DXGI_FORMAT_UNKNOWN)
				D3DFORMATCASE(DXGI_FORMAT_R8_UNORM)
				D3DFORMATCASE(DXGI_FORMAT_D32_FLOAT_S8X24_UINT)
				D3DFORMATCASE(DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS)
				D3DFORMATCASE(DXGI_FORMAT_R32G8X24_TYPELESS)
				D3DFORMATCASE(DXGI_FORMAT_D24_UNORM_S8_UINT)
				D3DFORMATCASE(DXGI_FORMAT_R24_UNORM_X8_TYPELESS)
				D3DFORMATCASE(DXGI_FORMAT_R32_FLOAT)
				D3DFORMATCASE(DXGI_FORMAT_R16G16_UINT)
				D3DFORMATCASE(DXGI_FORMAT_R16G16_UNORM)
				D3DFORMATCASE(DXGI_FORMAT_R16G16_SNORM)
				D3DFORMATCASE(DXGI_FORMAT_R16G16_FLOAT)
				D3DFORMATCASE(DXGI_FORMAT_R32G32_FLOAT)
				D3DFORMATCASE(DXGI_FORMAT_R10G10B10A2_UNORM)
				D3DFORMATCASE(DXGI_FORMAT_R16G16B16A16_UINT)
				D3DFORMATCASE(DXGI_FORMAT_R8G8_SNORM)
				D3DFORMATCASE(DXGI_FORMAT_BC5_UNORM)
				D3DFORMATCASE(DXGI_FORMAT_R1_UNORM)
				D3DFORMATCASE(DXGI_FORMAT_R8G8B8A8_TYPELESS)
				D3DFORMATCASE(DXGI_FORMAT_B8G8R8A8_TYPELESS)
				D3DFORMATCASE(DXGI_FORMAT_BC7_UNORM)
				D3DFORMATCASE(DXGI_FORMAT_BC6H_UF16)
		default: TextureFormatText = EmptyString;
		}
#undef D3DFORMATCASE
		return TextureFormatText;
	}
}
using namespace D3D12RHI;

static FString GetD3D12TextureFlagString(uint32 TextureFlags)
{
	FString TextureFormatText = TEXT("");

	if (TextureFlags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
	{
		TextureFormatText += TEXT("D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET ");
	}

	if (TextureFlags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
	{
		TextureFormatText += TEXT("D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL ");
	}

	if (TextureFlags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)
	{
		TextureFormatText += TEXT("D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE ");
	}

	if (TextureFlags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
	{
		TextureFormatText += TEXT("D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS ");
	}
	return TextureFormatText;
}

/** Log the GPU progress of the given CommandListManager to the Error log if breadcrumb data is available */
static bool LogBreadcrumbData(D3D12RHI::FD3DGPUProfiler& GPUProfiler, FD3D12CommandListManager& CommandListManager)
{
	uint32* BreadCrumbData = (uint32*)CommandListManager.GetBreadCrumbResourceAddress();
	if (BreadCrumbData == nullptr)
	{
		return false;
	}

	uint32 EventCount = BreadCrumbData[0];
	bool bBeginEvent = BreadCrumbData[1] > 0;
	check(EventCount >= 0 && EventCount < (MAX_GPU_BREADCRUMB_DEPTH - 2));

	FString gpu_progress = FString::Printf(TEXT("[GPUBreadCrumb]\t%s Queue %d - %s"), GetD3DCommandQueueTypeName(CommandListManager.GetQueueType()), 
		CommandListManager.GetGPUIndex(), EventCount == 0 ? TEXT("No Data") : (bBeginEvent ? TEXT("Begin: ") : TEXT("End: ")));
	for (uint32 EventIndex = 0; EventIndex < EventCount; ++EventIndex)
	{
		if (EventIndex > 0)
		{
			gpu_progress.Append(TEXT(" - "));
		}

		// get the crc and try and translate back into a string
		uint32 event_crc = BreadCrumbData[EventIndex + 2];
		const FString* event_name = GPUProfiler.FindEventString(event_crc);
		if (event_name)
		{
			gpu_progress.Append(*event_name);
		}
		else
		{
			gpu_progress.Append(TEXT("Unknown Event"));
		}
	}

	UE_LOG(LogD3D12RHI, Error, TEXT("%s"), *gpu_progress);

	return true;
}

/** Log the GPU progress of the given Device to the Error log if breadcrumb data is available */
static void LogBreadcrumbData(ID3D12Device* Device)
{
	UE_LOG(LogD3D12RHI, Error, TEXT("[GPUBreadCrumb] Last tracked GPU operations:"));

	bool bValidData = true;

	// Check all the devices
	FD3D12DynamicRHI* D3D12RHI = FD3D12DynamicRHI::GetD3DRHI();
	D3D12RHI->ForEachDevice(Device, [&](FD3D12Device* Device)
	{
		bValidData = bValidData && LogBreadcrumbData(Device->GetGPUProfiler(), Device->GetCommandListManager());
		bValidData = bValidData && LogBreadcrumbData(Device->GetGPUProfiler(), Device->GetAsyncCommandListManager());
		bValidData = bValidData && LogBreadcrumbData(Device->GetGPUProfiler(), Device->GetCopyCommandListManager());
	});

	if (!bValidData)
	{
		UE_LOG(LogD3D12RHI, Error, TEXT("No Valid GPU Breadcrumb data found. Use -gpucrashdebugging to collect GPU progress when debugging GPU crashes."));
	}
}

#if PLATFORM_WINDOWS

static TArrayView<D3D12_DRED_BREADCRUMB_CONTEXT> GetBreadcrumbContexts(const D3D12_AUTO_BREADCRUMB_NODE* Node)
{
	return {};
}

static TArrayView<D3D12_DRED_BREADCRUMB_CONTEXT> GetBreadcrumbContexts(const D3D12_AUTO_BREADCRUMB_NODE1* Node)
{
	return MakeArrayView<D3D12_DRED_BREADCRUMB_CONTEXT>(Node->pBreadcrumbContexts, Node->BreadcrumbContextsCount);
}

struct FDred_1_1
{
	FDred_1_1(ID3D12Device* Device)
	{
		if (SUCCEEDED(Device->QueryInterface(IID_PPV_ARGS(Data.GetInitReference()))))
		{
			D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT DredAutoBreadcrumbsOutput;
			if (SUCCEEDED(Data->GetAutoBreadcrumbsOutput(&DredAutoBreadcrumbsOutput)))
			{
				BreadcrumbHead = DredAutoBreadcrumbsOutput.pHeadAutoBreadcrumbNode;
			}
		}
	}
	TRefCountPtr<ID3D12DeviceRemovedExtendedData> Data;
	const D3D12_AUTO_BREADCRUMB_NODE* BreadcrumbHead = nullptr;
};

struct FDred_1_2
{
	FDred_1_2(ID3D12Device* Device)
	{
		if (SUCCEEDED(Device->QueryInterface(IID_PPV_ARGS(Data.GetInitReference()))))
		{
			D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 DredAutoBreadcrumbsOutput;
			if (SUCCEEDED(Data->GetAutoBreadcrumbsOutput1(&DredAutoBreadcrumbsOutput)))
			{
				BreadcrumbHead = DredAutoBreadcrumbsOutput.pHeadAutoBreadcrumbNode;
			}
		}
	}
	TRefCountPtr<ID3D12DeviceRemovedExtendedData1> Data;
	const D3D12_AUTO_BREADCRUMB_NODE1* BreadcrumbHead = nullptr;
};

/** Log the DRED data to Error log if available */
template <typename FDred_T>
static bool LogDREDData(ID3D12Device* Device)
{
	// Should match all values from D3D12_AUTO_BREADCRUMB_OP
	static const TCHAR* OpNames[] =
	{
		TEXT("SetMarker"),
		TEXT("BeginEvent"),
		TEXT("EndEvent"),
		TEXT("DrawInstanced"),
		TEXT("DrawIndexedInstanced"),
		TEXT("ExecuteIndirect"),
		TEXT("Dispatch"),
		TEXT("CopyBufferRegion"),
		TEXT("CopyTextureRegion"),
		TEXT("CopyResource"),
		TEXT("CopyTiles"),
		TEXT("ResolveSubresource"),
		TEXT("ClearRenderTargetView"),
		TEXT("ClearUnorderedAccessView"),
		TEXT("ClearDepthStencilView"),
		TEXT("ResourceBarrier"),
		TEXT("ExecuteBundle"),
		TEXT("Present"),
		TEXT("ResolveQueryData"),
		TEXT("BeginSubmission"),
		TEXT("EndSubmission"),
		TEXT("DecodeFrame"),
		TEXT("ProcessFrames"),
		TEXT("AtomicCopyBufferUint"),
		TEXT("AtomicCopyBufferUint64"),
		TEXT("ResolveSubresourceRegion"),
		TEXT("WriteBufferImmediate"),
		TEXT("DecodeFrame1"),
		TEXT("SetProtectedResourceSession"),
		TEXT("DecodeFrame2"),
		TEXT("ProcessFrames1"),
		TEXT("BuildRaytracingAccelerationStructure"),
		TEXT("EmitRaytracingAccelerationStructurePostBuildInfo"),
		TEXT("CopyRaytracingAccelerationStructure"),
		TEXT("DispatchRays"),
		TEXT("InitializeMetaCommand"),
		TEXT("ExecuteMetaCommand"),
		TEXT("EstimateMotion"),
		TEXT("ResolveMotionVectorHeap"),
		TEXT("SetPipelineState1"),
		TEXT("InitializeExtensionCommand"),
		TEXT("ExecuteExtensionCommand"),
	};
	static_assert(UE_ARRAY_COUNT(OpNames) == D3D12_AUTO_BREADCRUMB_OP_EXECUTEEXTENSIONCOMMAND + 1, "OpNames array length mismatch");

	// Should match all valid values from D3D12_DRED_ALLOCATION_TYPE
	static const TCHAR* AllocTypesNames[] =
	{
		TEXT("CommandQueue"),
		TEXT("CommandAllocator"),
		TEXT("PipelineState"),
		TEXT("CommandList"),
		TEXT("Fence"),
		TEXT("DescriptorHeap"),
		TEXT("Heap"),
		TEXT("Unknown"),				// Unknown type - missing enum value in D3D12_DRED_ALLOCATION_TYPE
		TEXT("QueryHeap"),
		TEXT("CommandSignature"),
		TEXT("PipelineLibrary"),
		TEXT("VideoDecoder"),
		TEXT("Unknown"),				// Unknown type - missing enum value in D3D12_DRED_ALLOCATION_TYPE
		TEXT("VideoProcessor"),
		TEXT("Unknown"),				// Unknown type - missing enum value in D3D12_DRED_ALLOCATION_TYPE
		TEXT("Resource"),
		TEXT("Pass"),
		TEXT("CryptoSession"),
		TEXT("CryptoSessionPolicy"),
		TEXT("ProtectedResourceSession"),
		TEXT("VideoDecoderHeap"),
		TEXT("CommandPool"),
		TEXT("CommandRecorder"),
		TEXT("StateObjectr"),
		TEXT("MetaCommand"),
		TEXT("SchedulingGroup"),
		TEXT("VideoMotionEstimator"),
		TEXT("VideoMotionVectorHeap"),
		TEXT("VideoExtensionCommand"),
	};
	static_assert(UE_ARRAY_COUNT(AllocTypesNames) == D3D12_DRED_ALLOCATION_TYPE_VIDEO_EXTENSION_COMMAND - D3D12_DRED_ALLOCATION_TYPE_COMMAND_QUEUE + 1, "AllocTypes array length mismatch");

	FDred_T Dred(Device);
	if (Dred.Data.IsValid())
	{
		if (Dred.BreadcrumbHead)
		{
			UE_LOG(LogD3D12RHI, Error, TEXT("DRED: Last tracked GPU operations:"));

			FString ContextStr;
			TMap<int32, const wchar_t*> ContextStrings;

			uint32 TracedCommandLists = 0;
			auto Node = Dred.BreadcrumbHead;
			while (Node && Node->pLastBreadcrumbValue)
			{
				int32 LastCompletedOp = *Node->pLastBreadcrumbValue;

				if (LastCompletedOp != Node->BreadcrumbCount && LastCompletedOp != 0)
				{
					UE_LOG(LogD3D12RHI, Error, TEXT("DRED: Commandlist \"%s\" on CommandQueue \"%s\", %d completed of %d"), Node->pCommandListDebugNameW, Node->pCommandQueueDebugNameW, LastCompletedOp, Node->BreadcrumbCount);
					TracedCommandLists++;

					int32 FirstOp = FMath::Max(LastCompletedOp - 100, 0);
					int32 LastOp = FMath::Min(LastCompletedOp + 20, int32(Node->BreadcrumbCount) - 1);

					ContextStrings.Reset();
					for (const D3D12_DRED_BREADCRUMB_CONTEXT& Context : GetBreadcrumbContexts(Node))
					{
						ContextStrings.Add(Context.BreadcrumbIndex, Context.pContextString);
					}

					for (int32 Op = FirstOp; Op <= LastOp; ++Op)
					{
						D3D12_AUTO_BREADCRUMB_OP BreadcrumbOp = Node->pCommandHistory[Op];

						auto OpContextStr = ContextStrings.Find(Op);
						if (OpContextStr)
						{
							ContextStr = " [";
							ContextStr += *OpContextStr;
							ContextStr += "]";
						}
						else
						{
							ContextStr.Reset();
						}

						const TCHAR* OpName = (BreadcrumbOp < UE_ARRAY_COUNT(OpNames)) ? OpNames[BreadcrumbOp] : TEXT("Unknown Op");
						UE_LOG(LogD3D12RHI, Error, TEXT("\tOp: %d, %s%s%s"), Op, OpName, *ContextStr, (Op + 1 == LastCompletedOp) ? TEXT(" - LAST COMPLETED") : TEXT(""));
					}
				}

				Node = Node->pNext;
			}

			if (TracedCommandLists == 0)
			{
				UE_LOG(LogD3D12RHI, Error, TEXT("DRED: No command list found with active outstanding operations (all finished or not started yet)."));
			}
		}

		D3D12_DRED_PAGE_FAULT_OUTPUT DredPageFaultOutput;
		if (SUCCEEDED(Dred.Data->GetPageFaultAllocationOutput(&DredPageFaultOutput)) && DredPageFaultOutput.PageFaultVA != 0)
		{
			UE_LOG(LogD3D12RHI, Error, TEXT("DRED: PageFault at VA GPUAddress \"0x%llX\""), (long long)DredPageFaultOutput.PageFaultVA);

			const D3D12_DRED_ALLOCATION_NODE* Node = DredPageFaultOutput.pHeadExistingAllocationNode;
			if (Node)
			{
				UE_LOG(LogD3D12RHI, Error, TEXT("DRED: Active objects with VA ranges that match the faulting VA:"));
				while (Node)
				{
					int32 alloc_type_index = Node->AllocationType - D3D12_DRED_ALLOCATION_TYPE_COMMAND_QUEUE;
					const TCHAR* AllocTypeName = (alloc_type_index < UE_ARRAY_COUNT(AllocTypesNames)) ? AllocTypesNames[alloc_type_index] : TEXT("Unknown Alloc");
					UE_LOG(LogD3D12RHI, Error, TEXT("\tName: %s (Type: %s)"), Node->ObjectNameW, AllocTypeName);

					Node = Node->pNext;
				}
			}

			Node = DredPageFaultOutput.pHeadRecentFreedAllocationNode;
			if (Node)
			{
				UE_LOG(LogD3D12RHI, Error, TEXT("DRED: Recent freed objects with VA ranges that match the faulting VA:"));
				while (Node)
				{
					int32 alloc_type_index = Node->AllocationType - D3D12_DRED_ALLOCATION_TYPE_COMMAND_QUEUE;
					const TCHAR* AllocTypeName = (alloc_type_index < UE_ARRAY_COUNT(AllocTypesNames)) ? AllocTypesNames[alloc_type_index] : TEXT("Unknown Alloc");
					UE_LOG(LogD3D12RHI, Error, TEXT("\tName: %s (Type: %s)"), Node->ObjectNameW, AllocTypeName);

					Node = Node->pNext;
				}
			}
		}
		else
		{
			UE_LOG(LogD3D12RHI, Error, TEXT("DRED: No PageFault data."));
		}

		return true;
	}
	else
	{
		return false;
	}
}

#endif  // PLATFORM_WINDOWS

extern CORE_API bool GIsGPUCrashed;

static void TerminateOnOutOfMemory(HRESULT D3DResult, bool bCreatingTextures)
{
#if PLATFORM_WINDOWS
	if (bCreatingTextures)
	{
		FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *LOCTEXT("OutOfVideoMemoryTextures", "Out of video memory trying to allocate a texture! Make sure your video card has the minimum required memory, try lowering the resolution and/or closing other applications that are running. Exiting...").ToString(), TEXT("Error"));
	}
	else
	{
		FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *NSLOCTEXT("D3D12RHI", "OutOfMemory", "Out of video memory trying to allocate a rendering resource. Make sure your video card has the minimum required memory, try lowering the resolution and/or closing other applications that are running. Exiting...").ToString(), TEXT("Error"));
	}
#if STATS
	GetRendererModule().DebugLogOnCrash();
#endif
	FPlatformMisc::RequestExit(true);
#else // PLATFORM_WINDOWS
	UE_LOG(LogInit, Fatal, TEXT("Out of video memory trying to allocate a rendering resource"));
#endif // !PLATFORM_WINDOWS
}

#ifndef MAKE_D3DHRESULT
#define _FACD3D						0x876
#define MAKE_D3DHRESULT( code)		MAKE_HRESULT( 1, _FACD3D, code )
#endif	//MAKE_D3DHRESULT

namespace D3D12RHI
{
	void TerminateOnGPUCrash(ID3D12Device* InDevice, const void* InGPUCrashDump, const size_t InGPUCrashDumpSize)
	{		
		// Lock the cs, and never unlock - don't want another thread processing the same GPU crash
		// This call will force a request exit
		static FCriticalSection cs;
		cs.Lock();

		// Mark critical and gpu crash
		GIsCriticalError = true;
		GIsGPUCrashed = true;

		// Check GPU heartbeat - will trace Aftermath state
		if (GDynamicRHI)
		{
			GDynamicRHI->CheckGpuHeartbeat();
		}

		// Log RHI independent breadcrumbing data
		LogBreadcrumbData(InDevice);

		FD3D12DynamicRHI* D3D12RHI = FD3D12DynamicRHI::GetD3DRHI();
#if PLATFORM_WINDOWS
		// If no device provided then try and log the DRED status of each device
		D3D12RHI->ForEachDevice(InDevice, [&](FD3D12Device* IterationDevice)
			{
				if (InDevice == nullptr || InDevice == IterationDevice->GetDevice())
				{
					if (!LogDREDData<FDred_1_2>(IterationDevice->GetDevice()))
					{
						LogDREDData<FDred_1_1>(IterationDevice->GetDevice());
					}
				}
			});
#endif  // PLATFORM_WINDOWS
		
		// Build the error message
		FTextBuilder ErrorMessage;
		ErrorMessage.AppendLine(LOCTEXT("GPU Crashed", "GPU Crashed or D3D Device Removed.\n"));
		if (!D3D12RHI->GetAdapter().IsDebugDevice())
		{
			ErrorMessage.AppendLine(LOCTEXT("D3D Debug Device", "Use -d3ddebug to enable the D3D debug device."));
		}
		if (D3D12RHI->GetAdapter().GetGPUCrashDebuggingMode() != ED3D12GPUCrashDebugginMode::Disabled)
		{
			ErrorMessage.AppendLine(LOCTEXT("GPU Crash Debugging enabled", "Check log for GPU state information."));
		}
		else
		{
			ErrorMessage.AppendLine(LOCTEXT("GPU Crash Debugging disabled", "Use -gpucrashdebugging to track current GPU state."));
		}

		// And info on gpu crash dump as well
		if (InGPUCrashDump)
		{
			ErrorMessage.AppendLine(LOCTEXT("GPU CrashDump", "\nA GPU mini dump will be saved in the Crashes folder."));
		}
		
		// Make sure the log is flushed!
		GLog->PanicFlushThreadedLogs();
		GLog->Flush();

		// Show message box or trace information
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (!FApp::IsUnattended() && !IsDebuggerPresent())
		{
			FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *ErrorMessage.ToText().ToString(), TEXT("Error"));
		}
		else
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		{
			UE_LOG(LogD3D12RHI, D3D12RHI_GPU_CRASH_LOG_VERBOSITY, TEXT("%s"), *ErrorMessage.ToText().ToString());
		}

#if PLATFORM_WINDOWS
		// If we have crash dump data then dump to disc
		if (InGPUCrashDump != nullptr)
		{
			// Write out crash dump to project log dir - exception handling code will take care of copying it to the correct location
			const FString GPUMiniDumpPath = FPaths::Combine(FPaths::ProjectLogDir(), FWindowsPlatformCrashContext::UE4GPUAftermathMinidumpName);

			// Just use raw windows file routines for the GPU minidump (TODO: refactor to our own functions?)
			HANDLE FileHandle = CreateFileW(*GPUMiniDumpPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			if (FileHandle != INVALID_HANDLE_VALUE)
			{
				WriteFile(FileHandle, InGPUCrashDump, InGPUCrashDumpSize, nullptr, nullptr);
			}
			CloseHandle(FileHandle);

			// Report the GPU crash which will raise the exception (only interesting if we have a GPU dump)
			ReportGPUCrash(TEXT("Aftermath GPU Crash dump Triggered"), 0);

			// Force shutdown, we can't do anything useful anymore.
			FPlatformMisc::RequestExit(true);
		}
#endif // PLATFORM_WINDOWS

		// hard break here when the debugger is attached
		if (IsDebuggerPresent())
		{
			UE_DEBUG_BREAK();
		}
	}

	void VerifyD3D12Result(HRESULT D3DResult, const ANSICHAR* Code, const ANSICHAR* Filename, uint32 Line, ID3D12Device* Device, FString Message)
	{
		check(FAILED(D3DResult));

		const FString& ErrorString = GetD3D12ErrorString(D3DResult, Device);
		UE_LOG(LogD3D12RHI, Error, TEXT("%s failed \n at %s:%u \n with error %s\n%s"), ANSI_TO_TCHAR(Code), ANSI_TO_TCHAR(Filename), Line, *ErrorString, *Message);
		
		if (D3DResult == E_OUTOFMEMORY)
		{
			TerminateOnOutOfMemory(D3DResult, false);
		}
		else
		{
			TerminateOnGPUCrash(Device, nullptr, 0);
		}

		// Make sure the log is flushed!
		GLog->PanicFlushThreadedLogs();
		GLog->Flush();

		UE_LOG(LogD3D12RHI, Fatal, TEXT("%s failed \n at %s:%u \n with error %s\n%s"), ANSI_TO_TCHAR(Code), ANSI_TO_TCHAR(Filename), Line, *ErrorString, *Message);

		// Force shutdown, we can't do anything useful anymore.
		FPlatformMisc::RequestExit(true);
	}

	void VerifyD3D12CreateTextureResult(HRESULT D3DResult, const ANSICHAR* Code, const ANSICHAR* Filename, uint32 Line, const D3D12_RESOURCE_DESC& TextureDesc, ID3D12Device* Device)
	{
		check(FAILED(D3DResult));

		const FString ErrorString = GetD3D12ErrorString(D3DResult, nullptr);
		const TCHAR* D3DFormatString = GetD3D12TextureFormatString(TextureDesc.Format);

		UE_LOG(LogD3D12RHI, Error,
			TEXT("%s failed \n at %s:%u \n with error %s, \n Size=%ix%ix%i Format=%s(0x%08X), NumMips=%i, Flags=%s"),
			ANSI_TO_TCHAR(Code),
			ANSI_TO_TCHAR(Filename),
			Line,
			*ErrorString,
			TextureDesc.Width,
			TextureDesc.Height,
			TextureDesc.DepthOrArraySize,
			D3DFormatString,
			TextureDesc.Format,
			TextureDesc.MipLevels,
			*GetD3D12TextureFlagString(TextureDesc.Flags));

		// Terminate with device removed but we don't have any GPU crash dump information
		if (D3DResult == DXGI_ERROR_DEVICE_REMOVED || D3DResult == DXGI_ERROR_DEVICE_HUNG)
		{
			TerminateOnGPUCrash(Device, nullptr, 0);
		}
		else if (D3DResult == E_OUTOFMEMORY)
		{
			TerminateOnOutOfMemory(D3DResult, true);

#if STATS
			GetRendererModule().DebugLogOnCrash();
#endif // STATS
		}

		// Make sure the log is flushed!
		GLog->PanicFlushThreadedLogs();
		GLog->Flush();

		UE_LOG(LogD3D12RHI, Fatal,
			TEXT("%s failed \n at %s:%u \n with error %s, \n Size=%ix%ix%i Format=%s(0x%08X), NumMips=%i, Flags=%s"),
			ANSI_TO_TCHAR(Code),
			ANSI_TO_TCHAR(Filename),
			Line,
			*ErrorString,
			TextureDesc.Width,
			TextureDesc.Height,
			TextureDesc.DepthOrArraySize,
			D3DFormatString,
			TextureDesc.Format,
			TextureDesc.MipLevels,
			*GetD3D12TextureFlagString(TextureDesc.Flags));

		// Force shutdown, we can't do anything useful anymore.
		FPlatformMisc::RequestExit(true);
	}

	void VerifyComRefCount(IUnknown* Object, int32 ExpectedRefs, const TCHAR* Code, const TCHAR* Filename, int32 Line)
	{
		int32 NumRefs;

		if (Object)
		{
			Object->AddRef();
			NumRefs = Object->Release();

			checkSlow(NumRefs != ExpectedRefs);

			if (NumRefs != ExpectedRefs)
			{
				UE_LOG(
					LogD3D12RHI,
					Error,
					TEXT("%s:(%d): %s has %d refs, expected %d"),
					Filename,
					Line,
					Code,
					NumRefs,
					ExpectedRefs
					);
			}
		}
	}
}

void FD3D12QuantizedBoundShaderState::InitShaderRegisterCounts(const D3D12_RESOURCE_BINDING_TIER& ResourceBindingTier, const FShaderCodePackedResourceCounts& Counts, FShaderRegisterCounts& Shader, bool bAllowUAVs)
{
	static const uint32 MaxSamplerCount = MAX_SAMPLERS;
	static const uint32 MaxConstantBufferCount = MAX_CBS;
	static const uint32 MaxShaderResourceCount = MAX_SRVS;
	static const uint32 MaxUnorderedAccessCount = MAX_UAVS;

	// Round up and clamp values to their max
	// Note: Rounding and setting counts based on binding tier allows us to create fewer root signatures.

	// To reduce the size of the root signature, we only allow UAVs for certain shaders. 
	// This code makes the assumption that the engine only uses UAVs at the PS or CS shader stages.
	check(bAllowUAVs || (!bAllowUAVs && Counts.NumUAVs == 0));

	if (ResourceBindingTier <= D3D12_RESOURCE_BINDING_TIER_1)
	{
		Shader.SamplerCount = (Counts.NumSamplers > 0) ? FMath::Min(MaxSamplerCount, FMath::RoundUpToPowerOfTwo(Counts.NumSamplers)) : Counts.NumSamplers;
		Shader.ShaderResourceCount = (Counts.NumSRVs > 0) ? FMath::Min(MaxShaderResourceCount, FMath::RoundUpToPowerOfTwo(Counts.NumSRVs)) : Counts.NumSRVs;
	}
	else
	{
		Shader.SamplerCount = MaxSamplerCount;
		Shader.ShaderResourceCount = MaxShaderResourceCount;
	}

	if (ResourceBindingTier <= D3D12_RESOURCE_BINDING_TIER_2)
	{
		Shader.ConstantBufferCount = (Counts.NumCBs > MAX_ROOT_CBVS) ? FMath::Min(MaxConstantBufferCount, FMath::RoundUpToPowerOfTwo(Counts.NumCBs)) : Counts.NumCBs;
		Shader.UnorderedAccessCount = (Counts.NumUAVs > 0 && bAllowUAVs) ? FMath::Min(MaxUnorderedAccessCount, FMath::RoundUpToPowerOfTwo(Counts.NumUAVs)) : 0;
	}
	else
	{
		Shader.ConstantBufferCount = (Counts.NumCBs > MAX_ROOT_CBVS) ? MaxConstantBufferCount : Counts.NumCBs;
		Shader.UnorderedAccessCount = (bAllowUAVs) ? MaxUnorderedAccessCount : 0;
	}
}

void QuantizeBoundShaderState(
	const D3D12_RESOURCE_BINDING_TIER& ResourceBindingTier,
	const FD3D12BoundShaderState* const BSS,
	FD3D12QuantizedBoundShaderState &QBSS
	)
{
	// BSS quantizer. There is a 1:1 mapping of quantized bound shader state objects to root signatures.
	// The objective is to allow a single root signature to represent many bound shader state objects.
	// The bigger the quantization step sizes, the fewer the root signatures.
	FMemory::Memzero(&QBSS, sizeof(QBSS));
	QBSS.bAllowIAInputLayout = BSS->GetVertexDeclaration() != nullptr;	// Does the root signature need access to vertex buffers?

	const FD3D12VertexShader* const VertexShader = BSS->GetVertexShader();
	const FD3D12PixelShader* const PixelShader = BSS->GetPixelShader();
	const FD3D12HullShader* const HullShader = BSS->GetHullShader();
	const FD3D12DomainShader* const DomainShader = BSS->GetDomainShader();
	const FD3D12GeometryShader* const GeometryShader = BSS->GetGeometryShader();
	if (VertexShader) FD3D12QuantizedBoundShaderState::InitShaderRegisterCounts(ResourceBindingTier, VertexShader->ResourceCounts, QBSS.RegisterCounts[SV_Vertex]);
	if (PixelShader) FD3D12QuantizedBoundShaderState::InitShaderRegisterCounts(ResourceBindingTier, PixelShader->ResourceCounts, QBSS.RegisterCounts[SV_Pixel], true);
	if (HullShader) FD3D12QuantizedBoundShaderState::InitShaderRegisterCounts(ResourceBindingTier, HullShader->ResourceCounts, QBSS.RegisterCounts[SV_Hull]);
	if (DomainShader) FD3D12QuantizedBoundShaderState::InitShaderRegisterCounts(ResourceBindingTier, DomainShader->ResourceCounts, QBSS.RegisterCounts[SV_Domain]);
	if (GeometryShader) FD3D12QuantizedBoundShaderState::InitShaderRegisterCounts(ResourceBindingTier, GeometryShader->ResourceCounts, QBSS.RegisterCounts[SV_Geometry]);
}

static void QuantizeBoundShaderStateCommon(
	D3D12_RESOURCE_BINDING_TIER ResourceBindingTier,
	const FShaderCodePackedResourceCounts& ResourceCounts,
	EShaderVisibility ShaderVisibility,
	bool bAllowUAVs,
	FD3D12QuantizedBoundShaderState &OutQBSS
)
{
	// BSS quantizer. There is a 1:1 mapping of quantized bound shader state objects to root signatures.
	// The objective is to allow a single root signature to represent many bound shader state objects.
	// The bigger the quantization step sizes, the fewer the root signatures.
	FMemory::Memzero(&OutQBSS, sizeof(OutQBSS));
	FD3D12QuantizedBoundShaderState::InitShaderRegisterCounts(ResourceBindingTier, ResourceCounts, OutQBSS.RegisterCounts[ShaderVisibility], bAllowUAVs);
}

void QuantizeBoundShaderState(
	const D3D12_RESOURCE_BINDING_TIER& ResourceBindingTier,
	const FD3D12ComputeShader* const ComputeShader,
	FD3D12QuantizedBoundShaderState &OutQBSS
	)
{
	check(ComputeShader);
	const bool bAllosUAVs = true;
	QuantizeBoundShaderStateCommon(ResourceBindingTier, ComputeShader->ResourceCounts, SV_All, bAllosUAVs, OutQBSS);
	check(OutQBSS.bAllowIAInputLayout == false); // No access to vertex buffers needed
}

#if D3D12_RHI_RAYTRACING

FD3D12QuantizedBoundShaderState GetRayTracingGlobalRootSignatureDesc()
{
	FD3D12QuantizedBoundShaderState OutQBSS = {};
	FShaderRegisterCounts& QBSSRegisterCounts = OutQBSS.RegisterCounts[SV_All];

	OutQBSS.RootSignatureType = RS_RayTracingGlobal;

	QBSSRegisterCounts.SamplerCount = MAX_SAMPLERS;
	QBSSRegisterCounts.ShaderResourceCount = MAX_SRVS;
	QBSSRegisterCounts.ConstantBufferCount = MAX_CBS;
	QBSSRegisterCounts.UnorderedAccessCount = MAX_UAVS;

	return OutQBSS;
}

void QuantizeBoundShaderState(
	EShaderFrequency ShaderFrequency,
	const D3D12_RESOURCE_BINDING_TIER& ResourceBindingTier,
	const FD3D12RayTracingShader* const RayTracingShader,
	FD3D12QuantizedBoundShaderState &OutQBSS
)
{
	FMemory::Memzero(&OutQBSS, sizeof(OutQBSS));
	FShaderRegisterCounts& QBSSRegisterCounts = OutQBSS.RegisterCounts[SV_All];

	switch (ShaderFrequency)
	{
	case SF_RayGen:
	{
		// Shared conservative root signature layout is used for all raygen and miss shaders.

		OutQBSS = GetRayTracingGlobalRootSignatureDesc();

		break;
	}

	case SF_RayHitGroup:
	case SF_RayCallable:
	case SF_RayMiss:
	{
		// Local root signature is used for hit group shaders, using the exact number of resources to minimize shader binding table record size.

		check(RayTracingShader);
		const FShaderCodePackedResourceCounts& Counts = RayTracingShader->ResourceCounts;

		OutQBSS.RootSignatureType = RS_RayTracingLocal;

		QBSSRegisterCounts.SamplerCount = Counts.NumSamplers;
		QBSSRegisterCounts.ShaderResourceCount = Counts.NumSRVs;
		QBSSRegisterCounts.ConstantBufferCount = Counts.NumCBs;
		QBSSRegisterCounts.UnorderedAccessCount = Counts.NumUAVs;

		check(QBSSRegisterCounts.SamplerCount <= MAX_SAMPLERS);
		check(QBSSRegisterCounts.ShaderResourceCount <= MAX_SRVS);
		check(QBSSRegisterCounts.ConstantBufferCount <= MAX_CBS);
		check(QBSSRegisterCounts.UnorderedAccessCount <= MAX_UAVS);

		break;
	}
	default:
		checkNoEntry(); // Unexpected shader target frequency
	}
}
#endif // D3D12_RHI_RAYTRACING

FD3D12BoundRenderTargets::FD3D12BoundRenderTargets(FD3D12RenderTargetView** RTArray, uint32 NumActiveRTs, FD3D12DepthStencilView* DSView)
{
	FMemory::Memcpy(RenderTargetViews, RTArray, sizeof(RenderTargetViews));
	DepthStencilView = DSView;
	NumActiveTargets = NumActiveRTs;
}

FD3D12BoundRenderTargets::~FD3D12BoundRenderTargets()
{
}

void LogExecuteCommandLists(uint32 NumCommandLists, ID3D12CommandList* const* ppCommandLists)
{
	for (uint32 i = 0; i < NumCommandLists; i++)
	{
		ID3D12CommandList* const pCurrentCommandList = ppCommandLists[i];
		UE_LOG(LogD3D12RHI, Log, TEXT("*** [tid:%08x] EXECUTE (CmdList: %016llX) %u/%u ***"), FPlatformTLS::GetCurrentThreadId(), pCurrentCommandList, i + 1, NumCommandLists);
	}
}

FString ConvertToResourceStateString(uint32 ResourceState)
{
	if (ResourceState == 0)
	{
		return FString(TEXT("D3D12_RESOURCE_STATE_COMMON"));
	}

	const TCHAR* ResourceStateNames[] =
	{
		TEXT("D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER"),
		TEXT("D3D12_RESOURCE_STATE_INDEX_BUFFER"),
		TEXT("D3D12_RESOURCE_STATE_RENDER_TARGET"),
		TEXT("D3D12_RESOURCE_STATE_UNORDERED_ACCESS"),
		TEXT("D3D12_RESOURCE_STATE_DEPTH_WRITE"),
		TEXT("D3D12_RESOURCE_STATE_DEPTH_READ"),
		TEXT("D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE"),
		TEXT("D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE"),
		TEXT("D3D12_RESOURCE_STATE_STREAM_OUT"),
		TEXT("D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT"),
		TEXT("D3D12_RESOURCE_STATE_COPY_DEST"),
		TEXT("D3D12_RESOURCE_STATE_COPY_SOURCE"),
		TEXT("D3D12_RESOURCE_STATE_RESOLVE_DEST"),
		TEXT("D3D12_RESOURCE_STATE_RESOLVE_SOURCE"),
	};

	FString ResourceStateString;
	uint16 NumStates = 0;
	for (uint16 i = 0; ResourceState && i < ARRAYSIZE(ResourceStateNames); i++)
	{
		if (ResourceState & 1)
		{
			if (NumStates > 0)
			{
				ResourceStateString += " | ";
			}

			ResourceStateString += ResourceStateNames[i];
			NumStates++;
		}
		ResourceState = ResourceState >> 1;
	}
	return ResourceStateString;
}

void LogResourceBarriers(uint32 NumBarriers, D3D12_RESOURCE_BARRIER* pBarriers, ID3D12CommandList* const pCommandList)
{
	// Configure what resource barriers are logged.
	const bool bLogAll = false;
	const bool bLogTransitionDepth = true;
	const bool bLogTransitionRenderTarget = true;
	const bool bLogTransitionUAV = true;

	// Create the state bit mask to indicate what barriers should be logged.
	uint32 ShouldLogMask = bLogAll ? static_cast<uint32>(-1) : 0;
	ShouldLogMask |= bLogTransitionDepth ? D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_DEPTH_WRITE : 0;
	ShouldLogMask |= bLogTransitionRenderTarget ? D3D12_RESOURCE_STATE_RENDER_TARGET : 0;
	ShouldLogMask |= bLogTransitionUAV ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : 0;

	for (uint32 i = 0; i < NumBarriers; i++)
	{
		D3D12_RESOURCE_BARRIER &currentBarrier = pBarriers[i];

		switch (currentBarrier.Type)
		{
		case D3D12_RESOURCE_BARRIER_TYPE_TRANSITION:
		{
			const FString StateBefore = ConvertToResourceStateString(static_cast<uint32>(currentBarrier.Transition.StateBefore));
			const FString StateAfter = ConvertToResourceStateString(static_cast<uint32>(currentBarrier.Transition.StateAfter));

			bool bShouldLog = bLogAll;
			if (!bShouldLog)
			{
				// See if we should log this transition.
				for (uint32 j = 0; (j < 2) && !bShouldLog; j++)
				{
					const D3D12_RESOURCE_STATES& State = (j == 0) ? currentBarrier.Transition.StateBefore : currentBarrier.Transition.StateAfter;
					bShouldLog = (State & ShouldLogMask) > 0;
				}
			}

			if (bShouldLog)
			{
				UE_LOG(LogD3D12RHI, Log, TEXT("*** BARRIER (CmdList: %016llX) %u/%u: %016llX (Sub: %u), %s -> %s"), pCommandList, i + 1, NumBarriers,
					currentBarrier.Transition.pResource,
					currentBarrier.Transition.Subresource,
					*StateBefore,
					*StateAfter);
			}
			break;
		}

		case D3D12_RESOURCE_BARRIER_TYPE_UAV:
			UE_LOG(LogD3D12RHI, Log, TEXT("*** BARRIER (CmdList: %016llX) %u/%u: UAV Barrier"), pCommandList, i + 1, NumBarriers);
			break;

		default:
			check(false);
			break;
		}		
	}
}


//==================================================================================================================================
// CResourceState
// Tracking of per-resource or per-subresource state
//==================================================================================================================================
//----------------------------------------------------------------------------------------------------------------------------------
void CResourceState::Initialize(uint32 SubresourceCount)
{
	check(0 == m_SubresourceState.Num());

	// Allocate space for per-subresource tracking structures
	check(SubresourceCount > 0);
	m_SubresourceState.SetNumUninitialized(SubresourceCount);
	check(m_SubresourceState.Num() == SubresourceCount);

	// All subresources start out in an unknown state
	SetResourceState(D3D12_RESOURCE_STATE_TBD);
}

//----------------------------------------------------------------------------------------------------------------------------------
bool CResourceState::AreAllSubresourcesSame() const
{
	return m_AllSubresourcesSame && (m_ResourceState != D3D12_RESOURCE_STATE_TBD);
}

//----------------------------------------------------------------------------------------------------------------------------------
bool CResourceState::CheckResourceState(D3D12_RESOURCE_STATES State) const
{
	if (m_AllSubresourcesSame)
	{
		return State == m_ResourceState;
	}
	else
	{
		// All subresources must be individually checked
		const uint32 numSubresourceStates = m_SubresourceState.Num();
		for (uint32 i = 0; i < numSubresourceStates; i++)
		{
			if (m_SubresourceState[i] != State)
			{
				return false;
			}
		}

		return true;
	}
}

//----------------------------------------------------------------------------------------------------------------------------------
bool CResourceState::CheckResourceStateInitalized() const
{
	return m_SubresourceState.Num() > 0;
}

//----------------------------------------------------------------------------------------------------------------------------------
D3D12_RESOURCE_STATES CResourceState::GetSubresourceState(uint32 SubresourceIndex) const
{
	if (m_AllSubresourcesSame)
	{
		return m_ResourceState;
	}
	else
	{
		check(SubresourceIndex < static_cast<uint32>(m_SubresourceState.Num()));
		return m_SubresourceState[SubresourceIndex];
	}
}

//----------------------------------------------------------------------------------------------------------------------------------
void CResourceState::SetResourceState(D3D12_RESOURCE_STATES State)
{
	m_AllSubresourcesSame = 1;

	m_ResourceState = State;

	// State is now tracked per-resource, so m_SubresourceState should not be read.
#if UE_BUILD_DEBUG
	const uint32 numSubresourceStates = m_SubresourceState.Num();
	for (uint32 i = 0; i < numSubresourceStates; i++)
	{
		m_SubresourceState[i] = D3D12_RESOURCE_STATE_CORRUPT;
	}
#endif
}

//----------------------------------------------------------------------------------------------------------------------------------
void CResourceState::SetSubresourceState(uint32 SubresourceIndex, D3D12_RESOURCE_STATES State)
{
	// If setting all subresources, or the resource only has a single subresource, set the per-resource state
	if (SubresourceIndex == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES ||
		m_SubresourceState.Num() == 1)
	{
		SetResourceState(State);
	}
	else
	{
		check(SubresourceIndex < static_cast<uint32>(m_SubresourceState.Num()));

		// If state was previously tracked on a per-resource level, then transition to per-subresource tracking
		if (m_AllSubresourcesSame)
		{
			const uint32 numSubresourceStates = m_SubresourceState.Num();
			for (uint32 i = 0; i < numSubresourceStates; i++)
			{
				m_SubresourceState[i] = m_ResourceState;
			}

			m_AllSubresourcesSame = 0;

			// State is now tracked per-subresource, so m_ResourceState should not be read.
#if UE_BUILD_DEBUG
			m_ResourceState = D3D12_RESOURCE_STATE_CORRUPT;
#endif
		}

		m_SubresourceState[SubresourceIndex] = State;
	}
}

bool FD3D12SyncPoint::IsValid() const
{
	return Fence != nullptr;
}

bool FD3D12SyncPoint::IsComplete() const
{
	check(IsValid());
	return Fence->IsFenceComplete(Value);
}

void FD3D12SyncPoint::WaitForCompletion() const
{
	check(IsValid());
	Fence->WaitForFence(Value);
}

// Forward declarations are required for the template functions
template bool AssertResourceState(ID3D12CommandList* pCommandList, FD3D12View<D3D12_RENDER_TARGET_VIEW_DESC>* pView, const D3D12_RESOURCE_STATES& State);
template bool AssertResourceState(ID3D12CommandList* pCommandList, FD3D12View<D3D12_UNORDERED_ACCESS_VIEW_DESC>* pView, const D3D12_RESOURCE_STATES& State);
template bool AssertResourceState(ID3D12CommandList* pCommandList, FD3D12View<D3D12_SHADER_RESOURCE_VIEW_DESC>* pView, const D3D12_RESOURCE_STATES& State);

template <class TView>
bool AssertResourceState(ID3D12CommandList* pCommandList, FD3D12View<TView>* pView, const D3D12_RESOURCE_STATES& State)
{
	// Check the view
	if (!pView)
	{
		// No need to check null views
		return true;
	}

	return AssertResourceState(pCommandList, pView->GetResource(), State, pView->GetViewSubresourceSubset());
}

bool AssertResourceState(ID3D12CommandList* pCommandList, FD3D12Resource* pResource, const D3D12_RESOURCE_STATES& State, uint32 Subresource)
{
	// Check the resource
	if (!pResource)
	{
		// No need to check null resources
		// Some dynamic SRVs haven't been mapped and updated yet so they actually don't have any backing resources.
		return true;
	}

	CViewSubresourceSubset SubresourceSubset(Subresource, pResource->GetMipLevels(), pResource->GetArraySize(), pResource->GetPlaneCount());
	return AssertResourceState(pCommandList, pResource, State, SubresourceSubset);
}

bool AssertResourceState(ID3D12CommandList* pCommandList, FD3D12Resource* pResource, const D3D12_RESOURCE_STATES& State, const CViewSubresourceSubset& SubresourceSubset)
{
#if PLATFORM_WINDOWS
	// Check the resource
	if (!pResource)
	{
		// No need to check null resources
		// Some dynamic SRVs haven't been mapped and updated yet so they actually don't have any backing resources.
		return true;
	}

	// Can only verify resource states if the debug layer is used
	static const bool bWithD3DDebug = D3D12RHI_ShouldCreateWithD3DDebug();
	if (!bWithD3DDebug)
	{
		UE_LOG(LogD3D12RHI, Fatal, TEXT("*** AssertResourceState requires the debug layer ***"));
		return false;
	}

	// Get the debug command queue
	TRefCountPtr<ID3D12DebugCommandList> pDebugCommandList;
	VERIFYD3D12RESULT(pCommandList->QueryInterface(pDebugCommandList.GetInitReference()));

	// Get the underlying resource
	ID3D12Resource* pD3D12Resource = pResource->GetResource();
	check(pD3D12Resource);

	// For each subresource in the view...
	for (CViewSubresourceSubset::CViewSubresourceIterator it = SubresourceSubset.begin(); it != SubresourceSubset.end(); ++it)
	{
		for (uint32 SubresourceIndex = it.StartSubresource(); SubresourceIndex < it.EndSubresource(); SubresourceIndex++)
		{
			const bool bGoodState = !!pDebugCommandList->AssertResourceState(pD3D12Resource, SubresourceIndex, State);
			if (!bGoodState)
			{
				return false;
			}
		}
	}
#endif // PLATFORM_WINDOWS

	return true;
}

//
// Stat declarations.
//

DEFINE_STAT(STAT_D3D12PresentTime);
DEFINE_STAT(STAT_D3D12CustomPresentTime);

DEFINE_STAT(STAT_D3D12NumCommandAllocators);
DEFINE_STAT(STAT_D3D12NumCommandLists);
DEFINE_STAT(STAT_D3D12NumPSOs);

DEFINE_STAT(STAT_D3D12TexturesAllocated);
DEFINE_STAT(STAT_D3D12TexturesReleased);
DEFINE_STAT(STAT_D3D12CreateTextureTime);
DEFINE_STAT(STAT_D3D12LockTextureTime);
DEFINE_STAT(STAT_D3D12UnlockTextureTime);
DEFINE_STAT(STAT_D3D12CreateBufferTime);
DEFINE_STAT(STAT_D3D12CopyToStagingBufferTime);
DEFINE_STAT(STAT_D3D12LockBufferTime);
DEFINE_STAT(STAT_D3D12UnlockBufferTime);
DEFINE_STAT(STAT_D3D12CommitTransientResourceTime);
DEFINE_STAT(STAT_D3D12DecommitTransientResourceTime);

DEFINE_STAT(STAT_D3D12NewBoundShaderStateTime);
DEFINE_STAT(STAT_D3D12CreateBoundShaderStateTime);
DEFINE_STAT(STAT_D3D12NumBoundShaderState);
DEFINE_STAT(STAT_D3D12SetBoundShaderState);

DEFINE_STAT(STAT_D3D12UpdateUniformBufferTime);

DEFINE_STAT(STAT_D3D12CommitResourceTables);
DEFINE_STAT(STAT_D3D12SetTextureInTableCalls);

DEFINE_STAT(STAT_D3D12ClearShaderResourceViewsTime);
DEFINE_STAT(STAT_D3D12SetShaderResourceViewTime);
DEFINE_STAT(STAT_D3D12SetUnorderedAccessViewTime);
DEFINE_STAT(STAT_D3D12CommitGraphicsConstants);
DEFINE_STAT(STAT_D3D12CommitComputeConstants);
DEFINE_STAT(STAT_D3D12SetShaderUniformBuffer);

DEFINE_STAT(STAT_D3D12ApplyStateTime);
DEFINE_STAT(STAT_D3D12ApplyStateRebuildPSOTime);
DEFINE_STAT(STAT_D3D12ApplyStateFindPSOTime);
DEFINE_STAT(STAT_D3D12ApplyStateSetSRVTime);
DEFINE_STAT(STAT_D3D12ApplyStateSetUAVTime);
DEFINE_STAT(STAT_D3D12ApplyStateSetVertexBufferTime);
DEFINE_STAT(STAT_D3D12ApplyStateSetConstantBufferTime);
DEFINE_STAT(STAT_D3D12ClearMRT);

DEFINE_STAT(STAT_D3D12ExecuteCommandListTime);
DEFINE_STAT(STAT_D3D12WaitForFenceTime);

DEFINE_STAT(STAT_D3D12UsedVideoMemory);
DEFINE_STAT(STAT_D3D12AvailableVideoMemory);
DEFINE_STAT(STAT_D3D12TotalVideoMemory);
DEFINE_STAT(STAT_D3D12TextureAllocatorWastage);

DEFINE_STAT(STAT_D3D12BufferPoolMemoryAllocated);
DEFINE_STAT(STAT_D3D12BufferPoolMemoryUsed);
DEFINE_STAT(STAT_D3D12BufferPoolMemoryFree);
DEFINE_STAT(STAT_D3D12BufferPoolAlignmentWaste);
DEFINE_STAT(STAT_D3D12BufferPoolPageCount);
DEFINE_STAT(STAT_D3D12BufferPoolFullPages);
DEFINE_STAT(STAT_D3D12BufferStandAloneUsedMemory);

DEFINE_STAT(STAT_UniqueSamplers);

DEFINE_STAT(STAT_ViewHeapChanged);
DEFINE_STAT(STAT_SamplerHeapChanged);

DEFINE_STAT(STAT_NumViewOnlineDescriptorHeaps);
DEFINE_STAT(STAT_NumSamplerOnlineDescriptorHeaps);
DEFINE_STAT(STAT_NumReuseableSamplerOnlineDescriptorTables);
DEFINE_STAT(STAT_NumReuseableSamplerOnlineDescriptors);
DEFINE_STAT(STAT_NumReservedViewOnlineDescriptors);
DEFINE_STAT(STAT_NumReservedSamplerOnlineDescriptors);
DEFINE_STAT(STAT_NumReusedSamplerOnlineDescriptors);

DEFINE_STAT(STAT_GlobalViewHeapFreeDescriptors);
DEFINE_STAT(STAT_GlobalViewHeapReservedDescriptors);
DEFINE_STAT(STAT_GlobalViewHeapUsedDescriptors);
DEFINE_STAT(STAT_GlobalViewHeapWastedDescriptors);
DEFINE_STAT(STAT_GlobalViewHeapBlockAllocations);

DEFINE_STAT(STAT_ViewOnlineDescriptorHeapMemory);
DEFINE_STAT(STAT_SamplerOnlineDescriptorHeapMemory);

#undef LOCTEXT_NAMESPACE
