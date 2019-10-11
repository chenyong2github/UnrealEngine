/*
 *
 * Copyright (C) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 *
 * File Name:  igd12ext.h
 * 
 * Abstract:   Public header for Intel D3D12 Extensions Framework
 * 
 * Notes:      This file is intended to be included by the application to use
 *             Intel D3D12 Extensions Framework
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
#define ID3D12_EXT_DLL  "igdext64.dll"
#define ID3D12_UMD_DLL  "igd12umd64"
#else 
#define ID3D12_EXT_DLL  "igdext32.dll"
#define ID3D12_UMD_DLL  "igd12umd32"
#endif

#define D3D12_EXT_MAKE_VERSION(MajorVersion, MinorVersion)                              ( ( ( MajorVersion & 0xFF ) << 24 ) | ( ( MinorVersion & 0xFF) << 16 ) )
#define D3D12_EXT_MAKE_FULL_VERSION(MajorVersion, MinorVersion, Revision)               ( ( ( MajorVersion & 0xFF ) << 24 ) | ( ( MinorVersion & 0xFF) << 16 ) | ( Revision & 0xFFFF ) )
#define D3D12_EXT_GET_VERSION_MAJOR(Version)                                            ( ( Version & 0xFF000000 ) >> 24 )
#define D3D12_EXT_GET_VERSION_MINOR(Version)                                            ( ( Version & 0x00FF0000 ) >> 16 )
#define D3D12_EXT_GET_VERSION_REVISION(Version)                                         ( Version & 0x0000FFFF )
#define D3D12_EXT_GET_VERSION_NO_REVISION(Version)                                      ( Version & 0xFFFF0000 )

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
        ExtensionVersion    requestedExtensionVersion;  ///< D3D12 Intel Extension Framework interface version requested
        ExtensionVersion    returnedExtensionVersion;   ///< D3D12 Intel Extension Framework interface version obtained
    };

    struct ExtensionAppInfo
    {
        const wchar_t*  pApplicationName;               ///< Application name
        uint32_t        applicationVersion;             ///< Application version
        const wchar_t*  pEngineName;                    ///< Engine name
        uint32_t        engineVersion;                  ///< Engine version
    };
#endif

    #define	D3D12_COARSE_PIXEL_SIZE_OBJECT_COUNT_PER_PIPELINE	( 16 )

    //////////////////////////////////////////////////////////////////////////
    enum D3D12_COARSE_PIXEL_SIZE_MODE
    {
        COARSE_PIXEL_SIZE_NONE              = 0x0,
        COARSE_PIXEL_SIZE_CONSTANT          = 0x1,
        COARSE_PIXEL_SIZE_PER_PIXEL_RADIAL  = 0x2,
    };

    struct D3D12_COARSE_PIXEL_SIZE_DESC
    {
        D3D12_COARSE_PIXEL_SIZE_MODE    Mode;
        float                           MinSizeX;
        float                           MinSizeY;
        float                           MaxSizeX;
        float                           MaxSizeY;
        float                           CenterX;
        float                           CenterY;
        float                           AspectRatio;
        float                           RadiusMinSize;
        float                           RadiusMaxSize;
    };

    //////////////////////////////////////////////////////////////////////////
    typedef struct D3D12_VIEWPORT_0001
    {
        FLOAT                           TopLeftX;
        FLOAT                           TopLeftY;
        FLOAT                           Width;
        FLOAT                           Height;
        FLOAT                           MinDepth;
        FLOAT                           MaxDepth;
        D3D12_COARSE_PIXEL_SIZE_DESC    CpsDesc;
    } D3D12_VIEWPORT_0001;
    using D3D12_VIEWPORT = D3D12_VIEWPORT_0001;

    //////////////////////////////////////////////////////////////////////////
    typedef enum DXGI_FORMAT_0001
    {
        DXGI_FORMAT_R10G10B10_FLOAT_A2_UNORM = 133

    } DXGI_FORMAT_0001;
    using DXGI_FORMAT = DXGI_FORMAT_0001;

    typedef enum D3D12_RESOURCE_FLAGS_0001
    {
        D3D12_RESOURCE_FLAG_ALLOW_PROCEDURAL_TEXTURE = 0x400,
        D3D12_RESOURCE_FLAG_ALLOW_TEXEL_MASK = 0x800
    } D3D12_RESOURCE_FLAGS_0001;
    using D3D12_RESOURCE_FLAGS = D3D12_RESOURCE_FLAGS_0001;

    typedef enum D3D12_TEXEL_MASK_GRANULARITY
    {
        D3D12_GRANULARITY_DEFAULT   = 0,
        D3D12_GRANULARITY_1x1       = 1, //     reserved
        D3D12_GRANULARITY_2x2       = 2,
        D3D12_GRANULARITY_4x2       = 3,
        D3D12_GRANULARITY_4x4       = 4,
        D3D12_GRANULARITY_8x4       = 5,
        D3D12_GRANULARITY_8x8       = 6,
        D3D12_GRANULARITY_4Kb       = 7,
        D3D12_GRANULARITY_64Kb      = 8
    } D3D12_TEXEL_MASK_GRANULARITY;

    struct D3D12_RESOURCE_DESC_0001
    {                      
        union
        {
            ::D3D12_RESOURCE_DESC           *pD3D12Desc;
            //::D3D12_RESOURCE_DESC1          *pD3D12Desc1;         // Future definitions
        };

        // Corner Texel Mode specific
        BOOL                                CornerTexelMode;
    };
    struct D3D12_RESOURCE_DESC_0002 : D3D12_RESOURCE_DESC_0001
    {
        // Extending supported surface formats
        DXGI_FORMAT                         Format;
        D3D12_RESOURCE_FLAGS                Flags;
    };
    struct D3D12_RESOURCE_DESC_0003 : D3D12_RESOURCE_DESC_0002
    {
        // Texel Mask Granularity
        D3D12_TEXEL_MASK_GRANULARITY        TexelMaskGranularity;
    };
    using D3D12_RESOURCE_DESC = D3D12_RESOURCE_DESC_0003;

    //////////////////////////////////////////////////////////////////////////
    enum D3D12_SAMPLETAPDISCARD_FILTER_TYPE
    {
        FILTER_NONE             = 0x0,
        FILTER_NULL_TEXELS      = 0x1,
        FILTER_BORDER_TEXELS    = 0x2,
        FILTER_BOTH             = 0x3
    };

    struct D3D12_SAMPLER_DESC_0001
    {
        union
        {
            ::D3D12_SAMPLER_DESC            *pD3D12Desc;
        };

        BOOL                                CPSLODCompensationEnable;
    };
    
    struct D3D12_SAMPLER_DESC_0002 : D3D12_SAMPLER_DESC_0001
    {
        D3D12_SAMPLETAPDISCARD_FILTER_TYPE  SampleTapDiscardFilterMode;
    };
    using D3D12_SAMPLER_DESC = D3D12_SAMPLER_DESC_0002;

    //////////////////////////////////////////////////////////////////////////
    typedef enum D3D12_SB_TOKENIZED_PROGRAM_TYPE_0001
    {
        D3D12_SB_TEXEL_SHADER = 6,                     /// TEXEL_SHADER stage
    } D3D12_SB_TOKENIZED_PROGRAM_TYPE_0001;

    struct D3D12_GRAPHICS_PIPELINE_STATE_DESC_0001
    {
        union
        {
            ::D3D12_GRAPHICS_PIPELINE_STATE_DESC    *pD3D12Desc;
        };

        D3D12_INPUT_LAYOUT_DESC             InputLayout;
        /// Extension shader bypass
        D3D12_SHADER_BYTECODE               VS;         /// VertexShader
        D3D12_SHADER_BYTECODE               PS;         /// PixelShader
        D3D12_SHADER_BYTECODE               DS;         /// DomainShader
        D3D12_SHADER_BYTECODE               HS;         /// HullShader
        D3D12_SHADER_BYTECODE               GS;         /// PixelShader
    };
    struct D3D12_GRAPHICS_PIPELINE_STATE_DESC_0002 : D3D12_GRAPHICS_PIPELINE_STATE_DESC_0001
    {
        // CPS Specific 
        bool                                CPS;        /// Coarse Pixel Shading
    };
    struct D3D12_GRAPHICS_PIPELINE_STATE_DESC_0003 : D3D12_GRAPHICS_PIPELINE_STATE_DESC_0002
    {
        // AMFS specific
        D3D12_SHADER_BYTECODE               TS[ 8 ];    /// TexelShader
        UINT                                NumProceduralTextures;
        DXGI_FORMAT                         PTVFormats[ 8 ];
    };
    using D3D12_GRAPHICS_PIPELINE_STATE_DESC = D3D12_GRAPHICS_PIPELINE_STATE_DESC_0003;

    //////////////////////////////////////////////////////////////////////////
    struct D3D12_COMPUTE_PIPELINE_STATE_DESC_0001
    {
        union
        {
            ::D3D12_COMPUTE_PIPELINE_STATE_DESC     *pD3D12Desc;
        };

        /// Extension shader bypass
        D3D12_SHADER_BYTECODE               CS;         /// ComputeShader
    };
    using D3D12_COMPUTE_PIPELINE_STATE_DESC = D3D12_COMPUTE_PIPELINE_STATE_DESC_0001;

     //////////////////////////////////////////////////////////////////////////
   enum D3D12_COMMAND_QUEUE_THROTTLE_POLICY
    {
        D3D12_COMMAND_QUEUE_THROTTLE_DYNAMIC = 0,
        D3D12_COMMAND_QUEUE_THROTTLE_MAX_PERFORMANCE = 255
    };

    struct D3D12_COMMAND_QUEUE_DESC_0001
    {
        union
        {
            ::D3D12_COMMAND_QUEUE_DESC              *pD3D12Desc;
        };

        /// Extension shader bypass
        D3D12_COMMAND_QUEUE_THROTTLE_POLICY     CommandThrottlePolicy;         /// Command Queue Throttle Policy
    };
    using D3D12_COMMAND_QUEUE_DESC = D3D12_COMMAND_QUEUE_DESC_0001;

    //////////////////////////////////////////////////////////////////////////
    /// AMFS specific
    #define	D3D12_PROCTEXTURE_MAX_TEXEL_BLOCK_U_OR_V_DIMENSION	( 8 )

    typedef enum D3D12_RESOURCE_STATES_0001
    {
        D3D12_RESOURCE_STATE_PROCEDURAL_TEXTURE     = 0x4000,
        D3D12_RESOURCE_STATE_TEXEL_MASK_RESOURCE    = 0x8000,
    } D3D12_RESOURCE_STATES_0001;
    using D3D12_RESOURCE_STATES = D3D12_RESOURCE_STATES_0001;

    //////////////////////////////////////////////////////////////////////////
    struct D3D12_RESOURCE_BARRIER_0001
    {
        union
        {
            ::D3D12_RESOURCE_BARRIER                *pD3D12Desc;
        };

        D3D12_RESOURCE_STATES   StateBefore;
        D3D12_RESOURCE_STATES   StateAfter;
    };
    using D3D12_RESOURCE_BARRIER = D3D12_RESOURCE_BARRIER_0001;

    //////////////////////////////////////////////////////////////////////////
    struct D3D12_SHADER_RESOURCE_VIEW_DESC_0001
    {
        union
        {
            ::D3D12_SHADER_RESOURCE_VIEW_DESC       *pD3D12Desc;
        };

        // Extending supported surface formats
        DXGI_FORMAT                 Format;
    };
    using D3D12_SHADER_RESOURCE_VIEW_DESC = D3D12_SHADER_RESOURCE_VIEW_DESC_0001;

    //////////////////////////////////////////////////////////////////////////
    struct D3D12_UNORDERED_ACCESS_VIEW_DESC_0001
    {
        union
        {
            ::D3D12_UNORDERED_ACCESS_VIEW_DESC      *pD3D12Desc;
        };

        // Extending supported surface formats
        DXGI_FORMAT                 Format;
    };
    using D3D12_UNORDERED_ACCESS_VIEW_DESC = D3D12_UNORDERED_ACCESS_VIEW_DESC_0001;


    //////////////////////////////////////////////////////////////////////////
    struct D3D12_RENDER_TARGET_VIEW_DESC_0001
    {
        union
        {
            ::D3D12_RENDER_TARGET_VIEW_DESC         *pD3D12Desc;
        };

        // Extending supported surface formats
        DXGI_FORMAT                 Format;
    };
    using D3D12_RENDER_TARGET_VIEW_DESC = D3D12_RENDER_TARGET_VIEW_DESC_0001;

    using D3D12_PROCEDURAL_TEXTURE_RESOURCE_VIEW_DESC = D3D12_SHADER_RESOURCE_VIEW_DESC;

    // Texel Mask Views
    using D3D12_BUFFER_TMV          = D3D12_BUFFER_SRV;
    using D3D12_TEX1D_TMV           = D3D12_TEX1D_SRV;
    using D3D12_TEX1D_ARRAY_TMV     = D3D12_TEX1D_ARRAY_SRV;
    using D3D12_TEX2D_TMV           = D3D12_TEX2D_SRV;
    using D3D12_TEX2D_ARRAY_TMV     = D3D12_TEX2D_ARRAY_SRV;
    using D3D12_TEX3D_TMV           = D3D12_TEX3D_SRV;
    using D3D12_TEXCUBE_TMV         = D3D12_TEXCUBE_SRV;
    using D3D12_TEXCUBE_ARRAY_TMV   = D3D12_TEXCUBE_ARRAY_SRV;

    typedef enum D3D12_TMV_DIMENSION
    {
        D3D12_TMV_DIMENSION_UNKNOWN         = 0,
        D3D12_TMV_DIMENSION_BUFFER          = 1,
        D3D12_TMV_DIMENSION_TEXTURE1D       = 2,
        D3D12_TMV_DIMENSION_TEXTURE1DARRAY  = 3,
        D3D12_TMV_DIMENSION_TEXTURE2D       = 4,
        D3D12_TMV_DIMENSION_TEXTURE2DARRAY  = 5,
        D3D12_TMV_DIMENSION_TEXTURE3D       = 6,
        D3D12_TMV_DIMENSION_TEXTURECUBE     = 7,
        D3D12_TMV_DIMENSION_TEXTURECUBEARRAY= 8,
    } D3D12_TMV_DIMENSION;

    typedef struct D3D12_TEXEL_MASK_VIEW_DESC
    {
        D3D12_TMV_DIMENSION ViewDimension;
        union 
        {
            D3D12_BUFFER_TMV Buffer;
            D3D12_TEX1D_TMV Texture1D;
            D3D12_TEX1D_ARRAY_TMV Texture1DArray;
            D3D12_TEX2D_TMV Texture2D;
            D3D12_TEX2D_ARRAY_TMV Texture2DArray;
            D3D12_TEX3D_TMV Texture3D;
            D3D12_TEXCUBE_TMV TextureCube;
            D3D12_TEXCUBE_ARRAY_TMV TextureCubeArray;
        };
    } D3D12_TEXEL_MASK_VIEW_DESC;

    typedef struct D3D12_REGION
    {
        D3D12_BOX box; // Z units used for array index on 2D resources
        UINT mipLevel;
    } D3D12_REGION;

    //////////////////////////////////////////////////////////////////////////
    struct D3D12_TEXTURE_COPY_LOCATION_0001
    {
        union
        {
            ::D3D12_TEXTURE_COPY_LOCATION       *pD3D12Desc;
        };

        DXGI_FORMAT                 Format;
    };
    using D3D12_TEXTURE_COPY_LOCATION = D3D12_TEXTURE_COPY_LOCATION_0001;

    //////////////////////////////////////////////////////////////////////////
    typedef enum D3D12_DESCRIPTOR_HEAP_TYPE_0001
    {
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV_PTV,         /// Constant buffer/Shader resource/Unordered access views/Procedural texture views
    } D3D12_DESCRIPTOR_HEAP_TYPE_0001;

    typedef enum D3D12_SHADER_VISIBILITY_0001
    {
        D3D12_SHADER_VISIBILITY_TEXEL = 6
    } D3D12_SHADER_VISIBILITY_0001;

    typedef enum D3D12_DESCRIPTOR_RANGE_TYPE_0001
    {
        D3D12_DESCRIPTOR_RANGE_TYPE_PTV = ( D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER + 1 ),
    } D3D12_DESCRIPTOR_RANGE_TYPE_0001;


    //////////////////////////////////////////////////////////////////////////
    /// @brief Extension Function Prototypes
    //////////////////////////////////////////////////////////////////////////

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Extension feature that allows texture filtering to compute the sum of the sample taps that do not touch a null texel, a border texel, or both
    /// @param pExtensionContext A pointer to the extension context associated with the current Device.
    /// @param pDesc A pointer to overridden D3D12_GRAPHICS_PIPELINE_STATE_DESC descriptor.
    /// @param riid The globally unique identifier (GUID) for the pipeline state interface.
    /// @param ppPipelineState A pointer to a memory block that receives a pointer to the ID3D12PipelineState interface for the pipeline state object.
    /// @returns HRESULT Returns S_OK if it was successful.
    typedef HRESULT( APIENTRY *PFNINTCDX12EXT_CREATEGRAPHICSPIPELINESTATE ) (
        ExtensionContext*                           pExtensionContext,
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC*   pDesc,
        REFIID                                      riid,
        void**                                      ppPipelineState );
    typedef HRESULT( APIENTRY *PFNINTCDX12EXT_CREATEGRAPHICSPIPELINESTATE_0001 ) (
        ExtensionContext*                           pExtensionContext,
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC_0001*   pDesc,
        REFIID                                      riid,
        void**                                      ppPipelineState );
    typedef HRESULT( APIENTRY *PFNINTCDX12EXT_CREATEGRAPHICSPIPELINESTATE_0002 ) (
        ExtensionContext*                           pExtensionContext,
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC_0002*   pDesc,
        REFIID                                      riid,
        void**                                      ppPipelineState );

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Creates a compute pipeline state object.
    /// @param pExtensionContext A pointer to the extension context associated with the current Device.
    /// @param pDesc A pointer to a D3D12_COMPUTE_PIPELINE_STATE_DESC structure that describes compute pipeline state.
    /// @param riid The globally unique identifier (GUID) for the pipeline state interface.
    /// @param ppPipelineState A pointer to a memory block that receives a pointer to the ID3D12PipelineState interface for the pipeline state object.
    /// @returns HRESULT Returns S_OK if it was successful.
    typedef HRESULT( APIENTRY *PFNINTCDX12EXT_CREATECOMPUTEPIPELINESTATE ) (
        ExtensionContext*                           pExtensionContext,
        const D3D12_COMPUTE_PIPELINE_STATE_DESC*    pDesc,
        REFIID                                      riid,
        void**                                      ppPipelineState );

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Creates a command queue.
    /// @param pExtensionContext A pointer to the extension context associated with the current Device.
    /// @param pDesc A pointer to a D3D12_COMPUTE_PIPELINE_STATE_DESC structure that describes compute pipeline state.
    /// @param riid The globally unique identifier (GUID) for the command queue interface.
    /// @param ppCommandQueue A pointer to a memory block that receives a pointer to the ID3D12CommandQueue interface for the command queue.
    /// @returns HRESULT Returns S_OK if it was successful.
    typedef HRESULT( APIENTRY *PFNINTCDX12EXT_CREATECOMMANDQUEUE ) (
        ExtensionContext*                           pExtensionContext,
        const D3D12_COMMAND_QUEUE_DESC*             pDesc,
        REFIID                                      riid,
        void**                                      ppCommandQueue );

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Extension feature that allows texture filtering to compute the sum of the sample taps that do not touch a null texel, a border texel, or both
    /// @param pExtensionContext A pointer to the extension context associated with the current Device.
    /// @param pCommandList A pointer to the ID3D12GraphicsCommandList interface for the command list.
    /// @param pDesc A pointer to D3D12_COARSE_PIXEL_SIZE_DESC descriptor.
    /// @returns HRESULT Returns S_OK if it was successful.
    typedef HRESULT( APIENTRY *PFNINTCDX12EXT_SETCOARSEPIXELSIZESTATE ) (
        ExtensionContext*                           pExtensionContext,
        ID3D12GraphicsCommandList*                  pCommandList,
        const D3D12_COARSE_PIXEL_SIZE_DESC*         pDesc );

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Bind an array of viewports to the rasterizer stage of the pipeline.
    /// @param pExtensionContext A pointer to the extension context associated with the current Device.
    /// @param pCommandList A pointer to the ID3D12GraphicsCommandList interface for the command list.
    /// @param NumViewports Number of viewports to bind. The range of valid values is (0, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE).
    /// @param pViewports An array of D3D12_VIEWPORT structures to bind to the device.
    typedef void( APIENTRY *PFNINTCDX12EXT_RSSETVIEWPORTS ) (
        ExtensionContext*                           pExtensionContext,
        ID3D12GraphicsCommandList*                  pCommandList,
        UINT                                        NumViewports,
        const D3D12_VIEWPORT*                       pViewports );

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @implements EXTENSION_FUNCS_V2#CreateCommittedResource
    /// @brief Creates both a resource and an implicit heap, such that the heap is big enough to contain the entire resource and the resource is mapped to the heap. Supported extensions:
    ///        CornerTextelMode - Enables texel values to be specified at integral positions (texel corners) instead of at half-texel offsets (texel centers)
    /// @param pExtensionContext A pointer to a D3D12_HEAP_PROPERTIES structure that provides properties for the resource's heap.
    /// @param pHeapProperties A pointer to the ID3D12Heap interface that represents the heap in which the resource is placed.
    /// @param HeapFlags The offset, in bytes, to the resource.
    /// @param pDesc A pointer to a overridden D3D12_RESOURCE_DESC structure that describes the resource.
    /// @param InitialResourceState The initial state of the resource, as a bitwise-OR'd combination of D3D12_RESOURCE_STATES enumeration constants.
    /// @param pOptimizedClearValue Specifies a D3D12_CLEAR_VALUE that describes the default value for a clear color.
    /// @param riidResource The globally unique identifier (GUID) for the resource interface.
    /// @param ppvResource A pointer to memory that receives the requested interface pointer to the created resource object.
    /// @returns HRESULT Returns S_OK if it was successful.
    typedef HRESULT( APIENTRY *PFNINTCDX12EXT_CREATECOMMITTEDRESOURCE ) (
        ExtensionContext*                           pExtensionContext,
        const D3D12_HEAP_PROPERTIES*                pHeapProperties,
        D3D12_HEAP_FLAGS                            HeapFlags,
        const D3D12_RESOURCE_DESC*                  pDesc,
        D3D12_RESOURCE_STATES                       InitialResourceState,
        const D3D12_CLEAR_VALUE*                    pOptimizedClearValue,
        REFIID                                      riidResource,
        void**                                      ppvResource );
    typedef HRESULT( APIENTRY *PFNINTCDX12EXT_CREATECOMMITTEDRESOURCE_0001 ) (
        ExtensionContext*                           pExtensionContext,
        const D3D12_HEAP_PROPERTIES*                pHeapProperties,
        D3D12_HEAP_FLAGS                            HeapFlags,
        const D3D12_RESOURCE_DESC_0001*             pDesc,
        D3D12_RESOURCE_STATES                       InitialResourceState,
        const D3D12_CLEAR_VALUE*                    pOptimizedClearValue,
        REFIID                                      riidResource,
        void**                                      ppvResource );
    typedef HRESULT( APIENTRY *PFNINTCDX12EXT_CREATECOMMITTEDRESOURCE_0002 ) (
        ExtensionContext*                           pExtensionContext,
        const D3D12_HEAP_PROPERTIES*                pHeapProperties,
        D3D12_HEAP_FLAGS                            HeapFlags,
        const D3D12_RESOURCE_DESC_0002*             pDesc,
        D3D12_RESOURCE_STATES                       InitialResourceState,
        const D3D12_CLEAR_VALUE*                    pOptimizedClearValue,
        REFIID                                      riidResource,
        void**                                      ppvResource );

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Creates a resource that is placed in a specific heap. Supported extensions:
    ///        CornerTextelMode - Enables texel values to be specified at integral positions (texel corners) instead of at half-texel offsets (texel centers)
    /// @param pExtensionContext A pointer to the extension context associated with the current Device
    /// @param pHeap A pointer to the ID3D12Heap interface that represents the heap in which the resource is placed.
    /// @param HeapOffset The offset, in bytes, to the resource.
    /// @param pDesc A pointer to a overridden D3D12_RESOURCE_DESC structure that describes the resource.
    /// @param InitialState The initial state of the resource, as a bitwise-OR'd combination of D3D12_RESOURCE_STATES enumeration constants.
    /// @param pOptimizedClearValue Specifies a D3D12_CLEAR_VALUE that describes the default value for a clear color.
    /// @param riid The globally unique identifier (GUID) for the resource interface.
    /// @param ppvResource A pointer to a memory block that receives a pointer to the resource.
    /// @returns HRESULT Returns S_OK if it was successful.
    typedef HRESULT( APIENTRY *PFNINTCDX12EXT_CREATEPLACEDRESOURCE ) (
        ExtensionContext*                           pExtensionContext,
        ID3D12Heap*                                 pHeap,
        UINT64                                      HeapOffset,
        const D3D12_RESOURCE_DESC*                  pDesc,
        D3D12_RESOURCE_STATES                       InitialState,
        const D3D12_CLEAR_VALUE*                    pOptimizedClearValue,
        REFIID                                      riid,
        void**                                      ppvResource );
    typedef HRESULT( APIENTRY *PFNINTCDX12EXT_CREATEPLACEDRESOURCE_0001 ) (
        ExtensionContext*                           pExtensionContext,
        ID3D12Heap*                                 pHeap,
        UINT64                                      HeapOffset,
        const D3D12_RESOURCE_DESC_0001*             pDesc,
        D3D12_RESOURCE_STATES                       InitialState,
        const D3D12_CLEAR_VALUE*                    pOptimizedClearValue,
        REFIID                                      riid,
        void**                                      ppvResource );
    typedef HRESULT( APIENTRY *PFNINTCDX12EXT_CREATEPLACEDRESOURCE_0002 ) (
        ExtensionContext*                           pExtensionContext,
        ID3D12Heap*                                 pHeap,
        UINT64                                      HeapOffset,
        const D3D12_RESOURCE_DESC_0002*             pDesc,
        D3D12_RESOURCE_STATES                       InitialState,
        const D3D12_CLEAR_VALUE*                    pOptimizedClearValue,
        REFIID                                      riid,
        void**                                      ppvResource );

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Creates a resource that is reserved, which is not yet mapped to any pages in a heap. Supported extensions:
    ///        CornerTextelMode - Enables texel values to be specified at integral positions (texel corners) instead of at half-texel offsets (texel centers)
    /// @param pExtensionContext A pointer to the extension context associated with the current Device
    /// @param pDesc A pointer to a overridden D3D12_RESOURCE_DESC structure that describes the resource.
    /// @param InitialState The initial state of the resource, as a bitwise-OR'd combination of D3D12_RESOURCE_STATES enumeration constants.
    /// @param pOptimizedClearValue Specifies a D3D12_CLEAR_VALUE that describes the default value for a clear color.
    /// @param riid The globally unique identifier (GUID) for the resource interface.
    /// @param ppvResource A pointer to a memory block that receives a pointer to the resource.
    /// @returns HRESULT Returns S_OK if it was successful.
    typedef HRESULT( APIENTRY *PFNINTCDX12EXT_CREATERESERVEDRESOURCE ) (
        ExtensionContext*                           pExtensionContext,
        const D3D12_RESOURCE_DESC*                  pDesc,
        D3D12_RESOURCE_STATES                       InitialState,
        const D3D12_CLEAR_VALUE*                    pOptimizedClearValue,
        REFIID                                      riid,
        void**                                      ppvResource );
    typedef HRESULT( APIENTRY *PFNINTCDX12EXT_CREATERESERVEDRESOURCE_0001 ) (
        ExtensionContext*                           pExtensionContext,
        const D3D12_RESOURCE_DESC_0001*             pDesc,
        D3D12_RESOURCE_STATES                       InitialState,
        const D3D12_CLEAR_VALUE*                    pOptimizedClearValue,
        REFIID                                      riid,
        void**                                      ppvResource );
    typedef HRESULT( APIENTRY *PFNINTCDX12EXT_CREATERESERVEDRESOURCE_0002 ) (
        ExtensionContext*                           pExtensionContext,
        const D3D12_RESOURCE_DESC_0002*             pDesc,
        D3D12_RESOURCE_STATES                       InitialState,
        const D3D12_CLEAR_VALUE*                    pOptimizedClearValue,
        REFIID                                      riid,
        void**                                      ppvResource );

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Notifies the driver that it needs to synchronize multiple accesses to resources.
    /// @param pExtensionContext A pointer to the extension context associated with the current Device.
    /// @param handle Describes the CPU descriptor handle that represents the start of the heap that holds the sampler.
    /// @param pDesc A pointer to overridden D3D12_GRAPHICS_PIPELINE_STATE_DESC descriptor.
    /// @returns HRESULT Returns S_OK if it was successful.
    typedef HRESULT( APIENTRY *PFNINTCDX12EXT_CREATESAMPLER ) (
        ExtensionContext*                           pExtensionContext,
        const D3D12_SAMPLER_DESC*                   pDesc,
        D3D12_CPU_DESCRIPTOR_HANDLE                 DestDescriptor );
    typedef HRESULT( APIENTRY *PFNINTCDX12EXT_CREATESAMPLER_0001 ) (
        ExtensionContext*                           pExtensionContext,
        const D3D12_SAMPLER_DESC_0001*              pDesc,
        D3D12_CPU_DESCRIPTOR_HANDLE                 DestDescriptor );

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Notifies the driver that it needs to synchronize multiple accesses to resources.
    /// @param pExtensionContext A pointer to the extension context associated with the current Device.
    /// @param pCommandList A pointer to the ID3D12GraphicsCommandList interface for the command list.
    /// @param NumBarriers The number of submitted barrier descriptions.
    /// @param pBarriers Pointer to an array of barrier descriptions.
    typedef void ( APIENTRY *PFNINTCDX12EXT_RESOURCEBARRIER )(
        ExtensionContext*                           pExtensionContext,
        ID3D12GraphicsCommandList*                  pCommandList,
        UINT                                        NumBarriers,
        const D3D12_RESOURCE_BARRIER*               pBarriers );

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Creates a shader-resource view for accessing data in a resource.
    /// @param pExtensionContext A pointer to the extension context associated with the current Device.
    /// @param pResource A pointer to the ID3D12Resource object that represents the shader resource.
    /// @param pDesc A pointer to a D3D12_SHADER_RESOURCE_VIEW_DESC structure that describes the shader-resource view.
    /// @param DestDescriptor Describes the CPU descriptor handle that represents the shader-resource view. This handle can be created in a shader-visible or non-shader-visible descriptor heap.
    typedef void ( APIENTRY *PFNINTCDX12EXT_CREATESHADERRESOURCEVIEW )(
        ExtensionContext*                           pExtensionContext,
        ID3D12Resource*                             pResource,
        const D3D12_SHADER_RESOURCE_VIEW_DESC*      pDesc,
        D3D12_CPU_DESCRIPTOR_HANDLE                 DestDescriptor );

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Creates a render-target view for accessing resource data.
    /// @param pExtensionContext A pointer to the extension context associated with the current Device.
    /// @param pResource A pointer to the ID3D12Resource object that represents the render target.
    /// @param pDesc A pointer to a D3D12_RENDER_TARGET_VIEW_DESC structure that describes the render-target view.
    /// @param DestDescriptor Describes the CPU descriptor handle that represents the start of the heap that holds the render-target view.
    typedef void ( APIENTRY *PFNINTCDX12EXT_CREATERENDERTARGETVIEW )(
        ExtensionContext*                           pExtensionContext,
        ID3D12Resource*                             pResource,
        const D3D12_RENDER_TARGET_VIEW_DESC*        pDesc,
        D3D12_CPU_DESCRIPTOR_HANDLE                 DestDescriptor );

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Creates a view for unordered accessing.
    /// @param pExtensionContext A pointer to the extension context associated with the current Device.
    /// @param pResource The ID3D12Resource for the counter( if any ) associated with the UAV.
    /// @param pCounterResource A pointer to the ID3D12Resource object that represents the unordered access.
    /// @param pDesc A pointer to a D3D12_UNORDERED_ACCESS_VIEW_DESC structure that describes the unordered-access view.
    /// @param DestDescriptor Describes the CPU descriptor handle that represents the start of the heap that holds the unordered-access view.
    typedef void ( APIENTRY *PFNINTCDX12EXT_CREATEUNORDEREDACCESSVIEW )(
        ExtensionContext*                           pExtensionContext,
        ID3D12Resource*                             pResource,
        ID3D12Resource*                             pCounterResource,
        const D3D12_UNORDERED_ACCESS_VIEW_DESC*     pDesc,
        D3D12_CPU_DESCRIPTOR_HANDLE                 DestDescriptor );


    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Creates a procedural texture view to write-access to a resource data
    /// @param pExtensionContext A pointer to the extension context associated with the current Device.
    /// @param pResource A pointer to the ID3D12Resource object that represents the procedural texture resource.
    /// @param pDesc A pointer to a D3D12_PROCEDURAL_TEXTURE_RESOURCE_VIEW_DESC structure that describes the procedural texture resource view.
    /// @param DestDescriptor Describes the CPU descriptor handle that represents the procedural texture resource view. 
    typedef void ( APIENTRY *PFNINTCDX12EXT_CREATEPROCEDURALTEXTURERESOURCEVIEW )(
        ExtensionContext*                                   pExtensionContext,
        ID3D12Resource*                                     pResource,
        const D3D12_PROCEDURAL_TEXTURE_RESOURCE_VIEW_DESC*  pDesc,
        D3D12_CPU_DESCRIPTOR_HANDLE                         DestDescriptor );


    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief This method uses the GPU to copy texture data between two locations. Both the source and the destination may reference texture data located within either a buffer resource or a texture resource.
    /// @param pExtensionContext A pointer to the extension context associated with the current Device.
    /// @param pCommandList A pointer to the ID3D12GraphicsCommandList interface for the command list.
    /// @param pDst Specifies the destination D3D12_TEXTURE_COPY_LOCATION. The subresource referred to must be in the D3D12_RESOURCE_STATE_COPY_DEST state.
    /// @param DstX The x-coordinate of the upper left corner of the destination region.
    /// @param DstY The y-coordinate of the upper left corner of the destination region. For a 1D subresource, this must be zero.
    /// @param DstZ The z-coordinate of the upper left corner of the destination region. For a 1D or 2D subresource, this must be zero.
    /// @param pSrc Specifies the source D3D12_TEXTURE_COPY_LOCATION. The subresource referred to must be in the D3D12_RESOURCE_STATE_COPY_SOURCE state.
    /// @param pSrcBox Specifies an optional D3D12_BOX that sets the size of the source texture to copy.
    typedef void ( APIENTRY *PFNINTCDX12EXT_COPYTEXTUREREGION )(
        ExtensionContext*                           pExtensionContext,
        ID3D12GraphicsCommandList*                  pCommandList,
        const D3D12_TEXTURE_COPY_LOCATION*          pDst,
        UINT                                        DstX,
        UINT                                        DstY,
        UINT                                        DstZ,
        const D3D12_TEXTURE_COPY_LOCATION*          pSrc,
        const D3D12_BOX*                            pSrcBox );

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Clear a procedural texture view to return its texels to the unshaded state.
    /// @param pExtensionContext A pointer to the extension context associated with the current Device.
    /// @param pCommandList A pointer to the ID3D12GraphicsCommandList interface for the command list.
    /// @param ProceduralTextureView Specifies a D3D12_CPU_DESCRIPTOR_HANDLE structure that describes the CPU descriptor handle that represents the start of the heap for the procedural texture to be cleared.
    /// @param NumRects The number of rectangles in the array that the pRects parameter specifies.
    /// @param pRects An array of D3D12_RECT structures for the rectangles in the procedural texture view to clear.
    typedef void ( APIENTRY *PFNINTCDX12EXT_CLEARPROCEDURALTEXTUREVIEW )(
        ExtensionContext*                           pExtensionContext,
        ID3D12GraphicsCommandList*                  pCommandList,
        D3D12_GPU_DESCRIPTOR_HANDLE                 ProceduralTextureViewGPUDescriptor,
        D3D12_CPU_DESCRIPTOR_HANDLE                 ProceduralTextureViewCPUDescriptor,
        UINT                                        NumRects,
        const D3D12_RECT*                           pRects );

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Query a procedural texture for its shaded/unshaded state over a sub-region (which may be the full resource). 
    /// @param pExtensionContext A pointer to the extension context associated with the current Device.
    /// @param pCommandList A pointer to the ID3D12GraphicsCommandList interface for the command list.
    /// @param pDstBuffer Specifies the destination ID3D12Resource buffer. The destination buffer is filled in with status bits that are tightly packed in memory starting at BufferStartOffsetInBytes. Each status bit encodes whether the corresponding texel block is 0=unshaded or 1=shaded. Status bits are stored in row-major (scanline order).
    /// @param BufferStartOffsetInBytes Specifies a UINT64 offset (in bytes) into the destination resource.
    /// @param pProceduralTextureResource Specifies the source ID3D12Resource procedural texture.
    /// @param pSrcBox D3D12_BOX box used to specify the queried region of the source procedural texture resource.
    /// @param pTexelBlockWidth Receives the texel block width.
    /// @param pTexelBlockHeight Receives the textel block height.
    typedef void ( APIENTRY *PFNINTCDX12EXT_COPYPROCEDURALTEXTURESTATUS )(
        ExtensionContext*                           pExtensionContext,
        ID3D12GraphicsCommandList*                  pCommandList,
        ID3D12Resource*                             pDstBuffer,
        UINT64                                      BufferStartOffsetInBytes,
        ID3D12Resource*                             pProceduralTextureResource,
        const D3D12_BOX*                            pSrcBox,
        UINT*                                       pTexelBlockWidth,
        UINT*                                       pTexelBlockHeight );

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Sets CPU descriptor handles for the procedural textures.
    /// @param pExtensionContext A pointer to the extension context associated with the current Device.
    /// @param pCommandList A pointer to the ID3D12GraphicsCommandList interface for the command list.
    /// @param NumProceduralTextureDescriptors The number of entries in the pProceduralTextureDescriptors array.
    /// @param pProceduralTextureDescriptors Specifies an array of D3D12_CPU_DESCRIPTOR_HANDLE structures that describe the CPU descriptor handles that represents the start of the heap of procedural texture descriptors.
    typedef void ( APIENTRY *PFNINTCDX12EXT_SETPROCEDURALTEXTURES )(
        ExtensionContext*                           pExtensionContext,
        ID3D12GraphicsCommandList*                  pCommandList,
        UINT                                        NumProceduralTextureDescriptors,
        const D3D12_GPU_DESCRIPTOR_HANDLE*          pProceduralTextureGPUDescriptors );

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Creates a texel mask view.
    /// @param pExtensionContext A pointer to the extension context associated with the current Device.
    /// @param pResource A pointer to the ID3D12Resource object that represents the texel mask resource.
    /// @param pDesc A pointer to a D3D12_TEXEL_MASK_VIEW_DESC structure that describes the texel mask view.
    /// @param DestDescriptor Describes the CPU descriptor handle that represents the texel mask view. 
    typedef void ( APIENTRY *PFNINTCDX12EXT_CREATETEXELMASKVIEW )(
        ExtensionContext*                           pExtensionContext,
        ID3D12Resource*                             pResource,
        const D3D12_TEXEL_MASK_VIEW_DESC*           pDesc,
        D3D12_CPU_DESCRIPTOR_HANDLE                 DestDescriptor );

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Returns the driver default texel mask granularity setting.
    /// @param pExtensionContext A pointer to the extension context associated with the current Device.
    typedef D3D12_TEXEL_MASK_GRANULARITY ( APIENTRY *PFNINTCDX12EXT_GETDEFAULTTEXELMASKGRANULARITY )(
        ExtensionContext*                           pExtensionContext );

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Clear a texel mask view to unshaded/shaded state.
    /// @param pExtensionContext A pointer to the extension context associated with the current Device.
    /// @param pCommandList A pointer to the ID3D12GraphicsCommandList interface for the command list.
    /// @param ViewGPUHandleInCurrentHeap Describes the GPU descriptor handle that represents the texel mask view.
    /// @param ViewCPUHandle Describes the CPU descriptor handle that represents the texel mask view.
    /// @param pResource A pointer to the ID3D12Resource object that represents the texel mask resource.
    /// @param Value A boolean value which represents whether the texel mask view is in shaded/unshaded state.
    /// @param NumRegions Number of regions to clear.
    /// @param pRegions Region descriptor.
    typedef void ( APIENTRY *PFNINTCDX12EXT_CLEARTEXELMASKVIEW )(
        ExtensionContext*                           pExtensionContext,
        ID3D12GraphicsCommandList*                  pCommandList,
        D3D12_GPU_DESCRIPTOR_HANDLE                 ViewGPUHandleInCurrentHeap,
        D3D12_CPU_DESCRIPTOR_HANDLE                 ViewCPUHandle,
        ID3D12Resource                              *pResource,
        BOOL                                        Value,
        UINT                                        NumRegions,
        const D3D12_REGION                          *pRegions );

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Sets CPU descriptor handles for the procedural textures.
    /// @param pExtensionContext A pointer to the extension context associated with the current Device.
    /// @param pCommandList A pointer to the ID3D12GraphicsCommandList interface for the command list.
    /// @param pSrcTexelMaskResource Source of the resolve operation.
    /// @param pDstResource Destination of the resolve operation.
    /// @param SrcSubResource Source subresource to resolve.
    /// @param DstSubResource Destination subresource to resolve into.
    /// @param DstGranularity Granularity of the destination, which can be different than that of the source, represented by D3D12_TEXEL_MASK_GRANULARITY.
    typedef void ( APIENTRY *PFNINTCDX12EXT_RESOLVETEXELMASK )(
        ExtensionContext*                           pExtensionContext,
        ID3D12GraphicsCommandList*                  pCommandList,
        ID3D12Resource*                             pSrcTexelMaskResource,
        ID3D12Resource*                             pDstResource,
        UINT                                        SrcSubResource,
        UINT                                        DstSubResource,
        D3D12_TEXEL_MASK_GRANULARITY                DstGranularity );

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Sets CPU descriptor handles for the procedural textures.
    /// @param pExtensionContext A pointer to the extension context associated with the current Device.
    /// @param pCommandList A pointer to the ID3D12GraphicsCommandList interface for the command list.
    /// @param NumProceduralTextureDescriptors The number of entries in the pRootTMVs array.
    /// @param pProceduralTextureDescriptors Specifies an array of D3D12_GPU_DESCRIPTOR_HANDLE structures that represent texel mask view descriptors.
    typedef void ( APIENTRY *PFNINTCDX12EXT_SETROOTTMVS )(
        ExtensionContext*                           pExtensionContext,
        ID3D12GraphicsCommandList*                  pCommandList,
        UINT                                        NumRootTMVs,
        const D3D12_GPU_DESCRIPTOR_HANDLE*          pRootTMVs );

    //////////////////////////////////////////////////////////////////////////
    /// @brief Extensions supported in version 1.0.x
    //////////////////////////////////////////////////////////////////////////
    struct D3D12_EXTENSION_FUNCS_01000000
    {
        PFNINTCDX12EXT_CREATEGRAPHICSPIPELINESTATE_0001     CreateGraphicsPipelineState;            ///< @see ExtensionContext::CreateGraphicsPipelineState
        PFNINTCDX12EXT_CREATECOMPUTEPIPELINESTATE           CreateComputePipelineState;             ///< @see ExtensionContext::CreateComputePipelineState
    };

    struct D3D12_EXTENSION_FUNCS_01000001
    {
        PFNINTCDX12EXT_CREATEGRAPHICSPIPELINESTATE_0001     CreateGraphicsPipelineState;            ///< @see ExtensionContext::CreateGraphicsPipelineState
        PFNINTCDX12EXT_CREATECOMPUTEPIPELINESTATE           CreateComputePipelineState;             ///< @see ExtensionContext::CreateComputePipelineState
        PFNINTCDX12EXT_CREATECOMMANDQUEUE                   CreateCommandQueue;                     ///< @see ExtensionContext::CreateCommandQueue
    };

    //////////////////////////////////////////////////////////////////////////
    /// @brief Extensions supported in version 2.0.x
    //////////////////////////////////////////////////////////////////////////
    struct D3D12_EXTENSION_FUNCS_02000000
    {
        PFNINTCDX12EXT_CREATEGRAPHICSPIPELINESTATE_0002     CreateGraphicsPipelineState;            ///< @see ExtensionContext::CreateGraphicsPipelineState
        PFNINTCDX12EXT_CREATECOMPUTEPIPELINESTATE           CreateComputePipelineState;             ///< @see ExtensionContext::CreateComputePipelineState
        PFNINTCDX12EXT_SETCOARSEPIXELSIZESTATE              SetCoarsePixelSizeState;                ///< @see ExtensionContext::SetCoarsePixelSizeState
        PFNINTCDX12EXT_CREATESAMPLER_0001                   CreateSampler;                          ///< @see ExtensionContext::CreateSampler
    };

    struct D3D12_EXTENSION_FUNCS_02000001
    {
        PFNINTCDX12EXT_CREATEGRAPHICSPIPELINESTATE_0002     CreateGraphicsPipelineState;            ///< @see ExtensionContext::CreateGraphicsPipelineState
        PFNINTCDX12EXT_CREATECOMPUTEPIPELINESTATE           CreateComputePipelineState;             ///< @see ExtensionContext::CreateComputePipelineState
        PFNINTCDX12EXT_CREATECOMMANDQUEUE                   CreateCommandQueue;                     ///< @see ExtensionContext::CreateCommandQueue
        PFNINTCDX12EXT_SETCOARSEPIXELSIZESTATE              SetCoarsePixelSizeState;                ///< @see ExtensionContext::SetCoarsePixelSizeState
        PFNINTCDX12EXT_CREATESAMPLER_0001                   CreateSampler;                          ///< @see ExtensionContext::CreateSampler
    };

    //////////////////////////////////////////////////////////////////////////
    /// @brief Extensions supported in version 3.1.x
    //////////////////////////////////////////////////////////////////////////
    struct D3D12_EXTENSION_FUNCS_03010000
    {
        PFNINTCDX12EXT_CREATEGRAPHICSPIPELINESTATE_0002     CreateGraphicsPipelineState;            ///< @see ExtensionContext::CreateGraphicsPipelineState
        PFNINTCDX12EXT_CREATECOMPUTEPIPELINESTATE           CreateComputePipelineState;             ///< @see ExtensionContext::CreateComputePipelineState
        PFNINTCDX12EXT_SETCOARSEPIXELSIZESTATE              SetCoarsePixelSizeState;                ///< @see ExtensionContext::SetCoarsePixelSizeState
        PFNINTCDX12EXT_CREATESAMPLER                        CreateSampler;                          ///< @see ExtensionContext::CreateSampler
        PFNINTCDX12EXT_CREATECOMMITTEDRESOURCE_0001         CreateCommittedResource;                ///< @see ExtensionContext::CreateCommittedResource
        PFNINTCDX12EXT_CREATEPLACEDRESOURCE_0001            CreatePlacedResource;                   ///< @see ExtensionContext::CreatePlacedResource
        PFNINTCDX12EXT_CREATERESERVEDRESOURCE_0001          CreateReservedResource;                 ///< @see ExtensionContext::CreateReservedResource
    };

    struct D3D12_EXTENSION_FUNCS_03010001
    {
        PFNINTCDX12EXT_CREATEGRAPHICSPIPELINESTATE_0002     CreateGraphicsPipelineState;            ///< @see ExtensionContext::CreateGraphicsPipelineState
        PFNINTCDX12EXT_CREATECOMPUTEPIPELINESTATE           CreateComputePipelineState;             ///< @see ExtensionContext::CreateComputePipelineState
        PFNINTCDX12EXT_CREATECOMMANDQUEUE                   CreateCommandQueue;                     ///< @see ExtensionContext::CreateCommandQueue
        PFNINTCDX12EXT_SETCOARSEPIXELSIZESTATE              SetCoarsePixelSizeState;                ///< @see ExtensionContext::SetCoarsePixelSizeState
        PFNINTCDX12EXT_CREATESAMPLER                        CreateSampler;                          ///< @see ExtensionContext::CreateSampler
        PFNINTCDX12EXT_CREATECOMMITTEDRESOURCE_0001         CreateCommittedResource;                ///< @see ExtensionContext::CreateCommittedResource
        PFNINTCDX12EXT_CREATEPLACEDRESOURCE_0001            CreatePlacedResource;                   ///< @see ExtensionContext::CreatePlacedResource
        PFNINTCDX12EXT_CREATERESERVEDRESOURCE_0001          CreateReservedResource;                 ///< @see ExtensionContext::CreateReservedResource
    };

    //////////////////////////////////////////////////////////////////////////
    /// @brief Extensions supported in version 3.2.x
    //////////////////////////////////////////////////////////////////////////
    struct D3D12_EXTENSION_FUNCS_03020000
    {
        PFNINTCDX12EXT_CREATEGRAPHICSPIPELINESTATE_0002     CreateGraphicsPipelineState;            ///< @see ExtensionContext::CreateGraphicsPipelineState
        PFNINTCDX12EXT_CREATECOMPUTEPIPELINESTATE           CreateComputePipelineState;             ///< @see ExtensionContext::CreateComputePipelineState
        PFNINTCDX12EXT_SETCOARSEPIXELSIZESTATE              SetCoarsePixelSizeState;                ///< @see ExtensionContext::SetCoarsePixelSizeState
        PFNINTCDX12EXT_CREATESAMPLER                        CreateSampler;                          ///< @see ExtensionContext::CreateSampler
        PFNINTCDX12EXT_CREATECOMMITTEDRESOURCE_0001         CreateCommittedResource;                ///< @see ExtensionContext::CreateCommittedResource
        PFNINTCDX12EXT_CREATEPLACEDRESOURCE_0001            CreatePlacedResource;                   ///< @see ExtensionContext::CreatePlacedResource
        PFNINTCDX12EXT_CREATERESERVEDRESOURCE_0001          CreateReservedResource;                 ///< @see ExtensionContext::CreateReservedResource
        PFNINTCDX12EXT_RSSETVIEWPORTS                       RSSetViewports;                         ///< @see ExtensionContext::RSSetViewports
    };

    struct D3D12_EXTENSION_FUNCS_03020001
    {
        PFNINTCDX12EXT_CREATEGRAPHICSPIPELINESTATE_0002     CreateGraphicsPipelineState;            ///< @see ExtensionContext::CreateGraphicsPipelineState
        PFNINTCDX12EXT_CREATECOMPUTEPIPELINESTATE           CreateComputePipelineState;             ///< @see ExtensionContext::CreateComputePipelineState
        PFNINTCDX12EXT_CREATECOMMANDQUEUE                   CreateCommandQueue;                     ///< @see ExtensionContext::CreateCommandQueue
        PFNINTCDX12EXT_SETCOARSEPIXELSIZESTATE              SetCoarsePixelSizeState;                ///< @see ExtensionContext::SetCoarsePixelSizeState
        PFNINTCDX12EXT_CREATESAMPLER                        CreateSampler;                          ///< @see ExtensionContext::CreateSampler
        PFNINTCDX12EXT_CREATECOMMITTEDRESOURCE_0001         CreateCommittedResource;                ///< @see ExtensionContext::CreateCommittedResource
        PFNINTCDX12EXT_CREATEPLACEDRESOURCE_0001            CreatePlacedResource;                   ///< @see ExtensionContext::CreatePlacedResource
        PFNINTCDX12EXT_CREATERESERVEDRESOURCE_0001          CreateReservedResource;                 ///< @see ExtensionContext::CreateReservedResource
        PFNINTCDX12EXT_RSSETVIEWPORTS                       RSSetViewports;                         ///< @see ExtensionContext::RSSetViewports
    };

    //////////////////////////////////////////////////////////////////////////
    /// @brief Extensions supported in version 4.1.x
    //////////////////////////////////////////////////////////////////////////
    struct D3D12_EXTENSION_FUNCS_04010000
    {
        PFNINTCDX12EXT_CREATEGRAPHICSPIPELINESTATE          CreateGraphicsPipelineState;            ///< @see ExtensionContext::CreateGraphicsPipelineState
        PFNINTCDX12EXT_CREATECOMPUTEPIPELINESTATE           CreateComputePipelineState;             ///< @see ExtensionContext::CreateComputePipelineState
        PFNINTCDX12EXT_SETCOARSEPIXELSIZESTATE              SetCoarsePixelSizeState;                ///< @see ExtensionContext::SetCoarsePixelSizeState
        PFNINTCDX12EXT_CREATESAMPLER                        CreateSampler;                          ///< @see ExtensionContext::CreateSampler
        PFNINTCDX12EXT_CREATECOMMITTEDRESOURCE_0002         CreateCommittedResource;                ///< @see ExtensionContext::CreateCommittedResource
        PFNINTCDX12EXT_CREATEPLACEDRESOURCE_0002            CreatePlacedResource;                   ///< @see ExtensionContext::CreatePlacedResource
        PFNINTCDX12EXT_CREATERESERVEDRESOURCE_0002          CreateReservedResource;                 ///< @see ExtensionContext::CreateReservedResource
        PFNINTCDX12EXT_RSSETVIEWPORTS                       RSSetViewports;                         ///< @see ExtensionContext::RSSetViewports
        PFNINTCDX12EXT_RESOURCEBARRIER                      ResourceBarrier;                        ///< @see ExtensionContext::ResourceBarrier
        PFNINTCDX12EXT_CREATESHADERRESOURCEVIEW             CreateShaderResourceView;               ///< @see ExtensionContext::CreateShaderResourceView
        PFNINTCDX12EXT_CREATERENDERTARGETVIEW               CreateRenderTargetView;                 ///< @see ExtensionContext::CreateRenderTargetView
        PFNINTCDX12EXT_CREATEUNORDEREDACCESSVIEW            CreateUnorderedAccessView;              ///< @see ExtensionContext::CreateUnorderedAccessView
        PFNINTCDX12EXT_CREATEPROCEDURALTEXTURERESOURCEVIEW  CreateProceduralTextureResourceView;    ///< @see ExtensionContext::CreateProceduralTextureResourceView
        PFNINTCDX12EXT_COPYTEXTUREREGION                    CopyTextureRegion;                      ///< @see ExtensionContext::CopyTextureRegion
        PFNINTCDX12EXT_CLEARPROCEDURALTEXTUREVIEW           ClearProceduralTextureView;             ///< @see ExtensionContext::ClearProceduralTextureView
        PFNINTCDX12EXT_COPYPROCEDURALTEXTURESTATUS          CopyProceduralTextureStatus;            ///< @see ExtensionContext::CopyProceduralTextureStatus
        PFNINTCDX12EXT_SETPROCEDURALTEXTURES                SetProceduralTextures;                  ///< @see ExtensionContext::SetProceduralTextures
    };

    struct D3D12_EXTENSION_FUNCS_04010001
    {
        PFNINTCDX12EXT_CREATEGRAPHICSPIPELINESTATE          CreateGraphicsPipelineState;            ///< @see ExtensionContext::CreateGraphicsPipelineState
        PFNINTCDX12EXT_CREATECOMPUTEPIPELINESTATE           CreateComputePipelineState;             ///< @see ExtensionContext::CreateComputePipelineState
        PFNINTCDX12EXT_CREATECOMMANDQUEUE                   CreateCommandQueue;                     ///< @see ExtensionContext::CreateCommandQueue
        PFNINTCDX12EXT_SETCOARSEPIXELSIZESTATE              SetCoarsePixelSizeState;                ///< @see ExtensionContext::SetCoarsePixelSizeState
        PFNINTCDX12EXT_CREATESAMPLER                        CreateSampler;                          ///< @see ExtensionContext::CreateSampler
        PFNINTCDX12EXT_CREATECOMMITTEDRESOURCE_0002         CreateCommittedResource;                ///< @see ExtensionContext::CreateCommittedResource
        PFNINTCDX12EXT_CREATEPLACEDRESOURCE_0002            CreatePlacedResource;                   ///< @see ExtensionContext::CreatePlacedResource
        PFNINTCDX12EXT_CREATERESERVEDRESOURCE_0002          CreateReservedResource;                 ///< @see ExtensionContext::CreateReservedResource
        PFNINTCDX12EXT_RSSETVIEWPORTS                       RSSetViewports;                         ///< @see ExtensionContext::RSSetViewports
        PFNINTCDX12EXT_RESOURCEBARRIER                      ResourceBarrier;                        ///< @see ExtensionContext::ResourceBarrier
        PFNINTCDX12EXT_CREATESHADERRESOURCEVIEW             CreateShaderResourceView;               ///< @see ExtensionContext::CreateShaderResourceView
        PFNINTCDX12EXT_CREATERENDERTARGETVIEW               CreateRenderTargetView;                 ///< @see ExtensionContext::CreateRenderTargetView
        PFNINTCDX12EXT_CREATEUNORDEREDACCESSVIEW            CreateUnorderedAccessView;              ///< @see ExtensionContext::CreateUnorderedAccessView
        PFNINTCDX12EXT_CREATEPROCEDURALTEXTURERESOURCEVIEW  CreateProceduralTextureResourceView;    ///< @see ExtensionContext::CreateProceduralTextureResourceView
        PFNINTCDX12EXT_COPYTEXTUREREGION                    CopyTextureRegion;                      ///< @see ExtensionContext::CopyTextureRegion
        PFNINTCDX12EXT_CLEARPROCEDURALTEXTUREVIEW           ClearProceduralTextureView;             ///< @see ExtensionContext::ClearProceduralTextureView
        PFNINTCDX12EXT_COPYPROCEDURALTEXTURESTATUS          CopyProceduralTextureStatus;            ///< @see ExtensionContext::CopyProceduralTextureStatus
        PFNINTCDX12EXT_SETPROCEDURALTEXTURES                SetProceduralTextures;                  ///< @see ExtensionContext::SetProceduralTextures
    };

    struct D3D12_EXTENSION_FUNCS_04010002
    {
        PFNINTCDX12EXT_CREATEGRAPHICSPIPELINESTATE          CreateGraphicsPipelineState;            ///< @see ExtensionContext::CreateGraphicsPipelineState
        PFNINTCDX12EXT_CREATECOMPUTEPIPELINESTATE           CreateComputePipelineState;             ///< @see ExtensionContext::CreateComputePipelineState
        PFNINTCDX12EXT_CREATECOMMANDQUEUE                   CreateCommandQueue;                     ///< @see ExtensionContext::CreateCommandQueue
        PFNINTCDX12EXT_SETCOARSEPIXELSIZESTATE              SetCoarsePixelSizeState;                ///< @see ExtensionContext::SetCoarsePixelSizeState
        PFNINTCDX12EXT_CREATESAMPLER                        CreateSampler;                          ///< @see ExtensionContext::CreateSampler
        PFNINTCDX12EXT_CREATECOMMITTEDRESOURCE              CreateCommittedResource;                ///< @see ExtensionContext::CreateCommittedResource
        PFNINTCDX12EXT_CREATEPLACEDRESOURCE                 CreatePlacedResource;                   ///< @see ExtensionContext::CreatePlacedResource
        PFNINTCDX12EXT_CREATERESERVEDRESOURCE               CreateReservedResource;                 ///< @see ExtensionContext::CreateReservedResource
        PFNINTCDX12EXT_RSSETVIEWPORTS                       RSSetViewports;                         ///< @see ExtensionContext::RSSetViewports
        PFNINTCDX12EXT_RESOURCEBARRIER                      ResourceBarrier;                        ///< @see ExtensionContext::ResourceBarrier
        PFNINTCDX12EXT_CREATESHADERRESOURCEVIEW             CreateShaderResourceView;               ///< @see ExtensionContext::CreateShaderResourceView
        PFNINTCDX12EXT_CREATERENDERTARGETVIEW               CreateRenderTargetView;                 ///< @see ExtensionContext::CreateRenderTargetView
        PFNINTCDX12EXT_CREATEUNORDEREDACCESSVIEW            CreateUnorderedAccessView;              ///< @see ExtensionContext::CreateUnorderedAccessView
        PFNINTCDX12EXT_CREATEPROCEDURALTEXTURERESOURCEVIEW  CreateProceduralTextureResourceView;    ///< @see ExtensionContext::CreateProceduralTextureResourceView
        PFNINTCDX12EXT_COPYTEXTUREREGION                    CopyTextureRegion;                      ///< @see ExtensionContext::CopyTextureRegion
        PFNINTCDX12EXT_CLEARPROCEDURALTEXTUREVIEW           ClearProceduralTextureView;             ///< @see ExtensionContext::ClearProceduralTextureView
        PFNINTCDX12EXT_COPYPROCEDURALTEXTURESTATUS          CopyProceduralTextureStatus;            ///< @see ExtensionContext::CopyProceduralTextureStatus
        PFNINTCDX12EXT_SETPROCEDURALTEXTURES                SetProceduralTextures;                  ///< @see ExtensionContext::SetProceduralTextures
        PFNINTCDX12EXT_CREATETEXELMASKVIEW                  CreateTexelMaskView;                    ///< @see ExtensionContext::CreateTexelMaskView
        PFNINTCDX12EXT_GETDEFAULTTEXELMASKGRANULARITY       GetDefaultTexelMaskGranularity;         ///< @see ExtensionContext::GetDefaultTexelMaskGranularity
        PFNINTCDX12EXT_CLEARTEXELMASKVIEW                   ClearTexelMaskView;                     ///< @see ExtensionContext::ClearTexelMaskView
        PFNINTCDX12EXT_RESOLVETEXELMASK                     ResolveTexelMask;                       ///< @see ExtensionContext::ResolveTexelMask
        PFNINTCDX12EXT_SETROOTTMVS                          SetRootTMVs;                            ///< @see ExtensionContext::SetRootTMVs
    };

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Returns all D3D12 Intel Extensions interface versions supported on a current platform/driver/header file combination.
    ///        It is guaranteed that the application can initialize every extensions interface version returned by this call.
    /// @param pDevice A pointer to the current Device.
    /// @param supportedExtVersionsCount A pointer to the variable that will hold the number of supported versions.
    /// @param pSupportedExtVersions A pointer to the table of supported versions.
    ///        Pointer is null if Init fails.
    /// @returns HRESULT Returns S_OK if it was successful.
    ///                  Returns invalid HRESULT if the call was unsuccessful.
    INTC_EXT_API HRESULT D3D12GetSupportedVersions(
        ID3D12Device*                               pDevice,
        UINT32*                                     supportedExtVersionsCount,
        UINT32*                                     pSupportedExtVersions );

    typedef HRESULT( APIENTRY *PFNINTCDX12EXT_D3D12GETSUPPORTEDVERSIONS ) (
        ID3D12Device*,
        UINT32*,
        UINT32* );

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Creates D3D12 Intel Extensions Device Context and returns ppfnExtensionContext Extension Context object and
    ///        ppfnExtensionFuncs extension function pointers table. This function must be called prior to using extensions.
    /// @param pDevice A pointer to the current Device.
    /// @param ppExtensionContext A pointer to the extension context associated with the current Device.
    /// @param ppfnExtensionFuncs A pointer to the table of extension functions supported by a current driver.
    ///        Pointer is null if Init fails.
    /// @param extensionFuncsSize A size of the ppfnExtensionFuncs structure.
    /// @param pExtensionInfo A pointer to the ExtensionInfo structure that should be filled only with the requestedExtensionVersion member.
    ///        Returns details on Intel Graphics Hardware and negotiated extension API interface version.
    /// @param pExtensionAppInfo A pointer to the ExtensionAppInfo structure that should be passed to the driver identifying application and engine.
    /// @returns HRESULT Returns S_OK if it was successful.
    ///                  Returns E_INVALIDARG if invalid arguments are passed.
    ///                  Returns E_OUTOFMEMORY if extensions are not supported by the driver.
    INTC_EXT_API HRESULT D3D12CreateDeviceExtensionContext(
        ID3D12Device*                               pDevice,
        ExtensionContext**                          ppExtensionContext,
        void**                                      ppfnExtensionFuncs,
        UINT32                                      extensionFuncsSize,
        ExtensionInfo*                              pExtensionInfo,
        ExtensionAppInfo*                           pExtensionAppInfo );

    typedef HRESULT( APIENTRY *PFNINTCDX12EXT_D3D12CREATEDEVICEEXTENSIONCONTEXT ) (
        ID3D12Device*,
        ExtensionContext**,
        void**,
        UINT32,
        ExtensionInfo*,
        ExtensionAppInfo* );

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Destroys D3D12 Intel Extensions Device Context and provides cleanup for the Intel Extensions Framework. 
    ///        No D3D12 extensions can be used after calling this function.
    /// @param ppExtensionContext A pointer to the extension context associated with the current Device.
    /// @returns HRESULT Returns S_OK if it was successful.
    ///                  Returns E_INVALIDARG if invalid arguments are passed.
    INTC_EXT_API HRESULT D3D12DestroyDeviceExtensionContext(
        ExtensionContext**                          ppExtensionContext );

    typedef HRESULT( APIENTRY *PFNINTCDX12EXT_D3D12DESTROYDEVICEEXTENSIONCONTEXT ) (
        ExtensionContext** );

    ////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Extension library loading helper function.
    /// @details 
    ///     Function helps load D3D12 Extensions Framework and return hLibModule handle. 
    ///     If useCurrentProcessDir is set, the function tries to load the library from the current
    ///     process directory first. If that was unsuccessful or useCurrentProcessDir was not set, 
    ///     it tries to find the full path to the Intel DX12 UMD Driver module that must be loaded
    ///     by the current process. Library is loaded from the same path (whether it is DriverStore
    ///     location or system32 folder).
    /// @param useCurrentProcessDir If set, the function tries to load the library from the current
    ///        process directory first.
    ////////////////////////////////////////////////////////////////////////////////////////
    __inline HMODULE D3D12LoadIntelExtensionsLibrary( BOOL useCurrentProcessDir = false )
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
                size_t  pos = szPath.rfind( '\\' );
                if( pos != std::string::npos )
                {
                    szPath.erase( pos );
                    szPath += '\\';
                    szPath += ID3D12_EXT_DLL;
                }
                else
                {
                    // Something is wrong with the current process path, try using library name only
                    szPath = ID3D12_EXT_DLL;
                }

                // Try to load the library
                hLibModule = LoadLibraryExA( szPath.c_str(), NULL, 0 );
                if( hLibModule )
                {
                    return hLibModule;
                }
            }
        }

        // Try to load library from the Intel DX12 UMD Graphics Driver location (most likely DriverStore or system32)
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
                    size_t  pos = szPath.find( ID3D12_UMD_DLL );
                    if( pos != std::string::npos )
                    {
                        szPath.erase( pos );
                        szPath += ID3D12_EXT_DLL;

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
