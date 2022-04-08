// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "Misc/ConfigCacheIni.h"
#include "ImageCore.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/ITextureFormat.h"
#include "Interfaces/ITextureFormatModule.h"
#include "TextureCompressorModule.h"
#include "PixelFormat.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryWriter.h"
#include "TextureBuildFunction.h"
#include "DerivedDataBuildFunctionFactory.h"
#include "DerivedDataSharedString.h"
#include "HAL/IConsoleManager.h"

/****
* 
* TextureFormatASTC runs the ARM astcenc.exe command line tool
* 
* or (by default) redirects to Intel ISPC texcomp* 
* 
*****/

// when GASTCCompressor == 0 ,use TextureFormatIntelISPCTexComp instead of this
int32 GASTCCompressor = 0;
static FAutoConsoleVariableRef CVarASTCCompressor(
	TEXT("cook.ASTCTextureCompressor"),
	GASTCCompressor,
	TEXT("0: IntelISPC, 1: Arm"),
	ECVF_Default | ECVF_ReadOnly
);
int32 GASTCHDRProfile = 0;
static FAutoConsoleVariableRef CVarAllowASTCHDRProfile(
	TEXT("cook.AllowASTCHDRProfile"),
	GASTCHDRProfile,
	TEXT("whether to allow ASTC HDR profile, the hdr format is only supported on some devices, e.g. Apple A13, Mali-G72, Adreno (TM) 660"),
	ECVF_Default | ECVF_ReadOnly
);
#if PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_MAC
	#define SUPPORTS_ISPC_ASTC	1
#else
	#define SUPPORTS_ISPC_ASTC	0
#endif

// increment this if you change anything that will affect compression in this file, including FORCED_NORMAL_MAP_COMPRESSION_SIZE_VALUE
#define BASE_ASTC_FORMAT_VERSION 40

#define MAX_QUALITY_BY_SIZE 4
#define MAX_QUALITY_BY_SPEED 3
#define FORCED_NORMAL_MAP_COMPRESSION_SIZE_VALUE 3


DEFINE_LOG_CATEGORY_STATIC(LogTextureFormatASTC, Log, All);

class FASTCTextureBuildFunction final : public FTextureBuildFunction
{
	const UE::DerivedData::FUtf8SharedString& GetName() const final
	{
		static const UE::DerivedData::FUtf8SharedString Name(UTF8TEXTVIEW("ASTCTexture"));
		return Name;
	}

	void GetVersion(UE::DerivedData::FBuildVersionBuilder& Builder, ITextureFormat*& OutTextureFormatVersioning) const final
	{
		static FGuid Version(TEXT("4788dab5-b99c-479f-bc34-6d7df1cf30e5"));
		Builder << Version;
		OutTextureFormatVersioning = FModuleManager::GetModuleChecked<ITextureFormatModule>(TEXT("TextureFormatASTC")).GetTextureFormat();
	}
};

/**
 * Macro trickery for supported format names.
 */
#define ENUM_SUPPORTED_FORMATS(op) \
	op(ASTC_RGB) \
	op(ASTC_RGBA) \
	op(ASTC_RGBAuto) \
	op(ASTC_NormalAG) \
	op(ASTC_NormalRG)

	#define DECL_FORMAT_NAME(FormatName) static FName GTextureFormatName##FormatName = FName(TEXT(#FormatName));
		ENUM_SUPPORTED_FORMATS(DECL_FORMAT_NAME);
	#undef DECL_FORMAT_NAME

	#define DECL_FORMAT_NAME_ENTRY(FormatName) GTextureFormatName##FormatName ,
		static FName GSupportedTextureFormatNames[] =
		{
			ENUM_SUPPORTED_FORMATS(DECL_FORMAT_NAME_ENTRY)
		};
	#undef DECL_FORMAT_NAME_ENTRY
#undef ENUM_SUPPORTED_FORMATS

// ASTC file header format
#if PLATFORM_SUPPORTS_PRAGMA_PACK
	#pragma pack(push, 4)
#endif

	#define ASTC_MAGIC_CONSTANT 0x5CA1AB13
	struct FASTCHeader
	{
		uint32 Magic;
		uint8  BlockSizeX;
		uint8  BlockSizeY;
		uint8  BlockSizeZ;
		uint8  TexelCountX[3];
		uint8  TexelCountY[3];
		uint8  TexelCountZ[3];
	};

#if PLATFORM_SUPPORTS_PRAGMA_PACK
	#pragma pack(pop)
#endif


static int32 GetDefaultCompressionBySizeValue(FCbObjectView InFormatConfigOverride)
{
	if (InFormatConfigOverride)
	{
		// If we have an explicit format config, then use it directly
		FCbFieldView FieldView = InFormatConfigOverride.FindView("DefaultASTCQualityBySize");
		checkf(FieldView.HasValue(), TEXT("Missing DefaultASTCQualityBySize key from FormatConfigOverride"));
		int32 CompressionModeValue = FieldView.AsInt32();
		checkf(!FieldView.HasError(), TEXT("Failed to parse DefaultASTCQualityBySize value from FormatConfigOverride"));
		return CompressionModeValue;
	}

	// start at default quality, then lookup in .ini file
	int32 CompressionModeValue = 0;
	GConfig->GetInt(TEXT("/Script/UnrealEd.CookerSettings"), TEXT("DefaultASTCQualityBySize"), CompressionModeValue, GEngineIni);
	
	FParse::Value(FCommandLine::Get(), TEXT("-astcqualitybysize="), CompressionModeValue);
	CompressionModeValue = FMath::Min<uint32>(CompressionModeValue, MAX_QUALITY_BY_SIZE);
	
	return CompressionModeValue;
}

static int32 GetDefaultCompressionBySpeedValue(FCbObjectView InFormatConfigOverride)
{
	if (InFormatConfigOverride)
	{
		// If we have an explicit format config, then use it directly
		FCbFieldView FieldView = InFormatConfigOverride.FindView("DefaultASTCQualityBySpeed");
		checkf(FieldView.HasValue(), TEXT("Missing DefaultASTCQualityBySpeed key from FormatConfigOverride"));
		int32 CompressionModeValue = FieldView.AsInt32();
		checkf(!FieldView.HasError(), TEXT("Failed to parse DefaultASTCQualityBySpeed value from FormatConfigOverride"));
		return CompressionModeValue;
	}

	// start at default quality, then lookup in .ini file
	int32 CompressionModeValue = 0;
	GConfig->GetInt(TEXT("/Script/UnrealEd.CookerSettings"), TEXT("DefaultASTCQualityBySpeed"), CompressionModeValue, GEngineIni);
	
	FParse::Value(FCommandLine::Get(), TEXT("-astcqualitybyspeed="), CompressionModeValue);
	CompressionModeValue = FMath::Min<uint32>(CompressionModeValue, MAX_QUALITY_BY_SPEED);
	
	return CompressionModeValue;
}

static FString GetQualityString(const FCbObjectView& InFormatConfigOverride, int32 OverrideSizeValue=-1, int32 OverrideSpeedValue=-1)
{
	// convert to a string
	FString CompressionMode;
	switch (OverrideSizeValue >= 0 ? OverrideSizeValue : GetDefaultCompressionBySizeValue(InFormatConfigOverride))
	{
		case 0:	CompressionMode = TEXT("12x12"); break;
		case 1:	CompressionMode = TEXT("10x10"); break;
		case 2:	CompressionMode = TEXT("8x8"); break;
		case 3:	CompressionMode = TEXT("6x6"); break;
		case 4:	CompressionMode = TEXT("4x4"); break;
		default: UE_LOG(LogTemp, Fatal, TEXT("ASTC size quality higher than expected"));
	}
	
	switch (OverrideSpeedValue >= 0 ? OverrideSpeedValue : GetDefaultCompressionBySpeedValue(InFormatConfigOverride))
	{
		case 0:	CompressionMode += TEXT(" -fastest"); break;
		case 1:	CompressionMode += TEXT(" -fast"); break;
		case 2:	CompressionMode += TEXT(" -medium"); break;
		case 3:	CompressionMode += TEXT(" -thorough"); break;
		default: UE_LOG(LogTemp, Fatal, TEXT("ASTC speed quality higher than expected"));
	}

	return CompressionMode;
}

static EPixelFormat GetQualityFormat(const FCbObjectView& InFormatConfigOverride, int32 OverrideSizeValue=-1, bool bHDRFormat = false)
{
	// convert to a string
	EPixelFormat Format = PF_Unknown;
	if (bHDRFormat)
	{
		switch (OverrideSizeValue >= 0 ? OverrideSizeValue : GetDefaultCompressionBySizeValue(InFormatConfigOverride))
		{
			case 0:	Format = PF_ASTC_12x12_HDR; break;
			case 1:	Format = PF_ASTC_10x10_HDR; break;
			case 2:	Format = PF_ASTC_8x8_HDR; break;
			case 3:	Format = PF_ASTC_6x6_HDR; break;
			case 4:	Format = PF_ASTC_4x4_HDR; break;
			default: UE_LOG(LogTemp, Fatal, TEXT("Max quality higher than expected"));
		}
	}
	else
	{
		switch (OverrideSizeValue >= 0 ? OverrideSizeValue : GetDefaultCompressionBySizeValue(InFormatConfigOverride))
		{
			case 0:	Format = PF_ASTC_12x12; break;
			case 1:	Format = PF_ASTC_10x10; break;
			case 2:	Format = PF_ASTC_8x8; break;
			case 3:	Format = PF_ASTC_6x6; break;
			case 4:	Format = PF_ASTC_4x4; break;
			default: UE_LOG(LogTemp, Fatal, TEXT("Max quality higher than expected"));
		}
	}
	return Format;
}

static uint16 GetQualityVersion(const FCbObjectView& InFormatConfigOverride, int32 OverrideSizeValue = -1)
{
	// top 3 bits for size compression value, and next 3 for speed
	return ((OverrideSizeValue >= 0 ? OverrideSizeValue : GetDefaultCompressionBySizeValue(InFormatConfigOverride)) << 13) | (GetDefaultCompressionBySpeedValue(InFormatConfigOverride) << 10);
}

static bool CompressSliceToASTC(
	const FImageView & SourceImage,
	FString CompressionParameters,
	TArray64<uint8>& OutCompressedData,
	IImageWrapperModule& ImageWrapperModule
	)
{
	bool bHDR = ERawImageFormat::IsHDR(SourceImage.Format);
	
	EImageFormat FileFormat = bHDR ? EImageFormat::EXR : EImageFormat::PNG;
	TArray64<uint8> FileData;
	bool bCompressSucceeded = ImageWrapperModule.CompressImage(FileData,FileFormat,SourceImage,(int32)EImageCompressionQuality::Uncompressed);
	if ( ! bCompressSucceeded )
	{
		UE_LOG(LogTextureFormatASTC, Error, TEXT("CompressSliceToASTC CompressImage failed"));
		return false;
	}

	int64 FileDataSize = FileData.Num();

	// make a random file name to write the image :
	FGuid Guid;
	FPlatformMisc::CreateGuid(Guid);
	FString InputFilePath = FString::Printf(TEXT("Cache/%08x-%08x-%08x-%08x-RGBToASTCIn"), Guid.A, Guid.B, Guid.C, Guid.D) + TEXT(".") + ImageWrapperModule.GetExtension(FileFormat);
	FString OutputFilePath = FString::Printf(TEXT("Cache/%08x-%08x-%08x-%08x-RGBToASTCOut.astc"), Guid.A, Guid.B, Guid.C, Guid.D);

	InputFilePath  = FPaths::ProjectIntermediateDir() + InputFilePath;
	OutputFilePath = FPaths::ProjectIntermediateDir() + OutputFilePath;

	// write to InputFilePath :
	{
		FArchive* PNGFile = IFileManager::Get().CreateFileWriter(*InputFilePath);
		while (!PNGFile)
		{
			// CreateFileWriter occasionally returns NULL due to error code ERROR_SHARING_VIOLATION
			// ... no choice but to wait for the file to become free to access
		
			UE_LOG(LogTextureFormatASTC, Display, TEXT("CreateFileWriter for %s failed, trying again..."), *InputFilePath);

			FPlatformProcess::Sleep(0.01f);                             
			PNGFile = IFileManager::Get().CreateFileWriter(*InputFilePath);   
		}
		PNGFile->Serialize((void*)&FileData[0], FileDataSize);
		delete PNGFile;
	}

	// FileData written, can free now :
	FileData.Reset();

	// Compress PNG file to ASTC (using the reference astcenc.exe from ARM)
	FString Params = (bHDR ? TEXT("-ch ") : TEXT("-cl ")) + FString::Printf(TEXT("\"%s\" \"%s\" %s"),
		*InputFilePath,
		*OutputFilePath,
		*CompressionParameters
	);

	UE_LOG(LogTextureFormatASTC, Display, TEXT("Compressing to ASTC (options = '%s')..."), *CompressionParameters);

	// Start Compressor
#if PLATFORM_MAC_X86
	FString CompressorPath(FPaths::EngineDir() + TEXT("Binaries/ThirdParty/ARM/Mac/astcenc-sse2"));
#elif PLATFORM_MAC_ARM64
	FString CompressorPath(FPaths::EngineDir() + TEXT("Binaries/ThirdParty/ARM/Mac/astcenc-neon"));
#elif PLATFORM_LINUX
	FString CompressorPath(FPaths::EngineDir() + TEXT("Binaries/ThirdParty/ARM/Linux64/astcenc-sse2"));
#elif PLATFORM_WINDOWS
	FString CompressorPath(FPaths::EngineDir() + TEXT("Binaries/ThirdParty/ARM/Win64/astcenc-sse2.exe"));
#else
#error Unsupported platform
#endif

	// run the astcenc process :
	{
		FProcHandle Proc = FPlatformProcess::CreateProc(*CompressorPath, *Params, true, false, false, NULL, -1, NULL, NULL);

		// Failed to start the compressor process
		if (!Proc.IsValid())
		{
			UE_LOG(LogTextureFormatASTC, Error, TEXT("Failed to start astcenc for compressing images (%s)"), *CompressorPath);
			return false;
		}

		// Wait for the process to complete
		FPlatformProcess::WaitForProc(Proc);
		int ReturnCode = -1;
		FPlatformProcess::GetProcReturnCode(Proc, &ReturnCode);
		FPlatformProcess::CloseProc(Proc);
		
		// Did it work?
		if ( ReturnCode != 0)
		{
			UE_LOG(LogTextureFormatASTC, Error, TEXT("ASTC encoder failed with return code %d, mip size (%d, %d). Leaving '%s' for testing."), ReturnCode, SourceImage.SizeX, SourceImage.SizeY, *InputFilePath);
			return false;
		}
	}

	// Open compressed file and put the data in OutCompressedImage
	{
		// Get raw file data
		TArray64<uint8> ASTCData;
		if ( ! FFileHelper::LoadFileToArray(ASTCData, *OutputFilePath) )
		{
			UE_LOG(LogTextureFormatASTC, Error, TEXT("Failed load output of astcenc (%s -> %s)"),*InputFilePath,*OutputFilePath);
			return false;
		}

		// Process it
		FASTCHeader* Header = (FASTCHeader*)ASTCData.GetData();
			
		// Fiddle with the texel count data to get the right value
		uint32 TexelCountX =
			(Header->TexelCountX[0] <<  0) + 
			(Header->TexelCountX[1] <<  8) + 
			(Header->TexelCountX[2] << 16);
		uint32 TexelCountY =
			(Header->TexelCountY[0] <<  0) + 
			(Header->TexelCountY[1] <<  8) + 
			(Header->TexelCountY[2] << 16);
		uint32 TexelCountZ =
			(Header->TexelCountZ[0] <<  0) + 
			(Header->TexelCountZ[1] <<  8) + 
			(Header->TexelCountZ[2] << 16);

		if ( TexelCountX != SourceImage.SizeX ||
			 TexelCountY != SourceImage.SizeY )
		{
			UE_LOG(LogTextureFormatASTC, Warning, TEXT("Unexpected image size mismatch : %d x %d != %d x %d"),
				TexelCountX,TexelCountY,SourceImage.SizeX,SourceImage.SizeY);
		}

		// Calculate size of this mip in blocks
		uint32 MipSizeX = (TexelCountX + Header->BlockSizeX - 1) / Header->BlockSizeX;
		uint32 MipSizeY = (TexelCountY + Header->BlockSizeY - 1) / Header->BlockSizeY;

		// TexelCountZ ignored

		// A block is always 16 bytes
		uint64 MipSize = (uint64)MipSizeX * MipSizeY * 16;

		// Copy the compressed data
		OutCompressedData.Empty(MipSize);
		OutCompressedData.AddUninitialized(MipSize);
		void* MipData = OutCompressedData.GetData();

		// Calculate the offset to get to the mip data
		check(sizeof(FASTCHeader) == 16);
		check(ASTCData.Num() == (sizeof(FASTCHeader) + MipSize));
		FMemory::Memcpy(MipData, ASTCData.GetData() + sizeof(FASTCHeader), MipSize);
	}
		
	// Delete intermediate files
	IFileManager::Get().Delete(*InputFilePath);
	IFileManager::Get().Delete(*OutputFilePath);
	return true;
}

/**
 * ASTC texture format handler.
 */
class FTextureFormatASTC : public ITextureFormat
{
public:
	FTextureFormatASTC()
	:	IntelISPCTexCompFormat(*FModuleManager::LoadModuleChecked<ITextureFormatModule>(TEXT("TextureFormatIntelISPCTexComp")).GetTextureFormat()),
		ImageWrapperModule(FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper")))
	{
		// LoadModule has to be done on Main thread
		// can't be done on-demand in the Compress call
	}

	virtual bool AllowParallelBuild() const override
	{
#if SUPPORTS_ISPC_ASTC
		if(GASTCCompressor == 0)
		{
			return IntelISPCTexCompFormat.AllowParallelBuild();
		}
#endif
		return true;
	}
	virtual FName GetEncoderName(FName Format) const override
	{
#if SUPPORTS_ISPC_ASTC
		if (GASTCCompressor == 0)
		{
			return IntelISPCTexCompFormat.GetEncoderName(Format);
		}
#endif
		static const FName ASTCName("ArmASTC");
		return ASTCName;
	}

	virtual FCbObject ExportGlobalFormatConfig(const FTextureBuildSettings& BuildSettings) const override
	{
#if SUPPORTS_ISPC_ASTC
		if(GASTCCompressor == 0)
		{
			return IntelISPCTexCompFormat.ExportGlobalFormatConfig(BuildSettings);
		}
#endif
		FCbWriter Writer;
		Writer.BeginObject("TextureFormatASTCSettings");
		Writer.AddInteger("DefaultASTCQualityBySize", GetDefaultCompressionBySizeValue(FCbObjectView()));
		Writer.AddInteger("DefaultASTCQualityBySpeed", GetDefaultCompressionBySpeedValue(FCbObjectView()));
		Writer.EndObject();
		return Writer.Save().AsObject();
	}

	// Version for all ASTC textures, whether it's handled by the ARM encoder or the ISPC encoder.
	virtual uint16 GetVersion(
		FName Format,
		const FTextureBuildSettings* BuildSettings = nullptr
	) const override
	{
		return BASE_ASTC_FORMAT_VERSION;
	}

	virtual FString GetDerivedDataKeyString(const FTextureBuildSettings& BuildSettings) const override
	{
		return FString::Printf(TEXT("ASTCCmpr_%d_%d_%d"), GetQualityVersion(BuildSettings.FormatConfigOverride, BuildSettings.CompressionQuality), GASTCCompressor, int32(GASTCHDRProfile > 0 && BuildSettings.bHDRSource));
	}

	virtual FTextureFormatCompressorCaps GetFormatCapabilities() const override
	{
		FTextureFormatCompressorCaps RetCaps;
		// use defaults
		return RetCaps;
	}

	virtual void GetSupportedFormats(TArray<FName>& OutFormats) const override
	{
		for (int32 i = 0; i < UE_ARRAY_COUNT(GSupportedTextureFormatNames); ++i)
		{
			OutFormats.Add(GSupportedTextureFormatNames[i]);
		}
	}

	virtual EPixelFormat GetPixelFormatForImage(const FTextureBuildSettings& BuildSettings, const struct FImage& Image, bool bImageHasAlphaChannel) const override
	{
		// special case for normal maps
		if (BuildSettings.TextureFormatName == GTextureFormatNameASTC_NormalAG || BuildSettings.TextureFormatName == GTextureFormatNameASTC_NormalRG)
		{
			return GetQualityFormat(BuildSettings.FormatConfigOverride, FORCED_NORMAL_MAP_COMPRESSION_SIZE_VALUE);
		}
		
		return GetQualityFormat(BuildSettings.FormatConfigOverride, BuildSettings.CompressionQuality, Image.Format == ERawImageFormat::RGBA16F);
	}

	virtual bool CompressImage(
			const FImage& InImage,
			const FTextureBuildSettings& BuildSettings,
			FStringView DebugTexturePathName,
			bool bImageHasAlphaChannel,
			FCompressedImage2D& OutCompressedImage
		) const override
	{
#if SUPPORTS_ISPC_ASTC
		if(GASTCCompressor == 0)
		{
			// Route ASTC compression work to the ISPC module instead.
			return IntelISPCTexCompFormat.CompressImage(InImage, BuildSettings, DebugTexturePathName, bImageHasAlphaChannel, OutCompressedImage);
		}
#endif

		// note: BuildSettings.bHDRSource is a weird thing to be checking here
		//  it should look at whether the requested output format is HDR
		//  the input->output mapping has already been done, you shouldn't do it again
		bool bHDRImage = BuildSettings.bHDRSource && GASTCHDRProfile;
		if ( BuildSettings.bHDRSource && ! GASTCHDRProfile )
		{
			UE_LOG(LogTextureFormatASTC, Display, TEXT("HDR Image passed to ASTC but ASTC HDR is disabled, will compress as 8 bit: (%s)"),*FString(DebugTexturePathName));
		}

		// Get Raw Image Data from passed in FImage & convert to BGRA8 or RGBA16F
		// note: wasteful, often copies image to same format
		FImage Image;
		InImage.CopyTo(Image, bHDRImage ? ERawImageFormat::RGBA16F : ERawImageFormat::BGRA8, BuildSettings.GetDestGammaSpace());

		// Determine the compressed pixel format and compression parameters
		EPixelFormat CompressedPixelFormat = GetPixelFormatForImage(BuildSettings, Image, bImageHasAlphaChannel);

		FString CompressionParameters = TEXT("");

		bool bIsRGBColor = (BuildSettings.TextureFormatName == GTextureFormatNameASTC_RGB ||
			((BuildSettings.TextureFormatName == GTextureFormatNameASTC_RGBAuto) && !bImageHasAlphaChannel));
		bool bIsRGBAColor = (BuildSettings.TextureFormatName == GTextureFormatNameASTC_RGBA ||
			((BuildSettings.TextureFormatName == GTextureFormatNameASTC_RGBAuto) && bImageHasAlphaChannel));

		if (bIsRGBColor)
		{
			if (bHDRImage)
			{
				CompressionParameters = FString::Printf(TEXT("%s %s -cw 1 1 1 0"), *GetQualityString(BuildSettings.FormatConfigOverride, BuildSettings.CompressionQuality), /*BuildSettings.bSRGB ? TEXT("-srgb") :*/ TEXT(""));
			}
			else
			{
				CompressionParameters = FString::Printf(TEXT("%s %s -esw bgra -cw 1 1 1 0"), *GetQualityString(BuildSettings.FormatConfigOverride, BuildSettings.CompressionQuality), /*BuildSettings.bSRGB ? TEXT("-srgb") :*/ TEXT("") );
			}
		}
		else if (bIsRGBAColor)
		{
			CompressionParameters = FString::Printf(TEXT("%s %s -esw bgra -cw 1 1 1 1"), *GetQualityString(BuildSettings.FormatConfigOverride, BuildSettings.CompressionQuality), /*BuildSettings.bSRGB ? TEXT("-srgb") :*/ TEXT("") );
		}
		else if (BuildSettings.TextureFormatName == GTextureFormatNameASTC_NormalAG)
		{
			CompressionParameters = FString::Printf(TEXT("%s -esw 0g0b -cw 0 1 0 1 -dblimit 60 -b 2.5 -v 3 1 1 0 50 0 -va 1 1 0 50"), *GetQualityString(BuildSettings.FormatConfigOverride, FORCED_NORMAL_MAP_COMPRESSION_SIZE_VALUE, -1));
		}
		else if (BuildSettings.TextureFormatName == GTextureFormatNameASTC_NormalRG)
		{
			CompressionParameters = FString::Printf(TEXT("%s -esw bg00 -cw 1 1 0 0 -dblimit 60 -b 2.5 -v 3 1 1 0 50 0 -va 1 1 0 50"), *GetQualityString(BuildSettings.FormatConfigOverride, FORCED_NORMAL_MAP_COMPRESSION_SIZE_VALUE, -1));
		}
		else
		{
			check(false);
		}

		// Compress the image, slice by slice
		bool bCompressionSucceeded = true;

		for (int32 SliceIndex = 0; SliceIndex < Image.NumSlices; ++SliceIndex)
		{
			TArray64<uint8> CompressedSliceData;

			FImageView Slice = Image.GetSlice(SliceIndex);
			
			bCompressionSucceeded = CompressSliceToASTC(Slice,CompressionParameters,CompressedSliceData,ImageWrapperModule);

			if ( ! bCompressionSucceeded )
			{
				return false;
			}
			OutCompressedImage.RawData.Append(CompressedSliceData);
		}

		if (bCompressionSucceeded)
		{
			OutCompressedImage.SizeX = Image.SizeX;
			OutCompressedImage.SizeY = Image.SizeY;
			OutCompressedImage.SizeZ = (BuildSettings.bVolume || BuildSettings.bTextureArray) ? Image.NumSlices : 1;
			OutCompressedImage.PixelFormat = CompressedPixelFormat;
		}
		return bCompressionSucceeded;
	}

private:
	const ITextureFormat& IntelISPCTexCompFormat;
	
	IImageWrapperModule& ImageWrapperModule;
};

/**
 * Module for ASTC texture compression.
 */
static ITextureFormat* Singleton = NULL;

class FTextureFormatASTCModule : public ITextureFormatModule
{
public:
	FTextureFormatASTCModule()
	{
	}
	virtual ~FTextureFormatASTCModule()
	{
		delete Singleton;
		Singleton = NULL;
	}
	
	virtual void StartupModule() override
	{
	}

	virtual bool CanCallGetTextureFormats() override { return false; }

	virtual ITextureFormat* GetTextureFormat()
	{
		if (!Singleton)
		{
			Singleton = new FTextureFormatASTC();
		}
		return Singleton;
	}

	static inline UE::DerivedData::TBuildFunctionFactory<FASTCTextureBuildFunction> BuildFunctionFactory;
};

IMPLEMENT_MODULE(FTextureFormatASTCModule, TextureFormatASTC);
