// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureFormatOodlePCH.h"
#include "CoreMinimal.h"
#include "ImageCore.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/ITextureFormat.h"
#include "Interfaces/ITextureFormatModule.h"
#include "TextureCompressorModule.h"
#include "Engine/TextureLODSettings.h"
#include "PixelFormat.h"
#include "DeviceProfiles/DeviceProfile.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Engine/Texture.h"
#include "Misc/ConfigCacheIni.h"
#include "Async/TaskGraphInterfaces.h"

#include "oodle2tex.h"

/**********

Oodle Texture can do both RDO (rate distortion optimization) and non-RDO encoding to BC1-7

by default this plugin enables RDO with a moderate quality level (lambda=30).
Set DefaultRDOLambda=40 for smaller compressed sizes.

quality can be controlled at three levels :

1. Each Texture can choose an individual setting with LossyCompressionAmount
2. If that is "Default", the setting is looked up in the LODGroup
3. If that is not set, the global default is used (DefaultRDOLambda)

Lambda up to 40 usually produces very high quality.  The need to for per-Texture adjustment should be rare
and is mainly for when textures are used in unusual ways (not just diffuse color or normal maps).

Oodle Texture can encode BC1-7.  It does not currently encode ASTC or other mobile formats.

=====================

TextureFormatOodle handles formats OODLE_DXT1,etc.

Use of this format (instead of DXT1) is enabled with TextureFormatPrefix in config, such as :

\Engine\Config\BaseEngine.ini

[AlternateTextureCompression]
TextureCompressionFormat="TextureFormatOodle"
TextureFormatPrefix="OODLE_"
bEnableInEditor=True

When this is enabled, the formats like "DXT1" are renamed to "OODLE_DXT1" and are handled by this encoder.

You can control whether Oodle Texture is used in the Editor with bEnableInEditor.  If this is false, non-RDO 
encoding will be done in the editor, so artists won't see the RDO results while editing, Oodle Texture will 
only be used in cooked builds in that case.

Oodle Texture RDO encoding can be slow, but is cached in the DDC so should only be slow the first time.  
A fast local network shared DDC is recommended.

The default setting currently has "bForceRDOOff=True" so Oodle Texture will be used, but non-RDO encoding will
be done, just like a traditional BCN encoder (maximizing quality without considering rate).  To enable RDO 
encoding set "bForceRDOOff=False".

========================


Oodle Texture Settings
----------------------

TextureFormatOodle reads settings from Engine.ini ; they're created by default
when not found.  Note they are created in per-platform Engine.ini, you can
find them and move them up to DefaultEngine if you want them to be global.

The INI settings block looks like :

[TextureFormatOodle]
bForceAllBC23ToBC7=False
bForceRDOOff=False
bDebugColor=False
DefaultRDOLambda=30
GlobalLambdaMultiplier=1.0

The sense of the bools is set so that all-false is default behavior.

TextureFormatOodle by default tries to exactly reproduce the legacy behavior of
TextureFormatDXT+TextureFormatISPC , just with Oodle Texture RDO encoding.

The behavior of the options is :

bForceAllBC23ToBC7 :

If true, all BC2 & 3 (DXT3 and DXT5) is encoded to BC7 instead.

On DX11 games, BC7 usualy has higher quality and takes the same space in memory as BC3.

For example in Unreal, "AutoDXT" selects DXT1 (BC1) for opaque textures and DXT5 (BC3)
for textures with alpha.  If you turn on this option, the BC3 will change to BC7, so
"AutoDXT" will now select BC1 for opaque and BC7 for alpha.

It is off by default to make default behavior match the old encoders.

bForceRDOOff :

Force Oodle Texture to use non-RDO encoding.  This sets lambda to 0 for all encodes.
(this is different than setting DefaultRDOLambda=0 because it also applies to textures
that have per-texture lambda overrides set)

bDebugColor :

Fills the encoded texture with a solid color depending on their BCN format.
This is a handy way to see that you are in fact getting Oodle Texture in your game.
It's also an easy way to spot textures that aren't BCN compressed, since they will not
be solid color.  (for example I found that lots of the Unreal demo content uses "HDR"
which is an uncompressed format, instead of "HDRCompressed" (BC6))  The color indicates
the actual compressed format output (BC1-7).

DefaultRDOLambda :

global default lambda value that is used if no per-texture lambda is set.
(see next section)

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
then it falls through to the global default, which is "DefaultRDOLambda" from our
INI block.  eg. for "WorldSpecular" above it would use the DefaultRDOLambda setting.

At each stage, TLCA_Default means "inherit from parent".

TLCA_None means disable RDO entirely.  We do not recommend this, use TLCA_Lowest 
instead when you need very high quality.

Note that the Unreal Editor texture dialog shows live compression results (if bEnableInEditor is true).
When you're in the editor and you adjust the LossyCompressionAmount or import a 
new texture, it shows the Oodle Texture encoded result in the texture preview. 



*********/


DEFINE_LOG_CATEGORY_STATIC(LogTextureFormatOodle, Log, All);

// user data passed to Oodle Jobify system
static int OodleJobifyNumThreads = 0;
static void *OodleJobifyUserPointer = nullptr;

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

// register support for OODLE_ prefixed names like "OODLE_DXT1"
#define DECL_FORMAT_NAME(FormatName) static FName GTextureFormatName##FormatName = FName(TEXT("OODLE_" #FormatName));

ENUSUPPORTED_FORMATS(DECL_FORMAT_NAME);
#undef DECL_FORMAT_NAME

#define DECL_FORMAT_NAME_ENTRY(FormatName) GTextureFormatName##FormatName ,
static FName GSupportedTextureFormatNames[] =
{
	ENUSUPPORTED_FORMATS(DECL_FORMAT_NAME_ENTRY)
};
#undef DECL_FORMAT_NAME_ENTRY
#undef ENUSUPPORTED_FORMATS

class FTextureFormatOodle : public ITextureFormat
{
public:

	// the sense of these bools is set so that default behavior = all false
	bool bForceAllBC23ToBC7; // change BC2 & 3 (aka DXT3 and DXT5) to BC7 
	bool bForceRDOOff; // use Oodle Texture but without RDO ; for debugging/testing , use LossyCompresionAmount to do this per-Texture
	bool bDebugColor; // color textures by their BCN, for data discovery
	// if no lambda is set on Texture or lodgroup, fall through to this global default :
	int DefaultRDOLambda;
	// after lambda is set, multiply by this scale factor :
	//	(multiplies the default and per-Texture overrides)
	//	is intended to let you do last minute whole-game adjustment
	float GlobalLambdaMultiplier;

	FTextureFormatOodle() :
		bForceAllBC23ToBC7(false),
		bForceRDOOff(false),
		bDebugColor(false),
		DefaultRDOLambda(OodleTex_RDOLagrangeLambda_Default),
		GlobalLambdaMultiplier(1.f)
	{
	}


	virtual ~FTextureFormatOodle()
	{
	}
	
	virtual bool AllowParallelBuild() const override
	{
		return true;
	}
	
	virtual bool UsesTaskGraph() const override
	{
		return true;
	}

	void Init()
	{
		// this is done at Singleton init time, the first time GetTextureFormat() is called

		#define OODLETEXTURE_INI_SECTION	TEXT("TextureFormatOodle")
		
		// Check that the our config section exists, and if not, init with defaults
		//  this will add it to your per-build Engine.ini
		// eg: C:\UnrealEngine\Games\oodletest\Saved\Config\Windows\Engine.ini
		// you can then move or copy it to DefaultEngine.ini if you like
		if (!GConfig->DoesSectionExist(OODLETEXTURE_INI_SECTION, GEngineIni))
		{
			GConfig->SetBool(OODLETEXTURE_INI_SECTION, TEXT("bForceAllBC23ToBC7"), bForceAllBC23ToBC7, GEngineIni);
			GConfig->SetBool(OODLETEXTURE_INI_SECTION, TEXT("bForceRDOOff"), bForceRDOOff, GEngineIni);
			GConfig->SetBool(OODLETEXTURE_INI_SECTION, TEXT("bDebugColor"), bDebugColor, GEngineIni);
			GConfig->SetFloat(OODLETEXTURE_INI_SECTION, TEXT("GlobalLambdaMultiplier"), GlobalLambdaMultiplier, GEngineIni);
			GConfig->SetInt(OODLETEXTURE_INI_SECTION, TEXT("DefaultRDOLambda"), DefaultRDOLambda, GEngineIni);

			GConfig->Flush(false);
		}
		
		// Class config variables
		GConfig->GetBool(OODLETEXTURE_INI_SECTION, TEXT("bForceAllBC23ToBC7"), bForceAllBC23ToBC7, GEngineIni);
		GConfig->GetBool(OODLETEXTURE_INI_SECTION, TEXT("bForceRDOOff"), bForceRDOOff, GEngineIni);
		GConfig->GetBool(OODLETEXTURE_INI_SECTION, TEXT("bDebugColor"), bDebugColor, GEngineIni);
		GConfig->GetFloat(OODLETEXTURE_INI_SECTION, TEXT("GlobalLambdaMultiplier"), GlobalLambdaMultiplier, GEngineIni);
		GConfig->GetInt(OODLETEXTURE_INI_SECTION, TEXT("DefaultRDOLambda"), DefaultRDOLambda, GEngineIni);

		// sanitize config values :
		DefaultRDOLambda = FMath::Clamp(DefaultRDOLambda,0,100);

		if ( GlobalLambdaMultiplier <= 0.f )
		{
			GlobalLambdaMultiplier = 1.f;
		}
		
		if ( bForceRDOOff )
		{
			UE_LOG(LogTextureFormatOodle, Display, TEXT("Oodle Texture %s init RDO Off"), TEXT(OodleTextureVersion) );
		}
		else
		{
			UE_LOG(LogTextureFormatOodle, Display, TEXT("Oodle Texture %s init RDO On with DefaultRDOLambda=%d"), TEXT(OodleTextureVersion), DefaultRDOLambda);
		}
	}
	
	void GetOodleCompressSettings(EPixelFormat * OutCompressedPixelFormat,int * OutRDOLambda, OodleTex_EncodeEffortLevel * OutEffortLevel, const struct FTextureBuildSettings& InBuildSettings, bool bHasAlpha) const
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
		if ( bForceAllBC23ToBC7 &&
			(CompressedPixelFormat == PF_DXT3 || CompressedPixelFormat == PF_DXT5 ) )
		{
			CompressedPixelFormat = PF_BC7;
		}

		*OutCompressedPixelFormat = CompressedPixelFormat;

		int RDOLambda = -1;

		// LossyCompressionAmount for per-Texture override
		//	also inherits from LODGroup if not set per-Texture
		int32 LossyCompressionAmount = InBuildSettings.LossyCompressionAmount;

		switch (LossyCompressionAmount)
		{
			default:
			case TLCA_Default: break;         // "Default"
			case TLCA_None:    RDOLambda = 0; break;    // "No lossy compression"
			case TLCA_Lowest:  RDOLambda = 5; break;    // "Lowest (Best Image quality, largest filesize)"
			case TLCA_Low:     RDOLambda = 15; break;   // "Low"
			case TLCA_Medium:  RDOLambda = 30; break;   // "Medium"
			case TLCA_High:    RDOLambda = 40; break;   // "High"
			case TLCA_Highest: RDOLambda = 60; break;   // "Highest (Worst Image quality, smallest filesize)"
		}

		if ( RDOLambda == -1 )
		{
			// not set
			// get global default from config
			RDOLambda = DefaultRDOLambda;
		}

		if ( RDOLambda > 0 && GlobalLambdaMultiplier != 1.f )
		{
			RDOLambda = (int)( GlobalLambdaMultiplier * RDOLambda + 0.5f );
			// don't let it change to 0 :
			if ( RDOLambda <= 0 )
			{
				RDOLambda = 1;
			}
		}

		RDOLambda = FMath::Clamp(RDOLambda,0,100);

		// ini option to force non-RDO encoding :
		if ( bForceRDOOff )
		{
			RDOLambda = 0;
		}
			
		// "Normal" is medium quality/speed
		OodleTex_EncodeEffortLevel EffortLevel = OodleTex_EncodeEffortLevel_Normal;
		// EffortLevel might be set to faster modes for previewing vs cooking or something
		//	but I don't see people setting that per-Texture or in lod groups or any of that
		//  it's more about cook mode (fast vs final bake)

		*OutRDOLambda = RDOLambda;
		*OutEffortLevel = EffortLevel;
	}
	
	// increment this to invalidate Derived Data Cache to recompress everything
	#define DDC_OODLE_TEXTURE_VERSION 11

	virtual uint16 GetVersion(FName Format, const struct FTextureBuildSettings* InBuildSettings) const override
	{
		// note: InBuildSettings == NULL is used by GetVersionFormatNumbersForIniVersionStrings
		//	just to get a displayable version number

		return DDC_OODLE_TEXTURE_VERSION; 
	}
	
	virtual FString GetDerivedDataKeyString(const class UTexture& InTexture, const struct FTextureBuildSettings* InBuildSettings) const override
	{
		check( InBuildSettings != NULL );

		// return all parameters that affect our output Texture
		// so if any of them change, we rebuild
		
		int RDOLambda;
		OodleTex_EncodeEffortLevel EffortLevel;
		EPixelFormat CompressedPixelFormat;

		// @todo Oodle this is not quite the same "bHasAlpha" that Compress will see
		//	bHasAlpha is used for AutoDXT -> DXT1/5
		//	we do have Texture.CompressionNoAlpha but that's not quite what we want
		// do go ahead and read CompressionNoAlpha so that we invalidate DDC when that changes
		bool bHasAlpha = ! InTexture.CompressionNoAlpha; 
		
		GetOodleCompressSettings(&CompressedPixelFormat,&RDOLambda,&EffortLevel,*InBuildSettings,bHasAlpha);

		// store the actual lambda in DDC key (rather than "LossyCompressionAmount")
		// that way any changes in how LossyCompressionAmount maps to lambda get rebuilt

		int icpf = (int)CompressedPixelFormat;

		check(RDOLambda<256);
		if ( bDebugColor )
		{
			RDOLambda = 256;
			EffortLevel = OodleTex_EncodeEffortLevel_Default;
		}
		
		return FString::Printf(TEXT("Oodle_CPF%d_L%d_E%d"), icpf, (int)RDOLambda, (int)EffortLevel);
	}

	virtual void GetSupportedFormats(TArray<FName>& OutFormats) const override
	{
		OutFormats.Append(GSupportedTextureFormatNames, sizeof(GSupportedTextureFormatNames)/sizeof(GSupportedTextureFormatNames[0]) ); 
	}

	virtual FTextureFormatCompressorCaps GetFormatCapabilities() const override
	{
		return FTextureFormatCompressorCaps(); // Default capabilities.
	}
	
	virtual EPixelFormat GetPixelFormatForImage(const struct FTextureBuildSettings& InBuildSettings, const struct FImage& Image, bool bHasAlpha) const override
	{
		int RDOLambda;
		OodleTex_EncodeEffortLevel EffortLevel;		
		EPixelFormat CompressedPixelFormat;
		GetOodleCompressSettings(&CompressedPixelFormat,&RDOLambda,&EffortLevel,InBuildSettings,bHasAlpha);
		return CompressedPixelFormat;
	}

	virtual bool CompressImage(const FImage& InImage, const struct FTextureBuildSettings& InBuildSettings, const bool bInHasAlpha, FCompressedImage2D& OutImage) const override
	{
		check(InImage.SizeX > 0);
		check(InImage.SizeY > 0);
		check(InImage.NumSlices > 0);

		// InImage always comes in as F32 in linear light
		//	(Unreal has just made mips in that format)
		// we are run simultaneously on all mips using the GLargeThreadPool
		
		// bHasAlpha = DetectAlphaChannel , scans the A's for non-opaque , in in CompressMipChain
		//	used by AutoDXT
		bool bHasAlpha = bInHasAlpha;

		int RDOLambda;
		OodleTex_EncodeEffortLevel EffortLevel;		
		EPixelFormat CompressedPixelFormat;
		GetOodleCompressSettings(&CompressedPixelFormat,&RDOLambda,&EffortLevel,InBuildSettings,bHasAlpha);

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

		bool bTFODoLog;
		// choose log verbosity :
		#if 0
		// verbose logging ; logs for every mip
		bTFODoLog = true;
		#elif 1
		// only log large mips
		bTFODoLog = InImage.SizeX >= 1024 || InImage.SizeY >= 1024;
		#else
		// no logging
		bTFODoLog = false;
		#endif

		if ( bTFODoLog )
		{
			UE_LOG(LogTextureFormatOodle, Display, TEXT("%s encode %i x %i x %i to format %s (Oodle %s) lambda=%i effort=%i "), \
				RDOLambda ? TEXT("RDO") : TEXT("non-RDO"), InImage.SizeX, InImage.SizeY, InImage.NumSlices, 
				*TextureFormatName.ToString(), *FString(OodleTex_BC_GetName(OodleBCN)),
				RDOLambda, (int)EffortLevel);
		}

		// input Image comes in as F32 in linear light
		// for BC6 we just leave that alone
		// for all others we must convert to 8 bit to get Gamma correction
		// because Unreal only does Gamma correction on the 8 bit conversion
		//	(this loses precision for BC4,5 which would like 16 bit input)

		FImage Image;
		OodleTex_PixelFormat OodlePF;
		if ( OodleBCN == OodleTex_BC6U )
		{
			// could avoid this copy most of the time
			InImage.CopyTo(Image, ERawImageFormat::RGBA32F, EGammaSpace::Linear);
			OodlePF = OodleTex_PixelFormat_4_F32_RGBA;

			// BC6 is assumed to be a linear-light HDR Image by default
			// use OodleTex_BCNFlag_BC6_NonRGBData if it is some other kind of data
		}
		else
		{
			// @todo Oodle for BC4/5 we'd prefer 16 bit over 8

			EGammaSpace Gamma = InBuildSettings.GetGammaSpace();
			// note in unreal if Gamma == Pow22 due to legacy Gamma,
			//	we still want to encode to sRGB
			// (CopyTo does that even without this change, but let's make it explicit)
			if ( Gamma == EGammaSpace::Pow22 ) Gamma = EGammaSpace::sRGB;

			InImage.CopyTo(Image, ERawImageFormat::BGRA8, Gamma);

			// if requested format was DXT1
			// Unreal assumes that will not encode any alpha channel in the source
			//	(Unreal's "compress without alpha" just selects DXT1)
			// the legacy NVTT behavior for DXT1 was to always encode opaque pixels
			// for DXT1 we use BC1_WithTransparency which will preserve the input A transparency bit
			//	so we need to force the A's to be 255 coming into Oodle
			//	so for DXT1 we force bHasAlpha = false
			// force Oodle to ignore input alpha :
			if ( bHasAlpha )
				OodlePF = OodleTex_PixelFormat_4_U8_BGRA;
			else
				OodlePF = OodleTex_PixelFormat_4_U8_BGRx; // makes Oodle read A=255 even if source has other
		}
		
		// verify OodlePF matches Image :
		check( Image.GetBytesPerPixel() == OodleTex_PixelFormat_BytesPerPixel(OodlePF) );
		
		OodleTex_Surface InSurf = { 0 };
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
			// lets you visually identify BCN textures in the Editor or game

			// use fast encoding settings for debug color :
			RDOLambda = 0;
			EffortLevel = OodleTex_EncodeEffortLevel_Low;

			if ( OodlePF == OodleTex_PixelFormat_4_F32_RGBA )
			{
				//BC6 = purple
				check(OodleBCN == OodleTex_BC6U);
				for(float * ptr = (float *) ImageBasePtr; ptr< (float *)(ImageBasePtr + InBytesTotal); ptr += 4)
				{
					// RGBA floats
					ptr[0] = 0.5f;
					ptr[1] = 0;
					ptr[2] = 0.8f;
					ptr[3] = 1.f;
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

				for(uint8 * ptr = ImageBasePtr; ptr < (ImageBasePtr + InBytesTotal); ptr += 4)
				{
					*((uint32 *)ptr) = DebugColor;
				}
			}			
		}

		int BytesPerBlock = OodleTex_BC_BytesPerBlock(OodleBCN);
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

		// encode each slice
		// @todo Oodle alternatively could do [Image.NumSlices] array of OodleTex_Surface
		//	and call OodleTex_Encode with the array
		//  would be slightly better for parallelism with multi-slice images & cube maps
		//	that's a rare case so don't bother for now
		// (the main parallelism is from running many mips at once which is done by our caller)
		bool bCompressionSucceeded = true;
		for (int Slice = 0; Slice < Image.NumSlices; ++Slice)
		{
			InSurf.pixels = ImageBasePtr + Slice * InBytesPerSlice;
			uint8 * OutSlicePtr = OutBlocksBasePtr + Slice * OutBytesPerSlice;

			OodleTex_Err OodleErr;
			if (RDOLambda == 0)
			{
				OodleErr = OodleTex_EncodeBCN_LinearSurfaces(OodleBCN, OutSlicePtr, NumBlocksPerSlice, 
					&InSurf, 1, OodlePF, NULL, EffortLevel,
					OodleTex_BCNFlags_None, OodleJobifyNumThreads, OodleJobifyUserPointer);
			}
			else
			{
				OodleErr = OodleTex_EncodeBCN_RDO(OodleBCN, OutSlicePtr, NumBlocksPerSlice, 
					&InSurf, 1, OodlePF, NULL, RDOLambda, 
					OodleTex_BCNFlags_None, OodleTex_RDO_ErrorMetric_Default, OodleJobifyNumThreads, OodleJobifyUserPointer);
			}
			if (OodleErr != OodleTex_Err_OK)
			{
				const char * OodleErrStr = OodleTex_Err_GetName(OodleErr);
				UE_LOG(LogTextureFormatOodle, Display, TEXT("Oodle Texture encode failed!? %s"), OodleErrStr );
				bCompressionSucceeded = false;
				break;
			}
		}

		return bCompressionSucceeded;
	}
};

//===============================================================

static ITextureFormat* Singleton = NULL;


// TFO_ plugins to Oodle to run Oodle system services in Unreal
// @todo Oodle : factor this out and share for Core & Net some day

// global map of TaskGraph references to uint64 for Oodle Jobify system
// protected by TaskIdMapLock
static uint8 PadToCacheLine1[64];
static FCriticalSection TaskIdMapLock;
// would be more efficient to split task ids into bins to reduce contention
static uint64 NextTaskId = 1;
static TMap<uint64, FGraphEventRef> TaskIdMap;
static uint8 PadToCacheLine2[64];

static OO_U64 OODLE_CALLBACK TFO_RunJob(t_fp_Oodle_Job* JobFunction, void* JobData, OO_U64* Dependencies, int NumDependencies, void* UserPtr)
{
	FGraphEventArray Prerequisites;
	if ( NumDependencies > 0 )
	{
		// map uint64 dependencies to TaskGraph refs
		FScopeLock Lock(&TaskIdMapLock);
		Prerequisites.Reserve(NumDependencies);
		for (int DependencyIndex = 0; DependencyIndex < NumDependencies; DependencyIndex++)
		{
			uint64 Id = Dependencies[DependencyIndex];
			FGraphEventRef Task = TaskIdMap[Id];
			// operator [] does a check that Task was found
			Prerequisites.Add(Task);
		}
	}

	// don't hold lock while dispatching task
	FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady(
		[JobFunction, JobData]()
		{
			JobFunction(JobData);
		}, TStatId(), &Prerequisites);
	
	// scope lock for NextTaskId and TaskIdMap
	TaskIdMapLock.Lock();
	uint64 Id = NextTaskId++;
	TaskIdMap.Add(Id, MoveTemp(Task));
	TaskIdMapLock.Unlock();

	return Id;
}

static void OODLE_CALLBACK TFO_WaitJob(OO_U64 JobHandle, void* UserPtr)
{
	TaskIdMapLock.Lock();
	FGraphEventRef Task = TaskIdMap[JobHandle];
	// TMap operator [] checks that value is found
	// can remove immediately (task may still be running)
	//	because once WaitJob is called this handle can never be referred to by calling code
	TaskIdMap.Remove(JobHandle);
	TaskIdMapLock.Unlock();
	
	// don't hold lock while waiting
	FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);
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

static void TFO_InstallPlugins()
{
	// Install Unreal system plugins to OodleTex
	// this should only be done once
	// and should be done before any other Oodle calls
	// plugins to Core/Tex/Net are independent

	OodleJobifyUserPointer = nullptr;
	OodleJobifyNumThreads = FTaskGraphInterface::Get().GetNumWorkerThreads();

	// @@ TEMP @todo clamp OodleJobifyNumThreads to avoid int overflow
	if ( OodleJobifyNumThreads > 16 ) OodleJobifyNumThreads = 16;

	OodleTex_Plugins_SetJobSystemAndCount(TFO_RunJob, TFO_WaitJob, OodleJobifyNumThreads);

	OodleTex_Plugins_SetAssertion(TFO_OodleAssert);
	OodleTex_Plugins_SetPrintf(TFO_OodleLog);
	OodleTex_Plugins_SetAllocators(TFO_OodleMallocAligned, TFO_OodleFree);
}

class FTextureFormatOodleModule : public ITextureFormatModule
{
public:
	FTextureFormatOodleModule() { }
	virtual ~FTextureFormatOodleModule()
	{
		ITextureFormat * p = Singleton;
		Singleton = NULL;
		if ( p )
			delete p;
	}

	virtual void StartupModule() override
	{
	}

	virtual ITextureFormat* GetTextureFormat()
	{
		// this is called twice
		
		if (!Singleton) // not thread safe
		{
			TFO_InstallPlugins();

			FTextureFormatOodle * ptr = new FTextureFormatOodle();
			ptr->Init();
			Singleton = ptr;
		}
		return Singleton;
	}
};

IMPLEMENT_MODULE(FTextureFormatOodleModule, TextureFormatOodle);

