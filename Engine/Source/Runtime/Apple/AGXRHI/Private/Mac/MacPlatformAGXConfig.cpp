// Copyright Epic Games, Inc. All Rights Reserved.

#include "MacPlatformAGXConfig.h"
#include "RHI.h"
#include "AGXRHIPrivate.h"


//------------------------------------------------------------------------------

#pragma mark - Mac Platform AGXRHI Config Definitions -

#define MAC_PLATFORM_AGXRHI_ADAPTER_DRIVER_ON_DENY_LIST                                 AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_DEVICE_IS_AMD_PRE_GCN_ARCHITECTURE                          AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_DEVICE_IS_INTEGRATED                                        AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_ADAPTER_NAME                                                TEXT("Unknown AGX Adapter")
#define MAC_PLATFORM_AGXRHI_ADAPTER_DRIVER_DATE                                         TEXT("Unknown Metal plugin date")
#define MAC_PLATFORM_AGXRHI_ADAPTER_USER_DRIVER_VERSION                                 TEXT("Unknown Metal plugin version")
#define MAC_PLATFORM_AGXRHI_ADAPTER_INTERNAL_DRIVER_VERSION                             TEXT("1.0.1")
#define MAC_PLATFORM_AGXRHI_DEVICE_ID                                                   2275
#define MAC_PLATFORM_AGXRHI_DEVICE_REVISION                                             1

#define MAC_PLATFORM_AGXRHI_HARDWARE_HIDDEN_SURFACE_REMOVAL                             AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_IS_INITIALIZED                                              AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_ATTACHMENT_VARIABLE_RATE_SHADING_ENABLED                    AGXRHI_TRUE
#define MAC_PLATFORM_AGXRHI_FORCE_NO_DELETION_LATENCY_FOR_STREAMING_TEXTURES            AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_IS_HDR_ENABLED                                              AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_LAZY_SHADER_CODE_LOADING                                    AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_NEEDS_EXTRA_DELETION_LATENCY                                AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_NEEDS_UNATLASED_CSM_DEPTHS_WORKAROUND                       AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_REQUIRES_RENDER_TARGET_FOR_PIXEL_SHADER_UAVS                AGXRHI_TRUE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_ARRAY_INDEX_FROM_ANY_SHADER                        AGXRHI_TRUE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_ASYNC_TEXTURE_CREATION                             AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_ATOMIC_UINT64                                      AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_ATTACHMENT_VARIABLE_RATE_SHADING                   AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_BACKBUFFER_WITH_CUSTOM_DEPTHSTENCIL                AGXRHI_TRUE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_BASE_VERTEX_INDEX                                  AGXRHI_TRUE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_COMPLEX_VARIABLE_RATE_SHADING_COMBINER_OPS         AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_CONSERVATIVE_RASTERIZATION                         AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_COPY_TO_TEXTURE_MULTIPLE_MIPS                      AGXRHI_TRUE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_DEPTH_UAV                                          AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_DIRECT_GPU_MEMORY_LOCK                             AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_DRAW_INDIRECT                                      AGXRHI_TRUE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_DYNAMIC_RESOLUTION                                 AGXRHI_TRUE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_EFFICIENT_UPLOAD_ON_RESOURCE_CREATION              AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_EXACT_OCCLUSION_QUERIES                            AGXRHI_TRUE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_EXPLICIT_FMASK                                     AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_EXPLICIT_HTILE                                     AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_FIRST_INSTANCE                                     AGXRHI_TRUE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_FRAME_CYCLES_BUBBLES_REMOVAL                       AGXRHI_TRUE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_GPU_TIMESTAMP_BUBBLES_REMOVAL                      AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_HDR_OUTPUT                                         AGXRHI_TRUE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_INLINE_RAY_TRACING                                 AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_LATE_VARIABLE_RATE_SHADING_UPDATE                  AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_LAZY_SHADER_CODE_LOADING                           AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_MESH_SHADERS_TIER_0                                AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_MESH_SHADERS_TIER_1                                AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_MSAA_DEPTH_SAMPLE_ACCESS                           AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_MULTITHREADED_SHADER_CREATION                      AGXRHI_TRUE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_MULTITHREADING                                     AGXRHI_TRUE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_PARALLEL_RHI_EXECUTE                               AGXRHI_TRUE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_PIPELINE_FILE_CACHE                                AGXRHI_TRUE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_PIPELINE_STATE_SORT_KEY                            AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_PIPELINE_VARIABLE_RATE_SHADING                     AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_PIXEL_SHADER_UAVS                                  AGXRHI_TRUE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_PRIMITIVE_SHADERS                                  AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_QUAD_TOPOLOGY                                      AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_RAY_TRACING                                        AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_RAY_TRACING_AMD_HIT_TOKEN                          AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_RAY_TRACING_ASYNC_BUILD_ACCELERATION_STRUCTURE     AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_RAY_TRACING_DISPATCH_INDIRECT                      AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_RAY_TRACING_PSO_ADDITIONS                          AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_RAY_TRACING_SHADERS                                AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_RECT_TOPOLOGY                                      AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_RESOLVE_CUBEMAP_FACES                              AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_RESUMMARIZE_HTILE                                  AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_RHI_ON_TASK_THREAD                                 AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_RHI_THREAD                                         AGXRHI_TRUE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_RWTEXTURE_BUFFERS                                  AGXRHI_TRUE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_SHADER_TIMESTAMP                                   AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_STENCIL_REF_FROM_PIXEL_SHADER                      AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_TEXTURE_STREAMING                                  AGXRHI_TRUE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_UAV_FORMAT_ALIASING                                AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_UPDATE_FROM_BUFFER_TEXTURE                         AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_VARIABLE_RATE_SHADING_ATTACHMENT_ARRAY_TEXTURES    AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_WAVE_OPERATIONS                                    AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_THREAD_NEEDS_KICKING                                        AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_VARIABLE_RATE_SHADING_ENABLED                               AGXRHI_TRUE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_DEPTH_BOUNDS_TEST                                  AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_EFFICIENT_ASYNC_COMPUTE                            AGXRHI_TRUE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_IMAGE_EXTERNAL                                     AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_MOBILE_MULTIVIEW                                   AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_PARALLEL_OCCLUSION_QUERIES                         AGXRHI_TRUE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_PARALLEL_RENDERING_TASKS_WITH_SEPARATE_RHI_THREAD  AGXRHI_TRUE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_PIXEL_LOCAL_STORAGE                                AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_QUADBUFFER_STEREO                                  AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_RENDER_DEPTH_TARGETABLE_SHADER_RESOURCES           AGXRHI_TRUE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_RENDER_TARGET_FORMAT_PF_FLOAT_RGBA                 AGXRHI_TRUE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_RENDER_TARGET_FORMAT_PF_G8                         AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_SEPARATE_RENDER_TARGET_BLEND_STATE                 AGXRHI_FALSE // (GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5)
#define MAC_PLATFORM_AGXRHI_SUPPORTS_SHADER_DEPTHSTENCIL_FETCH                          AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_SHADER_FRAMEBUFFER_FETCH                           AGXRHI_TRUE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_SHADER_MRT_FRAMEBUFFER_FETCH                       AGXFHI_TRUE // GSupportsShaderFramebufferFetch
#define MAC_PLATFORM_AGXRHI_SUPPORTS_TEXTURE_3D                                         AGXRHI_TRUE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_TIMESTAMP_RENDER_QUERIES                           AGXRHI_TRUE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_TRANSIENT_RESOURCE_ALIASING                        AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_VOLUME_TEXTURE_RENDERING                           AGXRHI_TRUE
#define MAC_PLATFORM_AGXRHI_SUPPORTS_WIDE_MRT                                           AGXRHI_TRUE
#define MAC_PLATFORM_AGXRHI_USING_NULL_RHI                                              AGXRHI_FALSE
#define MAC_PLATFORM_AGXRHI_HDR_DISPLAY_OUTPUT_FORMAT                                   PF_Unknown
#define MAC_PLATFORM_AGXRHI_VARIABLE_RATE_SHADING_IMAGE_FORMAT                          PF_Unknown
#define MAC_PLATFORM_AGXRHI_MERGEABLE_ACCESS_MASK                                       (ERHIAccess::ReadOnlyMask | ERHIAccess::UAVMask)
#define MAC_PLATFORM_AGXRHI_MULTI_PIPELINE_MERGEABLE_ACCESS_MASK                        ERHIAccess::Unknown
#define MAC_PLATFORM_AGXRHI_MAX_FEATURE_LEVEL                                           ERHIFeatureLevel::SM5
#define MAC_PLATFORM_AGXRHI_MAX_SHADER_PLATFORM                                         SP_METAL_SM5
#define MAC_PLATFORM_AGXRHI_SHADER_PLATFORM_FOR_FEATURE_LEVEL                           { SP_NumPlatforms }
#define MAC_PLATFORM_AGXRHI_VARIABLE_RATE_SHADING_IMAGE_DATA_TYPE                       VRSImage_NotSupported
#define MAC_PLATFORM_AGXRHI_MAX_DISPATCH_THREAD_GROUPS_PER_DIMENSION                    { MAX_uint16, MAX_uint16, MAX_uint16 }
#define MAC_PLATFORM_AGXRHI_MAX_COMPUTE_DISPATCH_DIMENSION                              MAX_uint16
#define MAC_PLATFORM_AGXRHI_MAX_CUBE_TEXTURE_DIMENSIONS                                 16384
#define MAC_PLATFORM_AGXRHI_MAX_SHADOW_DEPTH_BUFFER_SIZE_X                              GMaxTextureDimensions
#define MAC_PLATFORM_AGXRHI_MAX_SHADOW_DEPTH_BUFFER_SIZE_Y                              GMaxTextureDimensions
#define MAC_PLATFORM_AGXRHI_MAX_TEXTURE_ARRAY_LAYERS                                    2048
#define MAC_PLATFORM_AGXRHI_MAX_TEXTURE_DIMENSIONS                                      16384
#define MAC_PLATFORM_AGXRHI_MAX_TEXTURE_MIP_COUNT                                       0
#define MAC_PLATFORM_AGXRHI_MAX_TEXTURE_SAMPLERS                                        16
#define MAC_PLATFORM_AGXRHI_MAX_VOLUME_TEXTURE_DIMENSIONS                               2048
#define MAC_PLATFORM_AGXRHI_MAX_WORKGROUP_INVOCATIONS                                   1024
#define MAC_PLATFORM_AGXRHI_POOL_SIZE_VRAM_PERCENTAGE                                   0
#define MAC_PLATFORM_AGXRHI_MAXIMUM_RECCOMMENDED_OUSTANDING_OCCLUSION_QUERIES           MAX_int32
#define MAC_PLATFORM_AGXRHI_MAXIMUM_WAVE_SIZE                                           0
#define MAC_PLATFORM_AGXRHI_MINIMUM_WAVE_SIZE                                           0
#define MAC_PLATFORM_AGXRHI_PERSISTENT_THREADGROUP_COUNT                                0
#define MAC_PLATFORM_AGXRHI_VARIABLE_RATE_SHADING_IMAGE_TILE_MAX_HEIGHT                 0
#define MAC_PLATFORM_AGXRHI_VARIABLE_RATE_SHADING_IMAGE_TILE_MAX_WIDTH                  0
#define MAC_PLATFORM_AGXRHI_VARIABLE_RATE_SHADING_IMAGE_TILE_MIN_HEIGHT                 0
#define MAC_PLATFORM_AGXRHI_VARIABLE_RATE_SHADING_IMAGE_TILE_MIN_WIDTH                  0
#define MAC_PLATFORM_AGXRHI_MAX_BUFFER_DIMENSIONS                                       (1 << 27)
#define MAC_PLATFORM_AGXRHI_MAX_COMPUTE_SHARED_MEMORY                                   32768
#define MAC_PLATFORM_AGXRHI_TEXTURE_POOL_SIZE                                           0
#define MAC_PLATFORM_AGXRHI_RAY_TRACING_ACCELERATION_STRUCTURE_ALIGNMENT                0
#define MAC_PLATFORM_AGXRHI_RAY_TRACING_INSTANCE_DESCRIPTOR_SIZE                        0
#define MAC_PLATFORM_AGXRHI_RAY_TRACING_SCRATCH_BUFFER_ALIGNMENT                        0
#define MAC_PLATFORM_AGXRHI_RAY_TRACING_SHADER_TABLE_ALIGNMENT                          0
#define MAC_PLATFORM_AGXRHI_VENDOR_ID                                                   0x106B
#define MAC_PLATFORM_AGXRHI_DEMOTED_LOCAL_MEMORY_SIZE                                   0
#define MAC_PLATFORM_AGXRHI_PRESENT_COUNTER                                             0
#define MAC_PLATFORM_AGXRHI_TRANSITION_PRIVATE_DATA_ALIGN_IN_BYTES                      alignof(FAGXTransitionData)
#define MAC_PLATFORM_AGXRHI_TRANSITION_PRIVATE_DATA_SIZE_IN_BYTES                       sizeof(FAGXTransitionData)


//------------------------------------------------------------------------------

#pragma mark - AGXRHI Internal Utility Routines -

/**
 * @fn AGXUtilCreateCString
 * 
 * @brief
 * This routine extracts a standard C string from a Foundation container.
 * 
 * @details
 * The caller is responsible for releasing resources allocated by this routine
 * by calling AGXUtilReleaseCString() on the returned pointer.
 * 
 * @param InStringRef
 * A Foundation string container object.
 * 
 * @return
 * A null-terminated C string upon success, or NULL if an error occurred during
 * the extraction.
 * 
 * @see
 * AGXUtilReleaseCString
 */
static const ANSICHAR* AGXUtilCreateCString(CFStringRef InStringRef)
{
	CFIndex   BufferLength = CFStringGetLength(InStringRef) + 1;
	ANSICHAR *Buffer       = new ANSICHAR[BufferLength] { '\0' };

	if (!CFStringGetCString(InStringRef, Buffer, BufferLength, kCFStringEncodingUTF8))
	{
		delete [] Buffer;
		Buffer = nullptr;
	}

	return Buffer;
}

/**
 * @fn AGXUtilReleaseCString
 * 
 * @brief
 * This routine releases resources allocated by AGXUtilCreateCString.
 * 
 * @details
 * Any call to AGXUtilCreateCString must be balanced with a call to this
 * routine.
 * 
 * @param InPointer
 * A pointer returned from AGXUtilCreateCString. It is valid to pass a NULL
 * pointer to this routine.
 * 
 * @see
 * AGXUtilCreateCString
 */
static void AGXUtilReleaseCString(const ANSICHAR *InPointer)
{
	if (InPointer != nullptr)
	{
		delete [] InPointer;
	}
}

static CFDateRef AGXUtilCopyLastModifiedTimeForFile(CFStringRef Path)
{
    CFDateRef FileLastModifiedDate = NULL;

    @autoreleasepool {
        NSError *Error = nil;

        NSDictionary *Attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:(__bridge NSString*)Path error:&Error];
        if (Attrs && !Error)
        {
            FileLastModifiedDate = (__bridge CFDateRef)[[Attrs fileModificationDate] retain];
        }
    } // autoreleasepool

    return FileLastModifiedDate;
}


//------------------------------------------------------------------------------

#pragma mark - Mac Platform AGXRHI Config Support Routines -

/**
 * @fn MacAGXConfig_PopulateAdapterInfo_GetVendorID
 * 
 * @brief
 * This routine populates the RHI global vendor identifier.
 * 
 * @param InServiceDictionary
 * The dictionary of properties defined by the underlying IOService.
 * 
 */
static void MacAGXConfig_PopulateAdapterInfo_GetVendorID(CFMutableDictionaryRef InServiceDictionary)
{
	CFStringRef Key = CFStringCreateWithCString(kCFAllocatorDefault, "vendor-id", kCFStringEncodingUTF8);

	CFTypeRef Value = CFDictionaryGetValue(InServiceDictionary, Key);
	if ((Value != nullptr) && (CFGetTypeID(Value) == CFDataGetTypeID()))
	{
		const UInt32 *U32Value = (const UInt32*)CFDataGetBytePtr((CFDataRef)Value);
		GRHIVendorId = *U32Value;
	}
	else
	{
		// The vendor-id cannot be zero or the RHI will assert. Set it to something
		// non-zero for testing purposes. Note this path will only be taken on non-
		// Apple GPUs, and will eventually be removed.
		GRHIVendorId = 1; // Cannot be zero. 
	}

	CFRelease(Key);
}

/**
 * @fn MacAGXConfig_PopulateAdapterInfo_GetAdapterName
 * 
 * @brief
 * This routine populates the RHI global for the adapter name.
 * 
 * @param InServiceDictionary
 * The dictionary of properties defined by the underlying IOService.
 */
static void MacAGXConfig_PopulateAdapterInfo_GetAdapterName(CFMutableDictionaryRef InServiceDictionary)
{
	CFStringRef Key = CFStringCreateWithCString(kCFAllocatorDefault, "model", kCFStringEncodingUTF8);

	CFTypeRef Value = CFDictionaryGetValue(InServiceDictionary, Key);
	if ((Value != nullptr) && (CFGetTypeID(Value) == CFStringGetTypeID()))
	{
		CFStringRef     ValueAsString = (CFStringRef)Value;
		const ANSICHAR* Model         = AGXUtilCreateCString(ValueAsString);

		if (Model)
		{
			GRHIAdapterName = FString::Printf(TEXT("%s (%s)"), UTF8_TO_TCHAR([[AGXUtilGetDevice() name] UTF8String] /*AGXUtilGetDevice()->name()->utf8String()*/), ANSI_TO_TCHAR(Model));
		}
		else
		{
			GRHIAdapterName = UTF8_TO_TCHAR([[AGXUtilGetDevice() name] UTF8String] /*AGXUtilGetDevice()->name()->utf8String()*/);
		}

		AGXUtilReleaseCString(Model);
	}
	else
	{
		GRHIAdapterName = MAC_PLATFORM_AGXRHI_ADAPTER_NAME;
	}

	CFRelease(Key);
}

/**
 * @fn MacAGXConfig_PopulateAdapterInfo_GetAdapterDriverInfo_GetAdapterDriverDate
 * 
 * @brief
 * This routine is called from MacAGXConfig_PopulateAdapterInfo_GetAdapterDriverInfo
 * to populate the RHI global for the adapter driver date.
 * 
 * @param InBundleRef
 * The Foundation bundle reference object representing the user-space Metal
 * plugin.
 */
static void MacAGXConfig_PopulateAdapterInfo_GetAdapterDriverInfo_GetAdapterDriverDate(CFBundleRef InBundleRef)
{
    CFURLRef            BundleURL     = CFBundleCopyBundleURL(InBundleRef);
    CFStringRef         BundlePath    = CFURLCopyFileSystemPath(BundleURL, kCFURLPOSIXPathStyle);
    CFDateRef           BundleDate    = AGXUtilCopyLastModifiedTimeForFile(BundlePath);
    CFDateFormatterRef  DateFormatter = CFDateFormatterCreateISO8601Formatter(kCFAllocatorDefault, kCFISO8601DateFormatWithDashSeparatorInDate | kCFISO8601DateFormatWithFullDate | kCFISO8601DateFormatWithFullTime);
    CFStringRef         DateString    = CFDateFormatterCreateStringWithDate(kCFAllocatorDefault, DateFormatter, BundleDate);
    const ANSICHAR     *DateCString   = AGXUtilCreateCString(DateString);
    
    if (DateCString != nullptr)
    {
		GRHIAdapterDriverDate = ANSI_TO_TCHAR(DateCString);
    }
    else
    {
        GRHIAdapterDriverDate = MAC_PLATFORM_AGXRHI_ADAPTER_DRIVER_DATE;
    }
    
    AGXUtilReleaseCString(DateCString);

    CFRelease(DateString);
    CFRelease(DateFormatter);
    CFRelease(BundleDate);
    CFRelease(BundlePath);
    CFRelease(BundleURL);
}

/**
 * @fn MacAGXConfig_PopulateAdapterInfo_GetAdapterDriverInfo_GetAdapterUserDriverVersion
 * 
 * @brief
 * This routine is called from MacAGXConfig_PopulateAdapterInfo_GetAdapterDriverInfo
 * to populate the RHI global for the adapter user-space driver version.
 * 
 * @param InBundleRef
 * The Foundation bundle reference object representing the user-space Metal
 * plugin.
 */
static void MacAGXConfig_PopulateAdapterInfo_GetAdapterDriverInfo_GetAdapterUserDriverVersion(CFBundleRef InBundleRef)
{
    CFStringRef     Version     = (CFStringRef)CFBundleGetValueForInfoDictionaryKey(InBundleRef, CFSTR("CFBundleShortVersionString"));
    const ANSICHAR *VersionCStr = AGXUtilCreateCString(Version);

    if (VersionCStr != nullptr)
    {
        GRHIAdapterUserDriverVersion = ANSI_TO_TCHAR(VersionCStr);
    }
    else
    {
        GRHIAdapterUserDriverVersion = MAC_PLATFORM_AGXRHI_ADAPTER_USER_DRIVER_VERSION;
    }

    AGXUtilReleaseCString(VersionCStr);
}

/**
 * @fn MacAGXConfig_PopulateAdapterInfo_GetAdapterDriverInfo
 * 
 * @brief
 * This routine populates the RHI global for the adapter driver date and user
 * driver version.
 * 
 * @details
 * The adapter driver date and user driver version are derived from the Metal
 * plugin bundle. This routine does the bundle discovery and calls supporting
 * routines to fill in the respective RHI globals.
 * 
 * @param InServiceDictionary
 * The dictionary of properties defined by the underlying IOService.
 * 
 * @see
 * MacAGXConfig_PopulateAdapterInfo_GetAdapterDriverInfo_GetAdapterDriverDate
 * MacAGXConfig_PopulateAdapterInfo_GetAdapterDriverInfo_GetAdapterUserDriverVersion
 */
static void MacAGXConfig_PopulateAdapterInfo_GetAdapterDriverInfo(CFMutableDictionaryRef InServiceDictionary)
{
    CFStringRef Key = CFStringCreateWithCString(kCFAllocatorDefault, "MetalPluginName", kCFStringEncodingUTF8);

    CFTypeRef Value = CFDictionaryGetValue(InServiceDictionary, Key);
    if ((Value != nullptr) && (CFGetTypeID(Value) == CFStringGetTypeID()))
    {
        CFStringRef ValueAsString = (CFStringRef)Value;

        CFStringRef BundleID  = CFStringCreateWithFormat(kCFAllocatorDefault, NULL, CFSTR("com.apple.%@"), ValueAsString);
        CFBundleRef BundleRef = CFBundleGetBundleWithIdentifier(BundleID);

        MacAGXConfig_PopulateAdapterInfo_GetAdapterDriverInfo_GetAdapterDriverDate(BundleRef);
        MacAGXConfig_PopulateAdapterInfo_GetAdapterDriverInfo_GetAdapterUserDriverVersion(BundleRef);

        CFRelease(BundleID);
    }

    CFRelease(Key);
}

/**
 * @fn MacAGXConfig_PopulateAdapterInfo
 * 
 * @brief
 * This routine populates the adapter and device related RHI globals.
 * 
 * @details
 * Not all of the RHI globals pertaining to adapter information are relevant
 * for the Mac. For these, the Mac platform AGXRHI simply fills in known
 * placeholder values.
 *   - GRHIAdapterInternalDriverVersion
 *   - GRHIDeviceId
 *   - GRHIDeviceRevision
 */
static void MacAGXConfig_PopulateAdapterInfo()
{
	GRHIAdapterDriverOnDenyList              = MAC_PLATFORM_AGXRHI_ADAPTER_DRIVER_ON_DENY_LIST;
	GRHIAdapterInternalDriverVersion         = MAC_PLATFORM_AGXRHI_ADAPTER_INTERNAL_DRIVER_VERSION;
	GRHIDeviceId                             = MAC_PLATFORM_AGXRHI_DEVICE_ID;
	GRHIDeviceRevision                       = MAC_PLATFORM_AGXRHI_DEVICE_REVISION;
	GRHIDeviceIsAMDPreGCNArchitecture        = MAC_PLATFORM_AGXRHI_DEVICE_IS_AMD_PRE_GCN_ARCHITECTURE;
	GRHIDeviceIsIntegrated                   = MAC_PLATFORM_AGXRHI_DEVICE_IS_INTEGRATED;

	CFMutableDictionaryRef Matching          = IORegistryEntryIDMatching([AGXUtilGetDevice() registryID] /*AGXUtilGetDevice()->registryID()*/);
	io_service_t           Service           = IOServiceGetMatchingService(kIOMasterPortDefault, Matching);
	CFMutableDictionaryRef ServiceDictionary = nullptr;

	if (IORegistryEntryCreateCFProperties(Service, &ServiceDictionary, kCFAllocatorDefault, kNilOptions) == kIOReturnSuccess)
	{
		MacAGXConfig_PopulateAdapterInfo_GetVendorID(ServiceDictionary);
		MacAGXConfig_PopulateAdapterInfo_GetAdapterName(ServiceDictionary);
		MacAGXConfig_PopulateAdapterInfo_GetAdapterDriverInfo(ServiceDictionary);

		CFRelease(ServiceDictionary);
	}

	IOObjectRelease(Service);
}

static void MacAGXConfig_PopulateFeaturesInfo()
{
    //
}

static void MacAGXConfig_PopulateLimitsInfo()
{
    //
}

static void MacAGXConfig_PopulatePixelFormatsInfo()
{
    //
}


//------------------------------------------------------------------------------

#pragma mark - Mac Platform AGXRHI Config Routines -

void FMacAGXConfig::PopulateRHIGlobals()
{
    MacAGXConfig_PopulateAdapterInfo();
    MacAGXConfig_PopulateFeaturesInfo();
    MacAGXConfig_PopulateLimitsInfo();
    MacAGXConfig_PopulatePixelFormatsInfo();
}

/*
GHardwareHiddenSurfaceRemoval                          = MAC_PLATFORM_AGXRHI_HARDWARE_HIDDEN_SURFACE_REMOVAL;
GIsRHIInitialized                                      = MAC_PLATFORM_AGXRHI_IS_INITIALIZED;
GRHIAttachmentVariableRateShadingEnabled               = MAC_PLATFORM_AGXRHI_ATTACHMENT_VARIABLE_RATE_SHADING_ENABLED;
GRHIForceNoDeletionLatencyForStreamingTextures         = MAC_PLATFORM_AGXRHI_FORCE_NO_DELETION_LATENCY_FOR_STREAMING_TEXTURES;
GRHIIsHDREnabled                                       = MAC_PLATFORM_AGXRHI_IS_HDR_ENABLED;
GRHILazyShaderCodeLoading                              = MAC_PLATFORM_AGXRHI_LAZY_SHADER_CODE_LOADING;
GRHINeedsExtraDeletionLatency                          = MAC_PLATFORM_AGXRHI_NEEDS_EXTRA_DELETION_LATENCY;
GRHINeedsUnatlasedCSMDepthsWorkaround                  = MAC_PLATFORM_AGXRHI_NEEDS_UNATLASED_CSM_DEPTHS_WORKAROUND;
GRHIRequiresRenderTargetForPixelShaderUAVs             = MAC_PLATFORM_AGXRHI_REQUIRES_RENDER_TARGET_FOR_PIXEL_SHADER_UAVS;
GRHISupportsArrayIndexFromAnyShader                    = MAC_PLATFORM_AGXRHI_SUPPORTS_ARRAY_INDEX_FROM_ANY_SHADER;
GRHISupportsAsyncTextureCreation                       = MAC_PLATFORM_AGXRHI_SUPPORTS_ASYNC_TEXTURE_CREATION;
GRHISupportsAtomicUInt64                               = MAC_PLATFORM_AGXRHI_SUPPORTS_ATOMIC_UINT64;
GRHISupportsAttachmentVariableRateShading              = MAC_PLATFORM_AGXRHI_SUPPORTS_ATTACHMENT_VARIABLE_RATE_SHADING;
GRHISupportsBackBufferWithCustomDepthStencil           = MAC_PLATFORM_AGXRHI_SUPPORTS_BACKBUFFER_WITH_CUSTOM_DEPTHSTENCIL;
GRHISupportsBaseVertexIndex                            = MAC_PLATFORM_AGXRHI_SUPPORTS_BASE_VERTEX_INDEX;
GRHISupportsComplexVariableRateShadingCombinerOps      = MAC_PLATFORM_AGXRHI_SUPPORTS_COMPLEX_VARIABLE_RATE_SHADING_COMBINER_OPS;
GRHISupportsConservativeRasterization                  = MAC_PLATFORM_AGXRHI_SUPPORTS_CONSERVATIVE_RASTERIZATION;
GRHISupportsCopyToTextureMultipleMips                  = MAC_PLATFORM_AGXRHI_SUPPORTS_COPY_TO_TEXTURE_MULTIPLE_MIPS;
GRHISupportsDepthUAV                                   = MAC_PLATFORM_AGXRHI_SUPPORTS_DEPTH_UAV;
GRHISupportsDirectGPUMemoryLock                        = MAC_PLATFORM_AGXRHI_SUPPORTS_DIRECT_GPU_MEMORY_LOCK;
GRHISupportsDrawIndirect                               = MAC_PLATFORM_AGXRHI_SUPPORTS_DRAW_INDIRECT;
GRHISupportsDynamicResolution                          = MAC_PLATFORM_AGXRHI_SUPPORTS_DYNAMIC_RESOLUTION;
GRHISupportsEfficientUploadOnResourceCreation          = MAC_PLATFORM_AGXRHI_SUPPORTS_EFFICIENT_UPLOAD_ON_RESOURCE_CREATION;
GRHISupportsExactOcclusionQueries                      = MAC_PLATFORM_AGXRHI_SUPPORTS_EXACT_OCCLUSION_QUERIES;
GRHISupportsExplicitFMask                              = MAC_PLATFORM_AGXRHI_SUPPORTS_EXPLICIT_FMASK;
GRHISupportsExplicitHTile                              = MAC_PLATFORM_AGXRHI_SUPPORTS_EXPLICIT_HTILE;
GRHISupportsFirstInstance                              = MAC_PLATFORM_AGXRHI_SUPPORTS_FIRST_INSTANCE;
GRHISupportsFrameCyclesBubblesRemoval                  = MAC_PLATFORM_AGXRHI_SUPPORTS_FRAME_CYCLES_BUBBLES_REMOVAL;
GRHISupportsGPUTimestampBubblesRemoval                 = MAC_PLATFORM_AGXRHI_SUPPORTS_GPU_TIMESTAMP_BUBBLES_REMOVAL;
GRHISupportsHDROutput                                  = MAC_PLATFORM_AGXRHI_SUPPORTS_HDR_OUTPUT;
GRHISupportsInlineRayTracing                           = MAC_PLATFORM_AGXRHI_SUPPORTS_INLINE_RAY_TRACING;
GRHISupportsLateVariableRateShadingUpdate              = MAC_PLATFORM_AGXRHI_SUPPORTS_LATE_VARIABLE_RATE_SHADING_UPDATE;
GRHISupportsLazyShaderCodeLoading                      = MAC_PLATFORM_AGXRHI_SUPPORTS_LAZY_SHADER_CODE_LOADING;
GRHISupportsMeshShadersTier0                           = MAC_PLATFORM_AGXRHI_SUPPORTS_MESH_SHADERS_TIER_0;
GRHISupportsMeshShadersTier1                           = MAC_PLATFORM_AGXRHI_SUPPORTS_MESH_SHADERS_TIER_1;
GRHISupportsMSAADepthSampleAccess                      = MAC_PLATFORM_AGXRHI_SUPPORTS_MSAA_DEPTH_SAMPLE_ACCESS;
GRHISupportsMultithreadedShaderCreation                = MAC_PLATFORM_AGXRHI_SUPPORTS_MULTITHREADED_SHADER_CREATION;
GRHISupportsMultithreading                             = MAC_PLATFORM_AGXRHI_SUPPORTS_MULTITHREADING;
GRHISupportsParallelRHIExecute                         = MAC_PLATFORM_AGXRHI_SUPPORTS_PARALLEL_RHI_EXECUTE;
GRHISupportsPipelineFileCache                          = MAC_PLATFORM_AGXRHI_SUPPORTS_PIPELINE_FILE_CACHE;
GRHISupportsPipelineStateSortKey                       = MAC_PLATFORM_AGXRHI_SUPPORTS_PIPELINE_STATE_SORT_KEY;
GRHISupportsPipelineVariableRateShading                = MAC_PLATFORM_AGXRHI_SUPPORTS_PIPELINE_VARIABLE_RATE_SHADING;
GRHISupportsPixelShaderUAVs                            = MAC_PLATFORM_AGXRHI_SUPPORTS_PIXEL_SHADER_UAVS;
GRHISupportsPrimitiveShaders                           = MAC_PLATFORM_AGXRHI_SUPPORTS_PRIMITIVE_SHADERS;
GRHISupportsQuadTopology                               = MAC_PLATFORM_AGXRHI_SUPPORTS_QUAD_TOPOLOGY;
GRHISupportsRayTracing                                 = MAC_PLATFORM_AGXRHI_SUPPORTS_RAY_TRACING;
GRHISupportsRayTracingAMDHitToken                      = MAC_PLATFORM_AGXRHI_SUPPORTS_RAY_TRACING_AMD_HIT_TOKEN;
GRHISupportsRayTracingAsyncBuildAccelerationStructure  = MAC_PLATFORM_AGXRHI_SUPPORTS_RAY_TRACING_ASYNC_BUILD_ACCELERATION_STRUCTURE;
GRHISupportsRayTracingDispatchIndirect                 = MAC_PLATFORM_AGXRHI_SUPPORTS_RAY_TRACING_DISPATCH_INDIRECT;
GRHISupportsRayTracingPSOAdditions                     = MAC_PLATFORM_AGXRHI_SUPPORTS_RAY_TRACING_PSO_ADDITIONS;
GRHISupportsRayTracingShaders                          = MAC_PLATFORM_AGXRHI_SUPPORTS_RAY_TRACING_SHADERS;
GRHISupportsRectTopology                               = MAC_PLATFORM_AGXRHI_SUPPORTS_RECT_TOPOLOGY;
GRHISupportsResolveCubemapFaces                        = MAC_PLATFORM_AGXRHI_SUPPORTS_RESOLVE_CUBEMAP_FACES;
GRHISupportsResummarizeHTile                           = MAC_PLATFORM_AGXRHI_SUPPORTS_RESUMMARIZE_HTILE;
GRHISupportsRHIOnTaskThread                            = MAC_PLATFORM_AGXRHI_SUPPORTS_RHI_ON_TASK_THREAD;
GRHISupportsRHIThread                                  = MAC_PLATFORM_AGXRHI_SUPPORTS_RHI_THREAD;
GRHISupportsRWTextureBuffers                           = MAC_PLATFORM_AGXRHI_SUPPORTS_RWTEXTURE_BUFFERS;
GRHISupportsShaderTimestamp                            = MAC_PLATFORM_AGXRHI_SUPPORTS_SHADER_TIMESTAMP;
GRHISupportsStencilRefFromPixelShader                  = MAC_PLATFORM_AGXRHI_SUPPORTS_STENCIL_REF_FROM_PIXEL_SHADER;
GRHISupportsTextureStreaming                           = MAC_PLATFORM_AGXRHI_SUPPORTS_TEXTURE_STREAMING;
GRHISupportsUAVFormatAliasing                          = MAC_PLATFORM_AGXRHI_SUPPORTS_UAV_FORMAT_ALIASING;
GRHISupportsUpdateFromBufferTexture                    = MAC_PLATFORM_AGXRHI_SUPPORTS_UPDATE_FROM_BUFFER_TEXTURE;
GRHISupportsVariableRateShadingAttachmentArrayTextures = MAC_PLATFORM_AGXRHI_SUPPORTS_VARIABLE_RATE_SHADING_ATTACHMENT_ARRAY_TEXTURES;
GRHISupportsWaveOperations                             = MAC_PLATFORM_AGXRHI_SUPPORTS_WAVE_OPERATIONS;
GRHIThreadNeedsKicking                                 = MAC_PLATFORM_AGXRHI_THREAD_NEEDS_KICKING;
GRHIVariableRateShadingEnabled                         = MAC_PLATFORM_AGXRHI_VARIABLE_RATE_SHADING_ENABLED;
GSupportsDepthBoundsTest                               = MAC_PLATFORM_AGXRHI_SUPPORTS_DEPTH_BOUNDS_TEST;
GSupportsEfficientAsyncCompute                         = MAC_PLATFORM_AGXRHI_SUPPORTS_EFFICIENT_ASYNC_COMPUTE;
GSupportsImageExternal                                 = MAC_PLATFORM_AGXRHI_SUPPORTS_IMAGE_EXTERNAL;
GSupportsMobileMultiView                               = MAC_PLATFORM_AGXRHI_SUPPORTS_MOBILE_MULTIVIEW;
GSupportsParallelOcclusionQueries                      = MAC_PLATFORM_AGXRHI_SUPPORTS_PARALLEL_OCCLUSION_QUERIES;
GSupportsParallelRenderingTasksWithSeparateRHIThread   = MAC_PLATFORM_AGXRHI_SUPPORTS_PARALLEL_RENDERING_TASKS_WITH_SEPARATE_RHI_THREAD;
GSupportsPixelLocalStorage                             = MAC_PLATFORM_AGXRHI_SUPPORTS_PIXEL_LOCAL_STORAGE;
GSupportsQuadBufferStereo                              = MAC_PLATFORM_AGXRHI_SUPPORTS_QUADBUFFER_STEREO;
GSupportsRenderDepthTargetableShaderResources          = MAC_PLATFORM_AGXRHI_SUPPORTS_RENDER_DEPTH_TARGETABLE_SHADER_RESOURCES;
GSupportsRenderTargetFormat_PF_FloatRGBA               = MAC_PLATFORM_AGXRHI_SUPPORTS_RENDERTARGET_FORMAT_PF_FLOAT_RGBA;
GSupportsRenderTargetFormat_PF_G8                      = MAC_PLATFORM_AGXRHI_SUPPORTS_RENDERTARGET_FORMAT_PF_G_8;
GSupportsSeparateRenderTargetBlendState                = MAC_PLATFORM_AGXRHI_SUPPORTS_SEPARATE_RENDERTARGET_BLEND_STATE;
GSupportsShaderDepthStencilFetch                       = MAC_PLATFORM_AGXRHI_SUPPORTS_SHADER_DEPTHSTENCIL_FETCH;
GSupportsShaderFramebufferFetch                        = MAC_PLATFORM_AGXRHI_SUPPORTS_SHADER_FRAMEBUFFER_FETCH;
GSupportsShaderMRTFramebufferFetch                     = MAC_PLATFORM_AGXRHI_SUPPORTS_SHADER_MRT_FRAMEBUFFER_FETCH;
GSupportsTexture3D                                     = MAC_PLATFORM_AGXRHI_SUPPORTS_TEXTURE_3D;
GSupportsTimestampRenderQueries                        = MAC_PLATFORM_AGXRHI_SUPPORTS_TIMESTAMP_RENDER_QUERIES;
GSupportsTransientResourceAliasing                     = MAC_PLATFORM_AGXRHI_SUPPORTS_TRANSIENT_RESOURCE_ALIASING;
GSupportsVolumeTextureRendering                        = MAC_PLATFORM_AGXRHI_SUPPORTS_VOLUME_TEXTURE_RENDERING;
GSupportsWideMRT                                       = MAC_PLATFORM_AGXRHI_SUPPORTS_WIDE_MRT;
GUsingNullRHI                                          = MAC_PLATFORM_AGXRHI_USING_NULL_RHI;
GRHIHDRDisplayOutputFormat                             = MAC_PLATFORM_AGXRHI_HDR_DISPLAY_OUTPUT_FORMAT;
GRHIVariableRateShadingImageFormat                     = MAC_PLATFORM_AGXRHI_VARIABLE_RATE_SHADING_IMAGE_FORMAT;
GRHIMergeableAccessMask                                = MAC_PLATFORM_AGXRHI_MERGEABLE_ACCESS_MASK;
GRHIMultiPipelineMergeableAccessMask                   = MAC_PLATFORM_AGXRHI_MULTI_PIPELINE_MERGEABLE_ACCESS_MASK;
GMaxRHIFeatureLevel                                    = MAC_PLATFORM_AGXRHI_MAX_FEATURE_LEVEL;
GMaxRHIShaderPlatform                                  = MAC_PLATFORM_AGXRHI_MAX_SHADER_PLATFORM;
GShaderPlatformForFeatureLevel                         = ERHIFeatureLevel::Num]	MAC_PLATFORM_AGXRHI_SHADER_PLATFORM_FOR_FEATURE_LEVEL;
GRHIVariableRateShadingImageDataType                   = MAC_PLATFORM_AGXRHI_VARIABLE_RATE_SHADING_IMAGE_DATA_TYPE;
GRHIMaxDispatchThreadGroupsPerDimension                = MAC_PLATFORM_AGXRHI_MAX_DISPATCH_THREAD_GROUPS_PER_DIMENSION;
GPixelFormats                                          = PF_MAX]	MAC_PLATFORM_AGXRHI_PIXEL_FORMATS;
GRHIDefaultMSAASampleOffsets                           = 1+2+4+8+16]	MAC_PLATFORM_AGXRHI_DEFAULT_MSAA_SAMPLE_OFFSETS;
GVertexElementTypeSupport                              = MAC_PLATFORM_AGXRHI_VERTEX_ELEMENT_TYPE_SUPPORT;
GCurrentNumDrawCallsRHI                                = MAX_NUM_GPUS]	MAC_PLATFORM_AGXRHI_CURRENT_NUM_DRAW_CALLS;
GCurrentNumPrimitivesDrawnRHI                          = MAX_NUM_GPUS]	MAC_PLATFORM_AGXRHI_CURRENT_NUM_PRIMITIVES_DRAWN;
GCurrentRendertargetMemorySize                         = MAC_PLATFORM_AGXRHI_CURRENT_RENDERTARGET_MEMORY_SIZE;
GCurrentTextureMemorySize                              = MAC_PLATFORM_AGXRHI_CURRENT_TEXTURE_MEMORY_SIZE;
GDrawUPIndexCheckCount                                 = MAC_PLATFORM_AGXRHI_DRAW_UP_INDEX_CHECK_COUNT;
GDrawUPVertexCheckCount                                = MAC_PLATFORM_AGXRHI_DRAW_UP_VERTEX_CHECK_COUNT;
GMaxComputeDispatchDimension                           = MAC_PLATFORM_AGXRHI_MAX_COMPUTE_DISPATCH_DIMENSION;
GMaxCubeTextureDimensions                              = MAC_PLATFORM_AGXRHI_MAX_CUBE_TEXTURE_DIMENSIONS;
GMaxShadowDepthBufferSizeX                             = MAC_PLATFORM_AGXRHI_MAX_SHADOW_DEPTH_BUFFER_SIZE_X;
GMaxShadowDepthBufferSizeY                             = MAC_PLATFORM_AGXRHI_MAX_SHADOW_DEPTH_BUFFER_SIZE_Y;
GMaxTextureArrayLayers                                 = MAC_PLATFORM_AGXRHI_MAX_TEXTURE_ARRAY_LAYERS;
GMaxTextureDimensions                                  = MAC_PLATFORM_AGXRHI_MAX_TEXTURE_DIMENSIONS;
GMaxTextureMipCount                                    = MAC_PLATFORM_AGXRHI_MAX_TEXTURE_MIP_COUNT;
GMaxTextureSamplers                                    = MAC_PLATFORM_AGXRHI_MAX_TEXTURE_SAMPLERS;
GMaxVolumeTextureDimensions                            = MAC_PLATFORM_AGXRHI_MAX_VOLUME_TEXTURE_DIMENSIONS;
GMaxWorkGroupInvocations                               = MAC_PLATFORM_AGXRHI_MAX_WORKGROUP_INVOCATIONS;
GNumDrawCallsRHI                                       = MAX_NUM_GPUS]	MAC_PLATFORM_AGXRHI_NUM_DRAW_CALLS;
GNumPrimitivesDrawnRHI                                 = MAX_NUM_GPUS]	MAC_PLATFORM_AGXRHI_NUM_PRIMITIVES_DRAWN;
GPoolSizeVRAMPercentage                                = MAC_PLATFORM_AGXRHI_POOL_SIZE_VRAM_PERCENTAGE;
GRHIMaximumReccommendedOustandingOcclusionQueries      = MAC_PLATFORM_AGXRHI_MAXIMUM_RECCOMMENDED_OUSTANDING_OCCLUSION_QUERIES;
GRHIMaximumWaveSize                                    = MAC_PLATFORM_AGXRHI_MAXIMUM_WAVE_SIZE;
GRHIMinimumWaveSize                                    = MAC_PLATFORM_AGXRHI_MINIMUM_WAVE_SIZE;
GRHIPersistentThreadGroupCount                         = MAC_PLATFORM_AGXRHI_PERSISTENT_THREAD_GROUP_COUNT;
GRHIVariableRateShadingImageTileMaxHeight              = MAC_PLATFORM_AGXRHI_VARIABLE_RATE_SHADING_IMAGE_TILE_MAX_HEIGHT;
GRHIVariableRateShadingImageTileMaxWidth               = MAC_PLATFORM_AGXRHI_VARIABLE_RATE_SHADING_IMAGE_TILE_MAX_WIDTH;
GRHIVariableRateShadingImageTileMinHeight              = MAC_PLATFORM_AGXRHI_VARIABLE_RATE_SHADING_IMAGE_TILE_MIN_HEIGHT;
GRHIVariableRateShadingImageTileMinWidth               = MAC_PLATFORM_AGXRHI_VARIABLE_RATE_SHADING_IMAGE_TILE_MIN_WIDTH;
GCurrentNumDrawCallsRHIPtr                             = MAC_PLATFORM_AGXRHI_CURRENT_NUM_DRAW_CALLS_PTR;
GMaxBufferDimensions                                   = MAC_PLATFORM_AGXRHI_MAX_BUFFER_DIMENSIONS;
GMaxComputeSharedMemory                                = MAC_PLATFORM_AGXRHI_MAX_COMPUTE_SHARED_MEMORY;
GTexturePoolSize                                       = MAC_PLATFORM_AGXRHI_TEXTURE_POOL_SIZE;
GRHIRayTracingAccelerationStructureAlignment           = MAC_PLATFORM_AGXRHI_RAY_TRACING_ACCELERATION_STRUCTURE_ALIGNMENT;
GRHIRayTracingInstanceDescriptorSize                   = MAC_PLATFORM_AGXRHI_RAY_TRACING_INSTANCE_DESCRIPTOR_SIZE;
GRHIRayTracingScratchBufferAlignment                   = MAC_PLATFORM_AGXRHI_RAY_TRACING_SCRATCH_BUFFER_ALIGNMENT;
GRHIRayTracingShaderTableAlignment                     = MAC_PLATFORM_AGXRHI_RAY_TRACING_SHADER_TABLE_ALIGNMENT;
GDemotedLocalMemorySize                                = MAC_PLATFORM_AGXRHI_DEMOTED_LOCAL_MEMORY_SIZE;
GRHIPresentCounter                                     = MAC_PLATFORM_AGXRHI_PRESENT_COUNTER;
GRHITransitionPrivateData_AlignInBytes                 = MAC_PLATFORM_AGXRHI_TRANSITION_PRIVATE_DATA_ALIGN_IN_BYTES;
GRHITransitionPrivateData_SizeInBytes                  = MAC_PLATFORM_AGXRHI_TRANSITION_PRIVATE_DATA_SIZE_IN_BYTES;
 */
