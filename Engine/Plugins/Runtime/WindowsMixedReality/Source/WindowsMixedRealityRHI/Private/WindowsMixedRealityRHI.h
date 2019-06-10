// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/AllowWindowsPlatformAtomics.h"

//#include <wrl.h>
#include <wrl/client.h>
#include "dxgi.h"
#include "Windows/HideWindowsPlatformAtomics.h"
#include "Windows/HideWindowsPlatformTypes.h"


#if PLATFORM_HOLOLENS
using namespace Windows::Graphics::Holographic;
using namespace Windows::UI::Core;
using namespace Windows::ApplicationModel::Core;
#endif

using namespace Microsoft::WRL;


/** Implements the WindowsMixedRealityRHI module as a dynamic RHI providing module. */
class FWindowsMixedRealityRHIModule : public IDynamicRHIModule
{
public:
	// IModuleInterface	
	virtual bool SupportsDynamicReloading() override;
	virtual void StartupModule() override;

	// IDynamicRHIModule
	virtual bool IsSupported() override;
	virtual FDynamicRHI* CreateRHI(ERHIFeatureLevel::Type RequestedFeatureLevel = ERHIFeatureLevel::Num) override;

private:
#if PLATFORM_HOLOLENS
	HolographicSpace^ HoloSpace;
#endif // PLATFORM_HOLOLENS

	FD3D11Adapter ChosenAdapter;
	// we don't use GetDesc().Description as there is a bug with Optimus where it can report the wrong name
	DXGI_ADAPTER_DESC ChosenDescription;

	ComPtr<IDXGIFactory1> DXGIFactory1;
};


class FWindowsMixedRealityDynamicRHI : public FD3D11DynamicRHI
{
public:
	FWindowsMixedRealityDynamicRHI(IDXGIFactory1* InDXGIFactory1, D3D_FEATURE_LEVEL InFeatureLevel, int32 InChosenAdapter, const DXGI_ADAPTER_DESC& ChosenDescription);

	virtual FViewportRHIRef RHICreateViewport(void* WindowHandle, uint32 SizeX, uint32 SizeY, bool bIsFullscreen, EPixelFormat PreferredPixelFormat) override;

	virtual void RHIBeginFrame() override;
	virtual void RHIEndFrame() override;

#if PLATFORM_HOLOLENS
public:
	void SetHolographicSpace(HolographicSpace^ holoSpace) { HoloSpace = holoSpace; }
	HolographicSpace^ GetHolographicSpace() const { return HoloSpace; }
private:
	HolographicSpace^ HoloSpace;
#endif // PLATFORM_HOLOLENS
	
};


class FWindowsMixedRealityViewport : public FD3D11Viewport
{
public:
	FWindowsMixedRealityViewport(class FD3D11DynamicRHI* InD3DRHI, HWND InWindowHandle, uint32 InSizeX, uint32 InSizeY, bool bInIsFullscreen, EPixelFormat InPreferredPixelFormat);

	void UpdateBackBuffer();
	void Resize(uint32 InSizeX, uint32 InSizeY, bool bInIsFullscreen, EPixelFormat PreferredPixelFormat);

private:
	TRefCountPtr<FD3D11Texture2D> OffscreenBackBuffer;
};