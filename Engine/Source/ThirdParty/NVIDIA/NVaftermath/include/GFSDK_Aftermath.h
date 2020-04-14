/*
* Copyright (c) 2017-2019, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

/*
*   █████  █████ ██████ ████  ████   ███████   ████  ██████ ██   ██
*   ██  ██ ██      ██   ██    ██  ██ ██ ██ ██ ██  ██   ██   ██   ██
*   ██  ██ ██      ██   ██    ██  ██ ██ ██ ██ ██  ██   ██   ██   ██
*   ██████ ████    ██   ████  █████  ██ ██ ██ ██████   ██   ███████
*   ██  ██ ██      ██   ██    ██  ██ ██    ██ ██  ██   ██   ██   ██
*   ██  ██ ██      ██   █████ ██  ██ ██    ██ ██  ██   ██   ██   ██   DEBUGGER
*                                                           ██   ██
*  ████████████████████████████████████████████████████████ ██ █ ██ ████████████
*
*
*
*  HOW TO USE AFTERMATH EVENT MARKERS:
*  -----------------------------------
*
*  1)  Call 'GFSDK_Aftermath_DXxx_Initialize', to initialize the library.
*      This must be done before any other library calls are made, and the method
*      must return 'GFSDK_Aftermath_Result_Success' for initialization to
*      be complete.
*
*      Initialization of Aftermath may fail for a variety of reasons, including:
*
*      o) The initialization function was already called for the device:
*           GFSDK_Aftermath_Result_FAIL_AlreadyInitialized.
*
*
*      o) Aftermath isn't supported on the GPU associated with the device or the NVIDIA
*         display driver version installed:
*           GFSDK_Aftermath_Result_FAIL_InvalidAdapter,
*           GFSDK_Aftermath_Result_FAIL_DriverInitFailed,
*           GFSDK_Aftermath_Result_FAIL_DriverVersionNotSupported,
*           GFSDK_Aftermath_Result_FAIL_NvApiIncompatible.
*
*
*      o) A D3D API debug layer, such as PIX, was detected that is incompatible with
*         Aftermath:
*           GFSDK_Aftermath_Result_FAIL_D3dDllInterceptionNotSupported
*
*
*      o) Aftermath was disabled on the system by the current user setting the
*         'HKEY_CURRENT_USER\Software\NVIDIA Corporation\Nsight Aftermath\ForceOff'
*         Windows registry key:
*           GFSDK_Aftermath_Result_FAIL_Disabled
*
*
*  2)  For each commandlist/device context you expect to use with Aftermath,
*      initialize them using the 'GFSDK_Aftermath_DXxx_CreateContextHandle',
*      function.
*
*
*  3)  Call 'GFSDK_Aftermath_SetEventMarker', to inject an event
*      marker directly into the command stream at that point.
*
*
*  4)  Once TDR/hang occurs, call the 'GFSDK_Aftermath_GetData' API
*      to fetch the event marker last processed by the GPU for each context.
*      This API also supports fetching the current execution state for each
*      the GPU.
*
*
*  5)  Before the app shuts down, each Aftermath context handle must be cleaned
*      up, this is done with the 'GFSDK_Aftermath_ReleaseContextHandle' call.
*
*  OPTIONAL:
*
*  o)  To query the fault reason after TDR, use the 'GFSDK_Aftermath_GetDeviceStatus'
*      call.  See 'GFSDK_Aftermath_Device_Status', for the full list of possible status.
*
*
*  o)  In the event of a GPU page fault, use the 'GFSDK_Aftermath_GetPageFaultInformation'
*      method to return more information about what might of gone wrong.
*      A GPU VA is returned, along with the resource descriptor of the resource that VA
*      lands in.  NOTE: It's not 100% certain that this is the resource which caused the
*      fault, only that the faulting VA lands within this resource in memory.
*
*
*  PERFORMANCE TIPS:
*
*  Do not use 'GFSDK_Aftermath_SetEventMarker' in high frequency code paths.
*  Injecting event markers introduces considerable CPU overhead. For reduced CPU
*  overhead, use 'GFSDK_Aftermath_SetEventMarker' with markerSize=0. This
*  instructs Aftermath not to allocate and copy off memory internally, relying on
*  the application to manage marker pointers itself.
*
*/

#ifndef GFSDK_Aftermath_H
#define GFSDK_Aftermath_H

#include "GFSDK_Aftermath_Defines.h"

// Here is defined a set of features that can be enabled/disable when using Aftermath
enum GFSDK_Aftermath_FeatureFlags
{
    // The minimal flag only allows use of the GetDeviceStatus entry point.
    GFSDK_Aftermath_FeatureFlags_Minimum = 0x00000000,

    // With this flag set, the SetEventMarker and GetData entry points are available.
    //
    // Using event markers should be considered carefully as they can cause very high
    // CPU overhead when used in high frequency code paths.
    //
    GFSDK_Aftermath_FeatureFlags_EnableMarkers = 0x00000001,

    // With this flag set, resources are tracked, and information about
    //  possible page fault candidates can be accessed using GetPageFaultInformation.
    GFSDK_Aftermath_FeatureFlags_EnableResourceTracking = 0x00000002,

    // With this flag set, event markers are automatically set for all draw calls,
    // compute dispatches and copy operations to capture the call stack for the
    // corresponding API call as the event marker payload.
    // Requires also GFSDK_Aftermath_FeatureFlags_EnableMarkers to be set.
    //
    // Using this option should be considered carefully. Enabling call stack capturing
    // can cause considerable CPU overhead.
    //
    GFSDK_Aftermath_FeatureFlags_CallStackCapturing = 0x40000000,

    // With this flag set, shader debug information is generated.
    // Not supported for UWP applications.
    GFSDK_Aftermath_FeatureFlags_GenerateShaderDebugInfo = 0x00000008,

    // DEPRECATED - do not use!
    //
    // With this flag set, a special shader compiler code path to generate source-level
    // shader line tables is enabled. This is not necessary for mapping shader addresses
    // to source lines, when using Nsight Graphics to analyze crash dumps!
    // This flag is only useful if GFSDK_Aftermath_FeatureFlags_GenerateShaderDebugInfo
    // is set, too.
    // Not supported for UWP applications.
    GFSDK_Aftermath_FeatureFlags_EnableShaderSourceTracking = 0x00008000,

    // Use all Aftermath features
    // Be careful when using this! Some features can cause considerable performance overhead,
    // for example, GFSDK_Aftermath_FeatureFlags_EnableMarkers.
    GFSDK_Aftermath_FeatureFlags_Maximum = GFSDK_Aftermath_FeatureFlags_Minimum |
                                           GFSDK_Aftermath_FeatureFlags_EnableMarkers |
                                           GFSDK_Aftermath_FeatureFlags_EnableResourceTracking |
                                           GFSDK_Aftermath_FeatureFlags_CallStackCapturing |
                                           GFSDK_Aftermath_FeatureFlags_GenerateShaderDebugInfo,
};

// Used with Aftermath entry points to reference an API object.
AFTERMATH_DECLARE_HANDLE(GFSDK_Aftermath_ContextHandle);
AFTERMATH_DECLARE_HANDLE(GFSDK_Aftermath_ResourceHandle);

// Used with, 'GFSDK_Aftermath_GetData'.  Filled with information,
//  about each requested context.
struct GFSDK_Aftermath_ContextData
{
    void* markerData;
    unsigned int markerSize;
    GFSDK_Aftermath_Context_Status status;

    // Call this when 'status' is 'GFSDK_Aftermath_Context_Status_Invalid;
    //  to determine what the error failure reason is.
    GFSDK_Aftermath_Result getErrorCode()
    {
        if (status == GFSDK_Aftermath_Context_Status_Invalid)
        {
            return (GFSDK_Aftermath_Result)(uintptr_t)markerData;
        }

        return GFSDK_Aftermath_Result_Success;
    }
};

// Minimal description of a graphics resource.
struct GFSDK_Aftermath_ResourceDescriptor
{
    // This is available in DX12 only and only if the application registers the
    //  resource pointers using GFSDK_Aftermath_DX12_RegisterResource().
#ifdef __d3d12_h__
    ID3D12Resource* pAppResource;
#else
    void* pAppResource;
#endif

    UINT64 size;

    UINT width;
    UINT height;
    UINT depth;

    UINT16 mipLevels;

    DXGI_FORMAT format;

    bool bIsBufferHeap : 1;
    bool bIsStaticTextureHeap : 1;
    bool bIsRtvDsvTextureHeap : 1;
    bool bPlacedResource : 1;

    bool bWasDestroyed : 1;
};

// Used with GFSDK_Aftermath_GetPageFaultInformation
struct GFSDK_Aftermath_PageFaultInformation
{
    UINT64 faultingGpuVA;
    GFSDK_Aftermath_ResourceDescriptor resourceDesc;
    bool bhasPageFaultOccured : 1;
};

/////////////////////////////////////////////////////////////////////////
// GFSDK_Aftermath_DX11_Initialize
// GFSDK_Aftermath_DX12_Initialize
// ---------------------------------
//
// [pDx11Device]; DX11-Only
//      the current dx11 device pointer.
//
// [pDx12Device]; DX12-Only
//      the current dx12 device pointer.
//
// flags;
//      set of features to enable when initializing Aftermath
//
// version;
//      Must be set to GFSDK_Aftermath_Version_API. Used for checking
//      against library version.
//
//// DESCRIPTION;
//      Library must be initialized before any other call is made.
//      This should be done after device creation.
//      Aftermath currently only supports one D3D device,
//      the first one that is initialized.
//
/////////////////////////////////////////////////////////////////////////
#ifdef __d3d11_h__
GFSDK_Aftermath_API GFSDK_Aftermath_DX11_Initialize(GFSDK_Aftermath_Version version, GFSDK_Aftermath_FeatureFlags flags, ID3D11Device* const pDx11Device);
#endif
#ifdef __d3d12_h__
GFSDK_Aftermath_API GFSDK_Aftermath_DX12_Initialize(GFSDK_Aftermath_Version version, GFSDK_Aftermath_FeatureFlags flags, ID3D12Device* const pDx12Device);
#endif
#ifdef __vulkan_h__
// See VK_ext
#endif

/////////////////////////////////////////////////////////////////////////
// GFSDK_Aftermath_DX11_CreateContextHandle
// GFSDK_Aftermath_DX12_CreateContextHandle
// ---------------------------------
//
// (pDx11DeviceContext); DX11-Only
//      Device context to use with Aftermath.
//
// (pDx12Unknown); DX12-Only
//      Command list, Command Queue, or Device to use with Aftermath
//      If a device, must be the same device given to GFSDK_Aftermath_DX12_Initialize()
//
// pOutContextHandle;
//      The context handle for the specified context/command list/command queue/device
//      to be used with future Aftermath calls.
//
//// DESCRIPTION;
//      Before Aftermath event markers can be inserted,
//      a context handle reference must first be fetched.
//
/////////////////////////////////////////////////////////////////////////
#ifdef __d3d11_h__
GFSDK_Aftermath_API GFSDK_Aftermath_DX11_CreateContextHandle(ID3D11DeviceContext* const pDx11DeviceContext, GFSDK_Aftermath_ContextHandle* pOutContextHandle);
#endif
#ifdef __d3d12_h__
GFSDK_Aftermath_API GFSDK_Aftermath_DX12_CreateContextHandle(IUnknown* const pDx12Unknown, GFSDK_Aftermath_ContextHandle* pOutContextHandle);
#endif

/////////////////////////////////////////////////////////////////////////
// GFSDK_Aftermath_API GFSDK_Aftermath_ReleaseContextHandle
// -------------------------------------
//
// contextHandle;
//      Context to release
//
// DESCRIPTION;
//      Cleans up any resources associated with an Aftermath context
//
/////////////////////////////////////////////////////////////////////////
GFSDK_Aftermath_API GFSDK_Aftermath_ReleaseContextHandle(const GFSDK_Aftermath_ContextHandle contextHandle);

/////////////////////////////////////////////////////////////////////////
// GFSDK_Aftermath_SetEventMarker
// -------------------------------------
//
// contextHandle;
//      Command list currently being populated.
//
// markerData;
//      Pointer to data used for event marker.
//      NOTE:    If markerSize is also provided, an internal copy will be
//               made of this data. In that case there is no need to keep it
//               around after this call - stack alloc is safe.
//
// markerSize;
//      Size of event marker data in bytes.
//      NOTE:   Passing a 0 for this parameter is valid, and will instruct
//              Aftermath to only copy off the pointer supplied by markerData,
//              rather than internally making a copy. This, however, precludes
//              Aftermath from storing the marker data in GPU crash dumps.
//      NOTE:   Aftermath will internally truncate marker data to a maximum
//              size of 1024 bytes.  Use markerSize=0 and manually manage
//              memory for markers if the application requires larger ones.
//
// DESCRIPTION;
//      Drops a event into the command stream with a payload that can be
//      linked back to the data given here, 'markerData'.  It's
//      safe to call from multiple threads simultaneously, normal D3D
//      API threading restrictions apply.
//
// Using event markers should be considered carefully as they can cause considerable
// CPU overhead when used in high frequency code paths.
//
/////////////////////////////////////////////////////////////////////////
GFSDK_Aftermath_API GFSDK_Aftermath_SetEventMarker(const GFSDK_Aftermath_ContextHandle contextHandle, const void* markerData, const unsigned int markerSize);

/////////////////////////////////////////////////////////////////////////
// GFSDK_Aftermath_GetData
// ------------------------------
//
// numContexts;
//      Number of contexts to fetch information for.
//      NOTE:   Passing a 0 for this parameter will only
//              return the GPU status in pStatusOut.
//
// pContextHandles;
//      Array of contexts containing Aftermath event markers.
//
// pOutContextData;
//      OUTPUT: context data for each context requested. Contains event
//              last reached on the GPU, and status of context if
//              applicable (DX12-Only).
//      NOTE: must allocate enough space for 'numContexts' worth of structures.
//            stack allocation is fine.
//
// pStatusOut;
//      OUTPUT: the current status of the GPU.
//
// DESCRIPTION;
//      Once a TDR/crash/hang has occurred (or whenever you like), call
//      this API to retrieve the event last processed by the GPU on the
//      given context.
//
/////////////////////////////////////////////////////////////////////////
GFSDK_Aftermath_API GFSDK_Aftermath_GetData(const unsigned int numContexts, const GFSDK_Aftermath_ContextHandle* pContextHandles, GFSDK_Aftermath_ContextData* pOutContextData);

/////////////////////////////////////////////////////////////////////////
// GFSDK_Aftermath_GetDeviceStatus
// ---------------------------------
//
// pOutStatus;
//      OUTPUT: Device status.
//
//// DESCRIPTION;
//      Return the status of a D3D device.  See GFSDK_Aftermath_Device_Status.
//
/////////////////////////////////////////////////////////////////////////
GFSDK_Aftermath_API GFSDK_Aftermath_GetDeviceStatus(GFSDK_Aftermath_Device_Status* pOutStatus);

/////////////////////////////////////////////////////////////////////////
// GFSDK_Aftermath_GetPageFaultInformation
// ---------------------------------
//
// pOutPageFaultInformation;
//      OUTPUT: Information about a page fault which may have occurred.
//
//// DESCRIPTION;
//      Return any information available about a recent page fault which
//      may have occurred, causing a device removed scenario.
//      See GFSDK_Aftermath_PageFaultInformation.
//
//      Requires WDDMv2 (Windows 10) or later
//
/////////////////////////////////////////////////////////////////////////
GFSDK_Aftermath_API GFSDK_Aftermath_GetPageFaultInformation(GFSDK_Aftermath_PageFaultInformation* pOutPageFaultInformation);

// BETA FEATURES
#ifdef __d3d12_h__

/////////////////////////////////////////////////////////////////////////
// GFSDK_Aftermath_DX12_RegisterResource
// ---------------------------------
//
// pResource;
//      ID3D12Resource to register.
//
// pOutResourceHandle;
//      OUTPUT: Aftermath resource handle for the resource that was registered.
//
//// DESCRIPTION;
//      Register an ID3D12Resource with Aftermath. This allows Aftermath
//      to map a GpuVA of a page fault to the corresponding ID3D12Resource.
//
//      NOTE1: This function is only supported on Windows 10 RS4 and RS5.
//      It will return GFSDK_Aftermath_Result_FAIL_D3dDllNotSupported, if the
//      version of the D3D DLL is not supported.
//      NOTE2: This function is not supported in UWP applications.
//      NOTE3: This function is not compatible with graphics debuggers, such as
//      Nsight Graphics or the Visual Studio Graphics Debugger. It may fail with
//      GFSDK_Aftermath_Result_FAIL_D3dDllInterceptionNotSupported when called,
//      if such a debugger is active.
//
/////////////////////////////////////////////////////////////////////////
GFSDK_Aftermath_API GFSDK_Aftermath_DX12_RegisterResource(ID3D12Resource* const pResource, GFSDK_Aftermath_ResourceHandle* pOutResourceHandle);

/////////////////////////////////////////////////////////////////////////
// GFSDK_Aftermath_DX12_UnregisterResource
// ---------------------------------
//
// resourceHandle;
//      Aftermath resource handle for a resource that was reagister earlier
//      with GFSDK_Aftermath_DX12_RegisterResource().
//
//// DESCRIPTION;
//      Unregisters a previously registered resource.
//
/////////////////////////////////////////////////////////////////////////
GFSDK_Aftermath_API GFSDK_Aftermath_DX12_UnregisterResource(const GFSDK_Aftermath_ResourceHandle resourceHandle);

#endif // BETA FEATURES

/////////////////////////////////////////////////////////////////////////
//
// NOTE: Function table provided - if dynamic loading is preferred.
//
/////////////////////////////////////////////////////////////////////////
#ifdef __d3d11_h__
GFSDK_Aftermath_PFN(*PFN_GFSDK_Aftermath_DX11_Initialize)(GFSDK_Aftermath_Version version, GFSDK_Aftermath_FeatureFlags flags, ID3D11Device* const pDx11Device);
GFSDK_Aftermath_PFN(*PFN_GFSDK_Aftermath_DX11_CreateContextHandle)(ID3D11DeviceContext* const pDx11DeviceContext, GFSDK_Aftermath_ContextHandle* pOutContextHandle);
#endif

#ifdef __d3d12_h__
GFSDK_Aftermath_PFN(*PFN_GFSDK_Aftermath_DX12_Initialize)(GFSDK_Aftermath_Version version, GFSDK_Aftermath_FeatureFlags flags, ID3D12Device* const pDx12Device);
GFSDK_Aftermath_PFN(*PFN_GFSDK_Aftermath_DX12_CreateContextHandle)(IUnknown* const pDx12CommandList, GFSDK_Aftermath_ContextHandle* pOutContextHandle);
#endif

GFSDK_Aftermath_PFN(*PFN_GFSDK_Aftermath_ReleaseContextHandle)(const GFSDK_Aftermath_ContextHandle contextHandle);
GFSDK_Aftermath_PFN(*PFN_GFSDK_Aftermath_SetEventMarker)(const GFSDK_Aftermath_ContextHandle contextHandle, const void* markerData, const unsigned int markerSize);
GFSDK_Aftermath_PFN(*PFN_GFSDK_Aftermath_GetData)(const unsigned int numContexts, const GFSDK_Aftermath_ContextHandle* ppContextHandles, GFSDK_Aftermath_ContextData* pOutContextData);
GFSDK_Aftermath_PFN(*PFN_GFSDK_Aftermath_GetDeviceStatus)(GFSDK_Aftermath_Device_Status* pOutStatus);
GFSDK_Aftermath_PFN(*PFN_GFSDK_Aftermath_GetPageFaultInformation)(GFSDK_Aftermath_PageFaultInformation* pOutPageFaultInformation);
#ifdef __d3d12_h__
GFSDK_Aftermath_PFN(*PFN_GFSDK_Aftermath_RegisterResource)(ID3D12Resource* const pResource, GFSDK_Aftermath_ResourceHandle* pOutResourceHandle);
GFSDK_Aftermath_PFN(*PFN_GFSDK_Aftermath_UnregisterResource)(const GFSDK_Aftermath_ResourceHandle resourceHandle);
#endif

#endif // GFSDK_Aftermath_H
