// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Synchronization/DisplayClusterRenderSyncPolicyNvidiaBase.h"

#include "Config/IPDisplayClusterConfigManager.h"
#include "Config/DisplayClusterConfigTypes.h"
#include "DisplayClusterGlobals.h"
#include "DisplayClusterLog.h"

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


namespace
{
	static IUnknown*       D3DDevice     = nullptr;
	static IDXGISwapChain* DXGISwapChain = nullptr;

	static NvU32 RequestedGroup   = 1;
	static NvU32 RequestedBarrier = 1;
}


FDisplayClusterRenderSyncPolicyNvidiaBase::FDisplayClusterRenderSyncPolicyNvidiaBase()
{
	const NvAPI_Status NvApiResult = NvAPI_Initialize();
	if (NvApiResult != NVAPI_OK)
	{
		UE_LOG(LogDisplayClusterRenderSync, Error, TEXT("NvAPI_Initialize() failed, error 0x%x"), NvApiResult);
	}
	else
	{
		bNvApiInitialised = true;
	}
}

FDisplayClusterRenderSyncPolicyNvidiaBase::~FDisplayClusterRenderSyncPolicyNvidiaBase()
{
	if (bNvApiInitialised)
	{
		if (bNvApiIBarrierSet)
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
	if (!bNvApiIBarrierSet)
	{
		// Use network barrier to guarantee that all NVIDIA barriers are initialized simultaneously
		SyncBarrierRenderThread();
		// Set up NVIDIA swap barrier
		bNvApiIBarrierSet = InitializeNvidiaSwapLock();
	}

	// Check if all required objects are available
	if (!D3DDevice || !DXGISwapChain)
	{
		// Present frame on a higher level
		return true;
	}

	if (bNvApiIBarrierSet && D3DDevice && DXGISwapChain)
	{
		// Present frame via NVIDIA API
		const NvAPI_Status NvApiResult = NvAPI_D3D1x_Present(D3DDevice, DXGISwapChain, (UINT)InOutSyncInterval, (UINT)0);
		if (NvApiResult != NVAPI_OK)
		{
			UE_LOG(LogDisplayClusterRenderSync, Warning, TEXT("NVAPI: An error occurred during frame presentation, error code 0x%x"), NvApiResult);
			// Present frame on a higher level
			return true;
		}

		// We presented current frame so no need to present it on higher level
		return false;
	}
	else
	{
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

	// Get requested sync group and barrier from config file (or defaults)
	IPDisplayClusterConfigManager* const ConfigMgr = GDisplayCluster->GetPrivateConfigMgr();
	if (ConfigMgr)
	{
		FDisplayClusterConfigNvidia CfgNvidia = ConfigMgr->GetConfigNvidia();
		RequestedGroup   = static_cast<NvU32>(CfgNvidia.SyncGroup);
		RequestedBarrier = static_cast<NvU32>(CfgNvidia.SyncBarrier);
	}

	// Join swap group
	NvApiResult = NvAPI_D3D1x_JoinSwapGroup(D3DDevice, DXGISwapChain, RequestedGroup, true);
	if(NvApiResult != NVAPI_OK)
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
	bNvApiIBarrierSet = true;

	return true;
}
