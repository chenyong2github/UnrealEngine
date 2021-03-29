// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if OCULUS_HMD_SUPPORTED_PLATFORMS

//-------------------------------------------------------------------------------------------------
// D3D11
//-------------------------------------------------------------------------------------------------

#if OCULUS_HMD_SUPPORTED_PLATFORMS_D3D11
#include "D3D11RHIPrivate.h"
#include "D3D11Util.h"
#endif // OCULUS_HMD_SUPPORTED_PLATFORMS_D3D11


//-------------------------------------------------------------------------------------------------
// D3D12
//-------------------------------------------------------------------------------------------------

#if OCULUS_HMD_SUPPORTED_PLATFORMS_D3D12
#include "D3D12RHIPrivate.h"
#include "D3D12Util.h"
#endif // OCULUS_HMD_SUPPORTED_PLATFORMS_D3D12


//-------------------------------------------------------------------------------------------------
// OpenGL
//-------------------------------------------------------------------------------------------------

#if OCULUS_HMD_SUPPORTED_PLATFORMS_OPENGL
#include "OpenGLDrvPrivate.h"
#include "OpenGLResources.h"
#endif // OCULUS_HMD_SUPPORTED_PLATFORMS_OPENGL


//-------------------------------------------------------------------------------------------------
// Vulkan
//-------------------------------------------------------------------------------------------------

#if OCULUS_HMD_SUPPORTED_PLATFORMS_VULKAN
#include "VulkanRHIPrivate.h"
#include "VulkanResources.h"
#endif // OCULUS_HMD_SUPPORTED_PLATFORMS_VULKAN

#endif // OCULUS_HMD_SUPPORTED_PLATFORMS