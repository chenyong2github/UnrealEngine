// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureFormatOodlePCH.h"
#include "CoreMinimal.h"
#include "ImageCore.h"
#include "Modules/ModuleManager.h"
#include "TextureCompressorModule.h"
#include "Interfaces/ITextureFormat.h"
#include "Interfaces/ITextureFormatModule.h"
#include "PixelFormat.h"
#include "Engine/TextureDefines.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ScopeLock.h"
#include "Misc/SecureHash.h"
#include "Async/TaskGraphInterfaces.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryWriter.h"
#include "DerivedDataBuildFunctionFactory.h"
#include "DerivedDataSharedString.h"
#include "Tasks/Task.h"
#include "TextureBuildFunction.h"
#include "HAL/FileManager.h"
#include "Misc/WildcardString.h"

#include "DDSFile.h"

#include "oodle2tex.h"

// Alternate job system - can set UseOodleExampleJobify in engine ini to enable.
#include "example_jobify.h"

/**********

Oodle Texture can do both RDO (rate distortion optimization) and non-RDO encoding to BC1-7.

This is controlled using the project texture compression settings and the corresponding
Compress Speed.

The texture property Lossy Compression Amount is converted to an RDO Lambda to use.
This property can be adjusted via LODGroup or per texture. If not set in either place,
the project settings provide a default value.

Oodle Texture can encode BC1-7.  It does not currently encode ASTC or other mobile formats.

=====================

TextureFormatOodle handles formats TFO_DXT1,etc.

Use of this format (instead of DXT1) is enabled with TextureFormatPrefix in config, such as :

\Engine\Config\BaseEngine.ini

[AlternateTextureCompression]
TextureCompressionFormat="TextureFormatOodle"
TextureFormatPrefix="TFO_"

When this is enabled, the formats like "DXT1" are renamed to "TFO_DXT1" and are handled by this encoder.

Oodle Texture RDO encoding can be slow, but is cached in the DDC so should only be slow the first time.  
A fast local network shared DDC is recommended.

RDO encoding and compression level can be enabled separately in the editor vs cooks using settings described
below.

========================


Oodle Texture Settings
----------------------

TextureFormatOodle reads settings from Engine.ini ; they're created by default
when not found.  Note they are created in per-platform Engine.ini, you can
find them and move them up to DefaultEngine if you want them to be global.

The INI settings block looks like :

[TextureFormatOodleSettings]
bForceAllBC23ToBC7=False
bDebugColor=False
GlobalLambdaMultiplier=1.0

The sense of the bools is set so that all-false is default behavior.

bForceAllBC23ToBC7 :

If true, all BC2 & 3 (DXT3 and DXT5) is encoded to BC7 instead.

On DX11 games, BC7 usualy has higher quality and takes the same space in memory as BC3.

For example in Unreal, "AutoDXT" selects DXT1 (BC1) for opaque textures and DXT5 (BC3)
for textures with alpha.  If you turn on this option, the BC3 will change to BC7, so
"AutoDXT" will now select BC1 for opaque and BC7 for alpha. Note that BC7 with alpha will
likely introduce color distortion that doesn't exist with DXT5 because DXT5 has the
alpha and color planes separate, where they are combine with BC7 - so the encoder can try
and swap color for alpha unlike DXT5.

It is off by default to make default behavior match the old encoders.

bDebugColor :

Fills the encoded texture with a solid color depending on their BCN format.
This is a handy way to see that you are in fact getting Oodle Texture in your game.
It's also an easy way to spot textures that aren't BCN compressed, since they will not
be solid color.  (for example I found that lots of the Unreal demo content uses "HDR"
which is an uncompressed format, instead of "HDRCompressed" (BC6))  The color indicates
the actual compressed format output (BC1-7).

GlobalLambdaMultiplier :

Takes all lambdas and scales them by this multiplier, so it affects the global default
and the per-texture lambdas.

It is recommended to leave this at 1.0 until you get near shipping your final game, at
which point you could tweak it to 0.9 or 1.1 to adjust your package size without having
to edit lots of per-texture lambdas.

Oodle Texture lambda
----------------------

The "lambda" parameter is the most important way of controlling Oodle Texture RDO.

"lambda" controls the tradeoff of size vs quality in the Rate Distortion Optimization.

Finding the right lambda settings will be a collaboration between artists and
programmers.  Programmers and technical artists may wish to find a global lambda
that meets your goals.  Individual texture artists may wish to tweak the lambda
per-texture when needed, but this should be rare - for the most part Oodle Texture
quality is very predictable and good on most textures.

Lambda first of all can be overridden per texture with the "LossyCompressionAmount"
setting.  This is a slider in the GUI in the editor that goes from Lowest to Highest.
The default value is "Default" and we recommend leaving that there most of the time.

If the per-texture LossyCompressionAmount is "Default", that means "inherit from LODGroup".

The LODGroup gives you a logical group of textures where you can adjust the lambda on that
whole set of textures rather than per-texture.

For example here I have changed "World" LossyCompressionAmount to TLCA_High, and 
"WorldNormalMap" to TLCA_Low :


[/Script/Engine.TextureLODSettings]
@TextureLODGroups=Group
TextureLODGroups=(Group=TEXTUREGROUP_World,MinLODSize=1,MaxLODSize=8192,LODBias=0,MinMagFilter=aniso,MipFilter=point,MipGenSettings=TMGS_SimpleAverage,LossyCompressionAmount=TLCA_High)
+TextureLODGroups=(Group=TEXTUREGROUP_WorldNormalMap,MinLODSize=1,MaxLODSize=8192,LODBias=0,MinMagFilter=aniso,MipFilter=point,MipGenSettings=TMGS_SimpleAverage,LossyCompressionAmount=TLCA_Low)
+TextureLODGroups=(Group=TEXTUREGROUP_WorldSpecular,MinLODSize=1,MaxLODSize=8192,LODBias=0,MinMagFilter=aniso,MipFilter=point,MipGenSettings=TMGS_SimpleAverage)


If the LossyCompressionAmount is not set on the LODGroup (which is the default), 
then it falls through to the global default, which is set in the texture compression
project settings.

At each stage, TLCA_Default means "inherit from parent".

TLCA_None means disable RDO entirely. We do not recommend this, use TLCA_Lowest 
instead when you need very high quality.

Note that the Unreal Editor texture dialog shows live compression results.
When you're in the editor and you adjust the LossyCompressionAmount or import a 
new texture, it shows the Oodle Texture encoded result in the texture preview.



*********/


DEFINE_LOG_CATEGORY_STATIC(LogTextureFormatOodle, Log, All);


/*****************
* 
* Function pointer types for the Oodle Texture functions we need to import :
* 
********************/

OODEFFUNC typedef OodleTex_Err (OOEXPLINK t_fp_OodleTex_EncodeBCN_RDO_Ex)(
    OodleTex_BC to_bcn,void * to_bcn_blocks,OO_SINTa num_blocks,
    const OodleTex_Surface * from_surfaces,OO_SINTa num_from_surfaces,OodleTex_PixelFormat from_format,
    const OodleTex_Layout * layout,
    int rdo_lagrange_lambda,
    const OodleTex_RDO_Options * options,
    int num_job_threads,void * jobify_user_ptr);
	
OODEFFUNC typedef void (OOEXPLINK t_fp_OodleTex_Plugins_SetAllocators)(
    t_fp_OodleTex_Plugin_MallocAligned * fp_OodleMallocAligned,
    t_fp_OodleTex_Plugin_Free * fp_OodleFree);
	
OODEFFUNC typedef void (OOEXPLINK t_fp_OodleTex_Plugins_SetJobSystemAndCount)(
    t_fp_OodleTex_Plugin_RunJob * fp_RunJob,
    t_fp_OodleTex_Plugin_WaitJob * fp_WaitJob,
    int target_parallelism);
	
OODEFFUNC typedef t_fp_OodleTex_Plugin_Printf * (OOEXPLINK t_fp_OodleTex_Plugins_SetPrintf)(t_fp_OodleTex_Plugin_Printf * fp_rrRawPrintf);

OODEFFUNC typedef t_fp_OodleTex_Plugin_DisplayAssertion * (OOEXPLINK t_fp_OodleTex_Plugins_SetAssertion)(t_fp_OodleTex_Plugin_DisplayAssertion * fp_rrDisplayAssertion);

OODEFFUNC typedef const char * (OOEXPLINK t_fp_OodleTex_Err_GetName)(OodleTex_Err error);

OODEFFUNC typedef const char * (OOEXPLINK t_fp_OodleTex_PixelFormat_GetName)(OodleTex_PixelFormat pf);

OODEFFUNC typedef const char * (OOEXPLINK t_fp_OodleTex_BC_GetName)(OodleTex_BC bcn);

OODEFFUNC typedef const char * (OOEXPLINK t_fp_OodleTex_RDO_UniversalTiling_GetName)(OodleTex_RDO_UniversalTiling tiling);

OODEFFUNC typedef OO_S32 (OOEXPLINK t_fp_OodleTex_BC_BytesPerBlock)(OodleTex_BC bcn);

OODEFFUNC typedef OO_S32 (OOEXPLINK t_fp_OodleTex_PixelFormat_BytesPerPixel)(OodleTex_PixelFormat pf);

/**
* 
* FOodleTextureVTable provides function calls to a specific version of the Oodle Texture dynamic lib
*   multiple FOodleTextureVTables may be loaded to support multi-version encoding
* 
**/
struct FOodleTextureVTable
{
	FName	Version;
	void *	DynamicLib  = nullptr;

	t_fp_OodleTex_EncodeBCN_RDO_Ex * fp_OodleTex_EncodeBCN_RDO_Ex = nullptr;

	t_fp_OodleTex_Plugins_SetAllocators * fp_OodleTex_Plugins_SetAllocators = nullptr;
	t_fp_OodleTex_Plugins_SetJobSystemAndCount * fp_OodleTex_Plugins_SetJobSystemAndCount = nullptr;
	t_fp_OodleTex_Plugins_SetPrintf * fp_OodleTex_Plugins_SetPrintf = nullptr;
	t_fp_OodleTex_Plugins_SetAssertion * fp_OodleTex_Plugins_SetAssertion = nullptr;
	
	t_fp_OodleTex_Err_GetName * fp_OodleTex_Err_GetName = nullptr;
	t_fp_OodleTex_PixelFormat_GetName * fp_OodleTex_PixelFormat_GetName = nullptr;
	t_fp_OodleTex_BC_GetName * fp_OodleTex_BC_GetName = nullptr;
	t_fp_OodleTex_RDO_UniversalTiling_GetName * fp_OodleTex_RDO_UniversalTiling_GetName = nullptr;
	t_fp_OodleTex_BC_BytesPerBlock * fp_OodleTex_BC_BytesPerBlock = nullptr;
	t_fp_OodleTex_PixelFormat_BytesPerPixel * fp_OodleTex_PixelFormat_BytesPerPixel = nullptr;

	FOodleTextureVTable()
	{
	}

	bool LoadDynamicLib(FString InVersionString)
	{
		Version = FName(InVersionString);

		// TFO_DLL_PREFIX/SUFFIX is set by the build.cs with the right names for this platform
		FString DynamicLibName = FString(TFO_DLL_PREFIX) + InVersionString + FString(TFO_DLL_SUFFIX);
		
		UE_LOG(LogTextureFormatOodle, Display, TEXT("Oodle Texture loading DLL: %s"), *DynamicLibName);

		DynamicLib = FPlatformProcess::GetDllHandle(*DynamicLibName);
		if ( DynamicLib == nullptr )
		{
			UE_LOG(LogTextureFormatOodle, Warning, TEXT("Oodle Texture %s requested but could not be loaded"), *DynamicLibName);
			Version = FName("invalid");
			return false;
		}
	
		fp_OodleTex_EncodeBCN_RDO_Ex = (t_fp_OodleTex_EncodeBCN_RDO_Ex *) FPlatformProcess::GetDllExport( DynamicLib, TEXT("OodleTex_EncodeBCN_RDO_Ex") );
		check( fp_OodleTex_EncodeBCN_RDO_Ex != nullptr );
		
		fp_OodleTex_Plugins_SetAllocators = (t_fp_OodleTex_Plugins_SetAllocators *) FPlatformProcess::GetDllExport( DynamicLib, TEXT("OodleTex_Plugins_SetAllocators") );
		check( fp_OodleTex_Plugins_SetAllocators != nullptr );
		
		fp_OodleTex_Plugins_SetJobSystemAndCount = (t_fp_OodleTex_Plugins_SetJobSystemAndCount *) FPlatformProcess::GetDllExport( DynamicLib, TEXT("OodleTex_Plugins_SetJobSystemAndCount") );
		check( fp_OodleTex_Plugins_SetJobSystemAndCount != nullptr );
				
		fp_OodleTex_Plugins_SetPrintf = (t_fp_OodleTex_Plugins_SetPrintf *) FPlatformProcess::GetDllExport( DynamicLib, TEXT("OodleTex_Plugins_SetPrintf") );
		check( fp_OodleTex_Plugins_SetPrintf != nullptr );
		
		fp_OodleTex_Plugins_SetAssertion = (t_fp_OodleTex_Plugins_SetAssertion *) FPlatformProcess::GetDllExport( DynamicLib, TEXT("OodleTex_Plugins_SetAssertion") );
		check( fp_OodleTex_Plugins_SetAssertion != nullptr );
		
		fp_OodleTex_Err_GetName = (t_fp_OodleTex_Err_GetName *) FPlatformProcess::GetDllExport( DynamicLib, TEXT("OodleTex_Err_GetName") );
		check( fp_OodleTex_Err_GetName != nullptr );
		
		fp_OodleTex_PixelFormat_GetName = (t_fp_OodleTex_PixelFormat_GetName *) FPlatformProcess::GetDllExport( DynamicLib, TEXT("OodleTex_PixelFormat_GetName") );
		check( fp_OodleTex_PixelFormat_GetName != nullptr );
		
		fp_OodleTex_BC_GetName = (t_fp_OodleTex_BC_GetName *) FPlatformProcess::GetDllExport( DynamicLib, TEXT("OodleTex_BC_GetName") );
		check( fp_OodleTex_BC_GetName != nullptr );
		
		fp_OodleTex_RDO_UniversalTiling_GetName = (t_fp_OodleTex_RDO_UniversalTiling_GetName *) FPlatformProcess::GetDllExport( DynamicLib, TEXT("OodleTex_RDO_UniversalTiling_GetName") );
		check( fp_OodleTex_RDO_UniversalTiling_GetName != nullptr );
		
		fp_OodleTex_BC_BytesPerBlock = (t_fp_OodleTex_BC_BytesPerBlock *) FPlatformProcess::GetDllExport( DynamicLib, TEXT("OodleTex_BC_BytesPerBlock") );
		check( fp_OodleTex_BC_BytesPerBlock != nullptr );
		
		fp_OodleTex_PixelFormat_BytesPerPixel = (t_fp_OodleTex_PixelFormat_BytesPerPixel *) FPlatformProcess::GetDllExport( DynamicLib, TEXT("OodleTex_PixelFormat_BytesPerPixel") );
		check( fp_OodleTex_PixelFormat_BytesPerPixel != nullptr );

		return true;
	}

	~FOodleTextureVTable()
	{
		if ( DynamicLib )
		{
			FPlatformProcess::FreeDllHandle(DynamicLib);
			DynamicLib = nullptr;
		}
	}
};


class FOodleTextureBuildFunction final : public FTextureBuildFunction
{
	const UE::DerivedData::FUtf8SharedString& GetName() const final
	{
		static const UE::DerivedData::FUtf8SharedString Name(UTF8TEXTVIEW("OodleTexture"));
		return Name;
	}

	void GetVersion(UE::DerivedData::FBuildVersionBuilder& Builder, ITextureFormat*& OutTextureFormatVersioning) const final
	{
		static FGuid Version(TEXT("e6b8884f-923a-44a1-8da1-298fb48865b2"));
		Builder << Version;
		OutTextureFormatVersioning = FModuleManager::GetModuleChecked<ITextureFormatModule>(TEXT("TextureFormatOodle")).GetTextureFormat();
	}
};



struct FOodlePixelFormatMapping 
{
	OodleDDS::EDXGIFormat DXGIFormat;
	OodleTex_PixelFormat OodlePF;
	bool bHasAlpha;
};

//  mapping from/to UNORM formats; sRGB-ness is handled separately.
// when there are multiple dxgi formats mapping to the same Oodle format, the first one is used
// for conversions from Oodle to DXGI
static FOodlePixelFormatMapping PixelFormatMap[] = 
{
	// dxgi											ootex								has_alpha
	{ OodleDDS::EDXGIFormat::R32G32B32A32_FLOAT,	OodleTex_PixelFormat_4_F32_RGBA,	true },
	{ OodleDDS::EDXGIFormat::R32G32B32_FLOAT,		OodleTex_PixelFormat_3_F32_RGB,		true },
	{ OodleDDS::EDXGIFormat::R16G16B16A16_FLOAT,	OodleTex_PixelFormat_4_F16_RGBA,	true },
	{ OodleDDS::EDXGIFormat::R8G8B8A8_UNORM,		OodleTex_PixelFormat_4_U8_RGBA,		true },
	{ OodleDDS::EDXGIFormat::R16G16B16A16_UNORM,	OodleTex_PixelFormat_4_U16,			true },
	{ OodleDDS::EDXGIFormat::R16G16_UNORM,			OodleTex_PixelFormat_2_U16,			false },
	{ OodleDDS::EDXGIFormat::R16G16_SNORM,			OodleTex_PixelFormat_2_S16,			false },
	{ OodleDDS::EDXGIFormat::R8G8_UNORM,			OodleTex_PixelFormat_2_U8,			false },
	{ OodleDDS::EDXGIFormat::R8G8_SNORM,			OodleTex_PixelFormat_2_S8,			false },
	{ OodleDDS::EDXGIFormat::R16_UNORM,				OodleTex_PixelFormat_1_U16,			false },
	{ OodleDDS::EDXGIFormat::R16_SNORM,				OodleTex_PixelFormat_1_S16,			false },
	{ OodleDDS::EDXGIFormat::R8_UNORM,				OodleTex_PixelFormat_1_U8,			false },
	{ OodleDDS::EDXGIFormat::R8_SNORM,				OodleTex_PixelFormat_1_S8,			false },
	{ OodleDDS::EDXGIFormat::B8G8R8A8_UNORM,		OodleTex_PixelFormat_4_U8_BGRA,		true },
	{ OodleDDS::EDXGIFormat::B8G8R8X8_UNORM,		OodleTex_PixelFormat_4_U8_BGRx,		false },
};

static OodleTex_PixelFormat OodlePFFromDXGIFormat(OodleDDS::EDXGIFormat InFormat) 
{
	InFormat = OodleDDS::DXGIFormatRemoveSRGB(InFormat);
	for (size_t i = 0; i < sizeof(PixelFormatMap) / sizeof(*PixelFormatMap); ++i) 
	{
		if (PixelFormatMap[i].DXGIFormat == InFormat) 
		{
			return PixelFormatMap[i].OodlePF;
		}
	}
	return OodleTex_PixelFormat_Invalid;
}

// don't need this for all DXGI formats, just the ones we can translate to Oodle Texture formats
static bool DXGIFormatHasAlpha(OodleDDS::EDXGIFormat InFormat)
{
	InFormat = OodleDDS::DXGIFormatRemoveSRGB(InFormat);
	for (size_t i = 0; i < sizeof(PixelFormatMap) / sizeof(*PixelFormatMap); ++i) 
	{
		if (PixelFormatMap[i].DXGIFormat == InFormat)
		{
			return PixelFormatMap[i].bHasAlpha;
		}
	}
	// when we don't know the format, the answer doesn't really matter; let's just say "yes"
	return true;
}

static OodleDDS::EDXGIFormat DXGIFormatFromOodlePF(OodleTex_PixelFormat pf) 
{
	for (size_t i = 0; i < sizeof(PixelFormatMap) / sizeof(*PixelFormatMap); ++i) 
	{
		if (PixelFormatMap[i].OodlePF == pf) 
		{
			return PixelFormatMap[i].DXGIFormat;
		}
	}
	return OodleDDS::EDXGIFormat::UNKNOWN;
}

struct FOodleBCMapping 
{
	OodleDDS::EDXGIFormat DXGIFormat;
	OodleTex_BC OodleBC;
};

static FOodleBCMapping BCFormatMap[] = 
{
	{ OodleDDS::EDXGIFormat::BC1_UNORM, OodleTex_BC1 },
	{ OodleDDS::EDXGIFormat::BC1_UNORM, OodleTex_BC1_WithTransparency },
	{ OodleDDS::EDXGIFormat::BC2_UNORM, OodleTex_BC2 },
	{ OodleDDS::EDXGIFormat::BC3_UNORM, OodleTex_BC3 },
	{ OodleDDS::EDXGIFormat::BC4_UNORM, OodleTex_BC4U },
	{ OodleDDS::EDXGIFormat::BC4_SNORM, OodleTex_BC4S },
	{ OodleDDS::EDXGIFormat::BC5_UNORM, OodleTex_BC5U },
	{ OodleDDS::EDXGIFormat::BC5_SNORM, OodleTex_BC5S },
	{ OodleDDS::EDXGIFormat::BC6H_UF16, OodleTex_BC6U },
	{ OodleDDS::EDXGIFormat::BC6H_SF16, OodleTex_BC6S },
	{ OodleDDS::EDXGIFormat::BC7_UNORM, OodleTex_BC7RGBA },
	{ OodleDDS::EDXGIFormat::BC7_UNORM, OodleTex_BC7RGB },
};

static OodleTex_BC OodleBCFromDXGIFormat(OodleDDS::EDXGIFormat InFormat) 
{
	InFormat = OodleDDS::DXGIFormatRemoveSRGB(InFormat);
	for (size_t i = 0; i < sizeof(BCFormatMap) / sizeof(*BCFormatMap); ++i) 
	{
		if (BCFormatMap[i].DXGIFormat == InFormat)
		{
			return BCFormatMap[i].OodleBC;
		}
	}
	return OodleTex_BC_Invalid;
}

static OodleDDS::EDXGIFormat DXGIFormatFromOodleBC(OodleTex_BC InBC) 
{
	for (size_t i = 0; i < sizeof(BCFormatMap) / sizeof(*BCFormatMap); ++i) 
	{
		if (BCFormatMap[i].OodleBC == InBC)
		{
			return BCFormatMap[i].DXGIFormat;
		}
	}
	return OodleDDS::EDXGIFormat::UNKNOWN;
}


static void TFO_Plugins_Init();
static void TFO_Plugins_Install(const FOodleTextureVTable * VTable);

// user data passed to Oodle Jobify system
static int OodleJobifyNumThreads = 0;
static void *OodleJobifyUserPointer = nullptr;
static bool OodleJobifyUseExampleJobify = false;

// enable this to make the DDC key unique (per build) for testing
//#define DO_FORCE_UNIQUE_DDC_KEY_PER_BUILD

#define ENUSUPPORTED_FORMATS(op) \
    op(DXT1) \
    op(DXT3) \
    op(DXT5) \
    op(DXT5n) \
    op(AutoDXT) \
    op(BC4) \
    op(BC5) \
	op(BC6H) \
	op(BC7)

// register support for TFO_ prefixed names like "TFO_DXT1"
#define TEXTURE_FORMAT_PREFIX	"TFO_"
#define DECL_FORMAT_NAME(FormatName) static FName GTextureFormatName##FormatName = FName(TEXT(TEXTURE_FORMAT_PREFIX #FormatName));

ENUSUPPORTED_FORMATS(DECL_FORMAT_NAME);
#undef DECL_FORMAT_NAME

#define DECL_FORMAT_NAME_ENTRY(FormatName) GTextureFormatName##FormatName ,
static FName GSupportedTextureFormatNames[] =
{
	ENUSUPPORTED_FORMATS(DECL_FORMAT_NAME_ENTRY)
};
#undef DECL_FORMAT_NAME_ENTRY
#undef ENUSUPPORTED_FORMATS

class FTextureFormatOodleConfig
{
public:
	struct FLocalDebugConfig
	{
		FLocalDebugConfig() :
			LogVerbosity(0)
		{
		}

		FString DebugDumpFilter; // dump textures that were encoded
		int LogVerbosity; // 0-2 ; 0=never, 1=large only, 2=always
	};

	FTextureFormatOodleConfig() :
		bForceAllBC23ToBC7(false),
		bDebugColor(false),
		GlobalLambdaMultiplier(1.f)
	{
	}

	const FLocalDebugConfig& GetLocalDebugConfig() const
	{
		return LocalDebugConfig;
	}	

	void ImportFromConfigCache()
	{
		const TCHAR* IniSection = TEXT("TextureFormatOodleSettings");
		
		//
		// Note that while this gets called during singleton init for the module,
		// the INIs don't exist when we're being run as a texture build worker,
		// so all of these GConfig calls do nothing.
		// 
		
		// Class config variables
		GConfig->GetBool(IniSection, TEXT("bForceAllBC23ToBC7"), bForceAllBC23ToBC7, GEngineIni);
		GConfig->GetBool(IniSection, TEXT("bDebugColor"), bDebugColor, GEngineIni);
		GConfig->GetString(IniSection, TEXT("DebugDumpFilter"), LocalDebugConfig.DebugDumpFilter, GEngineIni);
		GConfig->GetInt(IniSection, TEXT("LogVerbosity"), LocalDebugConfig.LogVerbosity, GEngineIni);
		GConfig->GetFloat(IniSection, TEXT("GlobalLambdaMultiplier"), GlobalLambdaMultiplier, GEngineIni);


		// sanitize config values :
		if ( GlobalLambdaMultiplier <= 0.f )
		{
			GlobalLambdaMultiplier = 1.f;
		}

		UE_LOG(LogTextureFormatOodle, Display, TEXT("Oodle Texture TFO init; latest sdk version = %s"),
			TEXT(OodleTextureVersion)
			);
		#ifdef DO_FORCE_UNIQUE_DDC_KEY_PER_BUILD
		UE_LOG(LogTextureFormatOodle, Display, TEXT("Oodle Texture DO_FORCE_UNIQUE_DDC_KEY_PER_BUILD"));
		#endif
	}

	FCbObject ExportToCb(const FTextureBuildSettings& BuildSettings) const
	{
		//
		// Here we write config stuff to the packet that gets sent to the build
		// workers.
		//
		// This is only for stuff that isn't already part of the build settings.
		//

		FCbWriter Writer;
		Writer.BeginObject("TextureFormatOodleSettings");

		if ((BuildSettings.TextureFormatName == GTextureFormatNameDXT3) ||
			(BuildSettings.TextureFormatName == GTextureFormatNameDXT5) ||
			(BuildSettings.TextureFormatName == GTextureFormatNameDXT5n) ||
			(BuildSettings.TextureFormatName == GTextureFormatNameAutoDXT) )
		{
			Writer.AddBool("bForceAllBC23ToBC7", bForceAllBC23ToBC7);
		}
		if (bDebugColor)
		{
			Writer.AddBool("bDebugColor", bDebugColor);
		}
		if (GlobalLambdaMultiplier != 1.f)
		{
			Writer.AddFloat("GlobalLambdaMultipler", GlobalLambdaMultiplier);
		}

		Writer.EndObject();

		return Writer.Save().AsObject();
	}

	void GetOodleCompressParameters(EPixelFormat * OutCompressedPixelFormat,int * OutRDOLambda, OodleTex_EncodeEffortLevel * OutEffortLevel, bool * bOutDebugColor, OodleTex_RDO_UniversalTiling* OutRDOUniversalTiling, const struct FTextureBuildSettings& InBuildSettings, bool bHasAlpha) const
	{
		FName TextureFormatName = InBuildSettings.TextureFormatName;

		EPixelFormat CompressedPixelFormat = PF_Unknown;
		if (TextureFormatName == GTextureFormatNameDXT1)
		{
			CompressedPixelFormat = PF_DXT1;
		}
		else if (TextureFormatName == GTextureFormatNameDXT3)
		{
			CompressedPixelFormat = PF_DXT3;
		}
		else if (TextureFormatName == GTextureFormatNameDXT5)
		{
			CompressedPixelFormat = PF_DXT5;
		}
		else if (TextureFormatName == GTextureFormatNameAutoDXT)
		{
			//not all "AutoDXT" comes in here
			// some AutoDXT is converted to "DXT1" before it gets here
			//	(by GetDefaultTextureFormatName if "compress no alpha" is set)

			// if you set bForceAllBC23ToBC7, the DXT5 will change to BC7
			CompressedPixelFormat = bHasAlpha ? PF_DXT5 : PF_DXT1;
		}
		else if (TextureFormatName == GTextureFormatNameDXT5n)
		{
			// Unreal already has global UseDXT5NormalMap config option
			// EngineSettings.GetString(TEXT("SystemSettings"), TEXT("Compat.UseDXT5NormalMaps")
			//	if that is false (which is the default) they use BC5
			// so this should be rarely use
			// (we prefer BC5 over DXT5n)
			CompressedPixelFormat = PF_DXT5;
		}
		else if (TextureFormatName == GTextureFormatNameBC4)
		{
			CompressedPixelFormat = PF_BC4;
		}
		else if (TextureFormatName == GTextureFormatNameBC5)
		{
			CompressedPixelFormat = PF_BC5;
		}
		else if (TextureFormatName == GTextureFormatNameBC6H)
		{
			CompressedPixelFormat = PF_BC6H;
		}
		else if (TextureFormatName == GTextureFormatNameBC7)
		{
			CompressedPixelFormat = PF_BC7;
		}
		else
		{
			UE_LOG(LogTextureFormatOodle,Fatal,
				TEXT("Unsupported TextureFormatName for compression: %s"),
				*TextureFormatName.ToString()
				);
		}
		
		// BC7 is just always better than BC2 & BC3
		//	so anything that came through as BC23, force to BC7 : (AutoDXT-alpha and Normals)
		// Note that we are using the value from the FormatConfigOverride if we have one, otherwise the default will be the value we have locally
		if ( InBuildSettings.FormatConfigOverride.FindView("bForceAllBC23ToBC7").AsBool(bForceAllBC23ToBC7) &&
			(CompressedPixelFormat == PF_DXT3 || CompressedPixelFormat == PF_DXT5 ) )
		{
			CompressedPixelFormat = PF_BC7;
		}

		*OutCompressedPixelFormat = CompressedPixelFormat;

		// Use the DDC2 provided value if it exists.
		bool bUseDebugColor = InBuildSettings.FormatConfigOverride.FindView("bDebugColor").AsBool(bDebugColor);

		float UseGlobalLambdaMultiplier = InBuildSettings.FormatConfigOverride.FindView("GlobalLambdaMultipler").AsFloat(GlobalLambdaMultiplier);

		//
		// Convert general build settings in to oodle relevant values.
		//
		int RDOLambda = InBuildSettings.OodleRDO;
		if (RDOLambda > 0 && UseGlobalLambdaMultiplier != 1.f)
		{
			RDOLambda = (int)(UseGlobalLambdaMultiplier * RDOLambda + 0.5f );
			// don't let it change to 0 :
			if ( RDOLambda <= 0 )
			{
				RDOLambda = 1;
			}
		}

		RDOLambda = FMath::Clamp(RDOLambda,0,100);

		// EffortLevel might be set to faster modes for previewing vs cooking or something
		//	but I don't see people setting that per-Texture or in lod groups or any of that
		//  it's more about cook mode (fast vs final bake)	
		
		// Note InBuildSettings.OodleEncodeEffort is an ETextureEncodeEffort
		//  we cast directly to OodleTex_EncodeEffortLevel
		//  the enum values must match exactly

		OodleTex_EncodeEffortLevel EffortLevel = (OodleTex_EncodeEffortLevel)InBuildSettings.OodleEncodeEffort;
		if (EffortLevel != OodleTex_EncodeEffortLevel_Default &&
			EffortLevel != OodleTex_EncodeEffortLevel_Low &&
			EffortLevel != OodleTex_EncodeEffortLevel_Normal &&
			EffortLevel != OodleTex_EncodeEffortLevel_High)
		{
			UE_LOG(LogTextureFormatOodle, Warning, TEXT("Invalid effort level passed to texture format oodle: %d is invalid, using default"), (uint32)EffortLevel);
			EffortLevel = OodleTex_EncodeEffortLevel_Default;
		}

		// map Unreal ETextureUniversalTiling to OodleTex_RDO_UniversalTiling
		//  enum values must match exactly
		OodleTex_RDO_UniversalTiling UniversalTiling = (OodleTex_RDO_UniversalTiling)InBuildSettings.OodleUniversalTiling;
		if ( UniversalTiling != OodleTex_RDO_UniversalTiling_Disable &&
			UniversalTiling != OodleTex_RDO_UniversalTiling_256KB &&
			UniversalTiling != OodleTex_RDO_UniversalTiling_64KB )
		{
			UE_LOG(LogTextureFormatOodle, Warning, TEXT("Invalid universal tiling value passed to texture format oodle: %d is invalid, disabling"), (uint32)UniversalTiling);
			UniversalTiling = OodleTex_RDO_UniversalTiling_Disable;
		}

		if (RDOLambda == 0)
		{
			// Universal tiling doesn't make sense without RDO.
			UniversalTiling = OodleTex_RDO_UniversalTiling_Disable;
		}

		#if 0
		// leave this if 0 block for developers to toggle for debugging
		// Debug Color any non-RDO
		//  easy way to make sure you're seeing RDO textures
		if (RDOLambda == 0)
		{
			bUseDebugColor = true;
		}
		#endif

		*bOutDebugColor = bUseDebugColor;
		*OutRDOLambda = RDOLambda;
		*OutEffortLevel = EffortLevel;
		*OutRDOUniversalTiling = UniversalTiling;
	}

private:
	// the sense of these bools is set so that default behavior = all false
	bool bForceAllBC23ToBC7; // change BC2 & 3 (aka DXT3 and DXT5) to BC7 
	bool bDebugColor; // color textures by their BCN, for data discovery
	// after lambda is set, multiply by this scale factor :
	//	(multiplies the default and per-Texture overrides)
	//	is intended to let you do last minute whole-game adjustment
	float GlobalLambdaMultiplier;
	FLocalDebugConfig LocalDebugConfig;
};

class FTextureFormatOodle : public ITextureFormat
{
public:

	FTextureFormatOodleConfig GlobalFormatConfig;
	TArray<FOodleTextureVTable> VTables;
	FName OodleTextureVersionLatest;
	FName OodleTextureSdkVersionToUseIfNone;

	FTextureFormatOodle() : OodleTextureVersionLatest(OodleTextureVersion),
		OodleTextureSdkVersionToUseIfNone("2.9.5")
	{
		// OodleTextureSdkVersionToUseIfNone is the fallback version to use if none is in the Texture uasset
		//	and also no remap pref is set
		// it should not be latest; it should be oldest (2.9.5)
		//  OodleTextureSdkVersionToUseIfNone should never be changed
	}


	virtual ~FTextureFormatOodle()
	{
	}
	
	virtual bool AllowParallelBuild() const override
	{
		return true;
	}

	virtual bool SupportsEncodeSpeed(FName Format) const override
	{
		return true;
	}

	virtual FName GetEncoderName(FName Format) const override
	{
		static const FName OodleName("EngineOodle");
		return OodleName;
	}
	
	virtual FCbObject ExportGlobalFormatConfig(const FTextureBuildSettings& BuildSettings) const override
	{
		return GlobalFormatConfig.ExportToCb(BuildSettings);
	}

	void Init()
	{
		TFO_Plugins_Init();

		// this is done at Singleton init time, the first time GetTextureFormat() is called
		GlobalFormatConfig.ImportFromConfigCache();

		// load ALL Oodle DLL versions we support :
		// !! add new versions of Oodle here !!
		VTables.SetNum(1);

		VTables[0].LoadDynamicLib( FString(TEXT("2.9.5")) );
		TFO_Plugins_Install(&(VTables[0]));
	}
	
	const FOodleTextureVTable * GetOodleTextureVTable(const FName & InVersion) const
	{
		for(int i=0;i<VTables.Num();i++)
		{
			if ( VTables[i].Version == InVersion )
			{
				return &(VTables[i]);
			}
		}

		return nullptr;
	}
	
	// increment this to invalidate Derived Data Cache to recompress everything
	#define DDC_OODLE_TEXTURE_VERSION 13

	virtual uint16 GetVersion(FName Format, const FTextureBuildSettings* InBuildSettings) const override
	{
		// note: InBuildSettings == NULL is used by GetVersionFormatNumbersForIniVersionStrings
		//	just to get a displayable version number

		return DDC_OODLE_TEXTURE_VERSION; 
	}
	
	virtual FString GetAlternateTextureFormatPrefix() const override
	{
		static const FString Prefix(TEXT(TEXTURE_FORMAT_PREFIX));
		return Prefix;
	}

	virtual FName GetLatestSdkVersion() const override
	{
		return OodleTextureVersionLatest;
	}

	virtual FString GetDerivedDataKeyString(const FTextureBuildSettings& InBuildSettings) const override
	{
		// return all parameters that affect our output Texture
		// so if any of them change, we rebuild
		
		int RDOLambda;
		OodleTex_EncodeEffortLevel EffortLevel;
		OodleTex_RDO_UniversalTiling RDOUniversalTiling;
		EPixelFormat CompressedPixelFormat;
		bool bDebugColor;

		// @todo Oodle this is not quite the same "bHasAlpha" that Compress will see
		//	bHasAlpha is used for AutoDXT -> DXT1/5
		//	we do have Texture.bForceNoAlphaChannel/CompressionNoAlpha but that's not quite what we want
		// do go ahead and read bForceNoAlphaChannel/CompressionNoAlpha so that we invalidate DDC when that changes
		bool bHasAlpha = !InBuildSettings.bForceNoAlphaChannel; 
		
		GlobalFormatConfig.GetOodleCompressParameters(&CompressedPixelFormat,&RDOLambda,&EffortLevel,&bDebugColor,&RDOUniversalTiling,InBuildSettings,bHasAlpha);

		int icpf = (int)CompressedPixelFormat;

		check(RDOLambda<256);
		if (bDebugColor)
		{
			RDOLambda = 256;
			EffortLevel = OodleTex_EncodeEffortLevel_Default;
		}
		
		FString DDCString = FString::Printf(TEXT("Oodle_CPF%d_L%d_E%d"), icpf, (int)RDOLambda, (int)EffortLevel);
		if (RDOUniversalTiling != OodleTex_RDO_UniversalTiling_Disable)
		{
			DDCString += FString::Printf(TEXT("_UT%d"), (int)RDOUniversalTiling);
		}

		// OodleTextureSdkVersion was added ; keys where OodleTextureSdkVersion is none are unchanged
		if ( ! InBuildSettings.OodleTextureSdkVersion.IsNone() )
		{
			DDCString += TEXT("_V");

			// concatenate VersionString without . characters which are illegal in DDC
			// version is something like "2.9.5" , we'll add something like "_V295"
			FString VersionString = InBuildSettings.OodleTextureSdkVersion.ToString();
			for(int32 i=0;i<VersionString.Len();i++)
			{
				if ( VersionString[i] != TEXT('.') )
				{
					DDCString += VersionString[i];					
				}
			}
		}

		#ifdef DO_FORCE_UNIQUE_DDC_KEY_PER_BUILD
		DDCString += TEXT(__DATE__);
		DDCString += TEXT(__TIME__);
		#endif

		return DDCString;
	}

	virtual void GetSupportedFormats(TArray<FName>& OutFormats) const override
	{
		OutFormats.Append(GSupportedTextureFormatNames, sizeof(GSupportedTextureFormatNames)/sizeof(GSupportedTextureFormatNames[0]) ); 
	}

	virtual FTextureFormatCompressorCaps GetFormatCapabilities() const override
	{
		return FTextureFormatCompressorCaps(); // Default capabilities.
	}
	
	virtual EPixelFormat GetPixelFormatForImage(const FTextureBuildSettings& InBuildSettings, const struct FImage& Image, bool bHasAlpha) const override
	{
		int RDOLambda;
		OodleTex_EncodeEffortLevel EffortLevel;
		OodleTex_RDO_UniversalTiling RDOUniversalTiling;
		EPixelFormat CompressedPixelFormat;
		bool bDebugColor;

		GlobalFormatConfig.GetOodleCompressParameters(&CompressedPixelFormat,&RDOLambda,&EffortLevel,&bDebugColor,&RDOUniversalTiling,InBuildSettings,bHasAlpha);
		return CompressedPixelFormat;
	}

	static void DebugDumpDDS(const FStringView & DebugTexturePathName,
			int32 SizeX,int32 SizeY,int32 Slice, OodleDDS::EDXGIFormat DebugFormat, const TCHAR * InOrOut,
			const void * PixelData, size_t PixelDataSize)
	{
		if (DebugFormat != OodleDDS::EDXGIFormat::UNKNOWN)
		{
			OodleDDS::FDDSFile* DDS = OodleDDS::FDDSFile::CreateEmpty2D(SizeX, SizeY, 1, DebugFormat, OodleDDS::FDDSFile::CREATE_FLAG_NONE);

			if ( DDS->Mips[0].DataSize != PixelDataSize )
			{
				UE_LOG(LogTextureFormatOodle, Warning, TEXT("DebugDump mip sizes don't match %dx%d: %lld != %lld"), SizeX,SizeY, DDS->Mips[0].DataSize, PixelDataSize);
			}
			size_t MipCopySize = FMath::Min(PixelDataSize,(size_t)DDS->Mips[0].DataSize);
			FMemory::Memcpy(DDS->Mips[0].Data, PixelData, MipCopySize);

			FString FileName = FString::Printf(TEXT("%.*s_%dx%d_S%d_%s.dds"), DebugTexturePathName.Len(), DebugTexturePathName.GetData(), SizeX, SizeY, Slice, InOrOut);

			// Object paths a) can contain slashes as its a path, and we dont want a hierarchy and b) can have random characters we don't want
			FileName = FPaths::MakeValidFileName(FileName, TEXT('_'));
			FileName = FPaths::ProjectSavedDir() + TEXT("OodleDebugImages/") + FileName;
				
			FArchive* Ar = IFileManager::Get().CreateFileWriter(*FileName);
			if (Ar != nullptr)
			{					
				DDS->SerializeToArchive(Ar);
				Ar->Close();
				delete Ar;
			}
			else
			{
				UE_LOG(LogTextureFormatOodle, Error, TEXT("Failed to open DDS debug file: %s"), *FileName);
			}

			delete DDS;
		}
	}

	virtual bool CompressImage(const FImage& InImage, const FTextureBuildSettings& InBuildSettings, FStringView DebugTexturePathName, const bool bInHasAlpha, FCompressedImage2D& OutImage) const override
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Oodle_CompressImage);

		check(InImage.SizeX > 0);
		check(InImage.SizeY > 0);
		check(InImage.NumSlices > 0);

		if ( InImage.SizeX > OODLETEX_MAX_SURFACE_DIMENSION || InImage.SizeY > OODLETEX_MAX_SURFACE_DIMENSION )
		{
			UE_LOG(LogTextureFormatOodle,Error,
				TEXT("Image larger than OODLETEX_MAX_SURFACE_DIMENSION : %dx%d > %d"),
				InImage.SizeX,InImage.SizeY,
				OODLETEX_MAX_SURFACE_DIMENSION
				);
			
			return false;			
		}

		FName CompressOodleTextureVersion(InBuildSettings.OodleTextureSdkVersion);

		if ( CompressOodleTextureVersion.IsNone() )
		{
			// legacy texture without version, and no remap is set up in prefs
			// use default:
			CompressOodleTextureVersion = OodleTextureSdkVersionToUseIfNone;
		}

		const FOodleTextureVTable * VTable = GetOodleTextureVTable(CompressOodleTextureVersion);
		if (VTable == nullptr)
		{
			UE_LOG(LogTextureFormatOodle,Error,
				TEXT("Unsupported OodleTextureSdkVersion: %s"),
				*CompressOodleTextureVersion.ToString()
				);
			
			return false;
		}

		// InImage always comes in as F32 in linear light
		//	(Unreal has just made mips in that format)
		// we are run simultaneously on all mips or VT tiles
		
		// bHasAlpha = DetectAlphaChannel , scans the A's for non-opaque , in in CompressMipChain
		//	used by AutoDXT
		bool bHasAlpha = bInHasAlpha;

		int RDOLambda;
		OodleTex_EncodeEffortLevel EffortLevel;
		OodleTex_RDO_UniversalTiling RDOUniversalTiling;
		EPixelFormat CompressedPixelFormat;
		bool bDebugColor;
		GlobalFormatConfig.GetOodleCompressParameters(&CompressedPixelFormat,&RDOLambda,&EffortLevel,&bDebugColor,&RDOUniversalTiling,InBuildSettings,bHasAlpha);

		OodleTex_BC OodleBCN = OodleTex_BC_Invalid;
		if ( CompressedPixelFormat == PF_DXT1 ) { OodleBCN = OodleTex_BC1_WithTransparency; bHasAlpha = false; }
		else if ( CompressedPixelFormat == PF_DXT3 ) { OodleBCN = OodleTex_BC2; }
		else if ( CompressedPixelFormat == PF_DXT5 ) { OodleBCN = OodleTex_BC3; }
		else if ( CompressedPixelFormat == PF_BC4 ) { OodleBCN = OodleTex_BC4U; }
		else if ( CompressedPixelFormat == PF_BC5 ) { OodleBCN = OodleTex_BC5U; }
		else if ( CompressedPixelFormat == PF_BC6H ) { OodleBCN = OodleTex_BC6U; }
		else if ( CompressedPixelFormat == PF_BC7 ) { OodleBCN = OodleTex_BC7RGBA; }
		else
		{
			UE_LOG(LogTextureFormatOodle,Fatal,
				TEXT("Unsupported CompressedPixelFormat for compression: %d"),
				(int)CompressedPixelFormat
				);
		}
		
		FName TextureFormatName = InBuildSettings.TextureFormatName;
		bool bIsVT = InBuildSettings.bVirtualStreamable;

		// LogVerbosity 0 : never
		// LogVerbosity 1 : only large mips
		// LogVerbosity 2 : always
		bool bIsLargeMip = InImage.SizeX >= 1024 || InImage.SizeY >= 1024;

		if ( GlobalFormatConfig.GetLocalDebugConfig().LogVerbosity >= 2 || (GlobalFormatConfig.GetLocalDebugConfig().LogVerbosity && bIsLargeMip) )
		{
			UE_LOG(LogTextureFormatOodle, Display, TEXT("%s encode %i x %i x %i to format %s%s (Oodle %s) lambda=%i effort=%i "), \
				RDOLambda ? TEXT("RDO") : TEXT("non-RDO"), InImage.SizeX, InImage.SizeY, InImage.NumSlices, 
				*TextureFormatName.ToString(),
				bIsVT ? TEXT(" VT") : TEXT(""),
				*FString((VTable->fp_OodleTex_BC_GetName)(OodleBCN)),
				RDOLambda, (int)EffortLevel);
		}

		// input Image comes in as F32 in linear light
		// for BC6 we just leave that alone
		// for all others we must convert to 8 bit to get Gamma correction
		// because Unreal only does Gamma correction on the 8 bit conversion
		//	(this loses precision for BC4,5 which would like 16 bit input)
		
		EGammaSpace Gamma = InBuildSettings.GetGammaSpace();		
		// note in unreal if Gamma == Pow22 due to legacy Gamma,
		//	we still want to encode to sRGB
		// (CopyTo does that even without this change, but let's make it explicit)
		if ( Gamma == EGammaSpace::Pow22 ) Gamma = EGammaSpace::sRGB;

		if ( ( OodleBCN == OodleTex_BC4U || OodleBCN == OodleTex_BC5U || OodleBCN == OodleTex_BC6U ) &&
			Gamma != EGammaSpace::Linear )
		{
			// BC4,5,6 should always be encoded to linear gamma
			
			UE_LOG(LogTextureFormatOodle, Display, TEXT("Image format %s (Oodle %s) encoded with non-Linear Gamma"), \
				*TextureFormatName.ToString(), *FString((VTable->fp_OodleTex_BC_GetName)(OodleBCN)) );
		}

		ERawImageFormat::Type ImageFormat;
		OodleTex_PixelFormat OodlePF;

		if (OodleBCN == OodleTex_BC6U)
		{
			ImageFormat = ERawImageFormat::RGBA32F;
			OodlePF = OodleTex_PixelFormat_4_F32_RGBA;
			// BC6 is assumed to be a linear-light HDR Image by default
			// use OodleTex_BCNFlag_BC6_NonRGBData if it is some other kind of data
			Gamma = EGammaSpace::Linear;
		}
		else if ((OodleBCN == OodleTex_BC4U || OodleBCN == OodleTex_BC5U) &&
			Gamma == EGammaSpace::Linear &&			
			!bDebugColor)
		{
			// for BC4/5 use 16-bit :
			//	BC4/5 should always have linear gamma
			// @todo we only need 1 or 2 channel 16-bit, not all 4; use our own converter
			//	or just let our encoder take F32 input?
			ImageFormat = ERawImageFormat::RGBA16;
			OodlePF = OodleTex_PixelFormat_4_U16;
		}
		else
		{
			ImageFormat = ERawImageFormat::BGRA8;
			// if requested format was DXT1
			// Unreal assumes that will not encode any alpha channel in the source
			//	(Unreal's "compress without alpha" just selects DXT1)
			// the legacy NVTT behavior for DXT1 was to always encode opaque pixels
			// for DXT1 we use BC1_WithTransparency which will preserve the input A transparency bit
			//	so we need to force the A's to be 255 coming into Oodle
			//	so for DXT1 we force bHasAlpha = false
			// force Oodle to ignore input alpha :
			OodlePF = bHasAlpha ? OodleTex_PixelFormat_4_U8_BGRA : OodleTex_PixelFormat_4_U8_BGRx;
		}

		bool bNeedsImageCopy = ImageFormat != InImage.Format ||
			Gamma != InImage.GammaSpace ||
			(CompressedPixelFormat == PF_DXT5 && TextureFormatName == GTextureFormatNameDXT5n) ||
			bDebugColor;
		FImage ImageCopy;
		if (bNeedsImageCopy)
		{
			InImage.CopyTo(ImageCopy, ImageFormat, Gamma);
		}
		const FImage& Image = bNeedsImageCopy ? ImageCopy : InImage;

		// verify OodlePF matches Image :
		check( Image.GetBytesPerPixel() == (VTable->fp_OodleTex_PixelFormat_BytesPerPixel)(OodlePF) );
		
		OodleTex_Surface InSurf = {};
		InSurf.width  = Image.SizeX;
		InSurf.height = Image.SizeY;
		InSurf.pixels = 0;
		InSurf.rowStrideBytes = Image.GetBytesPerPixel() * Image.SizeX;

		SSIZE_T InBytesPerSlice = InSurf.rowStrideBytes * Image.SizeY;
		uint8 * ImageBasePtr = (uint8 *) &(Image.RawData[0]);

		SSIZE_T InBytesTotal = InBytesPerSlice * Image.NumSlices;
		check( Image.RawData.Num() == InBytesTotal );
		

		if ( CompressedPixelFormat == PF_DXT5 &&
			TextureFormatName == GTextureFormatNameDXT5n)
		{
			// this is only used if Compat.UseDXT5NormalMaps

			// normal map comes in as RG , B&A can be ignored
			// in the optional use BC5 path, only the source RG pass through
			// normal was in RG , move to GA
			if ( OodlePF == OodleTex_PixelFormat_4_U8_BGRx )
			{
				OodlePF = OodleTex_PixelFormat_4_U8_BGRA;
			}
			check( OodlePF == OodleTex_PixelFormat_4_U8_BGRA );

			for(uint8 * ptr = ImageBasePtr; ptr < (ImageBasePtr + InBytesTotal); ptr += 4)
			{
				// ptr is BGRA
				ptr[3] = ptr[2];
				// match what NVTT does, it sets R=FF and B=0
				// NVTT also sets weight=0 for B so output B is undefined
				//   but output R is preserved at 1.f
				ptr[0] = 0xFF;
				ptr[2] = 0;
			}
		}

		if ( bDebugColor )
		{
			// fill Texture with solid color based on which BCN we would have output
			//   checker board if RDO
			// lets you visually identify BCN textures in the Editor or game
			const bool IsRDO = RDOLambda != 0;
			constexpr SSIZE_T CheckerSizeBits = 4;
			const SSIZE_T NumSlices = Image.NumSlices;
			const SSIZE_T SizeY = Image.SizeY;
			const SSIZE_T SizeX = Image.SizeX;

			// use fast encoding settings for debug color :
			RDOLambda = 0;
			EffortLevel = OodleTex_EncodeEffortLevel_Low;

			if ( OodlePF == OodleTex_PixelFormat_4_F32_RGBA )
			{
				//BC6 = purple
				check(OodleBCN == OodleTex_BC6U);

				if (IsRDO)
				{
					// debug color with checker board
					for (SSIZE_T Slice = 0; Slice < NumSlices; Slice++)
					{
						float* LineBase = (float*)(ImageBasePtr + InBytesPerSlice * Slice);
						for (SSIZE_T Y = 0; Y < SizeY; Y++, LineBase += (InSurf.rowStrideBytes / sizeof(float)))
						{
							for (SSIZE_T X = 0; X < SizeX; X++)
							{
								float* Pixel = LineBase + 4 * X;

								SSIZE_T GridOnY = Y & (1 << CheckerSizeBits);
								SSIZE_T GridOnX = X & (1 << CheckerSizeBits);
								SSIZE_T GridOn = GridOnX ^ GridOnY;

								if (GridOn)
								{
									Pixel[0] = 0.5f;
									Pixel[1] = 0;
									Pixel[2] = 0.8f;
									Pixel[3] = 1.f;
								}
								else
								{
									Pixel[0] = 1.0f;
									Pixel[1] = 1.0f;
									Pixel[2] = 1.0f;
									Pixel[3] = 1.f;
								}
							}
						} // each line
					} // each slice
				} // end if RDO
				else
				{
					for(float * ptr = (float *) ImageBasePtr; ptr < (float *)(ImageBasePtr + InBytesTotal); ptr += 4)
					{
						// RGBA floats
						ptr[0] = 0.5f;
						ptr[1] = 0;
						ptr[2] = 0.8f;
						ptr[3] = 1.f;
					}
				}
			}
			else
			{
				check( OodlePF == OodleTex_PixelFormat_4_U8_BGRA || OodlePF == OodleTex_PixelFormat_4_U8_BGRx );
				
				// BGRA in bytes
				uint32 DebugColor = 0xFF000000U; // alpha
				switch(OodleBCN)
				{
					case OodleTex_BC1_WithTransparency:
					case OodleTex_BC1: DebugColor |= 0xFF0000; break; // BC1 = red
					case OodleTex_BC2: DebugColor |= 0x008000; break; // BC2/3 = greens
					case OodleTex_BC3: DebugColor |= 0x00FF00; break;
					case OodleTex_BC4S:
					case OodleTex_BC4U: DebugColor |= 0x808000; break; // BC4/5 = yellows
					case OodleTex_BC5S: 
					case OodleTex_BC5U: DebugColor |= 0xFFFF00; break;
					case OodleTex_BC7RGB: DebugColor |= 0x8080FF; break; // BC7 = blues
					case OodleTex_BC7RGBA: DebugColor |= 0x0000FF; break;
					default: break;
				}

				if (IsRDO)
				{
					// debug color with checker board
					for (SSIZE_T Slice = 0; Slice < NumSlices; Slice++)
					{
						uint8* LineBase = ImageBasePtr + InBytesPerSlice * Slice;;
						for (SSIZE_T Y = 0; Y < SizeY; Y++, LineBase += InSurf.rowStrideBytes)
						{
							for (SSIZE_T X = 0; X < SizeX; X++)
							{
								uint8* Pixel = LineBase + 4 * X;

								SSIZE_T GridOnY = Y & (1 << CheckerSizeBits);
								SSIZE_T GridOnX = X & (1 << CheckerSizeBits);
								SSIZE_T GridOn = GridOnX ^ GridOnY;

								if (GridOn)
								{
									*((uint32*)Pixel) = DebugColor;
								}
								else
								{
									*((uint32*)Pixel) = 0xFF000000;
								}
							}
						} // each line
					} // each slice
				} // end if RDO
				else
				{
					for(uint8 * ptr = ImageBasePtr; ptr < (ImageBasePtr + InBytesTotal); ptr += 4)
					{
						*((uint32 *)ptr) = DebugColor;
					}
				}
			}			
		}

		int BytesPerBlock = (VTable->fp_OodleTex_BC_BytesPerBlock)(OodleBCN);
		int NumBlocksX = (Image.SizeX + 3)/4;
		int NumBlocksY = (Image.SizeY + 3)/4;
		OO_SINTa NumBlocksPerSlice = NumBlocksX * NumBlocksY;
		OO_SINTa OutBytesPerSlice = NumBlocksPerSlice * BytesPerBlock;
		OO_SINTa OutBytesTotal = OutBytesPerSlice * Image.NumSlices;

		OutImage.PixelFormat = CompressedPixelFormat;
		OutImage.SizeX = NumBlocksX*4;
		OutImage.SizeY = NumBlocksY*4;
		// note: cubes come in as 6 slices and go out as 1
		OutImage.SizeZ = (InBuildSettings.bVolume || InBuildSettings.bTextureArray) ? Image.NumSlices : 1;
		OutImage.RawData.AddUninitialized(OutBytesTotal);


		uint8 * OutBlocksBasePtr = (uint8 *) &OutImage.RawData[0];

		// Check if we want to dump the before/after images out.
		bool bImageDump = false;
		if (GlobalFormatConfig.GetLocalDebugConfig().DebugDumpFilter.Len() && 
			!bDebugColor && // don't bother if they are solid color
			(Image.SizeX >= 4 || Image.SizeY >= 4)) // don't bother if they are too small.
		{
			if (FWildcardString::IsMatchSubstring(*GlobalFormatConfig.GetLocalDebugConfig().DebugDumpFilter, DebugTexturePathName.GetData(), DebugTexturePathName.GetData() + DebugTexturePathName.Len(), ESearchCase::IgnoreCase))
			{
				bImageDump = true;
			}
		}

		int CurJobifyNumThreads = OodleJobifyNumThreads;
		void* CurJobifyUserPointer = OodleJobifyUserPointer;

		if (bIsVT)
		{
			// VT runs its tiles in a ParallelFor on the TaskGraph
			//   We internally also make tasks on TaskGraph
			//   it should not deadlock to do tasks from tasks, but it's not handled well
			//   parallelism at the VT tile level only works better
			// disable our own internal threading for VT tiles :
			CurJobifyNumThreads = OODLETEX_JOBS_DISABLE;
			CurJobifyUserPointer = nullptr;
		}

		// encode each slice
		// @todo Oodle alternatively could do [Image.NumSlices] array of OodleTex_Surface
		//	and call OodleTex_Encode with the array
		//  would be slightly better for parallelism with multi-slice images & cube maps
		//	that's a rare case so don't bother for now
		// (the main parallelism is from running many mips or VT tiles at once which is done by our caller)
		bool bCompressionSucceeded = true;
		for (int Slice = 0; Slice < Image.NumSlices; ++Slice)
		{
			InSurf.pixels = ImageBasePtr + Slice * InBytesPerSlice;
			uint8 * OutSlicePtr = OutBlocksBasePtr + Slice * OutBytesPerSlice;

			OodleTex_RDO_Options OodleOptions = { };
			OodleOptions.effort = EffortLevel;
			OodleOptions.metric = OodleTex_RDO_ErrorMetric_Default;
			OodleOptions.bcn_flags = OodleTex_BCNFlags_None;
			OodleOptions.universal_tiling = RDOUniversalTiling;

			if (bImageDump)
			{
				DebugDumpDDS(DebugTexturePathName,InSurf.width,InSurf.height,Slice,
					DXGIFormatFromOodlePF(OodlePF),TEXT("IN"),
					InSurf.pixels, InBytesPerSlice);
			}

			{
				TRACE_CPUPROFILER_EVENT_SCOPE(Oodle_EncodeBCN);

			// if RDOLambda == 0, does non-RDO encode :
			OodleTex_Err OodleErr = (VTable->fp_OodleTex_EncodeBCN_RDO_Ex)(OodleBCN, OutSlicePtr, NumBlocksPerSlice, 
					&InSurf, 1, OodlePF, NULL, RDOLambda, 
					&OodleOptions, CurJobifyNumThreads, CurJobifyUserPointer);

			if (OodleErr != OodleTex_Err_OK)
			{
				const char * OodleErrStr = (VTable->fp_OodleTex_Err_GetName)(OodleErr);
				UE_LOG(LogTextureFormatOodle, Display, TEXT("Oodle Texture encode failed!? %s"), OodleErrStr );
				bCompressionSucceeded = false;
				break;
			}
			}
			
			if (bImageDump)
			{
				DebugDumpDDS(DebugTexturePathName,InSurf.width,InSurf.height,Slice,
					DXGIFormatFromOodleBC(OodleBCN),TEXT("OUT"),
					OutSlicePtr, OutBytesPerSlice);
			}
		}

		return bCompressionSucceeded;
	}
};

//===============================================================

// TFO_ plugins to Oodle to run Oodle system services in Unreal
// @todo Oodle : factor this out and share for Core & Net some day

static OO_U64 OODLE_CALLBACK TFO_RunJob(t_fp_Oodle_Job* JobFunction, void* JobData, OO_U64* Dependencies, int NumDependencies, void* UserPtr)
{
	using namespace UE::Tasks;

	TRACE_CPUPROFILER_EVENT_SCOPE(Oodle_RunJob);
	
	TArray<Private::FTaskBase*> Prerequisites;
	Prerequisites.Reserve(NumDependencies);
	for (int DependencyIndex = 0; DependencyIndex < NumDependencies; DependencyIndex++)
	{
		Prerequisites.Add(reinterpret_cast<Private::FTaskBase*>(Dependencies[DependencyIndex]));
	}

	auto* Task{ new Private::FTaskBase };
	Task->Init(TEXT("OodleJob"), 
		[JobFunction, JobData]
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(OodleJob);
			JobFunction(JobData);
		}, 
		// Use Background priority so we don't use Foreground time in the Editor
		// @todo maybe it's better to inherit so the outer caller can tell us if we are high priority or not?
		IsInGameThread() ? ETaskPriority::Normal : ETaskPriority::BackgroundNormal
	);
	Task->AddPrerequisites(Prerequisites);
	Task->TryLaunch();

	return reinterpret_cast<uint64>(Task);
}

static void OODLE_CALLBACK TFO_WaitJob(OO_U64 JobHandle, void* UserPtr)
{
	using namespace UE::Tasks;

	TRACE_CPUPROFILER_EVENT_SCOPE(Oodle_WaitJob);

	Private::FTaskBase* Task = reinterpret_cast<Private::FTaskBase*>(JobHandle);
	Task->Wait();
	Task->Release();
}

static OO_BOOL OODLE_CALLBACK TFO_OodleAssert(const char* file, const int line, const char* function, const char* message)
{ 
	// AssertFailed exits the program
	FDebug::AssertFailed(message, file, line);

	// return true to issue a debug break at the execution site
	return true;
}

static void OODLE_CALLBACK TFO_OodleLog(int verboseLevel, const char* file, int line, const char* InFormat, ...)
{
	ANSICHAR TempString[1024];
	va_list Args;

	va_start(Args, InFormat);
	FCStringAnsi::GetVarArgs(TempString, UE_ARRAY_COUNT(TempString), InFormat, Args);
	va_end(Args);

	UE_LOG_CLINKAGE(LogTextureFormatOodle, Display, TEXT("Oodle Log: %s"), ANSI_TO_TCHAR(TempString));
}


static void* OODLE_CALLBACK TFO_OodleMallocAligned(OO_SINTa Bytes, OO_S32 Alignment)
{
	void * Ret = FMemory::Malloc(Bytes, Alignment);
	check( Ret != nullptr );
	return Ret;
}

static void OODLE_CALLBACK TFO_OodleFree(void* ptr)
{
	FMemory::Free(ptr);
}

// Init is only done once for all versions
static void TFO_Plugins_Init()
{
	// Install Unreal system plugins to OodleTex
	// this should only be done once
	// and should be done before any other Oodle calls
	// plugins to Core/Tex/Net are independent
	GConfig->GetBool(TEXT("TextureFormatOodleSettings"), TEXT("UseOodleExampleJobify"), OodleJobifyUseExampleJobify, GEngineIni);

	if (OodleJobifyUseExampleJobify)
	{
		UE_LOG(LogTextureFormatOodle, Display, TEXT("Using Oodle Example Jobify"));

		// Optionally we allow for users to use the internal Oodle job system instead of
		// thunking to the Unreal task graph.
		OodleJobifyUserPointer = example_jobify_init();
		OodleJobifyNumThreads = example_jobify_target_parallelism;
	}
	else
	{
		OodleJobifyUserPointer = nullptr;
		OodleJobifyNumThreads = FMath::Max(1, FTaskGraphInterface::Get().GetNumWorkerThreads());
	}
}

// Install is done for each Oodle DLL
static void TFO_Plugins_Install(const FOodleTextureVTable * VTable)
{
	if (OodleJobifyUseExampleJobify)
	{
		(VTable->fp_OodleTex_Plugins_SetJobSystemAndCount)(example_jobify_run_job_fptr, example_jobify_wait_job_fptr, example_jobify_target_parallelism);
	}
	else
	{
		(VTable->fp_OodleTex_Plugins_SetJobSystemAndCount)(TFO_RunJob, TFO_WaitJob, OodleJobifyNumThreads);
	}

	(VTable->fp_OodleTex_Plugins_SetAssertion)(TFO_OodleAssert);
	(VTable->fp_OodleTex_Plugins_SetPrintf)(TFO_OodleLog);
	(VTable->fp_OodleTex_Plugins_SetAllocators)(TFO_OodleMallocAligned, TFO_OodleFree);
}

//=========================================================================

static ITextureFormat* Singleton = nullptr;

class FTextureFormatOodleModule : public ITextureFormatModule
{
public:
	FTextureFormatOodleModule() { }
	virtual ~FTextureFormatOodleModule()
	{
		ITextureFormat * p = Singleton;
		Singleton = nullptr;
		if ( p )
			delete p;
	}

	virtual void StartupModule() override
	{
	}

	virtual ITextureFormat* GetTextureFormat()
	{
		// this is called twice
		
		if ( Singleton == nullptr ) // not thread safe
		{
			FTextureFormatOodle * ptr = new FTextureFormatOodle();
			ptr->Init();
			Singleton = ptr;
		}
		return Singleton;
	}

	static inline UE::DerivedData::TBuildFunctionFactory<FOodleTextureBuildFunction> BuildFunctionFactory;
};

IMPLEMENT_MODULE(FTextureFormatOodleModule, TextureFormatOodle);

