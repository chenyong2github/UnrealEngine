// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if PLATFORM_WINDOWS || PLATFORM_HOLOLENS
#define XR_USE_PLATFORM_WIN32		1
#define XR_USE_GRAPHICS_API_D3D11	1
#define XR_USE_GRAPHICS_API_D3D12	1
#endif

#if PLATFORM_WINDOWS
#define XR_USE_GRAPHICS_API_OPENGL	1
#endif

#if PLATFORM_ANDROID
#define XR_USE_PLATFORM_ANDROID 1
#define XR_USE_GRAPHICS_API_OPENGL_ES	1
#endif

#if PLATFORM_WINDOWS || PLATFORM_ANDROID || PLATFORM_LINUX
#define XR_USE_GRAPHICS_API_VULKAN 1
#endif

//-------------------------------------------------------------------------------------------------
// D3D11
//-------------------------------------------------------------------------------------------------

#ifdef XR_USE_GRAPHICS_API_D3D11
#include "D3D11RHIPrivate.h"
#include "D3D11Util.h"
#endif // XR_USE_GRAPHICS_API_D3D11


//-------------------------------------------------------------------------------------------------
// D3D12
//-------------------------------------------------------------------------------------------------

#ifdef XR_USE_GRAPHICS_API_D3D12
#include "D3D12RHIPrivate.h"
#include "D3D12Util.h"

#undef GetD3D11CubeFace
#undef VerifyD3D11Result
#undef GetD3D11TextureFromRHITexture
#undef FRingAllocation
#undef GetRenderTargetFormat
#undef ED3D11ShaderOffsetBuffer
#undef FindShaderResourceDXGIFormat
#undef FindUnorderedAccessDXGIFormat
#undef FindDepthStencilDXGIFormat
#undef HasStencilBits
#undef FVector4VertexDeclaration
#undef GLOBAL_CONSTANT_BUFFER_INDEX
#undef MAX_CONSTANT_BUFFER_SLOTS
#undef FD3DGPUProfiler
#undef FRangeAllocator
#endif // XR_USE_GRAPHICS_API_D3D12


//-------------------------------------------------------------------------------------------------
// OpenGL
//-------------------------------------------------------------------------------------------------

#if defined(XR_USE_GRAPHICS_API_OPENGL) || defined(XR_USE_GRAPHICS_API_OPENGL_ES)
#include "OpenGLDrvPrivate.h"
#include "OpenGLResources.h"

#ifdef XR_USE_PLATFORM_ANDROID
#include "Android/AndroidEGL.h"
#include "Android/AndroidOpenGL.h"
#endif

#endif // XR_USE_GRAPHICS_API_OPENGL

//-------------------------------------------------------------------------------------------------
// Vulkan
//-------------------------------------------------------------------------------------------------

#ifdef XR_USE_GRAPHICS_API_VULKAN
#include "VulkanRHIPrivate.h"
#include "VulkanResources.h"
#endif // XR_USE_GRAPHICS_API_VULKAN

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#endif

#if PLATFORM_ANDROID
#include <android_native_app_glue.h>
#endif

#include <openxr/openxr_platform.h>

#if PLATFORM_WINDOWS
#include "Windows/HideWindowsPlatformTypes.h"
#endif
