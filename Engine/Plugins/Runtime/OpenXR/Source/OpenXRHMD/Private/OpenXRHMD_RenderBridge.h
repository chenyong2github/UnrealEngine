// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HeadMountedDisplayBase.h"
#include "XRRenderBridge.h"
#include "XRSwapChain.h"

#include <openxr/openxr.h>

class FOpenXRHMD;

class FOpenXRRenderBridge : public FXRRenderBridge
{
public:
	FOpenXRRenderBridge()
		: AdapterLuid(0)
		, OpenXRHMD(nullptr)
	{ }

	void SetOpenXRHMD(FOpenXRHMD* InHMD) { OpenXRHMD = InHMD; }
	virtual uint64 GetGraphicsAdapterLuid() { return AdapterLuid; }

	virtual void* GetGraphicsBinding_RenderThread() = 0;
	virtual FXRSwapChainPtr CreateSwapchain(XrSession InSession, uint8 Format, uint32 SizeX, uint32 SizeY, uint32 NumMips, uint32 NumSamples, uint32 Flags, uint32 TargetableTextureFlags) = 0;

	/** FRHICustomPresent */
	virtual bool Present(int32& InOutSyncInterval);

protected:
	uint64 AdapterLuid;

private:
	FOpenXRHMD* OpenXRHMD;
};

#ifdef XR_USE_GRAPHICS_API_D3D11
FOpenXRRenderBridge* CreateRenderBridge_D3D11(XrInstance InInstance, XrSystemId InSystem);
#endif
#ifdef XR_USE_GRAPHICS_API_D3D12
FOpenXRRenderBridge* CreateRenderBridge_D3D12(XrInstance InInstance, XrSystemId InSystem);
#endif
#ifdef XR_USE_GRAPHICS_API_OPENGL
FOpenXRRenderBridge* CreateRenderBridge_OpenGL(XrInstance InInstance, XrSystemId InSystem);
#endif
#ifdef XR_USE_GRAPHICS_API_VULKAN
FOpenXRRenderBridge* CreateRenderBridge_Vulkan(XrInstance InInstance, XrSystemId InSystem);
#endif
