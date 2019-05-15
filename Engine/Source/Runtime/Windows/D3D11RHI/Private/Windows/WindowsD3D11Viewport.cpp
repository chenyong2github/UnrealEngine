// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	D3D11Viewport.cpp: D3D viewport RHI implementation.
=============================================================================*/

#include "D3D11RHIPrivate.h"
#include "RenderCore.h"
#include "Misc/CommandLine.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include <dwmapi.h>

#if defined(NTDDI_WIN10_RS2)
#include <dxgi1_6.h>
#else
#include <dxgi1_5.h>
#endif


extern FD3D11Texture2D* GetSwapChainSurface(FD3D11DynamicRHI* D3DRHI, EPixelFormat PixelFormat, uint32 SizeX, uint32 SizeY, IDXGISwapChain* SwapChain);

FD3D11Viewport::FD3D11Viewport(FD3D11DynamicRHI* InD3DRHI,HWND InWindowHandle,uint32 InSizeX,uint32 InSizeY,bool bInIsFullscreen, EPixelFormat InPreferredPixelFormat):
	D3DRHI(InD3DRHI),
	LastFlipTime(0),
	LastFrameComplete(0),
	LastCompleteTime(0),
	SyncCounter(0),
	bSyncedLastFrame(false),
	WindowHandle(InWindowHandle),
	MaximumFrameLatency(3),
	SizeX(InSizeX),
	SizeY(InSizeY),
	bIsFullscreen(bInIsFullscreen),
	PixelFormat(InPreferredPixelFormat),
	PixelColorSpace(EColorSpace::ERec709sRGB),
	bIsValid(true),
	FrameSyncEvent(InD3DRHI)
{
	check(IsInGameThread());
	D3DRHI->Viewports.Add(this);

	// Ensure that the D3D device has been created.
	D3DRHI->InitD3DDevice();

	// Create a backbuffer/swapchain for each viewport
	TRefCountPtr<IDXGIDevice> DXGIDevice;
	VERIFYD3D11RESULT_EX(D3DRHI->GetDevice()->QueryInterface(IID_IDXGIDevice, (void**)DXGIDevice.GetInitReference()), D3DRHI->GetDevice());

	// If requested, keep a handle to a DXGIOutput so we can force that display on fullscreen swap
	uint32 DisplayIndex = D3DRHI->GetHDRDetectedDisplayIndex();
	bForcedFullscreenDisplay = FParse::Value(FCommandLine::Get(), TEXT("FullscreenDisplay="), DisplayIndex);

	if (bForcedFullscreenDisplay || GRHISupportsHDROutput)
	{
		TRefCountPtr<IDXGIAdapter> DXGIAdapter;
		DXGIDevice->GetAdapter((IDXGIAdapter**)DXGIAdapter.GetInitReference());

		if (S_OK != DXGIAdapter->EnumOutputs(DisplayIndex, ForcedFullscreenOutput.GetInitReference()))
		{
			UE_LOG(LogD3D11RHI, Log, TEXT("Failed to find requested output display (%i)."), DisplayIndex);
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

	// Skip swap chain creation in off-screen rendering mode
	bNeedSwapChain = !FParse::Param(FCommandLine::Get(), TEXT("RenderOffScreen"));
	if (bNeedSwapChain)
	{
		// Create the swapchain.
		if (InD3DRHI->IsQuadBufferStereoEnabled())
		{
			IDXGIFactory2* Factory2 = (IDXGIFactory2*)D3DRHI->GetFactory();

			BOOL stereoEnabled = Factory2->IsWindowedStereoEnabled();
			if (stereoEnabled)
			{
				DXGI_SWAP_CHAIN_DESC1 SwapChainDesc1;
				FMemory::Memzero(&SwapChainDesc1, sizeof(DXGI_SWAP_CHAIN_DESC1));

				// Enable stereo 
				SwapChainDesc1.Stereo = true;
				// MSAA Sample count
				SwapChainDesc1.SampleDesc.Count = 1;
				SwapChainDesc1.SampleDesc.Quality = 0;

				SwapChainDesc1.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
				SwapChainDesc1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;
				// Double buffering required to create stereo swap chain
				SwapChainDesc1.BufferCount = 2;
				SwapChainDesc1.Scaling = DXGI_SCALING_NONE;
				SwapChainDesc1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
				SwapChainDesc1.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

				IDXGISwapChain1* SwapChain1 = nullptr;
				VERIFYD3D11RESULT_EX((Factory2->CreateSwapChainForHwnd(D3DRHI->GetDevice(), WindowHandle, &SwapChainDesc1, nullptr, nullptr, &SwapChain1)), D3DRHI->GetDevice());
				SwapChain = SwapChain1;
			}
			else
			{
				UE_LOG(LogD3D11RHI, Log, TEXT("FD3D11Viewport::FD3D11Viewport was not able to create stereo SwapChain; Please enable stereo in driver settings."));
				InD3DRHI->DisableQuadBufferStereo();
			}
		}

		// if stereo was not activated or not enabled in settings
		if (SwapChain == nullptr)
		{
			// Create the swapchain.
			DXGI_SWAP_CHAIN_DESC1 SwapChainDesc;
			FMemory::Memzero(&SwapChainDesc, sizeof(DXGI_SWAP_CHAIN_DESC1));
			SwapChainDesc.Width = SizeX;
			SwapChainDesc.Height = SizeY;
			SwapChainDesc.SampleDesc.Count = 1;
			SwapChainDesc.SampleDesc.Quality = 0;
			SwapChainDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
			SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;

			DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsSwapChainDesc = {};
			fsSwapChainDesc.Windowed = !bIsFullscreen;

			
			if (InD3DRHI->bAllowTearing || InD3DRHI->bAllowHDR || InD3DRHI->bAllowFlip)
			{
				// Needed for HDR
				BackBufferCount = 2;
				SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
			}
			else
			{
				// Optional for LDR
				BackBufferCount = 1;
				SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
			}

			SwapChainDesc.BufferCount = BackBufferCount;
			SwapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

			IDXGISwapChain1* SwapChain1 = nullptr;
			IDXGIFactory2* Factory2 = (IDXGIFactory2*)D3DRHI->GetFactory();

			VERIFYD3D11RESULT_EX((Factory2->CreateSwapChainForHwnd(D3DRHI->GetDevice(), WindowHandle, &SwapChainDesc, &fsSwapChainDesc, nullptr, &SwapChain1)), D3DRHI->GetDevice());
			SwapChain1->QueryInterface(__uuidof(IDXGISwapChain1), (void**)SwapChain.GetInitReference());

			// See if we are running on a HDR monitor
			
			EColorSpace ColorSpace = EColorSpace::ERec709sRGB;
#if WITH_EDITOR

			// 

			static auto CVarHDREnable = IConsoleManager::Get().FindConsoleVariable(TEXT("Editor.HDRSupport"));

			if (CVarHDREnable->GetInt() !=0 )
			{
				TRefCountPtr<IDXGIOutput> Output;
				if (SUCCEEDED(SwapChain->GetContainingOutput(Output.GetInitReference())))
				{
					TRefCountPtr<IDXGIOutput6> Output6;
					if (SUCCEEDED(Output->QueryInterface(IID_PPV_ARGS(Output6.GetInitReference()))))
					{
						DXGI_OUTPUT_DESC1 desc;
						Output6->GetDesc1(&desc);

						if (desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
						{
							// Display output is HDR10.
							ColorSpace = EColorSpace::ERec2020PQ;
						}
					}
				}

				if (ColorSpace == EColorSpace::ERec2020PQ)
				{
					TRefCountPtr<IDXGISwapChain3> swapChain3;
					if (SUCCEEDED(SwapChain->QueryInterface(IID_PPV_ARGS(swapChain3.GetInitReference()))))
					{
						UINT colorSpaceSupport = 0;
						DXGI_COLOR_SPACE_TYPE colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;

						swapChain3->CheckColorSpaceSupport(colorSpace, &colorSpaceSupport);

						if (colorSpaceSupport & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)
						{
							swapChain3->SetColorSpace1(colorSpace);
						}
					}
				}
			}
#endif // WITH_EDITOR
			PixelColorSpace = ColorSpace;
		}


		// Set the DXGI message hook to not change the window behind our back.
		D3DRHI->GetFactory()->MakeWindowAssociation(WindowHandle,DXGI_MWA_NO_WINDOW_CHANGES);
	}
	// Create a RHI surface to represent the viewport's back buffer.
	BackBuffer = GetSwapChainSurface(D3DRHI, PixelFormat, SizeX, SizeY, SwapChain);

	// Tell the window to redraw when they can.
	// @todo: For Slate viewports, it doesn't make sense to post WM_PAINT messages (we swallow those.)
	::PostMessage( WindowHandle, WM_PAINT, 0, 0 );

	BeginInitResource(&FrameSyncEvent);
}

void FD3D11Viewport::ConditionalResetSwapChain(bool bIgnoreFocus)
{
	if (!bIsValid)
	{
		// Check if the viewport's window is focused before resetting the swap chain's fullscreen state.
		HWND FocusWindow = ::GetFocus();
		const bool bIsFocused = FocusWindow == WindowHandle;
		const bool bIsIconic = !!::IsIconic(WindowHandle);
		if (bIgnoreFocus || (bIsFocused && !bIsIconic))
		{
			FlushRenderingCommands();

			// Explicit output selection in fullscreen only (commandline or HDR enabled)
			bool bNeedsForcedDisplay = bIsFullscreen && (bForcedFullscreenDisplay || PixelFormat == PF_FloatRGBA);
			HRESULT Result = SwapChain->SetFullscreenState(bIsFullscreen, bNeedsForcedDisplay ? ForcedFullscreenOutput : nullptr);

			if (SUCCEEDED(Result))
			{
				bIsValid = true;
			}
			else if (Result != DXGI_ERROR_NOT_CURRENTLY_AVAILABLE && Result != DXGI_STATUS_MODE_CHANGE_IN_PROGRESS)
			{
				UE_LOG(LogD3D11RHI, Error, TEXT("IDXGISwapChain::SetFullscreenState returned %08x, unknown error status."), Result);
			}
		}
	}

	DXGI_COLOR_SPACE_TYPE colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;

#if WITH_EDITOR
#if defined(NTDDI_WIN10_RS2)

	static auto CVarHDREnable = IConsoleManager::Get().FindConsoleVariable(TEXT("Editor.HDRSupport"));
	if (CVarHDREnable->GetInt() != 0)
	{
		if (SwapChain)
		{
			TRefCountPtr<IDXGIOutput> Output;
			if (SUCCEEDED(SwapChain->GetContainingOutput(Output.GetInitReference())))
			{
				TRefCountPtr<IDXGIOutput6> Output6;
				if (SUCCEEDED(Output->QueryInterface(IID_PPV_ARGS(Output6.GetInitReference()))))
				{
					DXGI_OUTPUT_DESC1 desc;
					Output6->GetDesc1(&desc);

					if (desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
					{
						// Display output is HDR10.
						colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
					}
				}
			}
		}
	}

	TRefCountPtr<IDXGISwapChain3> swapChain3;

	if (SUCCEEDED(SwapChain->QueryInterface(IID_PPV_ARGS(swapChain3.GetInitReference()))))
	{
		UINT colorSpaceSupport = 0;
		swapChain3->CheckColorSpaceSupport(colorSpace, &colorSpaceSupport);
		DXGI_SWAP_CHAIN_DESC desc;
		DXGI_SWAP_CHAIN_DESC1 desc1;

		swapChain3->GetDesc(&desc);
		swapChain3->GetDesc1(&desc1);


		if (colorSpaceSupport & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)
		{
			swapChain3->SetColorSpace1(colorSpace);
		}
	}
#endif
#endif


	if (colorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
	{
		PixelColorSpace = EColorSpace::ERec2020PQ;
	}
	else
	{
		PixelColorSpace = EColorSpace::ERec709sRGB;
	}


}

#include "Windows/HideWindowsPlatformTypes.h"
