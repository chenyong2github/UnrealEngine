// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Synchronization/DisplayClusterRenderSyncPolicySoftwareDX11.h"

#include "DisplayClusterLog.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include "DirectX/Include/DXGI.h"
#include "dwmapi.h"
#include "Windows/HideWindowsPlatformTypes.h"
#include "RenderCore/Public/ShaderCore.h"

#include "ShaderCore.h"
#include "D3D11RHI/Private/Windows/D3D11RHIBasePrivate.h"

#include "D3D11State.h"
#include "D3D11Resources.h"
#include "D3D11Viewport.h"

#include "NVIDIA/nvapi/nvapi.h"


static TAutoConsoleVariable<int32> CVarAdvancedSyncEnabled(
	TEXT("nDisplay.render.softsync.AdvancedSyncEnabled"),
	0,
	TEXT("Advanced DWM based render synchronization (0 = disabled)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarUseCustomRefreshRate(
	TEXT("nDisplay.render.softsync.UseCustomRefreshRate"),
	0,
	TEXT("Force custom refresh rate to be used in synchronization math (0 = disabled)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarCustomRefreshRate(
	TEXT("nDisplay.render.softsync.CustomRefreshRate"),
	60.f,
	TEXT("Custom refresh rate for synchronization math (Hz)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarVBlankThreshold(
	TEXT("nDisplay.render.softsync.VBlankThreshold"),
	0.003f,
	TEXT("Changes the threshold used to determine whether or not nDisplay SoftSync can make the current frame presentation"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarVBlankThresholdSleepMultiplier(
	TEXT("nDisplay.render.softsync.VBlankThresholdSleepMultipler"),
	1.5f,
	TEXT("Multiplier applied to VBlankThreshold to compute sleep time to skip frame presentation on upcoming VBlank"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32>  CVarVBlankBasisUpdate(
	TEXT("nDisplay.render.softsync.VBlankBasisUpdate"),
	1,
	TEXT("Update VBlank basis periodically to avoid time drifting (0 = disabled)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float>  CVarVBlankBasisUpdatePeriod(
	TEXT("nDisplay.render.softsync.VBlankBasisUpdatePeriod"),
	120.f,
	TEXT("VBlank basis update period in seconds"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32>  CVarLogDwmStats(
	TEXT("nDisplay.render.softsync.LogDwmStats"),
	0,
	TEXT("Print DWM stats to log (0 = disabled)"),
	ECVF_RenderThreadSafe
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
	bUseAdvancedSynchronization = (CVarAdvancedSyncEnabled.GetValueOnGameThread() != 0);

	if (bUseAdvancedSynchronization)
	{
		const int Result = NvAPI_Initialize();
		if (Result != NVAPI_OK)
		{
			UE_LOG(LogDisplayClusterRenderSync, Error, TEXT("Couldn't initialize NvAPI. Error code: %d"), Result);
			return;
		}

		UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("NvAPI has been initialized"));

		bNvApiInitialized = (Result == NVAPI_OK);
		if (bNvApiInitialized)
		{
			if (NvAPI_GSync_EnumSyncDevices(GSyncDeviceHandles, &GSyncDeviceAmount) != NVAPI_OK)
			{
				UE_LOG(LogDisplayClusterRenderSync, Warning, TEXT("Couldn't enumerate GSync devices or no devices found"));
			}
			else
			{
				UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("Found sync devices: %u"), GSyncDeviceAmount);
			}

			if (NvAPI_EnumPhysicalGPUs(PhysGPUHandles, &PhysGPUAmount) != NVAPI_OK)
			{
				UE_LOG(LogDisplayClusterRenderSync, Error, TEXT("Couldn't enumerate physical GPU devices"));
			}
			else
			{
				UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("Found physical GPUs: %u"), PhysGPUAmount);
			}

			while (NvAPI_EnumNvidiaDisplayHandle(DisplayAmount, &DisplayHandles[DisplayAmount]) != NVAPI_END_ENUMERATION)
			{
				++DisplayAmount;
				UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("Found display: %u"), DisplayAmount);
			}
		}
	}
}

FDisplayClusterRenderSyncPolicySoftwareDX11::~FDisplayClusterRenderSyncPolicySoftwareDX11()
{
	if (bNvApiInitialized)
	{
		NvAPI_Unload();
	}
}

bool FDisplayClusterRenderSyncPolicySoftwareDX11::SynchronizeClusterRendering(int32& InOutSyncInterval)
{
	check(IsInRenderingThread());
	if(!(GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport))
	{
		UE_LOG(LogDisplayClusterRenderSync, Warning, TEXT("Couldn't get a D3D11 viewport."));
		// Tell a caller to present the frame by himself
		return true;
	}

	FD3D11Viewport* const Viewport = static_cast<FD3D11Viewport*>(GEngine->GameViewport->Viewport->GetViewportRHI().GetReference());
	check(Viewport);

#if !WITH_EDITOR
	// Issue frame event
	Viewport->IssueFrameEvent();
	// Wait until GPU finish last frame commands
	Viewport->WaitForFrameEventCompletion();
#endif

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

	double B1B = 0.f;  // Barrier 1 before
	double B1A = 0.f;  // Barrier 1 after
	double TToB = 0.f; // Time to VBlank
	double SB = 0.f;   // Sleep before
	double SA = 0.f;   // Sleep after
	double B2B = 0.f;  // Barrier 2 before
	double B2A = 0.f;  // Barrier 2 after
	double PB = 0.f;   // Present before
	double PA = 0.f;   // Present after

	IDXGISwapChain* const SwapChain = static_cast<IDXGISwapChain*>(Viewport->GetNativeSwapChain());
	check(SwapChain);
	if (!SwapChain)
	{
		UE_LOG(LogDisplayClusterRenderSync, Error, TEXT("Couldn't get a swap chain."));
		// Tell a caller that he still needs to present a frame
		return true;
	}

	IDXGIOutput* DXOutput = nullptr;
	SwapChain->GetContainingOutput(&DXOutput);
	check(DXOutput);
	if (!DXOutput)
	{
		UE_LOG(LogDisplayClusterRenderSync, Error, TEXT("Couldn't get a DX output device."));
		// Tell a caller that he still needs to present a frame
		return true;
	}

	// Get dynamic values from console variables
	const float VBlankThreshold = CVarVBlankThreshold.GetValueOnRenderThread();
	const float VBlankThresholdSleepMultiplier = CVarVBlankThresholdSleepMultiplier.GetValueOnRenderThread();
	const int32 VBlankBasisUpdate = CVarVBlankBasisUpdate.GetValueOnRenderThread();
	const float VBlankBasisUpdatePeriod = CVarVBlankBasisUpdatePeriod.GetValueOnRenderThread();

	if (!bTimersInitialized)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_nDisplayPresent_SoftSync_Initialization);

		VBlankBasis = GetVBlankTimestamp(DXOutput);
		RefreshPeriod = GetRefreshPeriod();

		UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("##SYNC_LOG: Refresh period:     %lf"), RefreshPeriod);
		UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("##SYNC_LOG: VBlank basis:       %lf"), VBlankBasis);
		UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("##SYNC_LOG: VBlank threshold:   %lf"), VBlankThreshold);
		UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("##SYNC_LOG: VBlank sleep mult:  %lf"), VBlankThresholdSleepMultiplier);
		UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("##SYNC_LOG: VBlank sleep:       %lf"), VBlankThreshold * VBlankThresholdSleepMultiplier);

#ifdef UE_BUILD_DEBUG
		const int SamplesCount = 25;
		for (int i = 0; i < SamplesCount; ++i)
		{
			DXOutput->WaitForVBlank();
			UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("##SYNC_LOG: VBlank Sample #%2d: %lf"), i, FPlatformTime::Seconds());
		}
#endif

		bTimersInitialized = true;
	}

	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_nDisplayPresent_SoftSync_WaitBarrier);

		// Align render threads with a barrier
		B1B = FPlatformTime::Seconds();
		SyncBarrierRenderThread();
		B1A = FPlatformTime::Seconds();
	}

	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_nDisplayPresent_SoftSync_FindRefreshTimeOrigin);

		// Here we calculate how much time left before the next VBlank
		const double CurTime = FPlatformTime::Seconds();
		const double DiffTime = CurTime - VBlankBasis;
		const double TimeRemainder    = ::fmodl(DiffTime, RefreshPeriod);
		const double TimeLeftToVBlank = RefreshPeriod - TimeRemainder;

		TToB = TimeLeftToVBlank;

		// Skip upcoming VBlank if we're in red zone
		SB = FPlatformTime::Seconds();
		if (TimeLeftToVBlank < VBlankThreshold)
		{
			const double SleepTime = VBlankThreshold * VBlankThresholdSleepMultiplier;
			UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("DX11 advanced soft sync - Skipped VBlank. Sleeping %f"), SleepTime);
			//! FPlatformProcess::Sleep(SleepTime);
			VBlankBasis = GetVBlankTimestamp(DXOutput);
			FPlatformProcess::Sleep(0.005);
		}
		SA = FPlatformTime::Seconds();
	}

	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_nDisplayPresent_SoftSync_WaitBarrier_Second);

		// Align render threads again. After that, all render threads we'll be either before VBlank or after. The threshold,
		// sleep, this barrier and MaxFrameLatency==1 (for blocking Present calls) guarantee it.
		B2B = FPlatformTime::Seconds();
		SyncBarrierRenderThread();
		B2A = FPlatformTime::Seconds();
	}

	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_nDisplayPresent_SoftSync_SwapChainPresent);

		// Regardless of where we are, it's safe to present a frame now. If we need to update the VBlank basis,
		// we have to wait for a VBlank and store the time. We don't want to miss a frame presentation so we
		// present it with swap interval 0 right after VBlank signal.
		int SyncIntervalToUse = 1;
		if ((VBlankBasisUpdate > 0) && (B2A - VBlankBasis) > VBlankBasisUpdatePeriod)
		{
			VBlankBasis = GetVBlankTimestamp(DXOutput);
			SyncIntervalToUse = 0;
		}

		PB = FPlatformTime::Seconds();
		SwapChain->Present(SyncIntervalToUse, 0);
		PA = FPlatformTime::Seconds();
	}

	UE_LOG(LogDisplayClusterRenderSync, Verbose, TEXT("##SYNC_LOG - %d:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf"), FrameCounter, B1B, B1A, TToB, SB, SA, B2B, B2A, PB, PA);

	const bool LogDwmStats = (CVarLogDwmStats.GetValueOnRenderThread() != 0);
	if (LogDwmStats)
	{
		PrintDwmStats(FrameCounter);
	}

	++FrameCounter;

	// Tell a caller there is no need to present the frame
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
	const bool  bUseCustomRefreshRate = !!CVarUseCustomRefreshRate.GetValueOnRenderThread();

	// Sometimes the DWM returns a refresh rate value that doesn't correspond to the real system.
	// Use custom refresh rate for the synchronization algorithm below if required.
	if (bUseCustomRefreshRate)
	{
		const float CustomRefreshRate = FMath::Abs(CVarCustomRefreshRate.GetValueOnRenderThread());
		return 1.L / CustomRefreshRate;
	}
	else
	{
		// Obtain frame interval from the DWM
		DWM_TIMING_INFO TimingInfo;
		FMemory::Memzero(TimingInfo);
		TimingInfo.cbSize = sizeof(DWM_TIMING_INFO);
		DwmGetCompositionTimingInfo(nullptr, &TimingInfo);
		return FPlatformTime::ToSeconds(TimingInfo.qpcRefreshPeriod);
	}
}

void FDisplayClusterRenderSyncPolicySoftwareDX11::PrintDwmStats(uint32 FrameNum)
{
	DWM_TIMING_INFO TimingInfo;
	FMemory::Memzero(TimingInfo);
	TimingInfo.cbSize = sizeof(DWM_TIMING_INFO);
	DwmGetCompositionTimingInfo(nullptr, &TimingInfo);

	UE_LOG(LogDisplayClusterRenderSync, Log, TEXT(">>>>>>>>>>>>>>>>>>>>>>> DWM"));
	UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("##DWM_STAT(%d) cRefresh:               %llu"), FrameNum, TimingInfo.cRefresh);
	UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("##DWM_STAT(%d) cDXRefresh:             %u"),   FrameNum, TimingInfo.cDXRefresh);
	UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("##DWM_STAT(%d) qpcRefreshPeriod:       %lf"),  FrameNum, FPlatformTime::ToSeconds(TimingInfo.qpcRefreshPeriod));
	UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("##DWM_STAT(%d) qpcVBlank:              %lf"),  FrameNum, FPlatformTime::ToSeconds(TimingInfo.qpcVBlank));
	UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("##DWM_STAT(%d) cFrame:                 %llu"), FrameNum, TimingInfo.cFrame);
	UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("##DWM_STAT(%d) cDXPresent:             %u"),   FrameNum, TimingInfo.cDXPresent);
	UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("##DWM_STAT(%d) cRefreshFrame:          %llu"), FrameNum, TimingInfo.cRefreshFrame);
	UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("##DWM_STAT(%d) cDXRefreshConfirmed:    %llu"), FrameNum, TimingInfo.cDXRefreshConfirmed);
	UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("##DWM_STAT(%d) cFramesLate:            %llu"), FrameNum, TimingInfo.cFramesLate);
	UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("##DWM_STAT(%d) cFramesOutstanding:     %u"),   FrameNum, TimingInfo.cFramesOutstanding);
	UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("##DWM_STAT(%d) cFrameDisplayed:        %llu"), FrameNum, TimingInfo.cFrameDisplayed);
	UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("##DWM_STAT(%d) cRefreshFrameDisplayed: %llu"), FrameNum, TimingInfo.cRefreshFrameDisplayed);
	UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("##DWM_STAT(%d) cFrameComplete:         %llu"), FrameNum, TimingInfo.cFrameComplete);
	UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("##DWM_STAT(%d) qpcFrameComplete:       %lf"),  FrameNum, FPlatformTime::ToSeconds(TimingInfo.qpcFrameComplete));
	UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("##DWM_STAT(%d) cFramePending:          %llu"), FrameNum, TimingInfo.cFramePending);
	UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("##DWM_STAT(%d) qpcFramePending:        %lf"),  FrameNum, FPlatformTime::ToSeconds(TimingInfo.qpcFramePending));
	UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("##DWM_STAT(%d) cFramesDisplayed:       %llu"), FrameNum, TimingInfo.cFramesDisplayed);
	UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("##DWM_STAT(%d) cFramesComplete:        %llu"), FrameNum, TimingInfo.cFramesComplete);
	UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("##DWM_STAT(%d) cFramesPending:         %llu"), FrameNum, TimingInfo.cFramesPending);
	UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("##DWM_STAT(%d) cFramesDropped:         %llu"), FrameNum, TimingInfo.cFramesDropped);
	UE_LOG(LogDisplayClusterRenderSync, Log, TEXT("##DWM_STAT(%d) cFramesMissed:          %llu"), FrameNum, TimingInfo.cFramesMissed);
	UE_LOG(LogDisplayClusterRenderSync, Log, TEXT(">>>>>>"));
}
