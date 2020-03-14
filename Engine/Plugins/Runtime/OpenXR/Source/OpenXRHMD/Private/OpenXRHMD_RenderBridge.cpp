// Copyright Epic Games, Inc. All Rights Reserved.

#include "OpenXRHMD_RenderBridge.h"
#include "OpenXRHMD.h"
#include "OpenXRHMDPrivate.h"
#include "OpenXRHMDPrivateRHI.h"
#include "OpenXRHMD_Swapchain.h"

bool FOpenXRRenderBridge::Present(int32& InOutSyncInterval)
{
	if (OpenXRHMD)
	{
		OpenXRHMD->FinishRendering();
	}

	InOutSyncInterval = 0; // VSync off

	return true;
}

#ifdef XR_USE_GRAPHICS_API_D3D11
class FD3D11RenderBridge : public FOpenXRRenderBridge
{
public:
	FD3D11RenderBridge(XrInstance InInstance, XrSystemId InSystem)
		: Binding()
	{
		PFN_xrGetD3D11GraphicsRequirementsKHR GetD3D11GraphicsRequirementsKHR;
		XR_ENSURE(xrGetInstanceProcAddr(InInstance, "xrGetD3D11GraphicsRequirementsKHR", (PFN_xrVoidFunction*)&GetD3D11GraphicsRequirementsKHR));

		XrGraphicsRequirementsD3D11KHR Requirements;
		Requirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR;
		Requirements.next = nullptr;
		if (XR_ENSURE(GetD3D11GraphicsRequirementsKHR(InInstance, InSystem, &Requirements)))
		{
			AdapterLuid = reinterpret_cast<uint64&>(Requirements.adapterLuid);
		}
	}

	virtual void* GetGraphicsBinding() override
	{
		FD3D11DynamicRHI* DynamicRHI = static_cast<FD3D11DynamicRHI*>(GDynamicRHI);
		FD3D11Device* Device = DynamicRHI->GetDevice();

		Binding.type = XR_TYPE_GRAPHICS_BINDING_D3D11_KHR;
		Binding.next = nullptr;
		Binding.device = Device;
		return &Binding;
	}

	virtual FXRSwapChainPtr CreateSwapchain(XrSession InSession, uint8 Format, uint32 SizeX, uint32 SizeY, uint32 ArraySize, uint32 NumMips, uint32 NumSamples, uint32 Flags, uint32 TargetableTextureFlags) override final
	{
		return CreateSwapchain_D3D11(InSession, Format, SizeX, SizeY, ArraySize, NumMips, NumSamples, Flags, TargetableTextureFlags);
	}

private:
	XrGraphicsBindingD3D11KHR Binding;
};

FOpenXRRenderBridge* CreateRenderBridge_D3D11(XrInstance InInstance, XrSystemId InSystem) { return new FD3D11RenderBridge(InInstance, InSystem); }
#endif

#ifdef XR_USE_GRAPHICS_API_D3D12
class FD3D12RenderBridge : public FOpenXRRenderBridge
{
public:
	FD3D12RenderBridge(XrInstance InInstance, XrSystemId InSystem)
		: Binding()
	{
		XrGraphicsRequirementsD3D12KHR Requirements;
		Requirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR;
		Requirements.next = nullptr;
		if (XR_ENSURE(xrGetD3D12GraphicsRequirementsKHR(InInstance, InSystem, &Requirements)))
		{
			AdapterLuid = reinterpret_cast<uint64&>(Requirements.adapterLuid);
		}
	}

	virtual void* GetGraphicsBinding() override
	{
		FD3D12DynamicRHI* DynamicRHI = static_cast<FD3D12DynamicRHI*>(GDynamicRHI);
		ID3D12Device* Device = DynamicRHI->GetAdapter().GetD3DDevice();
		ID3D12CommandQueue* Queue = DynamicRHI->RHIGetD3DCommandQueue();

		Binding.type = XR_TYPE_GRAPHICS_BINDING_D3D12_KHR;
		Binding.next = nullptr;
		Binding.device = Device;
		Binding.queue = Queue;
		return &Binding;
	}

	virtual FXRSwapChainPtr CreateSwapchain(XrSession InSession, uint8 Format, uint32 SizeX, uint32 SizeY, uint32 ArraySize, uint32 NumMips, uint32 NumSamples, uint32 Flags, uint32 TargetableTextureFlags) override final
	{
		return CreateSwapchain_D3D12(InSession, Format, SizeX, SizeY, ArraySize, NumMips, NumSamples, Flags, TargetableTextureFlags);
	}

private:
	XrGraphicsBindingD3D12KHR Binding;
};

FOpenXRRenderBridge* CreateRenderBridge_D3D12(XrInstance InInstance, XrSystemId InSystem) { return new FD3D12RenderBridge(InInstance, InSystem); }
#endif

#ifdef XR_USE_GRAPHICS_API_OPENGL
class FOpenGLRenderBridge : public FOpenXRRenderBridge
{
public:
	FOpenGLRenderBridge(XrInstance InInstance, XrSystemId InSystem)
		: Binding()
	{
		XrGraphicsRequirementsOpenGLKHR Requirements;
		Requirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR;
		Requirements.next = nullptr;
		Requirements.minApiVersionSupported = 0;
		Requirements.maxApiVersionSupported = 0;
		XR_ENSURE(xrGetOpenGLGraphicsRequirementsKHR(InInstance, InSystem, &Requirements));

		XrVersion RHIVersion = XR_MAKE_VERSION(FOpenGL::GetMajorVersion(), FOpenGL::GetMinorVersion(), 0);
		if (RHIVersion < Requirements.minApiVersionSupported) //-V547
		{
			UE_LOG(LogHMD, Fatal, TEXT("The OpenGL API version does not meet the minimum version required by the OpenXR runtime"));
		}

		if (RHIVersion > Requirements.maxApiVersionSupported) //-V547
		{
			UE_LOG(LogHMD, Warning, TEXT("The OpenGL API version has not been tested with the OpenXR runtime"));
		}
	}

	virtual void* GetGraphicsBinding() override
	{
#if PLATFORM_WINDOWS
		Binding.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR;
		Binding.next = nullptr;
		Binding.hDC = wglGetCurrentDC();
		Binding.hGLRC = wglGetCurrentContext();
		return &Binding;
#endif
		return nullptr;
	}

	virtual FXRSwapChainPtr CreateSwapchain(XrSession InSession, uint8 Format, uint32 SizeX, uint32 SizeY, uint32 ArraySize, uint32 NumMips, uint32 NumSamples, uint32 Flags, uint32 TargetableTextureFlags) override final
	{
		return CreateSwapchain_OpenGL(InSession, Format, SizeX, SizeY, ArraySize, NumMips, NumSamples, Flags, TargetableTextureFlags);
	}

private:
#if PLATFORM_WINDOWS
	XrGraphicsBindingOpenGLWin32KHR Binding;
#endif
};

FOpenXRRenderBridge* CreateRenderBridge_OpenGL(XrInstance InInstance, XrSystemId InSystem) { return new FOpenGLRenderBridge(InInstance, InSystem); }
#endif

#ifdef XR_USE_GRAPHICS_API_VULKAN
class FVulkanRenderBridge : public FOpenXRRenderBridge
{
public:
	FVulkanRenderBridge(XrInstance InInstance, XrSystemId InSystem)
		: Binding()
		, Instance(InInstance)
		, System(InSystem)
	{
		XrGraphicsRequirementsVulkanKHR Requirements;
		Requirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR;
		Requirements.next = nullptr;
		Requirements.minApiVersionSupported = 0;
		Requirements.maxApiVersionSupported = 0;
		XR_ENSURE(xrGetVulkanGraphicsRequirementsKHR(InInstance, InSystem, &Requirements));

		// The extension uses the OpenXR version format instead of the Vulkan one
		XrVersion RHIVersion = XR_MAKE_VERSION(
			VK_VERSION_MAJOR(UE_VK_API_VERSION),
			VK_VERSION_MINOR(UE_VK_API_VERSION),
			VK_VERSION_PATCH(UE_VK_API_VERSION)
		);
		if (RHIVersion < Requirements.minApiVersionSupported) //-V547
		{
			UE_LOG(LogHMD, Fatal, TEXT("The Vulkan API version does not meet the minimum version required by the OpenXR runtime"));
		}

		if (RHIVersion > Requirements.maxApiVersionSupported) //-V547
		{
			UE_LOG(LogHMD, Warning, TEXT("The Vulkan API version has not been tested with the OpenXR runtime"));
		}
	}

	virtual void* GetGraphicsBinding() override
	{
		FVulkanDynamicRHI* DynamicRHI = static_cast<FVulkanDynamicRHI*>(GDynamicRHI);
		FVulkanDevice* Device = DynamicRHI->GetDevice();
		FVulkanQueue* Queue = Device->GetGraphicsQueue();

		Binding.type = XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR;
		Binding.next = nullptr;
		Binding.instance = DynamicRHI->GetInstance();
		Binding.physicalDevice = Device->GetPhysicalHandle();
		Binding.device = Device->GetInstanceHandle();
		Binding.queueFamilyIndex = Queue->GetFamilyIndex();
		Binding.queueIndex = 0;
		return &Binding;
	}

	virtual uint64 GetGraphicsAdapterLuid() override
	{
		if (!AdapterLuid)
		{
			FVulkanDynamicRHI* DynamicRHI = static_cast<FVulkanDynamicRHI*>(GDynamicRHI);
			XR_ENSURE(xrGetVulkanGraphicsDeviceKHR(Instance, System, DynamicRHI->GetInstance(), &Gpu));

			const VkPhysicalDeviceIDPropertiesKHR& vkPhysicalDeviceIDProperties = DynamicRHI->GetDevice()->GetDeviceIdProperties();
			check(vkPhysicalDeviceIDProperties.deviceLUIDValid);
			AdapterLuid = reinterpret_cast<const uint64&>(vkPhysicalDeviceIDProperties.deviceLUID);
		}
		return AdapterLuid;
	}

	virtual FXRSwapChainPtr CreateSwapchain(XrSession InSession, uint8 Format, uint32 SizeX, uint32 SizeY, uint32 ArraySize, uint32 NumMips, uint32 NumSamples, uint32 Flags, uint32 TargetableTextureFlags) override final
	{
		return CreateSwapchain_Vulkan(InSession, Format, SizeX, SizeY, ArraySize, NumMips, NumSamples, Flags, TargetableTextureFlags);
	}

private:
	XrGraphicsBindingVulkanKHR Binding;
	XrInstance Instance;
	XrSystemId System;
	VkPhysicalDevice Gpu;
};

FOpenXRRenderBridge* CreateRenderBridge_Vulkan(XrInstance InInstance, XrSystemId InSystem) { return new FVulkanRenderBridge(InInstance, InSystem); }
#endif
