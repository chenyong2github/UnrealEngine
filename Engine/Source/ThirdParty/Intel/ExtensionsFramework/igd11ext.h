/*
 *
 * Copyright (C) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 *
 * File Name:  igd11ext.h
 *
 * Abstract:   Public header for Intel D3D11 Extensions Framework
 *
 * Notes:      This file is intended to be included by the application to use
 *             Intel D3D11 Extensions Framework
 */

#pragma once

#include <string>
#include <psapi.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef EXPORT_INTC_EXT_API
#define INTC_EXT_API    __declspec(dllexport)
#else
#define INTC_EXT_API    __declspec(dllimport)
#endif

#ifdef _WIN64
#define ID3D11_EXT_DLL  "igdext64.dll"
#define ID3D11_UMD_DLL  "igd10iumd64"
#else 
#define ID3D11_EXT_DLL  "igdext32.dll"
#define ID3D11_UMD_DLL  "igd10iumd32"
#endif

namespace INTC
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Extension Context structure to pass to all extension calls
    //////////////////////////////////////////////////////////////////////////
    struct ExtensionContext;

#if !defined (D3D_INTEL_EXT_DESC)
#define D3D_INTEL_EXT_DESC
    struct IntelDeviceInfo
    {
        DWORD   GPUMaxFreq;
        DWORD   GPUMinFreq;
        DWORD   GTGeneration;
        DWORD   EUCount;
        DWORD   PackageTDP;
        DWORD   MaxFillRate;
        char    GTGenerationName[ 40 ];
    };

    union ExtensionVersion
    {
        struct
        {
            uint32_t    Revision : 16;              ///< Decodes revision number: 0000xxxx
            uint32_t    Minor    : 8;               ///< Decodes minor version number: 00xx0000
            uint32_t    Major    : 8;               ///< Decodes major version number: xx000000
        } Version;
        uint32_t    FullVersion;                    ///< Contains the full 32bit version number
    };

    struct ExtensionInfo
    {
        const wchar_t*      pDeviceDriverDesc;          ///< Intel Graphics Device description
        IntelDeviceInfo     intelDeviceInfo;            ///< Intel Graphics Device detailed information
        ExtensionVersion    requestedExtensionVersion;  ///< D3D11 Intel Extension Framework interface version requested
        ExtensionVersion    returnedExtensionVersion;   ///< D3D11 Intel Extension Framework interface version obtained
    };

    struct ExtensionAppInfo
    {
        const wchar_t*  pApplicationName;               ///< Application name
        uint32_t        applicationVersion;             ///< Application version
        const wchar_t*  pEngineName;                    ///< Engine name
        uint32_t        engineVersion;                  ///< Engine version
    };
#endif

    //////////////////////////////////////////////////////////////////////////
    /// @brief Extension Function Prototypes
    //////////////////////////////////////////////////////////////////////////

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief BeginUAVOverlap marks the beginning point for disabling GPU synchronization between consecutive draws and 
    ///        dispatches that share UAV resources.
    /// @param pExtensionContext A pointer to the extension context associated with the current Device.
    /// @returns HRESULT Returns S_OK if it was successful.
    typedef HRESULT( APIENTRY *PFNINTCDX11EXT_D3D11BEGINUAVOVERLAP ) (
        ExtensionContext*   pExtensionContext );

    /// @brief EndUAVOverlap marks the end point for disabling GPU synchronization between consecutive draws and dispatches 
    ///        that share UAV resources.
    /// @param pExtensionContext A pointer to the extension context associated with the current Device.
    /// @returns HRESULT Returns S_OK if it was successful.
    typedef HRESULT( APIENTRY *PFNINTCDX11EXT_D3D11ENDUAVOVERLAP ) (
        ExtensionContext*   pExtensionContext );

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief MultiDrawInstancedIndirect function submits multiple DrawInstancedIndirect in one call.
    /// @param pExtensionContext A pointer to the extension context associated with the current Device.
    /// @param pDeviceContext A pointer to the device context that will be used to generate rendering commands.
    /// @param drawCount The number of draws.
    /// @param pBufferForArgs Pointer to the Arguments Buffer.
    /// @param alignedByteOffsetForArgs Offset into the Arguments Buffer.
    /// @param byteStrideForArgs The stride between elements in the Argument Buffer.
    typedef void( APIENTRY* PFNINTCDX11EXT_D3D11MULTIDRAWINSTANCEDINDIRECT )(
        ExtensionContext*       pExtensionContext,
        ID3D11DeviceContext*    pDeviceContext,
        UINT                    drawCount,
        ID3D11Buffer*           pBufferForArgs,
        UINT                    alignedByteOffsetForArgs,
        UINT                    byteStrideForArgs );

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief MultiDrawIndexedInstancedIndirect function submits multiple DrawIndexedInstancedIndirect in one call.
    /// @param pExtensionContext A pointer to the extension context associated with the current Device.
    /// @param pDeviceContext A pointer to the device context that will be used to generate rendering commands.
    /// @param drawCount The number of draws.
    /// @param pBufferForArgs Pointer to the Arguments Buffer.
    /// @param alignedByteOffsetForArgs Offset into the Arguments Buffer.
    /// @param byteStrideForArgs The stride between elements in the Argument Buffer.
    typedef void( APIENTRY* PFNINTCDX11EXT_D3D11MULTIDRAWINDEXEDINSTANCEDINDIRECT )(
        ExtensionContext*       pExtensionContext,
        ID3D11DeviceContext*    pDeviceContext,
        UINT                    drawCount,
        ID3D11Buffer*           pBufferForArgs,
        UINT                    alignedByteOffsetForArgs,
        UINT                    byteStrideForArgs );

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief MultiDrawInstancedIndirect function submits multiple DrawInstancedIndirect in one call. The number of 
    ///        draws are passed using Draw Count Buffer. It must be less or equal the Max Count argument.
    /// @param pExtensionContext A pointer to the extension context associated with the current Device.
    /// @param pDeviceContext A pointer to the device context that will be used to generate rendering commands.
    /// @param pBufferForDrawCount Buffer that contains the number of draws.
    /// @param alignedByteOffsetForDrawCount Offset into the Draw Count Buffer.
    /// @param maxCount Maximum count of draws generated by this call.
    /// @param pBufferForArgs Pointer to the Arguments Buffer.
    /// @param alignedByteOffsetForArgs Offset into the Arguments Buffer.
    /// @param byteStrideForArgs The stride between elements in the Argument Buffer.
    typedef void( APIENTRY* PFNINTCDX11EXT_D3D11MULTIDRAWINSTANCEDINDIRECTCOUNTINDIRECT )(
        ExtensionContext*       pExtensionContext,
        ID3D11DeviceContext*    pDeviceContext,
        ID3D11Buffer*           pBufferForDrawCount,
        UINT                    alignedByteOffsetForDrawCount,
        UINT                    maxCount,
        ID3D11Buffer*           pBufferForArgs,
        UINT                    alignedByteOffsetForArgs,
        UINT                    byteStrideForArgs );

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief MultiDrawIndexedInstancedIndirect function submits multiple DrawInstancedIndirect in one call. The number of 
    ///        draws are passed using Draw Count Buffer. It must be less or equal the Max Count argument.
    /// @param pExtensionContext A pointer to the extension context associated with the current Device.
    /// @param pDeviceContext A pointer to the device context that will be used to generate rendering commands.
    /// @param pBufferForDrawCount Buffer that contains the number of draws.
    /// @param alignedByteOffsetForDrawCount Offset into the Draw Count Buffer.
    /// @param maxCount Maximum count of draws generated by this call.
    /// @param pBufferForArgs Pointer to the Arguments Buffer.
    /// @param alignedByteOffsetForArgs Offset into the Arguments Buffer.
    /// @param byteStrideForArgs The stride between elements in the Argument Buffer.
    typedef void( APIENTRY* PFNINTCDX11EXT_D3D11MULTIDRAWINDEXEDINSTANCEDINDIRECTCOUNTINDIRECT )(
        ExtensionContext*       pExtensionContext,
        ID3D11DeviceContext*    pDeviceContext,
        ID3D11Buffer*           pBufferForDrawCount,
        UINT                    alignedByteOffsetForDrawCount,
        UINT                    maxCount,
        ID3D11Buffer*           pBufferForArgs,
        UINT                    alignedByteOffsetForArgs,
        UINT                    byteStrideForArgs );

    //////////////////////////////////////////////////////////////////////////
    /// @brief Extensions supported in version 1.0.0 (4.0 legacy)
    //////////////////////////////////////////////////////////////////////////
    struct D3D11_EXTENSION_FUNCS_0400
    {
        PFNINTCDX11EXT_D3D11MULTIDRAWINSTANCEDINDIRECT                          D3D11MultiDrawInstancedIndirect;
        PFNINTCDX11EXT_D3D11MULTIDRAWINDEXEDINSTANCEDINDIRECT                   D3D11MultiDrawIndexedInstancedIndirect;
        PFNINTCDX11EXT_D3D11MULTIDRAWINSTANCEDINDIRECTCOUNTINDIRECT             D3D11MultiDrawInstancedIndirectCountIndirect;
        PFNINTCDX11EXT_D3D11MULTIDRAWINDEXEDINSTANCEDINDIRECTCOUNTINDIRECT      D3D11MultiDrawIndexedInstancedIndirectCountIndirect;
    };
    using D3D11ExtensionFuncs   = D3D11_EXTENSION_FUNCS_0400;


    struct D3D11_EXTENSION_FUNCS_01000000
    {
        PFNINTCDX11EXT_D3D11MULTIDRAWINSTANCEDINDIRECT                          D3D11MultiDrawInstancedIndirect;
        PFNINTCDX11EXT_D3D11MULTIDRAWINDEXEDINSTANCEDINDIRECT                   D3D11MultiDrawIndexedInstancedIndirect;
        PFNINTCDX11EXT_D3D11MULTIDRAWINSTANCEDINDIRECTCOUNTINDIRECT             D3D11MultiDrawInstancedIndirectCountIndirect;
        PFNINTCDX11EXT_D3D11MULTIDRAWINDEXEDINSTANCEDINDIRECTCOUNTINDIRECT      D3D11MultiDrawIndexedInstancedIndirectCountIndirect;
    };

    //////////////////////////////////////////////////////////////////////////
    /// @brief Extensions supported in version 1.0.1
    //////////////////////////////////////////////////////////////////////////
    struct D3D11_EXTENSION_FUNCS_01000001
    {
        PFNINTCDX11EXT_D3D11MULTIDRAWINSTANCEDINDIRECT                          D3D11MultiDrawInstancedIndirect;
        PFNINTCDX11EXT_D3D11MULTIDRAWINDEXEDINSTANCEDINDIRECT                   D3D11MultiDrawIndexedInstancedIndirect;
        PFNINTCDX11EXT_D3D11MULTIDRAWINSTANCEDINDIRECTCOUNTINDIRECT             D3D11MultiDrawInstancedIndirectCountIndirect;
        PFNINTCDX11EXT_D3D11MULTIDRAWINDEXEDINSTANCEDINDIRECTCOUNTINDIRECT      D3D11MultiDrawIndexedInstancedIndirectCountIndirect;
        PFNINTCDX11EXT_D3D11BEGINUAVOVERLAP                                     D3D11BeginUAVOverlap;
        PFNINTCDX11EXT_D3D11ENDUAVOVERLAP                                       D3D11EndUAVOverlap;
    };

    //////////////////////////////////////////////////////////////////////////
    /// @brief Extensions supported in version 1.0.2
    //////////////////////////////////////////////////////////////////////////
    /// Function table is the same as D3D11_EXTENSION_FUNCS_01000001
    /// New internal support for:
    ///     Wave Intrinsics (compiler extension)
    //////////////////////////////////////////////////////////////////////////
    using D3D11_EXTENSION_FUNCS_01000002 = D3D11_EXTENSION_FUNCS_01000001;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Returns all D3D11 Intel Extensions interface versions supported on a current platform/driver/header file combination.
    ///        It is guaranteed that the application can initialize every extensions interface version returned by this call.
    /// @param pDevice A pointer to the current Device.
    /// @param supportedExtVersionsCount A pointer to the variable that will hold the number of supported versions.
    /// @param pSupportedExtVersions A pointer to the table of supported versions.
    ///        Pointer is null if Init fails.
    /// @returns HRESULT Returns S_OK if it was successful.
    ///                  Returns invalid HRESULT if the call was unsuccessful.
    INTC_EXT_API HRESULT D3D11GetSupportedVersions(
        ID3D11Device*                               pDevice,
        UINT32*                                     supportedExtVersionsCount,
        UINT32*                                     pSupportedExtVersions );

    typedef HRESULT( APIENTRY *PFNINTCDX11EXT_D3D11GETSUPPORTEDVERSIONS ) (
        ID3D11Device*,
        UINT32*,
        UINT32* );

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Creates D3D11 Intel Extensions Device Context and returns ppfnExtensionContext Extension Context object and
    ///        ppfnExtensionFuncs extension function pointers table. This function must be called prior to using extensions.
    /// @param pDevice A pointer to the current Device.
    /// @param ppExtensionContext A pointer to the extension context associated with the current Device.
    /// @param ppfnExtensionFuncs A pointer to the table of extension functions supported by a current driver.
    ///        Pointer is null if Init fails.
    /// @param extensionFuncsSize A size of the ppfnExtensionFuncs structure.
    /// @param pExtensionInfo A pointer to the ExtensionInfo structure that should optionally be filled only with the requestedExtensionVersion member.
    ///        Returns details on Intel Graphics Hardware and negotiated extension API interface version.
    /// @param pExtensionAppInfo A pointer to the ExtensionAppInfo structure that should be passed to the driver identifying application and engine.
    /// @returns HRESULT Returns S_OK if it was successful.
    ///                  Returns E_INVALIDARG if invalid arguments are passed.
    ///                  Returns E_OUTOFMEMORY if extensions are not supported by the driver.
    INTC_EXT_API HRESULT D3D11CreateDeviceExtensionContext1(
        ID3D11Device*                               pDevice,
        ExtensionContext**                          ppExtensionContext,
        void**                                      ppfnExtensionFuncs,
        UINT32                                      extensionFuncsSize,
        ExtensionInfo*                              pExtensionInfo,
        ExtensionAppInfo*                           pExtensionAppInfo );

    typedef HRESULT( APIENTRY *PFNINTCDX11EXT_D3D11CREATEDEVICEEXTENSIONCONTEXT1 ) (
        ID3D11Device*,
        ExtensionContext**,
        void**                                      ppfnExtensionFuncs,
        UINT32                                      extensionFuncsSize,
        ExtensionInfo*,
        ExtensionAppInfo* );

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Legacy function. Only supports version 4.0 and Multi Draw Indirect extension.
    ///        Creates D3D11 Intel Extensions Device Context and returns ppfnExtensionContext Extension Context object and
    ///        ppfnExtensionFuncs extension function pointers table. This function must be called prior to using extensions.
    /// @param pDevice A pointer to the current Device.
    /// @param ppExtensionContext A pointer to the extension context associated with the current Device.
    /// @param ppfnExtensionFuncs A pointer to the table of extension functions supported by a current driver.
    ///        Pointer is null if Init fails.
    /// @param pExtensionInfo A pointer to the ExtensionInfo structure that should optionally be filled only with the requestedExtensionVersion member.
    ///        Returns details on Intel Graphics Hardware and negotiated extension API interface version.
    /// @param pExtensionAppInfo A pointer to the ExtensionAppInfo structure that should be passed to the driver identifying application and engine.
    /// @returns HRESULT Returns S_OK if it was successful.
    ///                  Returns E_INVALIDARG if invalid arguments are passed.
    ///                  Returns E_OUTOFMEMORY if extensions are not supported by the driver.
    INTC_EXT_API HRESULT D3D11CreateDeviceExtensionContext(
        ID3D11Device*                               pDevice,
        ExtensionContext**                          ppExtensionContext,
        D3D11ExtensionFuncs**                       ppfnExtensionFuncs,
        ExtensionInfo*                              pExtensionInfo,
        ExtensionAppInfo*                           pExtensionAppInfo );

    typedef HRESULT( APIENTRY *PFNINTCDX11EXT_D3D11CREATEDEVICEEXTENSIONCONTEXT ) (
        ID3D11Device*,
        ExtensionContext**,
        D3D11ExtensionFuncs**,
        ExtensionInfo*,
        ExtensionAppInfo* );

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Destroys D3D11 Intel Extensions Device Context and provides cleanup for the Intel Extensions Framework. 
    ///        No D3D11 extensions can be used after calling this function.
    /// @param ppExtensionContext A pointer to the extension context associated with the current Device.
    /// @returns HRESULT Returns S_OK if it was successful.
    ///                  Returns E_INVALIDARG if invalid arguments are passed.
    INTC_EXT_API HRESULT D3D11DestroyDeviceExtensionContext(
        ExtensionContext**                          ppExtensionContext );

    typedef HRESULT( APIENTRY *PFNINTCDX11EXT_D3D11DESTROYDEVICEEXTENSIONCONTEXT ) (
        ExtensionContext** );

    ////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Extension library loading helper function.
    /// @details 
    ///     Function helps load D3D11 Extensions Framework and return hLibModule handle. 
    ///     If useCurrentProcessDir is set, the function tries to load the library from the current
    ///     process directory first. If that was unsuccessful or useCurrentProcessDir was not set, 
    ///     it tries to find the full path to the Intel DX11 UMD Driver module that must be loaded
    ///     by the current process. Library is loaded from the same path (whether it is DriverStore
    ///     location or system32 folder).
    /// @param useCurrentProcessDir If set, the function tries to load the library from the current
    ///        process directory first.
    ////////////////////////////////////////////////////////////////////////////////////////
    __inline HMODULE D3D11LoadIntelExtensionsLibrary( BOOL useCurrentProcessDir = false )
    {
#ifdef _WIN32
        HMODULE         hLibModule = NULL;
        HANDLE          hProc = GetCurrentProcess();
        HMODULE         hMods[ 1024 ];
        DWORD           cbNeeded;
        char            szModFullPath[ MAX_PATH ];
        std::string     szPath;

        // Try to load library from the current process directory
        if( useCurrentProcessDir )
        {
            if( GetModuleFileNameA( NULL, szModFullPath, sizeof( szModFullPath ) / sizeof( char ) ) )
            {
                szPath = szModFullPath;

                // Find the path to the current process directory
                size_t pos = szPath.rfind( '\\' );
                if( pos != std::string::npos )
                {
                    szPath.erase( pos );
                    szPath += '\\';
                    szPath += ID3D11_EXT_DLL;
                }
                else
                {
                    // Something is wrong with the current process path, try using library name only
                    szPath = ID3D11_EXT_DLL;
                }

                // Try to load the library
                hLibModule = LoadLibraryExA( szPath.c_str(), NULL, 0 );
                if( hLibModule )
                {
                    return hLibModule;
                }
            }
        }

        // Try to load library from the Intel DX11 UMD Graphics Driver location (most likely DriverStore or system32)
        if( EnumProcessModules( hProc, hMods, sizeof( hMods ), &cbNeeded ) )
        {
            // Go through all the enumerated processes and find the driver module
            for( unsigned int i = 0; i < ( cbNeeded / sizeof( HMODULE ) ); i++ )
            {
                // Get the full path to the module
                if( GetModuleFileNameExA( hProc, hMods[ i ], szModFullPath, sizeof( szModFullPath ) / sizeof( char ) ) )
                {
                    szPath = szModFullPath;

                    // Find UMD driver module path
                    size_t  pos = szPath.find( ID3D11_UMD_DLL );
                    if( pos != std::string::npos )
                    {
                        szPath.erase( pos );
                        szPath += ID3D11_EXT_DLL;

                        // Try to load the library
                        hLibModule = LoadLibraryExA( szPath.c_str(), NULL, 0 );
                        if( hLibModule )
                        {
                            return hLibModule;
                        }
                    }
                }
            }
        }
#endif

        return NULL;
    }

} // namespace INTC

#ifdef __cplusplus
} // extern "C"
#endif
