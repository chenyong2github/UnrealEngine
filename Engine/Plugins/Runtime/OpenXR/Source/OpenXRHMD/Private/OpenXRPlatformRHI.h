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
#endif

#if PLATFORM_WINDOWS || PLATFORM_ANDROID
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
#endif // XR_USE_GRAPHICS_API_D3D12


//-------------------------------------------------------------------------------------------------
// OpenGL
//-------------------------------------------------------------------------------------------------

#ifdef XR_USE_GRAPHICS_API_OPENGL
#include "OpenGLDrvPrivate.h"
#include "OpenGLResources.h"
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
