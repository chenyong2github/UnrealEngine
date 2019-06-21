// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Render/Synchronization/DisplayClusterRenderSyncPolicySoftwareDX11.h"

#include "DisplayClusterLog.h"

#include "Engine/GameViewportClient.h"
#include "Engine/GameEngine.h"

#include "Stats/Stats2.h"

#include "DisplayClusterUtils/DisplayClusterTypesConverter.h"

#include "Render/Device/IDisplayClusterRenderDevice.h"
#include "Render/Device/DisplayClusterDeviceInternals.h"

#include "d3d11.h"
#include "dxgi1_2.h"
#include "D3D11Viewport.h"

#include "RenderResource.h"
#include "RenderUtils.h"

#include "UObject/Stack.h"

#include <exception>
#include "dwmapi.h"

#include "NVIDIA/nvapi/nvapi.h"


static int CVarAdvancedSyncEnabled_Value = 0;
static FAutoConsoleVariableRef CVarAdvancedSyncEnabled(
	TEXT("nDisplay.render.softsync.AdvancedSyncEnabled"),
	CVarAdvancedSyncEnabled_Value,
	TEXT("Advanced DWM based render synchronization (0 = disabled)")
);

static float CVarVBlankThreshold_Value = 0.003f;
static FAutoConsoleVariableRef CVarVBlankThreshold(
	TEXT("nDisplay.render.softsync.VBlankThreshold"),
	CVarVBlankThreshold_Value,
	TEXT("Changes the threshold used to determine whether or not nDisplay SoftSync can make the current frame presentation")
);

static float CVarVBlankThresholdSleepMultiplier_Value = 1.5f;
static FAutoConsoleVariableRef CVarVBlankSleepThresholdSleepMultiplier(
	TEXT("nDisplay.render.softsync.VBlankThresholdSleepMultipler"),
	CVarVBlankThresholdSleepMultiplier_Value,
	TEXT("Multiplier applied to VBlankThreshold to compute sleep time to skip frame presentation on upcoming VBlank")
);

static int CVarVBlankBasisUpdate_Value = 1;
static FAutoConsoleVariableRef  CVarVBlankBasisUpdate(
	TEXT("nDisplay.render.softsync.VBlankBasisUpdate"),
	CVarVBlankBasisUpdate_Value,
	TEXT("Update VBlank basis periodically to avoid time drifting")
);

static float CVarVBlankBasisUpdatePeriod_Value = 120.f;
static FAutoConsoleVariableRef  CVarVBlankBasisUpdatePeriod(
	TEXT("nDisplay.render.softsync.VBlankBasisUpdatePeriod"),
	CVarVBlankBasisUpdatePeriod_Value,
	TEXT("VBlank basis update period in seconds")
);

static int CVarLogDwmStats_Value = 0;
static FAutoConsoleVariableRef  CVarLogDwmStats(
	TEXT("nDisplay.render.softsync.LogDwmStats"),
	CVarLogDwmStats_Value,
	TEXT("Print DWM stats to log")
);

static int CVarObtainGpuData_Value = 0;
static FAutoConsoleVariableRef  CVarObtainGpuData(
	TEXT("nDisplay.render.softsync.GpuDataObtain"),
	CVarObtainGpuData_Value,
	TEXT("Obtain GPU data each frame")
);


DECLARE_STATS_GROUP(TEXT("nDisplay Cluster Custom Present"), STATGROUP_nDisplayPresent, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("nDisplay Present Policy None"), STAT_nDisplayPresent_None, STATGROUP_nDisplayPresent);
DECLARE_CYCLE_STAT(TEXT("nDisplay Present Policy Soft Swap Sync"), STAT_nDisplayPresent_SoftSwapSync, STATGROUP_nDisplayPresent);
DECLARE_CYCLE_STAT(TEXT("nDisplay Present Policy Nv Swap Sync"), STAT_nDisplayPresent_NvSwapSync, STATGROUP_nDisplayPresent);


static NvU32 GSyncDeviceAmount = 0;
static NvGSyncDeviceHandle GSyncDeviceHandles[4];
static NvU32 PhysGPUAmount = 0;
static NvPhysicalGpuHandle PhysGPUHandles[NVAPI_MAX_PHYSICAL_GPUS];
static NvU32 DisplayAmount = 0;
static NvDisplayHandle DisplayHandles[NVAPI_MAX_DISPLAYS];


FDisplayClusterRenderSyncPolicySoftwareDX11::FDisplayClusterRenderSyncPolicySoftwareDX11()
{
	// We check the console variable once at start. The advanced synchronization policy is not 
	// supposed to be activated/deactivated at runtime. Use .ini settings to set desired value.
	bUseAdvancedSynchronization = (CVarAdvancedSyncEnabled_Value != 0);

	if (bUseAdvancedSynchronization)
	{
		const int Result = NvAPI_Initialize();
		if (Result != NVAPI_OK)
		{
			UE_LOG(LogDisplayClusterRender, Error, TEXT("Couldn't initialize NvAPI. Error code: %d"), Result);
			return;
		}

		UE_LOG(LogDisplayClusterRender, Log, TEXT("NvAPI has been initialized"));

		bNvApiInitialized = (Result == NVAPI_OK);
		if (bNvApiInitialized)
		{
			if (NvAPI_GSync_EnumSyncDevices(GSyncDeviceHandles, &GSyncDeviceAmount) != NVAPI_OK)
			{
				UE_LOG(LogDisplayClusterRender, Warning, TEXT("Couldn't enumerate GSync devices or no devices found"));
			}
			else
			{
				UE_LOG(LogDisplayClusterRender, Log, TEXT("Found sync devices: %u"), GSyncDeviceAmount);
			}

			if (NvAPI_EnumPhysicalGPUs(PhysGPUHandles, &PhysGPUAmount) != NVAPI_OK)
			{
				UE_LOG(LogDisplayClusterRender, Error, TEXT("Couldn't enumerate physical GPU devices"));
			}
			else
			{
				UE_LOG(LogDisplayClusterRender, Log, TEXT("Found physical GPUs: %u"), PhysGPUAmount);
			}

			while (NvAPI_EnumNvidiaDisplayHandle(DisplayAmount, &DisplayHandles[DisplayAmount]) != NVAPI_END_ENUMERATION)
			{
				++DisplayAmount;
				UE_LOG(LogDisplayClusterRender, Log, TEXT("Found displays: %u"), DisplayAmount);
			}
		}
	}
}

FDisplayClusterRenderSyncPolicySoftwareDX11::~FDisplayClusterRenderSyncPolicySoftwareDX11()
{
	if (bUseAdvancedSynchronization)
	{
		NvAPI_Unload();
	}
}

bool FDisplayClusterRenderSyncPolicySoftwareDX11::SynchronizeClusterRendering(int32& InOutSyncInterval)
{
	if (!bUseAdvancedSynchronization)
	{
		// Sync by barrier only
		SyncBarrierRenderThread();
		// Tell a caller that he still needs to present a frame
		return true;
	}

	//////////////////////////////////////////////////////
	// So the advanced sync approach is desired. Let's go!
	//////////////////////////////////////////////////////
	SCOPE_CYCLE_COUNTER(STAT_nDisplayPresent_SoftSwapSync);

	if (!GEngine || !GEngine->GameViewport || !GEngine->GameViewport->Viewport)
	{
		return false;
	}

	double B1B = 0.f;  // Barrier 1 before
	double B1A = 0.f;  // Barrier 1 after
	double TToB = 0.f; // Time to VBlank
	double SB = 0.f;   // Sleep before
	double SA = 0.f;   // Sleep after
	double B2B = 0.f;  // Barrier 2 before
	double B2A = 0.f;  // Barrier 2 after
	double PB = 0.f;   // Present before
	double PA = 0.f;   // Present after

	++FrameCounter;

	FD3D11Viewport* const Viewport = static_cast<FD3D11Viewport*>(GEngine->GameViewport->Viewport->GetViewportRHI().GetReference());
	check(Viewport);
	if (!Viewport)
	{
		UE_LOG(LogDisplayClusterRender, Warning, TEXT("Couldn't get a viewport. Native present will be performed."));
		return true;
	}

	IDXGISwapChain* const SwapChain = (IDXGISwapChain*)Viewport->GetSwapChain();
	check(SwapChain);
	if (!SwapChain)
	{
		UE_LOG(LogDisplayClusterRender, Error, TEXT("Couldn't get a swap chain. Native present will be performed."));
		return true;
	}

	IDXGIOutput* DXOutput = nullptr;
	SwapChain->GetContainingOutput(&DXOutput);
	check(DXOutput);
	if (!DXOutput)
	{
		UE_LOG(LogDisplayClusterRender, Error, TEXT("Couldn't get a DX output device. Native present will be performed."));
		return true;
	}

	if (!bTimersInitialized)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_nDisplayPresent_SoftSync_Initialization);

		VBlankBasis = GetVBlankTimestamp(DXOutput);
		RefreshPeriod = GetRefreshPeriod();

		UE_LOG(LogDisplayClusterRender, Log, TEXT("##SYNC_LOG: Refresh period:     %lf"), RefreshPeriod);
		UE_LOG(LogDisplayClusterRender, Log, TEXT("##SYNC_LOG: VBlank basis:       %lf"), VBlankBasis);
		UE_LOG(LogDisplayClusterRender, Log, TEXT("##SYNC_LOG: VBlank threshold:   %lf"), CVarVBlankThreshold_Value);
		UE_LOG(LogDisplayClusterRender, Log, TEXT("##SYNC_LOG: VBlank sleep mult:  %lf"), CVarVBlankThresholdSleepMultiplier_Value);
		UE_LOG(LogDisplayClusterRender, Log, TEXT("##SYNC_LOG: VBlank sleep:       %lf"), CVarVBlankThreshold_Value * CVarVBlankThresholdSleepMultiplier_Value);

#ifdef UE_BUILD_DEBUG
		const int SamplesCount = 25;
		for (int i = 0; i < SamplesCount; ++i)
		{
			DXOutput->WaitForVBlank();
			UE_LOG(LogDisplayClusterRender, Log, TEXT("##SYNC_LOG: VBlank Sample #%2d: %lf"), i, FPlatformTime::Seconds());
		}
#endif

		bTimersInitialized = true;
	}

	{
		// Align render threads with a barrier
		QUICK_SCOPE_CYCLE_COUNTER(STAT_nDisplayPresent_SoftSync_WaitBarrier);
		B1B = FPlatformTime::Seconds();
		SyncBarrierRenderThread();
		B1A = FPlatformTime::Seconds();
	}

	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_nDisplayPresent_SoftSync_FindRefreshTimeOrigin);

		// Here we calculate how much time left before the next VBlank
		const double CurTime = FPlatformTime::Seconds();
		const double DiffTime = CurTime - VBlankBasis;
		const double TimeRem = ::fmodl(DiffTime, RefreshPeriod);
		const double LeftToVBlankTime = RefreshPeriod - TimeRem;

		TToB = TimeRem;

		// Skip upcoming VBlank if we're in red zone
		SB = FPlatformTime::Seconds();
		if (LeftToVBlankTime < CVarVBlankThreshold_Value)
		{
			const double SleepTime = CVarVBlankThreshold_Value * CVarVBlankThresholdSleepMultiplier_Value;
			UE_LOG(LogDisplayClusterRender, Warning, TEXT("FDisplayClusterNativePresentHandler::Present_SoftSwapSync - Skipped VBlank. Sleeping %f"), SleepTime);
			FPlatformProcess::Sleep(SleepTime);
		}
		SA = FPlatformTime::Seconds();
	}

	{
		// Align render threads again. After that, all render threads we'll be either before VBlank or after. The threshold,
		// sleep, this barrier and MaxFrameLatency==1 (for blocking Present calls) guarantee it.
		QUICK_SCOPE_CYCLE_COUNTER(STAT_nDisplayPresent_SoftSync_WaitBarrier_Second);
		B2B = FPlatformTime::Seconds();
		SyncBarrierRenderThread();
		B2A = FPlatformTime::Seconds();
	}

	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_nDisplayPresent_SoftSync_SwapChainPresent);

		// Regardless of where we are, it's safe to present a frame now. If we need to update the VBlank basis,
		// we have to wait for a VBlank and store the time. We don't want to miss a frame presentation so we
		// present it with swap interval 0 right after VBlank sygnal.
		int SyncIntervalToUse = 1;
		if ((CVarVBlankBasisUpdate_Value > 0) && (B2A - VBlankBasis) > CVarVBlankBasisUpdatePeriod_Value)
		{
			VBlankBasis = GetVBlankTimestamp(DXOutput);
			SyncIntervalToUse = 0;
		}

		PB = FPlatformTime::Seconds();
		SwapChain->Present(SyncIntervalToUse, 0);
		PA = FPlatformTime::Seconds();
	}

	UE_LOG(LogDisplayClusterRender, Verbose, TEXT("##SYNC_LOG - %d:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf"), FrameCounter, B1B, B1A, TToB, SB, SA, B2B, B2A, PB, PA);

	if (CVarLogDwmStats_Value > 0)
	{
		PrintDwmStats(FrameCounter);
	}

	// No need to perform native present
	return false;
}

double FDisplayClusterRenderSyncPolicySoftwareDX11::GetVBlankTimestamp(IDXGIOutput* const DXOutput) const
{
	if (DXOutput)
	{
		// Get timestamp right after the VBlank
		DXOutput->WaitForVBlank();
	}

	return FPlatformTime::Seconds();
}

double FDisplayClusterRenderSyncPolicySoftwareDX11::GetRefreshPeriod() const
{
	// Obtain frame interval from the DWM
	DWM_TIMING_INFO TimingInfo;
	FMemory::Memzero(TimingInfo);
	TimingInfo.cbSize = sizeof(DWM_TIMING_INFO);
	DwmGetCompositionTimingInfo(nullptr, &TimingInfo);
	return FPlatformTime::ToSeconds(TimingInfo.qpcRefreshPeriod);
}

void FDisplayClusterRenderSyncPolicySoftwareDX11::PrintDwmStats(uint32 FrameNum)
{
	DWM_TIMING_INFO TimingInfo;
	FMemory::Memzero(TimingInfo);
	TimingInfo.cbSize = sizeof(DWM_TIMING_INFO);
	DwmGetCompositionTimingInfo(nullptr, &TimingInfo);

	UE_LOG(LogDisplayClusterRender, Log, TEXT(">>>>>>>>>>>>>>>>>>>>>>> DWM"));
	UE_LOG(LogDisplayClusterRender, Log, TEXT("##DWM_STAT(%d) cRefresh:               %llu"), FrameNum, TimingInfo.cRefresh);
	UE_LOG(LogDisplayClusterRender, Log, TEXT("##DWM_STAT(%d) cDXRefresh:             %u"),   FrameNum, TimingInfo.cDXRefresh);
	UE_LOG(LogDisplayClusterRender, Log, TEXT("##DWM_STAT(%d) qpcRefreshPeriod:       %lf"),  FrameNum, FPlatformTime::ToSeconds(TimingInfo.qpcRefreshPeriod));
	UE_LOG(LogDisplayClusterRender, Log, TEXT("##DWM_STAT(%d) qpcVBlank:              %lf"),  FrameNum, FPlatformTime::ToSeconds(TimingInfo.qpcVBlank));
	UE_LOG(LogDisplayClusterRender, Log, TEXT("##DWM_STAT(%d) cFrame:                 %llu"), FrameNum, TimingInfo.cFrame);
	UE_LOG(LogDisplayClusterRender, Log, TEXT("##DWM_STAT(%d) cDXPresent:             %u"),   FrameNum, TimingInfo.cDXPresent);
	UE_LOG(LogDisplayClusterRender, Log, TEXT("##DWM_STAT(%d) cRefreshFrame:          %llu"), FrameNum, TimingInfo.cRefreshFrame);
	UE_LOG(LogDisplayClusterRender, Log, TEXT("##DWM_STAT(%d) cDXRefreshConfirmed:    %llu"), FrameNum, TimingInfo.cDXRefreshConfirmed);
	UE_LOG(LogDisplayClusterRender, Log, TEXT("##DWM_STAT(%d) cFramesLate:            %llu"), FrameNum, TimingInfo.cFramesLate);
	UE_LOG(LogDisplayClusterRender, Log, TEXT("##DWM_STAT(%d) cFramesOutstanding:     %u"),   FrameNum, TimingInfo.cFramesOutstanding);
	UE_LOG(LogDisplayClusterRender, Log, TEXT("##DWM_STAT(%d) cFrameDisplayed:        %llu"), FrameNum, TimingInfo.cFrameDisplayed);
	UE_LOG(LogDisplayClusterRender, Log, TEXT("##DWM_STAT(%d) cRefreshFrameDisplayed: %llu"), FrameNum, TimingInfo.cRefreshFrameDisplayed);
	UE_LOG(LogDisplayClusterRender, Log, TEXT("##DWM_STAT(%d) cFrameComplete:         %llu"), FrameNum, TimingInfo.cFrameComplete);
	UE_LOG(LogDisplayClusterRender, Log, TEXT("##DWM_STAT(%d) qpcFrameComplete:       %lf"),  FrameNum, FPlatformTime::ToSeconds(TimingInfo.qpcFrameComplete));
	UE_LOG(LogDisplayClusterRender, Log, TEXT("##DWM_STAT(%d) cFramePending:          %llu"), FrameNum, TimingInfo.cFramePending);
	UE_LOG(LogDisplayClusterRender, Log, TEXT("##DWM_STAT(%d) qpcFramePending:        %lf"),  FrameNum, FPlatformTime::ToSeconds(TimingInfo.qpcFramePending));
	UE_LOG(LogDisplayClusterRender, Log, TEXT("##DWM_STAT(%d) cFramesDisplayed:       %llu"), FrameNum, TimingInfo.cFramesDisplayed);
	UE_LOG(LogDisplayClusterRender, Log, TEXT("##DWM_STAT(%d) cFramesComplete:        %llu"), FrameNum, TimingInfo.cFramesComplete);
	UE_LOG(LogDisplayClusterRender, Log, TEXT("##DWM_STAT(%d) cFramesPending:         %llu"), FrameNum, TimingInfo.cFramesPending);
	UE_LOG(LogDisplayClusterRender, Log, TEXT("##DWM_STAT(%d) cFramesDropped:         %llu"), FrameNum, TimingInfo.cFramesDropped);
	UE_LOG(LogDisplayClusterRender, Log, TEXT("##DWM_STAT(%d) cFramesMissed:          %llu"), FrameNum, TimingInfo.cFramesMissed);
	UE_LOG(LogDisplayClusterRender, Log, TEXT(">>>>>>"));
}
