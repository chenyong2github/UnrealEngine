// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	D3D11Viewport.h: D3D viewport RHI definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RenderResource.h"
#include "RenderUtils.h"

#if PLATFORM_HOLOLENS
#include "AllowWindowsPlatformTypes.h"
#include <dxgi1_2.h>
#include "HideWindowsPlatformTypes.h"
#endif

/** A D3D event query resource. */
class D3D11RHI_API FD3D11EventQuery : public FRenderResource
{
public:

	/** Initialization constructor. */
	FD3D11EventQuery(class FD3D11DynamicRHI* InD3DRHI):
		D3DRHI(InD3DRHI)
	{
	}

	/** Issues an event for the query to poll. */
	void IssueEvent();

	/** Waits for the event query to finish. */
	void WaitForCompletion();

	// FRenderResource interface.
	virtual void InitDynamicRHI() override;
	virtual void ReleaseDynamicRHI() override;

private:
	FD3D11DynamicRHI* D3DRHI;
	TRefCountPtr<ID3D11Query> Query;
};

static DXGI_FORMAT GetRenderTargetFormat(EPixelFormat PixelFormat)
{
	DXGI_FORMAT	DXFormat = (DXGI_FORMAT)GPixelFormats[PixelFormat].PlatformFormat;
	switch(DXFormat)
	{
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:		return DXGI_FORMAT_B8G8R8A8_UNORM;
		case DXGI_FORMAT_BC1_TYPELESS:			return DXGI_FORMAT_BC1_UNORM;
		case DXGI_FORMAT_BC2_TYPELESS:			return DXGI_FORMAT_BC2_UNORM;
		case DXGI_FORMAT_BC3_TYPELESS:			return DXGI_FORMAT_BC3_UNORM;
		case DXGI_FORMAT_R16_TYPELESS:			return DXGI_FORMAT_R16_UNORM;
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:		return DXGI_FORMAT_R8G8B8A8_UNORM;
		default: 								return DXFormat;
	}
}

class D3D11RHI_API FD3D11Viewport : public FRHIViewport
{
public:

	FD3D11Viewport(class FD3D11DynamicRHI* InD3DRHI) : D3DRHI(InD3DRHI), bFullscreenLost(false), FrameSyncEvent(InD3DRHI) {}
	FD3D11Viewport(class FD3D11DynamicRHI* InD3DRHI, HWND InWindowHandle, uint32 InSizeX, uint32 InSizeY, bool bInIsFullscreen, EPixelFormat InPreferredPixelFormat);
	~FD3D11Viewport();

	virtual void Resize(uint32 InSizeX, uint32 InSizeY, bool bInIsFullscreen, EPixelFormat PreferredPixelFormat);

	/**
	 * If the swap chain has been invalidated by DXGI, resets the swap chain to the expected state; otherwise, does nothing.
	 * Called once/frame by the game thread on all viewports.
	 * @param bIgnoreFocus - Whether the reset should happen regardless of whether the window is focused.
	 */
	void ConditionalResetSwapChain(bool bIgnoreFocus);

	/**
	 * Called whenever the Viewport is moved to see if it has moved between HDR or LDR monitors
	 */
	void CheckHDRMonitorStatus();


	/** Presents the swap chain. 
	 * Returns true if Present was done by Engine.
	 */
	bool Present(bool bLockToVsync);

	// Accessors.
	FIntPoint GetSizeXY() const { return FIntPoint(SizeX, SizeY); }
	FD3D11Texture2D* GetBackBuffer() const { return BackBuffer; }
	EColorSpaceAndEOTF GetPixelColorSpace() const { return PixelColorSpace; }

	void WaitForFrameEventCompletion()
	{
		FrameSyncEvent.WaitForCompletion();
	}

	void IssueFrameEvent()
	{
		FrameSyncEvent.IssueEvent();
	}

#if PLATFORM_HOLOLENS
	IDXGISwapChain1* GetSwapChain() const { return SwapChain; } 
#else
	IDXGISwapChain* GetSwapChain() const { return SwapChain; }
#endif

	virtual void* GetNativeSwapChain() const override { return GetSwapChain(); }
	virtual void* GetNativeBackBufferTexture() const override { return GetBackBuffer()->GetResource(); }
	virtual void* GetNativeBackBufferRT() const override { return GetBackBuffer()->GetRenderTargetView(0, 0); }

	virtual void SetCustomPresent(FRHICustomPresent* InCustomPresent) override
	{
		CustomPresent = InCustomPresent;
	}
	virtual FRHICustomPresent* GetCustomPresent() const { return CustomPresent; }

	virtual void* GetNativeWindow(void** AddParam = nullptr) const override { return (void*)WindowHandle; }
	static FD3D11Texture2D* GetSwapChainSurface(FD3D11DynamicRHI* D3DRHI, EPixelFormat PixelFormat, uint32 SizeX, uint32 SizeY, IDXGISwapChain* SwapChain);

protected:

	void ResetSwapChainInternal(bool bIgnoreFocus);

	/** Presents the frame synchronizing with DWM. */
	void PresentWithVsyncDWM();

	/**
	 * Presents the swap chain checking the return result. 
	 * Returns true if Present was done by Engine.
	 */
	bool PresentChecked(int32 SyncInterval);

	FD3D11DynamicRHI* D3DRHI;
	uint64 LastFlipTime;
	uint64 LastFrameComplete;
	uint64 LastCompleteTime;
	int32 SyncCounter;
	bool bSyncedLastFrame;
	HWND WindowHandle;
	uint32 MaximumFrameLatency;
	uint32 SizeX;
	uint32 SizeY;
	uint32 BackBufferCount;
	bool bIsFullscreen : 1;
	bool bFullscreenLost : 1;
	EPixelFormat PixelFormat;
	EColorSpaceAndEOTF PixelColorSpace;
	bool bIsValid;
#if PLATFORM_HOLOLENS
	TRefCountPtr<IDXGISwapChain1> SwapChain;
#else
	TRefCountPtr<IDXGISwapChain> SwapChain;
#endif
	TRefCountPtr<FD3D11Texture2D> BackBuffer;

	// Support for selecting non-default output for display in fullscreen exclusive
	TRefCountPtr<IDXGIOutput>	ForcedFullscreenOutput;
	bool						bForcedFullscreenDisplay;

	// Whether to create swap chain and use swap chain's back buffer surface, 
	// or don't create swap chain and create an off-screen back buffer surface.
	// Currently used for pixel streaming plugin "windowless" mode to run in the cloud without on screen display.
	bool						bNeedSwapChain;

	/** An event used to track the GPU's progress. */
	FD3D11EventQuery FrameSyncEvent;

	FCustomPresentRHIRef CustomPresent;

	DXGI_MODE_DESC SetupDXGI_MODE_DESC() const;
};

template<>
struct TD3D11ResourceTraits<FRHIViewport>
{
	typedef FD3D11Viewport TConcreteType;
};
