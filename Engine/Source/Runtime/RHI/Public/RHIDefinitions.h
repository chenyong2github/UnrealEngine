// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RHIDefinitions.h: Render Hardware Interface definitions
		(that don't require linking).
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "PixelFormat.h"
#include "HAL/IConsoleManager.h"
#include "Serialization/MemoryLayout.h"

#ifndef USE_STATIC_SHADER_PLATFORM_ENUMS
#define USE_STATIC_SHADER_PLATFORM_ENUMS 0
#endif

#ifndef USE_STATIC_SHADER_PLATFORM_INFO
#define USE_STATIC_SHADER_PLATFORM_INFO 0
#endif

#ifndef RHI_RAYTRACING
#define RHI_RAYTRACING 0
#endif

enum EShaderFrequency : uint8
{
	SF_Vertex			= 0,
	SF_Hull				= 1,
	SF_Domain			= 2,
	SF_Pixel			= 3,
	SF_Geometry			= 4,
	SF_Compute			= 5,
	SF_RayGen			= 6,
	SF_RayMiss			= 7,
	SF_RayHitGroup		= 8,
	SF_RayCallable		= 9,

	SF_NumFrequencies	= 10,

	// Number of standard SM5-style shader frequencies for graphics pipeline (excluding compute)
	SF_NumGraphicsFrequencies = 5,

	// Number of standard SM5-style shader frequencies (including compute)
	SF_NumStandardFrequencies = 6,

	SF_NumBits			= 4,
};
static_assert(SF_NumFrequencies <= (1 << SF_NumBits), "SF_NumFrequencies will not fit on SF_NumBits");

/** @warning: update *LegacyShaderPlatform* when the below changes */
enum EShaderPlatform
{
	SP_PCD3D_SM5					= 0,
	SP_OPENGL_SM4_REMOVED			UE_DEPRECATED(4.27, "ShaderPlatform is removed; please don't use.") = 1,
	SP_PS4_REMOVED					UE_DEPRECATED(4.27, "ShaderPlatform is removed; please don't use.") = 2,
	SP_OPENGL_PCES2_REMOVED			UE_DEPRECATED(4.27, "ShaderPlatform is removed; please don't use.") = 3,
	SP_XBOXONE_D3D12_REMOVED		UE_DEPRECATED(4.27, "ShaderPlatform is removed; please don't use.") = 4,
	SP_PCD3D_SM4_REMOVED			UE_DEPRECATED(4.27, "ShaderPlatform is removed; please don't use.") = 5,
	SP_OPENGL_SM5_REMOVED			UE_DEPRECATED(4.27, "ShaderPlatform is removed; please don't use.") = 6,
	SP_PCD3D_ES2_REMOVED			UE_DEPRECATED(4.27, "ShaderPlatform is removed; please don't use.") = 7,
	SP_OPENGL_ES2_ANDROID_REMOVED	UE_DEPRECATED(4.27, "ShaderPlatform is removed; please don't use.") = 8,
	SP_OPENGL_ES2_WEBGL_REMOVED		UE_DEPRECATED(4.27, "ShaderPlatform is removed; please don't use.") = 9, 
	SP_OPENGL_ES2_IOS_REMOVED		UE_DEPRECATED(4.27, "ShaderPlatform is removed; please don't use.") = 10,
	SP_METAL						= 11,
	SP_METAL_MRT					= 12,
	SP_OPENGL_ES31_EXT_REMOVED		UE_DEPRECATED(4.27, "ShaderPlatform is removed; please don't use.") = 13,
	SP_PCD3D_ES3_1					= 14,
	SP_OPENGL_PCES3_1				= 15,
	SP_METAL_SM5					= 16,
	SP_VULKAN_PCES3_1				= 17,
	SP_METAL_SM5_NOTESS				= 18,
	SP_VULKAN_SM4_REMOVED			UE_DEPRECATED(4.27, "ShaderPlatform is removed; please don't use.") = 19,
	SP_VULKAN_SM5					= 20,
	SP_VULKAN_ES3_1_ANDROID			= 21,
	SP_METAL_MACES3_1 				= 22,
	SP_METAL_MACES2_REMOVED			UE_DEPRECATED(4.27, "ShaderPlatform is removed; please don't use.") = 23,
	SP_OPENGL_ES3_1_ANDROID			= 24,
	SP_SWITCH_REMOVED				UE_DEPRECATED(4.27, "ShaderPlatform is removed; please don't use.") = 25,
	SP_SWITCH_FORWARD_REMOVED		UE_DEPRECATED(4.27, "ShaderPlatform is removed; please don't use.") = 26,
	SP_METAL_MRT_MAC				= 27,
	SP_VULKAN_SM5_LUMIN				= 28,
	SP_VULKAN_ES3_1_LUMIN			= 29,
	SP_METAL_TVOS					= 30,
	SP_METAL_MRT_TVOS				= 31,
	/**********************************************************************************/
	/* !! Do not add any new platforms here. Add them below SP_StaticPlatform_Last !! */
	/**********************************************************************************/

	//---------------------------------------------------------------------------------
	/** Pre-allocated block of shader platform enum values for platform extensions */
#define DDPI_NUM_STATIC_SHADER_PLATFORMS 16
	SP_StaticPlatform_First = 32,

	// Pull in the extra shader platform definitions from platform extensions.
	// @todo - when we remove EShaderPlatform, fix up the shader platforms defined in UEBuild[Platform].cs files.
#ifdef DDPI_EXTRA_SHADERPLATFORMS
	DDPI_EXTRA_SHADERPLATFORMS
#endif

	SP_StaticPlatform_Last  = (SP_StaticPlatform_First + DDPI_NUM_STATIC_SHADER_PLATFORMS - 1),

	//  Add new platforms below this line, starting from (SP_StaticPlatform_Last + 1)
	//---------------------------------------------------------------------------------
	SP_VULKAN_SM5_ANDROID			= SP_StaticPlatform_Last+1,

	SP_NumPlatforms,
	SP_NumBits						= 7,
};
static_assert(SP_NumPlatforms <= (1 << SP_NumBits), "SP_NumPlatforms will not fit on SP_NumBits");

struct FGenericStaticShaderPlatform final
{
	inline FGenericStaticShaderPlatform(const EShaderPlatform InPlatform) : Platform(InPlatform) {}
	inline operator EShaderPlatform() const
	{
		return Platform;
	}

	inline bool operator == (const EShaderPlatform Other) const
	{
		return Other == Platform;
	}
	inline bool operator != (const EShaderPlatform Other) const
	{
		return Other != Platform;
	}
private:
	const EShaderPlatform Platform;
};

#if USE_STATIC_SHADER_PLATFORM_ENUMS
#include COMPILED_PLATFORM_HEADER(StaticShaderPlatform.inl)
#else
using FStaticShaderPlatform = FGenericStaticShaderPlatform;
#endif

class FStaticShaderPlatformNames
{
private:
	static const uint32 NumPlatforms = DDPI_NUM_STATIC_SHADER_PLATFORMS;

	struct FPlatform
	{
		FName Name;
		FName ShaderPlatform;
		FName ShaderFormat;
	} Platforms[NumPlatforms];

	FStaticShaderPlatformNames()
	{
#ifdef DDPI_SHADER_PLATFORM_NAME_MAP
		struct FStaticNameMapEntry
		{
			FName Name;
			FName PlatformName;
			int32 Index;
		} NameMap[] =
		{
			DDPI_SHADER_PLATFORM_NAME_MAP
		};

		for (int32 MapIndex = 0; MapIndex < UE_ARRAY_COUNT(NameMap); ++MapIndex)
		{
			FStaticNameMapEntry const& Entry = NameMap[MapIndex];
			check(IsStaticPlatform(EShaderPlatform(Entry.Index)));
			uint32 PlatformIndex = Entry.Index - SP_StaticPlatform_First;

			FPlatform& Platform = Platforms[PlatformIndex];
			check(Platform.Name == NAME_None); // Check we've not already seen this platform

			Platform.Name = Entry.PlatformName;
			Platform.ShaderPlatform = FName(*FString::Printf(TEXT("SP_%s"), *Entry.Name.ToString()), FNAME_Add);
			Platform.ShaderFormat = FName(*FString::Printf(TEXT("SF_%s"), *Entry.Name.ToString()), FNAME_Add);
		}
#endif
	}

public:
	static inline FStaticShaderPlatformNames const& Get()
	{
		static FStaticShaderPlatformNames Names;
		return Names;
	}

	static inline bool IsStaticPlatform(EShaderPlatform Platform)
	{
		return Platform >= SP_StaticPlatform_First && Platform <= SP_StaticPlatform_Last;
	}

	inline const FName& GetShaderPlatform(EShaderPlatform Platform) const
	{
		return Platforms[GetStaticPlatformIndex(Platform)].ShaderPlatform;
	}

	inline const FName& GetShaderFormat(EShaderPlatform Platform) const
	{
		return Platforms[GetStaticPlatformIndex(Platform)].ShaderFormat;
	}

	inline const FName& GetPlatformName(EShaderPlatform Platform) const
	{
		return Platforms[GetStaticPlatformIndex(Platform)].Name;
	}

private:
	static inline uint32 GetStaticPlatformIndex(EShaderPlatform Platform)
	{
		check(IsStaticPlatform(Platform));
		return uint32(Platform) - SP_StaticPlatform_First;
	}
};

/**
 * The RHI's feature level indicates what level of support can be relied upon.
 * Note: these are named after graphics API's like ES3 but a feature level can be used with a different API (eg ERHIFeatureLevel::ES3.1 on D3D11)
 * As long as the graphics API supports all the features of the feature level (eg no ERHIFeatureLevel::SM5 on OpenGL ES3.1)
 */
namespace ERHIFeatureLevel
{
	enum Type
	{
		/** Feature level defined by the core capabilities of OpenGL ES2. Deprecated */
		ES2_REMOVED,

		/** Feature level defined by the core capabilities of OpenGL ES3.1 & Metal/Vulkan. */
		ES3_1,

		/**
		 * Feature level defined by the capabilities of DX10 Shader Model 4.
		 * SUPPORT FOR THIS FEATURE LEVEL HAS BEEN ENTIRELY REMOVED.
		 */
		SM4_REMOVED,

		/**
		 * Feature level defined by the capabilities of DX11 Shader Model 5.
		 *   Compute shaders with shared memory, group sync, UAV writes, integer atomics
		 *   Indirect drawing
		 *   Pixel shaders with UAV writes
		 *   Cubemap arrays
		 *   Read-only depth or stencil views (eg read depth buffer as SRV while depth test and stencil write)
		 * Tessellation is not considered part of Feature Level SM5 and has a separate capability flag.
		 */
		SM5,
		Num
	};
};
DECLARE_INTRINSIC_TYPE_LAYOUT(ERHIFeatureLevel::Type);

struct FGenericStaticFeatureLevel
{
	inline FGenericStaticFeatureLevel(const ERHIFeatureLevel::Type InFeatureLevel) : FeatureLevel(InFeatureLevel) {}
	inline FGenericStaticFeatureLevel(const TEnumAsByte<ERHIFeatureLevel::Type> InFeatureLevel) : FeatureLevel(InFeatureLevel) {}

	inline operator ERHIFeatureLevel::Type() const
	{
		return FeatureLevel;
	}

	inline bool operator == (const ERHIFeatureLevel::Type Other) const
	{
		return Other == FeatureLevel;
	}

	inline bool operator != (const ERHIFeatureLevel::Type Other) const
	{
		return Other != FeatureLevel;
	}

	inline bool operator <= (const ERHIFeatureLevel::Type Other) const
	{
		return FeatureLevel <= Other;
	}

	inline bool operator < (const ERHIFeatureLevel::Type Other) const
	{
		return FeatureLevel < Other;
	}

	inline bool operator >= (const ERHIFeatureLevel::Type Other) const
	{
		return FeatureLevel >= Other;
	}

	inline bool operator > (const ERHIFeatureLevel::Type Other) const
	{
		return FeatureLevel > Other;
	}

private:
	ERHIFeatureLevel::Type FeatureLevel;
};

#if USE_STATIC_SHADER_PLATFORM_ENUMS
#include COMPILED_PLATFORM_HEADER(StaticFeatureLevel.inl)
#else
using FStaticFeatureLevel = FGenericStaticFeatureLevel;
#endif

extern RHI_API const FName LANGUAGE_D3D;
extern RHI_API const FName LANGUAGE_Metal;
extern RHI_API const FName LANGUAGE_OpenGL;
extern RHI_API const FName LANGUAGE_Vulkan;
extern RHI_API const FName LANGUAGE_Sony;
extern RHI_API const FName LANGUAGE_Nintendo;

class RHI_API FGenericDataDrivenShaderPlatformInfo
{
	FName Language;
	ERHIFeatureLevel::Type MaxFeatureLevel;
	uint32 bIsMobile: 1;
	uint32 bIsMetalMRT: 1;
	uint32 bIsPC: 1;
	uint32 bIsConsole: 1;
	uint32 bIsAndroidOpenGLES: 1;

	uint32 bSupportsMobileMultiView: 1;
	uint32 bSupportsVolumeTextureCompression: 1;
	uint32 bSupportsDistanceFields: 1; // used for DFShadows and DFAO - since they had the same checks
	uint32 bSupportsDiaphragmDOF: 1;
	uint32 bSupportsRGBColorBuffer: 1;
	uint32 bSupportsCapsuleShadows: 1;
	uint32 bSupportsVolumetricFog: 1; // also used for FVVoxelization
	uint32 bSupportsIndexBufferUAVs: 1;
	uint32 bSupportsInstancedStereo: 1;
	uint32 bSupportsMultiView: 1;
	uint32 bSupportsMSAA: 1;
	uint32 bSupports4ComponentUAVReadWrite: 1;
	uint32 bSupportsRenderTargetWriteMask: 1;
	uint32 bSupportsRayTracing: 1;
	uint32 bSupportsRayTracingIndirectInstanceData : 1; // Whether instance transforms can be copied from the GPU to the TLAS instances buffer
	uint32 bSupportsPathTracing : 1; // Whether real-time path tracer is supported on this platform (avoids compiling unnecessary shaders)
	uint32 bSupportsGPUSkinCache: 1;
	uint32 bSupportsGPUScene : 1;
	uint32 bSupportsByteBufferComputeShaders : 1;
	uint32 bSupportsPrimitiveShaders : 1;
	uint32 bSupportsUInt64ImageAtomics : 1;
	uint32 bSupportsTemporalHistoryUpscale : 1;
	uint32 bSupportsRTIndexFromVS : 1;
	uint32 bSupportsWaveOperations : 1; // Whether HLSL SM6 shader wave intrinsics are supported
	uint32 bRequiresExplicit128bitRT : 1;
	uint32 bSupportsGen5TemporalAA : 1;
	uint32 bTargetsTiledGPU: 1;
	uint32 bNeedsOfflineCompiler: 1;
	uint32 bSupportsAnisotropicMaterials : 1;
	uint32 bSupportsDualSourceBlending : 1;
	uint32 bRequiresGeneratePrevTransformBuffer : 1;
	uint32 bRequiresRenderTargetDuringRaster : 1;
	uint32 bRequiresDisableForwardLocalLights : 1;
	uint32 bCompileSignalProcessingPipeline : 1;
	uint32 bSupportsTessellation : 1;
	uint32 bSupportsPerPixelDBufferMask : 1;
	uint32 bIsHlslcc : 1;
	uint32 bSupportsVariableRateShading : 1;
	uint32 NumberOfComputeThreads : 10;
	uint32 bWaterUsesSimpleForwardShading : 1;
	uint32 bNeedsToSwitchVerticalAxisOnMobileOpenGL : 1;
	uint32 bSupportsHairStrandGeometry : 1;
	uint32 bSupportsDOFHybridScattering : 1;
	uint32 bNeedsExtraMobileFrames : 1;
	uint32 bSupportsHZBOcclusion : 1;
	uint32 bSupportsWaterIndirectDraw : 1;
	uint32 bSupportsAsyncPipelineCompilation : 1;
	uint32 bSupportsManualVertexFetch : 1;
	uint32 bRequiresReverseCullingOnMobile : 1;
	uint32 bOverrideFMaterial_NeedsGBufferEnabled : 1;
	uint32 bSupportsMobileDistanceField : 1;

		
#if WITH_EDITOR
	FText FriendlyName;
#endif

	// NOTE: When adding fields, you must also add to ParseDataDrivenShaderInfo!
	uint32 bContainsValidPlatformInfo : 1;

	FGenericDataDrivenShaderPlatformInfo()
	{
		FMemory::Memzero(this, sizeof(*this));

		SetDefaultValues();
	}

	void SetDefaultValues();

public:
	static void Initialize();
	static void ParseDataDrivenShaderInfo(const FConfigSection& Section, FGenericDataDrivenShaderPlatformInfo& Info);

	static FORCEINLINE_DEBUGGABLE const bool GetIsLanguageD3D(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].Language == LANGUAGE_D3D;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetIsLanguageMetal(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].Language == LANGUAGE_Metal;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetIsLanguageOpenGL(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].Language == LANGUAGE_OpenGL;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetIsLanguageVulkan(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].Language == LANGUAGE_Vulkan;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetIsLanguageSony(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].Language == LANGUAGE_Sony;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetIsLanguageNintendo(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].Language == LANGUAGE_Nintendo;
	}

	static FORCEINLINE_DEBUGGABLE const ERHIFeatureLevel::Type GetMaxFeatureLevel(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].MaxFeatureLevel;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetIsMobile(const EShaderPlatform Platform)
	{
		return Infos[Platform].bIsMobile;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetIsMetalMRT(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bIsMetalMRT;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetIsPC(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bIsPC;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetIsConsole(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bIsConsole;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetIsAndroidOpenGLES(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bIsAndroidOpenGLES;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetSupportsMobileMultiView(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsMobileMultiView;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetSupportsVolumeTextureCompression(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsVolumeTextureCompression;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetSupportsDistanceFields(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsDistanceFields;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetSupportsDiaphragmDOF(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsDiaphragmDOF;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetSupportsRGBColorBuffer(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsRGBColorBuffer;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetSupportsCapsuleShadows(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsCapsuleShadows;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetSupportsVolumetricFog(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsVolumetricFog;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetSupportsIndexBufferUAVs(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsIndexBufferUAVs;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetSupportsInstancedStereo(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsInstancedStereo;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetSupportsMultiView(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsMultiView;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetSupportsMSAA(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsMSAA;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetSupports4ComponentUAVReadWrite(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupports4ComponentUAVReadWrite;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetSupportsRenderTargetWriteMask(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsRenderTargetWriteMask;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetSupportsRayTracing(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsRayTracing;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetSupportsRayTracingIndirectInstanceData(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsRayTracingIndirectInstanceData;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetSupportsPathTracing(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsPathTracing;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetSupportsGPUSkinCache(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsGPUSkinCache;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetTargetsTiledGPU(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bTargetsTiledGPU;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetNeedsOfflineCompiler(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bNeedsOfflineCompiler;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetSupportsPrimitiveShaders(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsPrimitiveShaders;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetSupportsByteBufferComputeShaders(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsByteBufferComputeShaders;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetSupportsWaveOperations(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsWaveOperations;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetSupportsTemporalHistoryUpscale(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsTemporalHistoryUpscale;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetSupportsRTIndexFromVS(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsRTIndexFromVS;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetSupportsGPUScene(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsGPUScene;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetRequiresExplicit128bitRT(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bRequiresExplicit128bitRT;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetSupportsGen5TemporalAA(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsGen5TemporalAA;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetSupportsUInt64ImageAtomics(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsUInt64ImageAtomics;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetSupportsAnisotropicMaterials(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsAnisotropicMaterials;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetSupportsDualSourceBlending(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsDualSourceBlending;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetRequiresGeneratePrevTransformBuffer(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bRequiresGeneratePrevTransformBuffer;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetRequiresRenderTargetDuringRaster(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bRequiresRenderTargetDuringRaster;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetRequiresDisableForwardLocalLights(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bRequiresDisableForwardLocalLights;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetCompileSignalProcessingPipeline(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bCompileSignalProcessingPipeline;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetSupportsTessellation(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsTessellation;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetSupportsPerPixelDBufferMask(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsPerPixelDBufferMask;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetIsHlslcc(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bIsHlslcc;
	}

	static FORCEINLINE_DEBUGGABLE const bool GetSupportsVariableRateShading(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsVariableRateShading;
	}

	static FORCEINLINE_DEBUGGABLE const uint32 GetNumberOfComputeThreads(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].NumberOfComputeThreads;
	}

	static FORCEINLINE_DEBUGGABLE const uint32 GetWaterUsesSimpleForwardShading(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bWaterUsesSimpleForwardShading;
	}

	static FORCEINLINE_DEBUGGABLE const uint32 GetNeedsToSwitchVerticalAxisOnMobileOpenGL(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bNeedsToSwitchVerticalAxisOnMobileOpenGL;
	}

	static FORCEINLINE_DEBUGGABLE const uint32 GetSupportsHairStrandGeometry(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsHairStrandGeometry;
	}

	static FORCEINLINE_DEBUGGABLE const uint32 GetSupportsDOFHybridScattering(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsDOFHybridScattering;
	}

	static FORCEINLINE_DEBUGGABLE const uint32 GetNeedsExtraMobileFrames(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bNeedsExtraMobileFrames;
	}

	static FORCEINLINE_DEBUGGABLE const uint32 GetSupportsHZBOcclusion(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsHZBOcclusion;
	}

	static FORCEINLINE_DEBUGGABLE const uint32 GetSupportsWaterIndirectDraw(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsWaterIndirectDraw;
	}

	static FORCEINLINE_DEBUGGABLE const uint32 GetSupportsAsyncPipelineCompilation(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsAsyncPipelineCompilation;
	}

	static FORCEINLINE_DEBUGGABLE const uint32 GetSupportsManualVertexFetch(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsManualVertexFetch;
	}

	static FORCEINLINE_DEBUGGABLE const uint32 GetRequiresReverseCullingOnMobile(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bRequiresReverseCullingOnMobile;
	}

	static FORCEINLINE_DEBUGGABLE const uint32 GetOverrideFMaterial_NeedsGBufferEnabled(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bOverrideFMaterial_NeedsGBufferEnabled;
	}

	static FORCEINLINE_DEBUGGABLE const uint32 GetSupportsMobileDistanceField(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bSupportsMobileDistanceField;
	}

#if WITH_EDITOR
	static FORCEINLINE_DEBUGGABLE FText GetFriendlyName(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].FriendlyName;
	}
#endif

private:
	static FGenericDataDrivenShaderPlatformInfo Infos[SP_NumPlatforms];

public:
	static bool IsValid(const FStaticShaderPlatform Platform)
	{
		return Infos[Platform].bContainsValidPlatformInfo;
	}
};

#if USE_STATIC_SHADER_PLATFORM_ENUMS || USE_STATIC_SHADER_PLATFORM_INFO

#define IMPLEMENT_DDPSPI_SETTING_WITH_RETURN_TYPE(ReturnType, Function, Value) \
	static FORCEINLINE_DEBUGGABLE const ReturnType Function(const FStaticShaderPlatform Platform) \
	{ \
		checkSlow(!FGenericDataDrivenShaderPlatformInfo::IsValid(Platform) || FGenericDataDrivenShaderPlatformInfo::Function(Platform) == Value); \
		return Value; \
	}
#define IMPLEMENT_DDPSPI_SETTING(Function, Value) IMPLEMENT_DDPSPI_SETTING_WITH_RETURN_TYPE(bool, Function, Value)

#include COMPILED_PLATFORM_HEADER(DataDrivenShaderPlatformInfo.inl)

#else
using FDataDrivenShaderPlatformInfo = FGenericDataDrivenShaderPlatformInfo;
#endif

enum ERenderQueryType
{
	// e.g. WaitForFrameEventCompletion()
	RQT_Undefined,
	// Result is the number of samples that are not culled (divide by MSAACount to get pixels)
	RQT_Occlusion,
	// Result is current time in micro seconds = 1/1000 ms = 1/1000000 sec (not a duration).
	RQT_AbsoluteTime,
};

/** Maximum number of miplevels in a texture. */
enum { MAX_TEXTURE_MIP_COUNT = 15 };

/** Maximum number of static/skeletal mesh LODs */
enum { MAX_MESH_LOD_COUNT = 8 };

/** Maximum number of immutable samplers in a PSO. */
enum
{
	MaxImmutableSamplers = 2
};

/** The maximum number of vertex elements which can be used by a vertex declaration. */
enum
{
	MaxVertexElementCount = 16,
	MaxVertexElementCount_NumBits = 4,
};
static_assert(MaxVertexElementCount <= (1 << MaxVertexElementCount_NumBits), "MaxVertexElementCount will not fit on MaxVertexElementCount_NumBits");

/** The alignment in bytes between elements of array shader parameters. */
enum { ShaderArrayElementAlignBytes = 16 };

/** The number of render-targets that may be simultaneously written to. */
enum
{
	MaxSimultaneousRenderTargets = 8,
	MaxSimultaneousRenderTargets_NumBits = 3,
};
static_assert(MaxSimultaneousRenderTargets <= (1 << MaxSimultaneousRenderTargets_NumBits), "MaxSimultaneousRenderTargets will not fit on MaxSimultaneousRenderTargets_NumBits");

/** The number of UAVs that may be simultaneously bound to a shader. */
enum { MaxSimultaneousUAVs = 8 };

enum class ERHIZBuffer
{
	// Before changing this, make sure all math & shader assumptions are correct! Also wrap your C++ assumptions with
	//		static_assert(ERHIZBuffer::IsInvertedZBuffer(), ...);
	// Shader-wise, make sure to update Definitions.usf, HAS_INVERTED_Z_BUFFER
	FarPlane = 0,
	NearPlane = 1,

	// 'bool' for knowing if the API is using Inverted Z buffer
	IsInverted = (int32)((int32)ERHIZBuffer::FarPlane < (int32)ERHIZBuffer::NearPlane),
};


/**
* The RHI's currently enabled shading path.
*/
namespace ERHIShadingPath
{
	enum Type
	{
		Deferred,
		Forward,
		Mobile,
		Num
	};
}

enum ESamplerFilter
{
	SF_Point,
	SF_Bilinear,
	SF_Trilinear,
	SF_AnisotropicPoint,
	SF_AnisotropicLinear,

	ESamplerFilter_Num,
	ESamplerFilter_NumBits = 3,
};
static_assert(ESamplerFilter_Num <= (1 << ESamplerFilter_NumBits), "ESamplerFilter_Num will not fit on ESamplerFilter_NumBits");

enum ESamplerAddressMode
{
	AM_Wrap,
	AM_Clamp,
	AM_Mirror,
	/** Not supported on all platforms */
	AM_Border,

	ESamplerAddressMode_Num,
	ESamplerAddressMode_NumBits = 2,
};
static_assert(ESamplerAddressMode_Num <= (1 << ESamplerAddressMode_NumBits), "ESamplerAddressMode_Num will not fit on ESamplerAddressMode_NumBits");

enum ESamplerCompareFunction
{
	SCF_Never,
	SCF_Less
};

enum ERasterizerFillMode
{
	FM_Point,
	FM_Wireframe,
	FM_Solid,

	ERasterizerFillMode_Num,
	ERasterizerFillMode_NumBits = 2,
};
static_assert(ERasterizerFillMode_Num <= (1 << ERasterizerFillMode_NumBits), "ERasterizerFillMode_Num will not fit on ERasterizerFillMode_NumBits");

enum ERasterizerCullMode
{
	CM_None,
	CM_CW,
	CM_CCW,

	ERasterizerCullMode_Num,
	ERasterizerCullMode_NumBits = 2,
};
static_assert(ERasterizerCullMode_Num <= (1 << ERasterizerCullMode_NumBits), "ERasterizerCullMode_Num will not fit on ERasterizerCullMode_NumBits");

enum EColorWriteMask
{
	CW_RED   = 0x01,
	CW_GREEN = 0x02,
	CW_BLUE  = 0x04,
	CW_ALPHA = 0x08,

	CW_NONE  = 0,
	CW_RGB   = CW_RED | CW_GREEN | CW_BLUE,
	CW_RGBA  = CW_RED | CW_GREEN | CW_BLUE | CW_ALPHA,
	CW_RG    = CW_RED | CW_GREEN,
	CW_BA    = CW_BLUE | CW_ALPHA,

	EColorWriteMask_NumBits = 4,
};

enum ECompareFunction
{
	CF_Less,
	CF_LessEqual,
	CF_Greater,
	CF_GreaterEqual,
	CF_Equal,
	CF_NotEqual,
	CF_Never,
	CF_Always,

	ECompareFunction_Num,
	ECompareFunction_NumBits = 3,

	// Utility enumerations
	CF_DepthNearOrEqual		= (((int32)ERHIZBuffer::IsInverted != 0) ? CF_GreaterEqual : CF_LessEqual),
	CF_DepthNear			= (((int32)ERHIZBuffer::IsInverted != 0) ? CF_Greater : CF_Less),
	CF_DepthFartherOrEqual	= (((int32)ERHIZBuffer::IsInverted != 0) ? CF_LessEqual : CF_GreaterEqual),
	CF_DepthFarther			= (((int32)ERHIZBuffer::IsInverted != 0) ? CF_Less : CF_Greater),
};
static_assert(ECompareFunction_Num <= (1 << ECompareFunction_NumBits), "ECompareFunction_Num will not fit on ECompareFunction_NumBits");

enum EStencilMask
{
	SM_Default,
	SM_255,
	SM_1,
	SM_2,
	SM_4,
	SM_8,
	SM_16,
	SM_32,
	SM_64,
	SM_128,
	SM_Count
};

enum EStencilOp
{
	SO_Keep,
	SO_Zero,
	SO_Replace,
	SO_SaturatedIncrement,
	SO_SaturatedDecrement,
	SO_Invert,
	SO_Increment,
	SO_Decrement,

	EStencilOp_Num,
	EStencilOp_NumBits = 3,
};
static_assert(EStencilOp_Num <= (1 << EStencilOp_NumBits), "EStencilOp_Num will not fit on EStencilOp_NumBits");

enum EBlendOperation
{
	BO_Add,
	BO_Subtract,
	BO_Min,
	BO_Max,
	BO_ReverseSubtract,

	EBlendOperation_Num,
	EBlendOperation_NumBits = 3,
};
static_assert(EBlendOperation_Num <= (1 << EBlendOperation_NumBits), "EBlendOperation_Num will not fit on EBlendOperation_NumBits");

enum EBlendFactor
{
	BF_Zero,
	BF_One,
	BF_SourceColor,
	BF_InverseSourceColor,
	BF_SourceAlpha,
	BF_InverseSourceAlpha,
	BF_DestAlpha,
	BF_InverseDestAlpha,
	BF_DestColor,
	BF_InverseDestColor,
	BF_ConstantBlendFactor,
	BF_InverseConstantBlendFactor,
	BF_Source1Color,
	BF_InverseSource1Color,
	BF_Source1Alpha,
	BF_InverseSource1Alpha,

	EBlendFactor_Num,
	EBlendFactor_NumBits = 4,
};
static_assert(EBlendFactor_Num <= (1 << EBlendFactor_NumBits), "EBlendFactor_Num will not fit on EBlendFactor_NumBits");

enum EVertexElementType
{
	VET_None,
	VET_Float1,
	VET_Float2,
	VET_Float3,
	VET_Float4,
	VET_PackedNormal,	// FPackedNormal
	VET_UByte4,
	VET_UByte4N,
	VET_Color,
	VET_Short2,
	VET_Short4,
	VET_Short2N,		// 16 bit word normalized to (value/32767.0,value/32767.0,0,0,1)
	VET_Half2,			// 16 bit float using 1 bit sign, 5 bit exponent, 10 bit mantissa 
	VET_Half4,
	VET_Short4N,		// 4 X 16 bit word, normalized 
	VET_UShort2,
	VET_UShort4,
	VET_UShort2N,		// 16 bit word normalized to (value/65535.0,value/65535.0,0,0,1)
	VET_UShort4N,		// 4 X 16 bit word unsigned, normalized 
	VET_URGB10A2N,		// 10 bit r, g, b and 2 bit a normalized to (value/1023.0f, value/1023.0f, value/1023.0f, value/3.0f)
	VET_UInt,
	VET_MAX,

	VET_NumBits = 5,
};
static_assert(VET_MAX <= (1 << VET_NumBits), "VET_MAX will not fit on VET_NumBits");
DECLARE_INTRINSIC_TYPE_LAYOUT(EVertexElementType);

enum ECubeFace
{
	CubeFace_PosX = 0,
	CubeFace_NegX,
	CubeFace_PosY,
	CubeFace_NegY,
	CubeFace_PosZ,
	CubeFace_NegZ,
	CubeFace_MAX
};

enum EUniformBufferUsage
{
	// the uniform buffer is temporary, used for a single draw call then discarded
	UniformBuffer_SingleDraw = 0,
	// the uniform buffer is used for multiple draw calls but only for the current frame
	UniformBuffer_SingleFrame,
	// the uniform buffer is used for multiple draw calls, possibly across multiple frames
	UniformBuffer_MultiFrame,
};

enum class EUniformBufferValidation
{
	None,
	ValidateResources
};

/** The base type of a value in a uniform buffer. */
enum EUniformBufferBaseType : uint8
{
	UBMT_INVALID,

	// Invalid type when trying to use bool, to have explicit error message to programmer on why
	// they shouldn't use bool in shader parameter structures.
	UBMT_BOOL,

	// Parameter types.
	UBMT_INT32,
	UBMT_UINT32,
	UBMT_FLOAT32,

	// RHI resources not tracked by render graph.
	UBMT_TEXTURE,
	UBMT_SRV,
	UBMT_UAV,
	UBMT_SAMPLER,

	// Resources tracked by render graph.
	UBMT_RDG_TEXTURE,
	UBMT_RDG_TEXTURE_ACCESS,
	UBMT_RDG_TEXTURE_SRV,
	UBMT_RDG_TEXTURE_UAV,
	UBMT_RDG_BUFFER,
	UBMT_RDG_BUFFER_ACCESS,
	UBMT_RDG_BUFFER_SRV,
	UBMT_RDG_BUFFER_UAV,
	UBMT_RDG_UNIFORM_BUFFER,

	// Nested structure.
	UBMT_NESTED_STRUCT,

	// Structure that is nested on C++ side, but included on shader side.
	UBMT_INCLUDED_STRUCT,

	// GPU Indirection reference of struct, like is currently named Uniform buffer.
	UBMT_REFERENCED_STRUCT,

	// Structure dedicated to setup render targets for a rasterizer pass.
	UBMT_RENDER_TARGET_BINDING_SLOTS,

	EUniformBufferBaseType_Num,
	EUniformBufferBaseType_NumBits = 5,
};
static_assert(EUniformBufferBaseType_Num <= (1 << EUniformBufferBaseType_NumBits), "EUniformBufferBaseType_Num will not fit on EUniformBufferBaseType_NumBits");
DECLARE_INTRINSIC_TYPE_LAYOUT(EUniformBufferBaseType);

/** Numerical type used to store the static slot indices. */
using FUniformBufferStaticSlot = uint8;

enum
{
	/** The maximum number of static slots allowed. */
	MAX_UNIFORM_BUFFER_STATIC_SLOTS = 255
};

/** Returns whether a static uniform buffer slot index is valid. */
inline bool IsUniformBufferStaticSlotValid(const FUniformBufferStaticSlot Slot)
{
	return Slot < MAX_UNIFORM_BUFFER_STATIC_SLOTS;
}

struct FRHIResourceTableEntry
{
public:
	static CONSTEXPR uint32 GetEndOfStreamToken()
	{
		return 0xffffffff;
	}

	static uint32 Create(uint16 UniformBufferIndex, uint16 ResourceIndex, uint16 BindIndex)
	{
		return ((UniformBufferIndex & RTD_Mask_UniformBufferIndex) << RTD_Shift_UniformBufferIndex) |
			((ResourceIndex & RTD_Mask_ResourceIndex) << RTD_Shift_ResourceIndex) |
			((BindIndex & RTD_Mask_BindIndex) << RTD_Shift_BindIndex);
	}

	static inline uint16 GetUniformBufferIndex(uint32 Data)
	{
		return (Data >> RTD_Shift_UniformBufferIndex) & RTD_Mask_UniformBufferIndex;
	}

	static inline uint16 GetResourceIndex(uint32 Data)
	{
		return (Data >> RTD_Shift_ResourceIndex) & RTD_Mask_ResourceIndex;
	}

	static inline uint16 GetBindIndex(uint32 Data)
	{
		return (Data >> RTD_Shift_BindIndex) & RTD_Mask_BindIndex;
	}

private:
	enum EResourceTableDefinitions
	{
		RTD_NumBits_UniformBufferIndex	= 8,
		RTD_NumBits_ResourceIndex		= 16,
		RTD_NumBits_BindIndex			= 8,

		RTD_Mask_UniformBufferIndex		= (1 << RTD_NumBits_UniformBufferIndex) - 1,
		RTD_Mask_ResourceIndex			= (1 << RTD_NumBits_ResourceIndex) - 1,
		RTD_Mask_BindIndex				= (1 << RTD_NumBits_BindIndex) - 1,

		RTD_Shift_BindIndex				= 0,
		RTD_Shift_ResourceIndex			= RTD_Shift_BindIndex + RTD_NumBits_BindIndex,
		RTD_Shift_UniformBufferIndex	= RTD_Shift_ResourceIndex + RTD_NumBits_ResourceIndex,
	};
	static_assert(RTD_NumBits_UniformBufferIndex + RTD_NumBits_ResourceIndex + RTD_NumBits_BindIndex <= sizeof(uint32)* 8, "RTD_* values must fit in 32 bits");
};

enum EResourceLockMode
{
	RLM_ReadOnly,
	RLM_WriteOnly,
	RLM_WriteOnly_NoOverwrite,
	RLM_Num
};

/** limited to 8 types in FReadSurfaceDataFlags */
enum ERangeCompressionMode
{
	// 0 .. 1
	RCM_UNorm,
	// -1 .. 1
	RCM_SNorm,
	// 0 .. 1 unless there are smaller values than 0 or bigger values than 1, then the range is extended to the minimum or the maximum of the values
	RCM_MinMaxNorm,
	// minimum .. maximum (each channel independent)
	RCM_MinMax,
};

enum class EPrimitiveTopologyType : uint8
{
	Triangle,
	Patch,
	Line,
	Point,
	//Quad,

	Num,
	NumBits = 2,
};
static_assert((uint32)EPrimitiveTopologyType::Num <= (1 << (uint32)EPrimitiveTopologyType::NumBits), "EPrimitiveTopologyType::Num will not fit on EPrimitiveTopologyType::NumBits");

enum EPrimitiveType
{
	// Topology that defines a triangle N with 3 vertex extremities: 3*N+0, 3*N+1, 3*N+2.
	PT_TriangleList,

	// Topology that defines a triangle N with 3 vertex extremities: N+0, N+1, N+2.
	PT_TriangleStrip,

	// Topology that defines a line with 2 vertex extremities: 2*N+0, 2*N+1.
	PT_LineList,

	// Topology that defines a quad N with 4 vertex extremities: 4*N+0, 4*N+1, 4*N+2, 4*N+3.
	// Supported only if GRHISupportsQuadTopology == true.
	PT_QuadList,

	// Topology that defines a point N with a single vertex N.
	PT_PointList,

	// Topology that defines a screen aligned rectangle N with only 3 vertex corners:
	//    3*N + 0 is upper-left corner,
	//    3*N + 1 is upper-right corner,
	//    3*N + 2 is the lower-left corner.
	// Supported only if GRHISupportsRectTopology == true.
	PT_RectList,

	// Tesselation patch list. Supported only if tesselation is supported.
	PT_1_ControlPointPatchList,
	PT_2_ControlPointPatchList,
	PT_3_ControlPointPatchList,
	PT_4_ControlPointPatchList,
	PT_5_ControlPointPatchList,
	PT_6_ControlPointPatchList,
	PT_7_ControlPointPatchList,
	PT_8_ControlPointPatchList,
	PT_9_ControlPointPatchList,
	PT_10_ControlPointPatchList,
	PT_11_ControlPointPatchList,
	PT_12_ControlPointPatchList,
	PT_13_ControlPointPatchList,
	PT_14_ControlPointPatchList,
	PT_15_ControlPointPatchList,
	PT_16_ControlPointPatchList,
	PT_17_ControlPointPatchList,
	PT_18_ControlPointPatchList,
	PT_19_ControlPointPatchList,
	PT_20_ControlPointPatchList,
	PT_21_ControlPointPatchList,
	PT_22_ControlPointPatchList,
	PT_23_ControlPointPatchList,
	PT_24_ControlPointPatchList,
	PT_25_ControlPointPatchList,
	PT_26_ControlPointPatchList,
	PT_27_ControlPointPatchList,
	PT_28_ControlPointPatchList,
	PT_29_ControlPointPatchList,
	PT_30_ControlPointPatchList,
	PT_31_ControlPointPatchList,
	PT_32_ControlPointPatchList,
	PT_Num,
	PT_NumBits = 6
};
static_assert(PT_Num <= (1 << 8), "EPrimitiveType doesn't fit in a byte");
static_assert(PT_Num <= (1 << PT_NumBits), "PT_NumBits is too small");

enum EVRSAxisShadingRate : uint8
{
	VRSASR_1X = 0x0,
	VRSASR_2X = 0x1,
	VRSASR_4X = 0x2,
};

enum EVRSShadingRate : uint8
{
	VRSSR_1x1  = (VRSASR_1X << 2) + VRSASR_1X,
	VRSSR_1x2  = (VRSASR_1X << 2) + VRSASR_2X,
	VRSSR_2x1  = (VRSASR_2X << 2) + VRSASR_1X,
	VRSSR_2x2  = (VRSASR_2X << 2) + VRSASR_2X,
	VRSSR_2x4  = (VRSASR_2X << 2) + VRSASR_4X,
	VRSSR_4x2  = (VRSASR_4X << 2) + VRSASR_2X,
	VRSSR_4x4  = (VRSASR_4X << 2) + VRSASR_4X,
};

enum EVRSRateCombiner : uint8
{
	VRSRB_Passthrough,
	VRSRB_Override,
	VRSRB_Min,
	VRSRB_Max,
	VRSRB_Sum,
};

enum EVRSImageDataType : uint8
{
	VRSImage_NotSupported,		// Image-based Variable Rate Shading is not supported on the current device/platform.
	VRSImage_Palette,			// Image-based VRS uses a palette of discrete, enumerated values to describe shading rate per tile.
	VRSImage_Fractional,		// Image-based VRS uses a floating point value to describe shading rate in X/Y (e.g. 1.0f is full rate, 0.5f is half-rate, 0.25f is 1/4 rate, etc).
};

/**
 *	Resource usage flags - for vertex and index buffers.
 */
enum EBufferUsageFlags
{
	BUF_None					= 0x0000,
	

	// Mutually exclusive write-frequency flags

	/** The buffer will be written to once. */
	BUF_Static					= 0x0001, 

	/** 
	 * The buffer will be written to occasionally, GPU read only, CPU write only.  The data lifetime is until the next update, or the buffer is destroyed.
	 */
	BUF_Dynamic					= 0x0002, 

	/** The buffer's data will have a lifetime of one frame.  It MUST be written to each frame, or a new one created each frame. */
	BUF_Volatile				= 0x0004, 

	// Mutually exclusive bind flags.
	BUF_UnorderedAccess			= 0x0008, // Allows an unordered access view to be created for the buffer.

	/** Create a byte address buffer, which is basically a structured buffer with a uint32 type. */
	BUF_ByteAddressBuffer		= 0x0020,

	/** Buffer that the GPU will use as a source for a copy. */
	BUF_SourceCopy				= 0x0040,

	/** Create a buffer that can be bound as a stream output target. */
	BUF_StreamOutput			= 0x0080,

	/** Create a buffer which contains the arguments used by DispatchIndirect or DrawIndirect. */
	BUF_DrawIndirect			= 0x0100,

	/** 
	 * Create a buffer that can be bound as a shader resource. 
	 * This is only needed for buffer types which wouldn't ordinarily be used as a shader resource, like a vertex buffer.
	 */
	BUF_ShaderResource			= 0x0200,

	/**
	 * Request that this buffer is directly CPU accessible
	 * (@todo josh: this is probably temporary and will go away in a few months)
	 */
	BUF_KeepCPUAccessible		= 0x0400,

	// Unused
	//BUF_ZeroStride			= 0x0800,

	/** Buffer should go in fast vram (hint only). Requires BUF_Transient */
	BUF_FastVRAM				= 0x1000,

	/** Buffer should be allocated from transient memory. */
	BUF_Transient				= 0x2000,

	/** Create a buffer that can be shared with an external RHI or process. */
	BUF_Shared					= 0x4000,

	/**
	 * Buffer contains opaque ray tracing acceleration structure data.
	 * Resources with this flag can't be bound directly to any shader stage and only can be used with ray tracing APIs.
	 * This flag is mutually exclusive with all other buffer flags except BUF_Static.
	*/
	BUF_AccelerationStructure	= 0x8000,

	BUF_VertexBuffer			= 0x10000,
	BUF_IndexBuffer				= 0x20000,
	BUF_StructuredBuffer		= 0x40000,

	// Helper bit-masks
	BUF_AnyDynamic = (BUF_Dynamic | BUF_Volatile),
};
ENUM_CLASS_FLAGS(EBufferUsageFlags);

enum class EGpuVendorId
{
	Unknown		= -1,
	NotQueried	= 0,

	Amd			= 0x1002,
	ImgTec		= 0x1010,
	Nvidia		= 0x10DE, 
	Arm			= 0x13B5, 
	Qualcomm	= 0x5143,
	Intel		= 0x8086,
};

/** An enumeration of the different RHI reference types. */
enum ERHIResourceType
{
	RRT_None,

	RRT_SamplerState,
	RRT_RasterizerState,
	RRT_DepthStencilState,
	RRT_BlendState,
	RRT_VertexDeclaration,
	RRT_VertexShader,
	RRT_HullShader,
	RRT_DomainShader,
	RRT_PixelShader,
	RRT_GeometryShader,
	RRT_ComputeShader,
	RRT_BoundShaderState,
	RRT_UniformBuffer,
	RRT_IndexBuffer,
	RRT_VertexBuffer,
	RRT_StructuredBuffer,
	RRT_Texture,
	RRT_Texture2D,
	RRT_Texture2DArray,
	RRT_Texture3D,
	RRT_TextureCube,
	RRT_TextureReference,
	RRT_RenderQuery,
	RRT_Viewport,
	RRT_UnorderedAccessView,
	RRT_ShaderResourceView,

	RRT_Num
};

/** Describes the dimension of a texture. */
enum class ETextureDimension
{
	Texture2D,
	Texture2DArray,
	Texture3D,
	TextureCube,
	TextureCubeArray
};

/** Flags used for texture creation */
enum ETextureCreateFlags
{
	TexCreate_None					= 0,

	// Texture can be used as a render target
	TexCreate_RenderTargetable		= 1<<0,
	// Texture can be used as a resolve target
	TexCreate_ResolveTargetable		= 1<<1,
	// Texture can be used as a depth-stencil target.
	TexCreate_DepthStencilTargetable= 1<<2,
	// Texture can be used as a shader resource.
	TexCreate_ShaderResource		= 1<<3,
	// Texture is encoded in sRGB gamma space
	TexCreate_SRGB					= 1<<4,
	// Texture data is writable by the CPU
	TexCreate_CPUWritable			= 1<<5,
	// Texture will be created with an un-tiled format
	TexCreate_NoTiling				= 1<<6,
	// Texture will be used for video decode
	TexCreate_VideoDecode			= 1<<7,
	// Texture that may be updated every frame
	TexCreate_Dynamic				= 1<<8,
	// Texture will be used as a render pass attachment that will be read from
	TexCreate_InputAttachmentRead	= 1<<9,
	/** Texture represents a foveation attachment */
	TexCreate_Foveation				= 1 << 10,
	// Disable automatic defragmentation if the initial texture memory allocation fails.
	TexCreate_DisableAutoDefrag		 UE_DEPRECATED(4.26, "TexCreate_DisableAutoDefrag is deprecated and getting removed; please don't use.") = 1 << 10,
	// This texture has no GPU or CPU backing. It only exists in tile memory on TBDR GPUs (i.e., mobile).
	TexCreate_Memoryless			= 1<<11,
	// Create the texture with the flag that allows mip generation later, only applicable to D3D11
	TexCreate_GenerateMipCapable	= 1<<12,
	// The texture can be partially allocated in fastvram
	TexCreate_FastVRAMPartialAlloc  = 1<<13,
	// Do not create associated shader resource view, only applicable to D3D11 and D3D12
	TexCreate_DisableSRVCreation = 1 << 14,
	// Do not allow Delta Color Compression (DCC) to be used with this texture
	TexCreate_DisableDCC		    = 1 << 15,
	// UnorderedAccessView (DX11 only)
	// Warning: Causes additional synchronization between draw calls when using a render target allocated with this flag, use sparingly
	// See: GCNPerformanceTweets.pdf Tip 37
	TexCreate_UAV					= 1<<16,
	// Render target texture that will be displayed on screen (back buffer)
	TexCreate_Presentable			= 1<<17,
	// Texture data is accessible by the CPU
	TexCreate_CPUReadback			= 1<<18,
	// Texture was processed offline (via a texture conversion process for the current platform)
	TexCreate_OfflineProcessed		= 1<<19,
	// Texture needs to go in fast VRAM if available (HINT only)
	TexCreate_FastVRAM				= 1<<20,
	// by default the texture is not showing up in the list - this is to reduce clutter, using the FULL option this can be ignored
	TexCreate_HideInVisualizeTexture= 1<<21,
	// Texture should be created in virtual memory, with no physical memory allocation made
	// You must make further calls to RHIVirtualTextureSetFirstMipInMemory to allocate physical memory
	// and RHIVirtualTextureSetFirstMipVisible to map the first mip visible to the GPU
	TexCreate_Virtual				= 1<<22,
	// Creates a RenderTargetView for each array slice of the texture
	// Warning: if this was specified when the resource was created, you can't use SV_RenderTargetArrayIndex to route to other slices!
	TexCreate_TargetArraySlicesIndependently	= 1<<23,
	// Texture that may be shared with DX9 or other devices
	TexCreate_Shared = 1 << 24,
	// RenderTarget will not use full-texture fast clear functionality.
	TexCreate_NoFastClear = 1 << 25,
	// Texture is a depth stencil resolve target
	TexCreate_DepthStencilResolveTarget = 1 << 26,
	// Flag used to indicted this texture is a streamable 2D texture, and should be counted towards the texture streaming pool budget.
	TexCreate_Streamable = 1 << 27,
	// Render target will not FinalizeFastClear; Caches and meta data will be flushed, but clearing will be skipped (avoids potentially trashing metadata)
	TexCreate_NoFastClearFinalize = 1 << 28,
	// Hint to the driver that this resource is managed properly by the engine for Alternate-Frame-Rendering in mGPU usage.
	TexCreate_AFRManual = 1 << 29,
	// Workaround for 128^3 volume textures getting bloated 4x due to tiling mode on PS4
	TexCreate_ReduceMemoryWithTilingMode = 1 << 30,
	/** Texture should be allocated for external access. Vulkan only - Reusing TexCreate_ReduceMemoryWithTilingMode value since the two should never be used together */
	TexCreate_External = 1 << 30,
	/** Texture should be allocated from transient memory. */
	TexCreate_Transient = 1 << 31
};
ENUM_CLASS_FLAGS(ETextureCreateFlags);

enum EAsyncComputePriority
{
	AsyncComputePriority_Default = 0,
	AsyncComputePriority_High,
};

/**
 * Async texture reallocation status, returned by RHIGetReallocateTexture2DStatus().
 */
enum ETextureReallocationStatus
{
	TexRealloc_Succeeded = 0,
	TexRealloc_Failed,
	TexRealloc_InProgress,
};

/**
 * Action to take when a render target is set.
 */
enum class ERenderTargetLoadAction : uint8
{
	// Untouched contents of the render target are undefined. Any existing content is not preserved.
	ENoAction,

	// Existing contents are preserved.
	ELoad,

	// The render target is cleared to the fast clear value specified on the resource.
	EClear,

	Num,
	NumBits = 2,
};
static_assert((uint32)ERenderTargetLoadAction::Num <= (1 << (uint32)ERenderTargetLoadAction::NumBits), "ERenderTargetLoadAction::Num will not fit on ERenderTargetLoadAction::NumBits");

/**
 * Action to take when a render target is unset or at the end of a pass. 
 */
enum class ERenderTargetStoreAction : uint8
{
	// Contents of the render target emitted during the pass are not stored back to memory.
	ENoAction,

	// Contents of the render target emitted during the pass are stored back to memory.
	EStore,

	// Contents of the render target emitted during the pass are resolved using a box filter and stored back to memory.
	EMultisampleResolve,

	Num,
	NumBits = 2,
};
static_assert((uint32)ERenderTargetStoreAction::Num <= (1 << (uint32)ERenderTargetStoreAction::NumBits), "ERenderTargetStoreAction::Num will not fit on ERenderTargetStoreAction::NumBits");

/**
 * Common render target use cases
 */
enum class ESimpleRenderTargetMode
{
	// These will all store out color and depth
	EExistingColorAndDepth,							// Color = Existing, Depth = Existing
	EUninitializedColorAndDepth,					// Color = ????, Depth = ????
	EUninitializedColorExistingDepth,				// Color = ????, Depth = Existing
	EUninitializedColorClearDepth,					// Color = ????, Depth = Default
	EClearColorExistingDepth,						// Clear Color = whatever was bound to the rendertarget at creation time. Depth = Existing
	EClearColorAndDepth,							// Clear color and depth to bound clear values.
	EExistingContents_NoDepthStore,					// Load existing contents, but don't store depth out.  depth can be written.
	EExistingColorAndClearDepth,					// Color = Existing, Depth = clear value
	EExistingColorAndDepthAndClearStencil,			// Color = Existing, Depth = Existing, Stencil = clear

	// If you add an item here, make sure to add it to DecodeRenderTargetMode() as well!
};

enum class EClearDepthStencil
{
	Depth,
	Stencil,
	DepthStencil,
};

/**
 * Hint to the driver on how to load balance async compute work.  On some platforms this may be a priority, on others actually masking out parts of the GPU for types of work.
 */
enum class EAsyncComputeBudget
{
	ELeast_0,			//Least amount of GPU allocated to AsyncCompute that still gets 'some' done.
	EGfxHeavy_1,		//Gfx gets most of the GPU.
	EBalanced_2,		//Async compute and Gfx share GPU equally.
	EComputeHeavy_3,	//Async compute can use most of the GPU
	EAll_4,				//Async compute can use the entire GPU.
};

inline bool IsPCPlatform(const FStaticShaderPlatform Platform)
{
	return Platform == SP_PCD3D_SM5 || Platform == SP_PCD3D_ES3_1 ||
		Platform == SP_OPENGL_PCES3_1 ||
		Platform == SP_METAL_SM5_NOTESS || Platform == SP_METAL_SM5 ||
		Platform == SP_VULKAN_PCES3_1 || Platform == SP_VULKAN_SM5 || Platform == SP_METAL_MACES3_1 || Platform == SP_METAL_MRT_MAC 
		|| FDataDrivenShaderPlatformInfo::GetIsPC(Platform);
}

/** Whether the shader platform corresponds to the ES3.1/Metal/Vulkan feature level. */
inline bool IsMobilePlatform(const EShaderPlatform Platform)
{
	return
		Platform == SP_METAL || Platform == SP_METAL_MACES3_1 || Platform == SP_METAL_TVOS
		|| Platform == SP_PCD3D_ES3_1
		|| Platform == SP_OPENGL_PCES3_1 || Platform == SP_OPENGL_ES3_1_ANDROID
		|| Platform == SP_VULKAN_ES3_1_ANDROID || Platform == SP_VULKAN_PCES3_1 || Platform == SP_VULKAN_ES3_1_LUMIN
		|| FDataDrivenShaderPlatformInfo::GetIsMobile(Platform);
}

inline bool IsOpenGLPlatform(const FStaticShaderPlatform Platform)
{
	return Platform == SP_OPENGL_PCES3_1
		|| Platform == SP_OPENGL_ES3_1_ANDROID
		|| FDataDrivenShaderPlatformInfo::GetIsLanguageOpenGL(Platform);
}

inline bool IsMetalPlatform(const FStaticShaderPlatform Platform)
{
	return Platform == SP_METAL || Platform == SP_METAL_MRT || Platform == SP_METAL_TVOS || Platform == SP_METAL_MRT_TVOS 
		|| Platform == SP_METAL_SM5_NOTESS || Platform == SP_METAL_SM5
		|| Platform == SP_METAL_MACES3_1 || Platform == SP_METAL_MRT_MAC
		|| FDataDrivenShaderPlatformInfo::GetIsLanguageMetal(Platform);
}

inline bool IsMetalMobilePlatform(const FStaticShaderPlatform Platform)
{
	return Platform == SP_METAL || Platform == SP_METAL_TVOS
		|| (FDataDrivenShaderPlatformInfo::GetIsLanguageOpenGL(Platform) && FDataDrivenShaderPlatformInfo::GetIsMobile(Platform));
}

inline bool IsMetalMRTPlatform(const FStaticShaderPlatform Platform)
{
	return Platform == SP_METAL_MRT || Platform == SP_METAL_MRT_TVOS || Platform == SP_METAL_MRT_MAC
		|| FDataDrivenShaderPlatformInfo::GetIsMetalMRT(Platform);
}

inline bool IsMetalSM5Platform(const FStaticShaderPlatform Platform)
{
	return Platform == SP_METAL_MRT || Platform == SP_METAL_MRT_TVOS || Platform == SP_METAL_SM5_NOTESS || Platform == SP_METAL_SM5 || Platform == SP_METAL_MRT_MAC
		|| (FDataDrivenShaderPlatformInfo::GetIsLanguageMetal(Platform) && FDataDrivenShaderPlatformInfo::GetMaxFeatureLevel(Platform) == ERHIFeatureLevel::SM5);
}

inline bool IsConsolePlatform(const FStaticShaderPlatform Platform)
{
	return FDataDrivenShaderPlatformInfo::GetIsConsole(Platform);
}

UE_DEPRECATED(4.27, "IsSwitchPlatform() is deprecated; please use DataDrivenShaderPlatformInfo instead.")
inline bool IsSwitchPlatform(const FStaticShaderPlatform Platform)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return Platform == SP_SWITCH_REMOVED || Platform == SP_SWITCH_FORWARD_REMOVED || 
		FDataDrivenShaderPlatformInfo::GetIsLanguageNintendo(Platform);	
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

UE_DEPRECATED(4.27, "IsPS4Platform() is deprecated; please use DataDrivenShaderPlatformInfo instead.") 
inline bool IsPS4Platform(const FStaticShaderPlatform Platform)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return Platform == SP_PS4_REMOVED
		|| FDataDrivenShaderPlatformInfo::GetIsLanguageSony(Platform);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

inline bool IsVulkanPlatform(const FStaticShaderPlatform Platform)
{
	return Platform == SP_VULKAN_SM5 || Platform == SP_VULKAN_SM5_LUMIN || Platform == SP_VULKAN_PCES3_1 
		|| Platform == SP_VULKAN_ES3_1_ANDROID || Platform == SP_VULKAN_ES3_1_LUMIN || Platform == SP_VULKAN_SM5_ANDROID
		|| FDataDrivenShaderPlatformInfo::GetIsLanguageVulkan(Platform);
}

inline bool IsVulkanSM5Platform(const FStaticShaderPlatform Platform)
{
	return Platform == SP_VULKAN_SM5 || Platform == SP_VULKAN_SM5_LUMIN || Platform == SP_VULKAN_SM5_ANDROID
		|| (FDataDrivenShaderPlatformInfo::GetIsLanguageVulkan(Platform) && FDataDrivenShaderPlatformInfo::GetMaxFeatureLevel(Platform) == ERHIFeatureLevel::SM5);
}

inline bool IsVulkanMobileSM5Platform(const EShaderPlatform Platform)
{
	return Platform == SP_VULKAN_SM5_ANDROID;
}

inline bool IsAndroidOpenGLESPlatform(const FStaticShaderPlatform Platform)
{
	return Platform == SP_OPENGL_ES3_1_ANDROID || FDataDrivenShaderPlatformInfo::GetIsAndroidOpenGLES(Platform);
}

inline bool IsVulkanMobilePlatform(const FStaticShaderPlatform Platform)
{
	return Platform == SP_VULKAN_PCES3_1 || Platform == SP_VULKAN_ES3_1_ANDROID || Platform == SP_VULKAN_ES3_1_LUMIN
		|| (FDataDrivenShaderPlatformInfo::GetIsLanguageVulkan(Platform) && FDataDrivenShaderPlatformInfo::GetIsMobile(Platform));
}

UE_DEPRECATED(4.27, "IsD3DPlatform(bIncludeXboxOne) is deprecated; please use IsD3DPlatform() and DataDrivenShaderPlatformInfo instead.") 
inline bool IsD3DPlatform(const FStaticShaderPlatform Platform, bool bIncludeXboxOne)
{
	switch (Platform)
	{
	case SP_PCD3D_SM5:
	case SP_PCD3D_ES3_1:
		return true;
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	case SP_XBOXONE_D3D12_REMOVED:
		return bIncludeXboxOne;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	default:
		return FDataDrivenShaderPlatformInfo::GetIsLanguageD3D(Platform);
	}

	return false;
}

inline bool IsD3DPlatform(const FStaticShaderPlatform Platform)
{
	switch (Platform)
	{
	case SP_PCD3D_SM5:
	case SP_PCD3D_ES3_1:
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	case SP_XBOXONE_D3D12_REMOVED:
PRAGMA_ENABLE_DEPRECATION_WARNINGS
		return true;
	default:
		return FDataDrivenShaderPlatformInfo::GetIsLanguageD3D(Platform);
	}

	return false;
}


inline bool IsHlslccShaderPlatform(const FStaticShaderPlatform Platform)
{
	return IsMetalPlatform(Platform) || IsVulkanPlatform(Platform) || IsOpenGLPlatform(Platform) || FDataDrivenShaderPlatformInfo::GetIsHlslcc(Platform);
}

UE_DEPRECATED(4.27, "Removed; please don't use.") 
inline bool IsDeprecatedShaderPlatform(const FStaticShaderPlatform ShaderPlatform)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return ShaderPlatform == SP_OPENGL_SM5_REMOVED || ShaderPlatform == SP_PCD3D_SM4_REMOVED || ShaderPlatform == SP_OPENGL_ES2_IOS_REMOVED ||
		ShaderPlatform == SP_PCD3D_ES2_REMOVED || ShaderPlatform == SP_METAL_MACES2_REMOVED || ShaderPlatform == SP_OPENGL_PCES2_REMOVED ||
		ShaderPlatform == SP_OPENGL_ES2_ANDROID_REMOVED || ShaderPlatform == SP_OPENGL_ES2_WEBGL_REMOVED ||
		ShaderPlatform == SP_VULKAN_SM4_REMOVED || ShaderPlatform == SP_OPENGL_SM4_REMOVED || ShaderPlatform == SP_OPENGL_ES31_EXT_REMOVED;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

inline FStaticFeatureLevel GetMaxSupportedFeatureLevel(const FStaticShaderPlatform InShaderPlatform)
{
	switch (InShaderPlatform)
	{
	case SP_PCD3D_SM5:
	case SP_METAL_SM5:
	case SP_METAL_MRT:
	case SP_METAL_MRT_TVOS:
	case SP_METAL_MRT_MAC:
	case SP_METAL_SM5_NOTESS:
	case SP_VULKAN_SM5:
	case SP_VULKAN_SM5_LUMIN:
	case SP_VULKAN_SM5_ANDROID:
		return ERHIFeatureLevel::SM5;
	case SP_METAL:
	case SP_METAL_TVOS:
	case SP_METAL_MACES3_1:
	case SP_PCD3D_ES3_1:
	case SP_OPENGL_PCES3_1:
	case SP_VULKAN_PCES3_1:
	case SP_VULKAN_ES3_1_ANDROID:
	case SP_VULKAN_ES3_1_LUMIN:
	case SP_OPENGL_ES3_1_ANDROID:
		return ERHIFeatureLevel::ES3_1;
	default:
		return FDataDrivenShaderPlatformInfo::GetMaxFeatureLevel(InShaderPlatform);
	}
}

/* Returns true if the shader platform Platform is used to simulate a mobile feature level on a PC platform. */
inline bool IsSimulatedPlatform(const FStaticShaderPlatform Platform)
{
	switch (Platform)
	{
		case SP_PCD3D_ES3_1:
		case SP_OPENGL_PCES3_1:
		case SP_METAL_MACES3_1:
		case SP_VULKAN_PCES3_1:
			return true;
		break;

		default:
			return false;
		break;
	}

	return false;
}

inline EShaderPlatform GetSimulatedPlatform(EShaderPlatform Platform)
{
	switch (Platform)
	{
		case SP_PCD3D_ES3_1:
		case SP_OPENGL_PCES3_1:
			return SP_OPENGL_ES3_1_ANDROID;
		break;

		default:
			return Platform;
		break;
	}

	return Platform;
}

/** Returns true if the feature level is supported by the shader platform. */
inline bool IsFeatureLevelSupported(const FStaticShaderPlatform InShaderPlatform, ERHIFeatureLevel::Type InFeatureLevel)
{
	return InFeatureLevel <= GetMaxSupportedFeatureLevel(InShaderPlatform);
}

inline bool RHINeedsToSwitchVerticalAxis(const FStaticShaderPlatform Platform)
{
#if WITH_EDITOR
	static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.ForceRHISwitchVerticalAxis"));
	if (CVar->GetValueOnAnyThread())
	{
		// only allow this for mobile preview.
		return IsMobilePlatform(Platform);
	}
#endif

	// ES3.1 need to flip when rendering to an RT that will be post processed
	return IsOpenGLPlatform(Platform) && IsMobilePlatform(Platform) && !IsPCPlatform(Platform) && !IsMetalMobilePlatform(Platform) && !IsVulkanPlatform(Platform)
			&& FDataDrivenShaderPlatformInfo::GetNeedsToSwitchVerticalAxisOnMobileOpenGL(Platform);
}

inline bool RHISupportsSeparateMSAAAndResolveTextures(const FStaticShaderPlatform Platform)
{
	// Metal mobile devices and Android ES3.1 need to handle MSAA and resolve textures internally (unless RHICreateTexture2D was changed to take an optional resolve target)
	return !IsMetalMobilePlatform(Platform) && !IsAndroidOpenGLESPlatform(Platform);
}

inline bool RHISupportsComputeShaders(const FStaticShaderPlatform Platform)
{
	return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5) 
		|| (GetMaxSupportedFeatureLevel(Platform) == ERHIFeatureLevel::ES3_1);
}

inline bool RHISupportsGeometryShaders(const FStaticShaderPlatform Platform)
{
	return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5) && !IsMetalPlatform(Platform) &&
			 !IsVulkanMobilePlatform(Platform) && !IsVulkanMobileSM5Platform(Platform);
}

inline bool RHIHasTiledGPU(const FStaticShaderPlatform Platform)
{
	// @todo MetalMRT Technically we should include (Platform == SP_METAL_MRT) but this would disable depth-pre-pass which is currently required.
	return Platform == SP_METAL || Platform == SP_METAL_TVOS
		|| Platform == SP_OPENGL_ES3_1_ANDROID
		|| Platform == SP_VULKAN_ES3_1_ANDROID
		|| Platform == SP_METAL_MRT || Platform == SP_METAL_MRT_TVOS
		|| Platform == SP_VULKAN_SM5_ANDROID
		|| FDataDrivenShaderPlatformInfo::GetTargetsTiledGPU(Platform);
}

inline bool RHISupportsMobileMultiView(const FStaticShaderPlatform Platform)
{
	return Platform == EShaderPlatform::SP_OPENGL_ES3_1_ANDROID || IsVulkanMobilePlatform(Platform)
		|| FDataDrivenShaderPlatformInfo::GetSupportsMobileMultiView(Platform);
}

inline bool RHISupportsNativeShaderLibraries(const FStaticShaderPlatform Platform)
{
	return IsMetalPlatform(Platform);
}

inline bool RHISupportsShaderPipelines(const FStaticShaderPlatform Platform)
{
	return !IsMobilePlatform(Platform);
}

inline bool RHISupportsDualSourceBlending(const FStaticShaderPlatform Platform)
{
	// For now only enable support for SM5
	// Metal RHI doesn't support dual source blending properly at the moment.
	return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5) && (IsD3DPlatform(Platform) || FDataDrivenShaderPlatformInfo::GetSupportsDualSourceBlending(Platform) || IsVulkanPlatform(Platform));
}

inline bool RHISupportsMultithreadedShaderCreation(const FStaticShaderPlatform Platform)
{
	// all but GL
	return !IsOpenGLPlatform(Platform);
}

// Return what the expected number of samplers will be supported by a feature level
// Note that since the Feature Level is pretty orthogonal to the RHI/HW, this is not going to be perfect
// If should only be used for a guess at the limit, the real limit will not be known until runtime
inline uint32 GetExpectedFeatureLevelMaxTextureSamplers(const FStaticFeatureLevel FeatureLevel)
{
	return 16;
}

/** Returns whether the shader parameter type references an RDG texture. */
inline bool IsRDGTextureReferenceShaderParameterType(EUniformBufferBaseType BaseType)
{
	return
		BaseType == UBMT_RDG_TEXTURE ||
		BaseType == UBMT_RDG_TEXTURE_SRV ||
		BaseType == UBMT_RDG_TEXTURE_UAV ||
		BaseType == UBMT_RDG_TEXTURE_ACCESS;
}
/** Returns whether the shader parameter type references an RDG buffer. */
inline bool IsRDGBufferReferenceShaderParameterType(EUniformBufferBaseType BaseType)
{
	return
		BaseType == UBMT_RDG_BUFFER ||
		BaseType == UBMT_RDG_BUFFER_SRV ||
		BaseType == UBMT_RDG_BUFFER_UAV ||
		BaseType == UBMT_RDG_BUFFER_ACCESS;
}

/** Returns whether the shader parameter type is a reference onto a RDG resource. */
inline bool IsRDGResourceReferenceShaderParameterType(EUniformBufferBaseType BaseType)
{
	return IsRDGTextureReferenceShaderParameterType(BaseType) || IsRDGBufferReferenceShaderParameterType(BaseType) || BaseType == UBMT_RDG_UNIFORM_BUFFER;
}

/** Returns whether the shader parameter type needs to be passdown to RHI through FRHIUniformBufferLayout when creating an uniform buffer. */
inline bool IsShaderParameterTypeForUniformBufferLayout(EUniformBufferBaseType BaseType)
{
	return
		// RHI resource referenced in shader parameter structures.
		BaseType == UBMT_TEXTURE ||
		BaseType == UBMT_SRV ||
		BaseType == UBMT_SAMPLER ||
		BaseType == UBMT_UAV ||

		// RHI is able to access RHI resources from RDG.
		IsRDGResourceReferenceShaderParameterType(BaseType) ||

		// Render graph uses FRHIUniformBufferLayout to walk pass' parameters.
		BaseType == UBMT_REFERENCED_STRUCT ||
		BaseType == UBMT_RENDER_TARGET_BINDING_SLOTS;
}

/** Returns whether the shader parameter type in FRHIUniformBufferLayout is actually ignored by the RHI. */
inline bool IsShaderParameterTypeIgnoredByRHI(EUniformBufferBaseType BaseType)
{
	return
		// Render targets bindings slots needs to be in FRHIUniformBufferLayout for render graph, but the RHI does not actually need to know about it.
		BaseType == UBMT_RENDER_TARGET_BINDING_SLOTS ||

		// Custom access states are used by the render graph.
		BaseType == UBMT_RDG_TEXTURE_ACCESS ||
		BaseType == UBMT_RDG_BUFFER_ACCESS ||

		// #yuriy_todo: RHI is able to dereference uniform buffer in root shader parameter structures
		BaseType == UBMT_REFERENCED_STRUCT ||
		BaseType == UBMT_RDG_UNIFORM_BUFFER;
}

inline EGpuVendorId RHIConvertToGpuVendorId(uint32 VendorId)
{
	switch ((EGpuVendorId)VendorId)
	{
	case EGpuVendorId::NotQueried:
		return EGpuVendorId::NotQueried;

	case EGpuVendorId::Amd:
	case EGpuVendorId::ImgTec:
	case EGpuVendorId::Nvidia:
	case EGpuVendorId::Arm:
	case EGpuVendorId::Qualcomm:
	case EGpuVendorId::Intel:
		return (EGpuVendorId)VendorId;

	default:
		break;
	}

	return EGpuVendorId::Unknown;
}

inline const TCHAR* GetShaderFrequencyString(EShaderFrequency Frequency, bool bIncludePrefix = true)
{
	const TCHAR* String = TEXT("SF_NumFrequencies");
	switch (Frequency)
	{
	case SF_Vertex:			String = TEXT("SF_Vertex"); break;
	case SF_Hull:			String = TEXT("SF_Hull"); break;
	case SF_Domain:			String = TEXT("SF_Domain"); break;
	case SF_Geometry:		String = TEXT("SF_Geometry"); break;
	case SF_Pixel:			String = TEXT("SF_Pixel"); break;
	case SF_Compute:		String = TEXT("SF_Compute"); break;
	case SF_RayGen:			String = TEXT("SF_RayGen"); break;
	case SF_RayMiss:		String = TEXT("SF_RayMiss"); break;
	case SF_RayHitGroup:	String = TEXT("SF_RayHitGroup"); break;
	case SF_RayCallable:	String = TEXT("SF_RayCallable"); break;

	default:
		checkf(0, TEXT("Unknown ShaderFrequency %d"), (int32)Frequency);
		break;
	}

	// Skip SF_
	int32 Index = bIncludePrefix ? 0 : 3;
	String += Index;
	return String;
};

inline bool IsRayTracingShaderFrequency(EShaderFrequency Frequency)
{
	switch (Frequency)
	{
	case SF_RayGen:
	case SF_RayMiss:
	case SF_RayHitGroup:
	case SF_RayCallable:
		return true;
	default:
		return false;
	}
}

#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
	#define GEOMETRY_SHADER(GeometryShader)	(GeometryShader)
#else
	#define GEOMETRY_SHADER(GeometryShader)	nullptr
#endif

#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
	#define TESSELLATION_SHADER(HullOrDomainShader)	(HullOrDomainShader)
#else
	#define TESSELLATION_SHADER(HullOrDomainShader)	nullptr
#endif
