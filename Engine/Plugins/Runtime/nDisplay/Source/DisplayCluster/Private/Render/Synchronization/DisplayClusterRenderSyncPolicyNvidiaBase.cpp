// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Synchronization/DisplayClusterRenderSyncPolicyNvidiaBase.h"

#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterHelpers.h"
#include "Misc/DisplayClusterLog.h"
#include "Misc/DisplayClusterTypesConverter.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "DynamicRHI.h"

#pragma warning(push)
#pragma warning (disable : 4005)
#include "Windows/AllowWindowsPlatformTypes.h"
#include "dxgi.h"
#include "d3d11.h"
#include "nvapi.h"
#include "Windows/HideWindowsPlatformTypes.h"
#pragma warning(pop)


// TEMPORARY DIAGNOSTICS START
static TAutoConsoleVariable<int32> CVarNvidiaSyncDiagnosticsInit(
	TEXT("nDisplay.sync.nvidia.diag.init"),
	0,
	TEXT("NVAPI diagnostics: init\n")
	TEXT("0 : disabled\n")
	TEXT("1 : enabled\n")
	,
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarNvidiaSyncDiagnosticsPresent(
	TEXT("nDisplay.sync.nvidia.diag.present"),
	0,
	TEXT("NVAPI diagnostics: present\n")
	TEXT("0 : disabled\n")
	TEXT("1 : enabled\n")
	,
	ECVF_RenderThreadSafe
);
// TEMPORARY DIAGNOSTICS END


namespace
{
	static IUnknown*       D3DDevice     = nullptr;
	static IDXGISwapChain* DXGISwapChain = nullptr;

	static uint32 RequestedGroup   = 1;
	static uint32 RequestedBarrier = 1;
}


FDisplayClusterRenderSyncPolicyNvidiaBase::FDisplayClusterRenderSyncPolicyNvidiaBase(const TMap<FString, FString>& Parameters)
	: FDisplayClusterRenderSyncPolicyBase(Parameters)
{
	const NvAPI_Status NvApiResult = NvAPI_Initialize();
	if (NvApiResult != NVAPI_OK)
	{
		UE_LOG(LogDisplayClusterRenderSync, Error, TEXT("NvAPI_Initialize() failed, error 0x%x"), NvApiResult);
	}
	else
	{
		bNvApiInitialized = true;
	}
}

FDisplayClusterRenderSyncPolicyNvidiaBase::~FDisplayClusterRenderSyncPolicyNvidiaBase()
{
	if (bNvApiInitialized)
	{
		if (bNvApiBarrierSet)
		{
			// Unbind from swap barrier
			NvAPI_D3D1x_BindSwapBarrier(D3DDevice, RequestedGroup, 0);
			// Leave swap group
			NvAPI_D3D1x_JoinSwapGroup(D3DDevice, DXGISwapChain, 0, false);
		}

		NvAPI_Unload();
	}
}

bool FDisplayClusterRenderSyncPolicyNvidiaBase::SynchronizeClusterRendering(int32& InOutSyncInterval)
{
	// Initialize barriers at first call
	if (!bNvApiBarrierSet)
	{
		bNvDiagInit = (CVarNvidiaSyncDiagnosticsInit.GetValueOnRenderThread() != 0);
		bNvDiagPresent = (CVarNvidiaSyncDiagnosticsPresent.GetValueOnRenderThread() != 0);
		UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("NVAPI DIAG: init=%d present=%d"), bNvDiagInit ? 1 : 0, bNvDiagPresent ? 1 : 0);

		// Use network barrier to guarantee that all NVIDIA barriers are initialized simultaneously
		SyncBarrierRenderThread();
		// Set up NVIDIA swap barrier
		bNvApiBarrierSet = InitializeNvidiaSwapLock();
	}

	// Check if all required objects are available
	if (!D3DDevice || !DXGISwapChain)
	{
		UE_LOG(LogDisplayClusterRenderSync, Warning, TEXT("Couldn't get DX resources, no swap synchronization will be performed"));
		// Present frame on a higher level
		return true;
	}

	// NVAPI Diagnostics: present
	// Align all threads on the timescale before calling NvAPI_D3D1x_Present
	// As a side-effect, it should avoid NvAPI_D3D1x_Present stuck on application kill
	if (bNvDiagPresent)
	{
		UE_LOG(LogDisplayClusterRenderSync, VeryVerbose, TEXT("NVAPI DIAG: wait start"));
		SyncBarrierRenderThread();
		UE_LOG(LogDisplayClusterRenderSync, VeryVerbose, TEXT("NVAPI DIAG: wait end"));
	}

	if (bNvApiBarrierSet && D3DDevice && DXGISwapChain)
	{
		UE_LOG(LogDisplayClusterRenderSync, VeryVerbose, TEXT("NVAPI: presenting the frame with sync..."));

		// Present frame via NVIDIA API
		const NvAPI_Status NvApiResult = NvAPI_D3D1x_Present(D3DDevice, DXGISwapChain, (UINT)InOutSyncInterval, (UINT)0);
		if (NvApiResult != NVAPI_OK)
		{
			UE_LOG(LogDisplayClusterRenderSync, Warning, TEXT("NVAPI: An error occurred during frame presentation, error code 0x%x"), NvApiResult);
			// Present frame on a higher level
			return true;
		}

		UE_LOG(LogDisplayClusterRenderSync, VeryVerbose, TEXT("NVAPI: the frame has been presented successfully"));

		// We presented current frame so no need to present it on higher level
		return false;
	}
	else
	{
		UE_LOG(LogDisplayClusterRenderSync, VeryVerbose, TEXT("NVAPI: Can't synchronize frame presentation"));
		// Something went wrong, let upper level present this frame
		return true;
	}
}

bool FDisplayClusterRenderSyncPolicyNvidiaBase::InitializeNvidiaSwapLock()
{
	// Get D3D1XDevice
	D3DDevice = static_cast<IUnknown*>(GDynamicRHI->RHIGetNativeDevice());
	check(D3DDevice);
	
	// Get IDXGISwapChain
	DXGISwapChain = static_cast<IDXGISwapChain*>(GEngine->GameViewport->Viewport->GetViewportRHI().GetReference()->GetNativeSwapChain());
	check(DXGISwapChain);

	if (!D3DDevice || !DXGISwapChain)
	{
		UE_LOG(LogDisplayClusterRenderSync, Error, TEXT("Couldn't get DX context data for NVIDIA swap barrier initialization"));
		return false;
	}

	// NVAPI Diagnostics: init
	// Here we guarantee all the nodes initialize NVIDIA sync on the same frame interval on the timescale
	if (bNvDiagInit)
	{
		IDXGIOutput* DXOutput = nullptr;
		DXGISwapChain->GetContainingOutput(&DXOutput);
		if (DXOutput)
		{
			UE_LOG(LogDisplayClusterRenderSync, VeryVerbose, TEXT("NVAPI DIAG: init start"));

			// Wait for the next V-blank interval
			DXOutput->WaitForVBlank();
			// Sleep a bit to make sure the V-blank period is over and none of the nodes will present a frame
			FPlatformProcess::Sleep(0.001f);
			// Sync on a network barrier just in case
			SyncBarrierRenderThread();

			UE_LOG(LogDisplayClusterRenderSync, VeryVerbose, TEXT("NVAPI DIAG: init end"));
		}
	}

	NvU32 MaxGroups = 0;
	NvU32 MaxBarriers = 0;

	// Get amount of available groups and barriers
	NvAPI_Status NvApiResult = NVAPI_ERROR;
	NvApiResult = NvAPI_D3D1x_QueryMaxSwapGroup(D3DDevice, &MaxGroups, &MaxBarriers);
	if (NvApiResult != NVAPI_OK)
	{
		UE_LOG(LogDisplayClusterRenderSync, Error, TEXT("NVAPI: Couldn't query group/barrier limits, error code 0x%x"), NvApiResult);
		return false;
	}

	// Make sure resources are available
	UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("NVAPI: max_groups=%d max_barriers=%d"), (int)MaxGroups, (int)MaxBarriers);
	if (!(MaxGroups > 0 && MaxBarriers > 0))
	{
		UE_LOG(LogDisplayClusterRenderSync, Error, TEXT("NVAPI: No available groups or barriers"));
		return false;
	}

	DisplayClusterHelpers::map::template ExtractValueFromString(GetParameters(), FString("SyncGroup"),   RequestedGroup);
	DisplayClusterHelpers::map::template ExtractValueFromString(GetParameters(), FString("SyncBarrier"), RequestedBarrier);

	// Join swap group
	NvApiResult = NvAPI_D3D1x_JoinSwapGroup(D3DDevice, DXGISwapChain, RequestedGroup, true);
	if (NvApiResult != NVAPI_OK)
	{
		UE_LOG(LogDisplayClusterRenderSync, Error, TEXT("NVAPI: Couldn't join swap group %d, error code 0x%x"), RequestedGroup, NvApiResult);
		return false;
	}
	else
	{
		UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("NVAPI: Successfully joined the swap group %d"), RequestedGroup);
	}

	// Bind to sync barrier
	NvApiResult = NvAPI_D3D1x_BindSwapBarrier(D3DDevice, RequestedGroup, RequestedBarrier);
	if (NvApiResult != NVAPI_OK)
	{
		UE_LOG(LogDisplayClusterRenderSync, Error, TEXT("NVAPI: Couldn't bind group %d to swap barrier %d, error code 0x%x"), RequestedGroup, RequestedBarrier, NvApiResult);
		return false;
	}
	else
	{
		UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("NVAPI: Successfully bound group %d to the swap barrier %d"), RequestedGroup, RequestedBarrier);
	}

	// Set barrier initialization flag
	bNvApiBarrierSet = true;

	return true;
}
