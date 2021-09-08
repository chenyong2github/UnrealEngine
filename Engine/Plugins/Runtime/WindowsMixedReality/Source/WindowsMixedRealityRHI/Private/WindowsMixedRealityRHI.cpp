// Copyright (c) Microsoft Corporation. All rights reserved.

#include "WindowsMixedRealityPrecompiled.h"
#include "WindowsMixedRealityRHI.h"
#include "D3D11Util.h"
#include "Engine/RendererSettings.h"
#include "StereoRenderTargetManager.h"
#include "D3D11Viewport.h"

#include "MixedRealityInterop.h"


#include "WindowsMixedRealityInteropLoader.h"

static WindowsMixedReality::MixedRealityInterop* HMD = nullptr;

bool FWindowsMixedRealityRHIModule::SupportsDynamicReloading()
{ 
    return false; 
}

void FWindowsMixedRealityRHIModule::StartupModule()
{
	LUID id = {0,0};

	if (!HMD)
	{
		HMD = WindowsMixedReality::LoadInteropLibrary();
		if (!HMD)
		{
			return;
		}
	}
#if PLATFORM_HOLOLENS
	if (HoloSpace)
	{
		return;
	}

	HoloSpace = HolographicSpace::CreateForCoreWindow(CoreWindow::GetForCurrentThread());
	if (HoloSpace != nullptr)
	{
		HMD->SetHolographicSpace(HoloSpace);
		id =
		{
			HoloSpace->PrimaryAdapterId.LowPart,
			HoloSpace->PrimaryAdapterId.HighPart
		};
	}

#endif

	TRefCountPtr<IDXGIAdapter> TempAdapter;

	VERIFYD3D11RESULT(CreateDXGIFactory1(__uuidof(IDXGIFactory1), &DXGIFactory1));

	D3D_FEATURE_LEVEL RequestedFeatureLevels[] =
	{
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_0
	};


	for (uint32 AdapterIndex = 0; DXGIFactory1->EnumAdapters(AdapterIndex, TempAdapter.GetInitReference()) != DXGI_ERROR_NOT_FOUND; ++AdapterIndex)
	{
		if (!TempAdapter)
		{
			continue;
		}

		DXGI_ADAPTER_DESC AdapterDesc;
		VERIFYD3D11RESULT(TempAdapter->GetDesc(&AdapterDesc));

		if (id.HighPart && id.LowPart)
		{
			if (FMemory::Memcmp(&AdapterDesc.AdapterLuid, &id, sizeof(LUID)) != 0)
			{
				continue;
			}
		}

		ComPtr <ID3D11Device> D3DDevice;
		ComPtr <ID3D11DeviceContext> D3DDeviceContext;
		D3D_FEATURE_LEVEL OutFeatureLevel;
		uint32 DeviceCreationFlags = D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if UE_BUILD_DEBUG
		DeviceCreationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif //UE_BUILD_DEBUG

		if (FAILED(D3D11CreateDevice(
			TempAdapter,
			D3D_DRIVER_TYPE_UNKNOWN,
			NULL,
			DeviceCreationFlags,
			RequestedFeatureLevels,
			UE_ARRAY_COUNT(RequestedFeatureLevels),
			D3D11_SDK_VERSION,
			&D3DDevice,
			&OutFeatureLevel,
			&D3DDeviceContext
		)))
		{
			continue; 
		}

		ChosenAdapter = FD3D11Adapter(TempAdapter, OutFeatureLevel);

		ChosenDescription = AdapterDesc;
		break;
	}

}

void FWindowsMixedRealityRHIModule::ShutdownModule()
{
	delete HMD;
	HMD = nullptr;
}



bool FWindowsMixedRealityRHIModule::IsSupported()
{
#if PLATFORM_HOLOLENS
	if (!HolographicSpace::IsAvailable)
	{
		return false;
	}

	//if (!HolographicSpace::IsConfigured) //somehow doesn't work on HL
	//{
	//	return false;
	//}

	if (!HolographicSpace::IsSupported)
	{
		return false;
	}
#endif
	return HMD != nullptr;
}


FDynamicRHI* FWindowsMixedRealityRHIModule::CreateRHI(ERHIFeatureLevel::Type RequestedFeatureLevel)
{
#if PLATFORM_HOLOLENS
	GMaxRHIFeatureLevel = ERHIFeatureLevel::ES3_1;
	GMaxRHIShaderPlatform = SP_PCD3D_ES3_1;
#endif

	GShaderPlatformForFeatureLevel[ERHIFeatureLevel::ES2_REMOVED] = SP_NumPlatforms;
	GShaderPlatformForFeatureLevel[ERHIFeatureLevel::ES3_1] = SP_PCD3D_ES3_1;
	GShaderPlatformForFeatureLevel[ERHIFeatureLevel::SM4_REMOVED] = SP_NumPlatforms;
	GShaderPlatformForFeatureLevel[ERHIFeatureLevel::SM5] = SP_PCD3D_SM5;

	FWindowsMixedRealityDynamicRHI * RHI = new FWindowsMixedRealityDynamicRHI(DXGIFactory1.Get(), ChosenAdapter.MaxSupportedFeatureLevel, ChosenAdapter);
	GD3D11RHI = RHI;

#if PLATFORM_HOLOLENS
	RHI->SetHolographicSpace(HoloSpace);
#endif

	return GD3D11RHI;
}


FWindowsMixedRealityDynamicRHI::FWindowsMixedRealityDynamicRHI(IDXGIFactory1* InDXGIFactory1, D3D_FEATURE_LEVEL InFeatureLevel, const FD3D11Adapter& InAdapter)
	: FD3D11DynamicRHI(InDXGIFactory1, InFeatureLevel, InAdapter)
{}


FViewportRHIRef FWindowsMixedRealityDynamicRHI::RHICreateViewport(void* WindowHandle, uint32 SizeX, uint32 SizeY, bool bIsFullscreen, EPixelFormat PreferredPixelFormat)
{
	check(IsInGameThread());

#if PLATFORM_HOLOLENS
	static const auto CVarMobileMultiView = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("vr.MobileMultiView"));
	static const auto CVarMobileHDR = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileHDR"));
	const bool bMobileHDR = (CVarMobileHDR && CVarMobileHDR->GetValueOnAnyThread() != 0);
	bIsMobileMultiViewEnabled = (GRHISupportsArrayIndexFromAnyShader && !bMobileHDR) && (CVarMobileMultiView && CVarMobileMultiView->GetValueOnAnyThread() != 0);
#endif
	
	// Use a default pixel format if none was specified	
	if (PreferredPixelFormat == EPixelFormat::PF_Unknown)
	{
		static const auto CVarDefaultBackBufferPixelFormat = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DefaultBackBufferPixelFormat"));
		PreferredPixelFormat = EDefaultBackBufferPixelFormat::Convert2PixelFormat(EDefaultBackBufferPixelFormat::FromInt(CVarDefaultBackBufferPixelFormat->GetValueOnGameThread()));
	}

	if (GIsEditor)
	{
		// DXGI device maximum frame latency counts the total number of presents enqueued rather than logical engine frames in flight. Inside the editor we can easily
		// run into the default limit of 3 presents for different HWNDs when a VR preview window is active, causing frame rate to be throttled by the local monitor.
		// Increase the limit so that the HMD can properly take ownership of frame pacing instead.
		IConsoleManager::Get().FindConsoleVariable(TEXT("RHI.MaximumFrameLatency"))->Set(16);
	}

	if (bIsMobileMultiViewEnabled)
	{
		return new FD3D11Viewport(this, (HWND)WindowHandle, SizeX, SizeY, bIsFullscreen, PreferredPixelFormat);
	}
	
	return new FWindowsMixedRealityViewport(this, (HWND)WindowHandle, SizeX, SizeY, bIsFullscreen, PreferredPixelFormat);
}

void FWindowsMixedRealityDynamicRHI::RHIBeginFrame()
{
	FD3D11DynamicRHI::RHIBeginFrame();

	if (bIsMobileMultiViewEnabled)
	{
		return;
	}
	
	for (int i=0; i< Viewports.Num(); ++i)
	{
		((FWindowsMixedRealityViewport *)Viewports[i])->UpdateBackBuffer();
	}
}

void FWindowsMixedRealityDynamicRHI::RHIEndFrame()
{
	FD3D11DynamicRHI::RHIEndFrame();
}


FWindowsMixedRealityViewport::FWindowsMixedRealityViewport(FD3D11DynamicRHI* InD3DRHI, HWND InWindowHandle, uint32 InSizeX, uint32 InSizeY, bool bInIsFullscreen, EPixelFormat InPreferredPixelFormat)
	: FD3D11Viewport(InD3DRHI)
{
	LastFlipTime = 0;
	LastFrameComplete = 0;
	LastCompleteTime = 0;
	SyncCounter  = 0;
	bSyncedLastFrame = false;
	WindowHandle = InWindowHandle;
	SizeX = InSizeX;
	SizeY = InSizeY;
	bIsFullscreen = bInIsFullscreen;
	PixelFormat  = InPreferredPixelFormat;
	ValidState = 0;

	D3DRHI->Viewports.Add(this);

	// Ensure that the D3D device has been created.
	D3DRHI->InitD3DDevice();

	// Create a backbuffer/swapchain for each viewport
	TRefCountPtr<IDXGIDevice> DXGIDevice;
	VERIFYD3D11RESULT_EX(D3DRHI->GetDevice()->QueryInterface(__uuidof(IDXGIDevice), (void**)DXGIDevice.GetInitReference()), D3DRHI->GetDevice());

	uint32 DisplayIndex = D3DRHI->GetHDRDetectedDisplayIndex();
	bForcedFullscreenDisplay = FParse::Value(FCommandLine::Get(), TEXT("FullscreenDisplay="), DisplayIndex);

	if (bForcedFullscreenDisplay || GRHISupportsHDROutput)
	{
		TRefCountPtr<IDXGIAdapter> DXGIAdapter;
		DXGIDevice->GetAdapter((IDXGIAdapter**)DXGIAdapter.GetInitReference());

		if (S_OK != DXGIAdapter->EnumOutputs(DisplayIndex, ForcedFullscreenOutput.GetInitReference()))
		{
			UE_LOG(LogWMRRHI, Log, TEXT("Failed to find requested output display (%i)."), DisplayIndex);
			ForcedFullscreenOutput = nullptr;
			bForcedFullscreenDisplay = false;
		}
	}
	else
	{
		ForcedFullscreenOutput = nullptr;
	}

	if (PixelFormat == PF_FloatRGBA && bIsFullscreen)
	{
		// Send HDR meta data to enable
		D3DRHI->EnableHDR();
	}
	/// INIT BACKBUFFER!!!!!!!!

	bNeedSwapChain = false;
	// Create a RHI surface to represent the viewport's back buffer.
	OffscreenBackBuffer = FD3D11Viewport::GetSwapChainSurface(D3DRHI, PixelFormat, SizeX, SizeY, nullptr);
	UpdateBackBuffer();

	BeginInitResource(&FrameSyncEvent);

}

void FWindowsMixedRealityViewport::UpdateBackBuffer()
{
	FTexture2DRHIRef OutShaderResourceTexture;
	FTexture2DRHIRef OutTargetableTexture;

	BackBuffer = nullptr;
	if (GEngine && GEngine->StereoRenderingDevice && GEngine->StereoRenderingDevice->GetRenderTargetManager())
	{
		if (GEngine
			->StereoRenderingDevice
			->GetRenderTargetManager()
			->AllocateRenderTargetTexture(0, SizeX, SizeY, PixelFormat, 1, TexCreate_None, TexCreate_RenderTargetable, OutTargetableTexture, OutShaderResourceTexture))
		{
			BackBuffer = (FD3D11Texture2D*)(OutTargetableTexture.GetReference());
		}
	}

	if(!BackBuffer)
	{
		BackBuffer = OffscreenBackBuffer;
	}
}

void FWindowsMixedRealityViewport::Resize(uint32 InSizeX, uint32 InSizeY, bool bInIsFullscreen, EPixelFormat PreferredPixelFormat)
{
	// Unbind any dangling references to resources
	D3DRHI->SetRenderTargets(0, nullptr, nullptr);
	D3DRHI->ClearState();
	D3DRHI->GetDeviceContext()->Flush(); // Potential perf hit

	if (IsValidRef(CustomPresent))
	{
		CustomPresent->OnBackBufferResize();
	}

	// Release our backbuffer reference, as required by DXGI before calling ResizeBuffers.
	if (IsValidRef(BackBuffer))
	{
		check(BackBuffer->GetRefCount() == 1 || BackBuffer == OffscreenBackBuffer);

		checkComRefCount(BackBuffer->GetResource(), 1);
		checkComRefCount(BackBuffer->GetRenderTargetView(0, -1), 1);
		checkComRefCount(BackBuffer->GetShaderResourceView(), 1);
	}
	BackBuffer.SafeRelease();

	if (SizeX != InSizeX || SizeY != InSizeY || PixelFormat != PreferredPixelFormat)
	{
		SizeX = InSizeX;
		SizeY = InSizeY;
		PixelFormat = PreferredPixelFormat;

		OffscreenBackBuffer = FD3D11Viewport::GetSwapChainSurface(D3DRHI, PixelFormat, SizeX, SizeY, nullptr);

		check(SizeX > 0);
		check(SizeY > 0);
	}

	if (bIsFullscreen != bInIsFullscreen)
	{
		bIsFullscreen = bInIsFullscreen;
		ValidState = VIEWPORT_INVALID;
	}

	// Float RGBA backbuffers are requested whenever HDR mode is desired
	if (PixelFormat == GRHIHDRDisplayOutputFormat && bIsFullscreen)
	{
		D3DRHI->EnableHDR();
	}
	else
	{
		D3DRHI->ShutdownHDR();
	}

	// Create a RHI surface to represent the viewport's back buffer.

	UpdateBackBuffer();
}


DEFINE_LOG_CATEGORY(LogWMRRHI);

IMPLEMENT_MODULE(FWindowsMixedRealityRHIModule, WindowsMixedRealityRHI)
