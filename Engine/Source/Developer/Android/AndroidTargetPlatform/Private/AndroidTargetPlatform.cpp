// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AndroidTargetPlatform.inl: Implements the FAndroidTargetPlatform class.
=============================================================================*/


/* FAndroidTargetPlatform structors
 *****************************************************************************/

#include "AndroidTargetPlatform.h"
#include "CoreTypes.h"
#include "Misc/AssertionMacros.h"
#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "UObject/NameTypes.h"
#include "Logging/LogMacros.h"
#include "Stats/Stats.h"
#include "Serialization/Archive.h"
#include "Misc/FileHelper.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IAndroidDeviceDetectionModule.h"
#include "Interfaces/IAndroidDeviceDetection.h"
#include "Modules/ModuleManager.h"
#include "Misc/SecureHash.h"


#if WITH_ENGINE
#include "AudioCompressionSettings.h"
#include "Sound/SoundWave.h"
#endif

#define LOCTEXT_NAMESPACE "FAndroidTargetPlatform"

class Error;
class FAndroidTargetDevice;
class FConfigCacheIni;
class FModuleManager;
class FScopeLock;
class FStaticMeshLODSettings;
class FTargetDeviceId;
class FTicker;
class IAndroidDeviceDetectionModule;
class UTexture;
class UTextureLODSettings;
struct FAndroidDeviceInfo;
enum class ETargetPlatformFeatures;
template<typename TPlatformProperties> class TTargetPlatformBase;



static FString GetLicensePath()
{
	auto &AndroidDeviceDetection = FModuleManager::LoadModuleChecked<IAndroidDeviceDetectionModule>("AndroidDeviceDetection");
	IAndroidDeviceDetection* DeviceDetection = AndroidDeviceDetection.GetAndroidDeviceDetection();
	FString ADBPath = DeviceDetection->GetADBPath();

	if (!FPaths::FileExists(*ADBPath))
	{
		return TEXT("");
	}

	// strip off the adb.exe part
	FString PlatformToolsPath;
	FString Filename;
	FString Extension;
	FPaths::Split(ADBPath, PlatformToolsPath, Filename, Extension);

	// remove the platform-tools part and point to licenses
	FPaths::NormalizeDirectoryName(PlatformToolsPath);
	FString LicensePath = PlatformToolsPath + "/../licenses";
	FPaths::CollapseRelativeDirectories(LicensePath);

	return LicensePath;
}

#if WITH_ENGINE
static bool GetLicenseHash(FSHAHash& LicenseHash)
{
	bool bLicenseValid = false;

	// from Android SDK Tools 25.2.3
	FString LicenseFilename = FPaths::EngineDir() + TEXT("Source/ThirdParty/Android/package.xml");

	// Create file reader
	TUniquePtr<FArchive> FileReader(IFileManager::Get().CreateFileReader(*LicenseFilename));
	if (FileReader)
	{
		// Create buffer for file input
		uint32 BufferSize = FileReader->TotalSize();
		uint8* Buffer = (uint8*)FMemory::Malloc(BufferSize);
		FileReader->Serialize(Buffer, BufferSize);

		uint8 StartPattern[] = "<license id=\"android-sdk-license\" type=\"text\">";
		int32 StartPatternLength = strlen((char *)StartPattern);

		uint8* LicenseStart = Buffer;
		uint8* BufferEnd = Buffer + BufferSize - StartPatternLength;
		while (LicenseStart < BufferEnd)
		{
			if (!memcmp(LicenseStart, StartPattern, StartPatternLength))
			{
				break;
			}
			LicenseStart++;
		}

		if (LicenseStart < BufferEnd)
		{
			LicenseStart += StartPatternLength;

			uint8 EndPattern[] = "</license>";
			int32 EndPatternLength = strlen((char *)EndPattern);

			uint8* LicenseEnd = LicenseStart;
			BufferEnd = Buffer + BufferSize - EndPatternLength;
			while (LicenseEnd < BufferEnd)
			{
				if (!memcmp(LicenseEnd, EndPattern, EndPatternLength))
				{
					break;
				}
				LicenseEnd++;
			}

			if (LicenseEnd < BufferEnd)
			{
				int32 LicenseLength = LicenseEnd - LicenseStart;
				FSHA1::HashBuffer(LicenseStart, LicenseLength, LicenseHash.Hash);
				bLicenseValid = true;
			}
		}
		FMemory::Free(Buffer);
	}

	return bLicenseValid;
}
#endif

static bool HasLicense()
{
#if WITH_ENGINE
	FString LicensePath = GetLicensePath();

	if (LicensePath.IsEmpty())
	{
		return false;
	}

	// directory must exist
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*LicensePath))
	{
		return false;
	}

	// license file must exist
	FString LicenseFilename = LicensePath + "/android-sdk-license";
	if (!PlatformFile.FileExists(*LicenseFilename))
	{
		return false;
	}

	FSHAHash LicenseHash;
	if (!GetLicenseHash(LicenseHash))
	{
		return false;
	}

	// contents must match hash of license text
	FString FileData = "";
	FFileHelper::LoadFileToString(FileData, *LicenseFilename);
	TArray<FString> lines;
	int32 lineCount = FileData.ParseIntoArray(lines, TEXT("\n"), true);

	FString LicenseString = LicenseHash.ToString().ToLower();
	for (FString &line : lines)
	{
		if (line.TrimStartAndEnd().Equals(LicenseString))
		{
			return true;
		}
	}
#endif

	// doesn't match
	return false;
}

FAndroidTargetPlatform::FAndroidTargetPlatform(bool bInIsClient )
	: bIsClient(bInIsClient)
	, DeviceDetection(nullptr)
	, bDistanceField(false)

{
	#if WITH_ENGINE
		FConfigCacheIni::LoadLocalIniFile(EngineSettings, TEXT("Engine"), true, *IniPlatformName());
			TextureLODSettings = nullptr; // These are registered by the device profile system.
		StaticMeshLODSettings.Initialize(EngineSettings);
		EngineSettings.GetBool(TEXT("/Script/Engine.RendererSettings"), TEXT("r.DistanceFields"), bDistanceField);
	#endif

	TickDelegate = FTickerDelegate::CreateRaw(this, &FAndroidTargetPlatform::HandleTicker);
	TickDelegateHandle = FTicker::GetCoreTicker().AddTicker(TickDelegate, 4.0f);
}


FAndroidTargetPlatform::~FAndroidTargetPlatform()
{
	 FTicker::GetCoreTicker().RemoveTicker(TickDelegateHandle);
}

FAndroidTargetDevicePtr FAndroidTargetPlatform::CreateTargetDevice(const ITargetPlatform& InTargetPlatform, const FString& InSerialNumber, const FString& InAndroidVariant) const
{
	return MakeShareable(new FAndroidTargetDevice(InTargetPlatform, InSerialNumber, InAndroidVariant));
}

static bool UsesVirtualTextures()
{
	static auto* CVarMobileVirtualTextures = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.VirtualTextures"));
	return CVarMobileVirtualTextures->GetValueOnAnyThread() != 0;
}

bool FAndroidTargetPlatform::SupportsES31() const
{
	// default no support for ES31
	bool bBuildForES31 = false;
#if WITH_ENGINE
	GConfig->GetBool(TEXT("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings"), TEXT("bBuildForES31"), bBuildForES31, GEngineIni);
#endif
	return bBuildForES31;
}

bool FAndroidTargetPlatform::SupportsVulkan() const
{
	// default to not supporting Vulkan
	bool bSupportsVulkan = false;
#if WITH_ENGINE
	GConfig->GetBool(TEXT("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings"), TEXT("bSupportsVulkan"), bSupportsVulkan, GEngineIni);
#endif
	return bSupportsVulkan;
}

bool FAndroidTargetPlatform::SupportsVulkanSM5() const
{
	// default to no support for VulkanSM5
	bool bSupportsMobileVulkanSM5 = false;
#if WITH_ENGINE
	GConfig->GetBool(TEXT("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings"), TEXT("bSupportsVulkanSM5"), bSupportsMobileVulkanSM5, GEngineIni);
#endif
	return bSupportsMobileVulkanSM5;
}

bool FAndroidTargetPlatform::SupportsSoftwareOcclusion() const
{
	static auto* CVarMobileAllowSoftwareOcclusion = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.AllowSoftwareOcclusion"));
	return CVarMobileAllowSoftwareOcclusion->GetValueOnAnyThread() != 0;
}

bool FAndroidTargetPlatform::SupportsLandscapeMeshLODStreaming() const
{
	bool bStreamLandscapeMeshLODs = false;
#if WITH_ENGINE
	GConfig->GetBool(TEXT("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings"), TEXT("bStreamLandscapeMeshLODs"), bStreamLandscapeMeshLODs, GEngineIni);
#endif
	return bStreamLandscapeMeshLODs;
}

/* ITargetPlatform overrides
 *****************************************************************************/

void FAndroidTargetPlatform::GetAllDevices( TArray<ITargetDevicePtr>& OutDevices ) const
{
	OutDevices.Reset();

	for (auto Iter = Devices.CreateConstIterator(); Iter; ++Iter)
	{
		OutDevices.Add(Iter.Value());
	}
}

ITargetDevicePtr FAndroidTargetPlatform::GetDefaultDevice( ) const
{
	// return the first device in the list
	if (Devices.Num() > 0)
	{
		auto Iter = Devices.CreateConstIterator();
		if (Iter)
		{
			return Iter.Value();
		}
	}

	return nullptr;
}

ITargetDevicePtr FAndroidTargetPlatform::GetDevice( const FTargetDeviceId& DeviceId )
{
	if (DeviceId.GetPlatformName() == PlatformName())
	{
		return Devices.FindRef(DeviceId.GetDeviceName());
	}

	return nullptr;
}

bool FAndroidTargetPlatform::IsRunningPlatform( ) const
{
	return false; // This platform never runs the target platform framework
}


bool FAndroidTargetPlatform::IsSdkInstalled(bool bProjectHasCode, FString& OutDocumentationPath) const
{
	OutDocumentationPath = FString("Shared/Tutorials/SettingUpAndroidTutorial");
	return true;
}

int32 FAndroidTargetPlatform::CheckRequirements(bool bProjectHasCode, EBuildConfiguration Configuration, bool bRequiresAssetNativization, FString& OutTutorialPath, FString& OutDocumentationPath, FText& CustomizedLogMessage) const
{
	OutDocumentationPath = TEXT("Platforms/Android/GettingStarted");

	int32 bReadyToBuild = ETargetPlatformReadyStatus::Ready;
	if (!IsSdkInstalled(bProjectHasCode, OutTutorialPath))
	{
		bReadyToBuild |= ETargetPlatformReadyStatus::SDKNotFound;
	}

	bool bEnableGradle;
	GConfig->GetBool(TEXT("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings"), TEXT("bEnableGradle"), bEnableGradle, GEngineIni);

	if (bEnableGradle)
	{
		// need to check license was accepted
		if (!HasLicense())
		{
			OutTutorialPath.Empty();
			CustomizedLogMessage = LOCTEXT("AndroidLicenseNotAcceptedMessageDetail", "SDK License must be accepted in the Android project settings to deploy your app to the device.");
			bReadyToBuild |= ETargetPlatformReadyStatus::LicenseNotAccepted;
		}
	}

	return bReadyToBuild;
}

bool FAndroidTargetPlatform::SupportsFeature( ETargetPlatformFeatures Feature ) const
{
	switch (Feature)
	{
		case ETargetPlatformFeatures::Packaging:
		case ETargetPlatformFeatures::DeviceOutputLog:
			return true;

		case ETargetPlatformFeatures::LowQualityLightmaps:
		case ETargetPlatformFeatures::MobileRendering:
			return SupportsES31() || SupportsVulkan();

		case ETargetPlatformFeatures::HighQualityLightmaps:
		case ETargetPlatformFeatures::DeferredRendering:
			return SupportsVulkanSM5();

		case ETargetPlatformFeatures::Tessellation:
			return false;

		case ETargetPlatformFeatures::SoftwareOcclusion:
			return SupportsSoftwareOcclusion();

		case ETargetPlatformFeatures::VirtualTextureStreaming:
			return UsesVirtualTextures();

		case ETargetPlatformFeatures::LandscapeMeshLODStreaming:
			return SupportsLandscapeMeshLODStreaming();

		case ETargetPlatformFeatures::DistanceFieldAO:
			return UsesDistanceFields();
			
		default:
			break;
	}

	return TTargetPlatformBase<FAndroidPlatformProperties>::SupportsFeature(Feature);
}


#if WITH_ENGINE

void FAndroidTargetPlatform::GetAllPossibleShaderFormats( TArray<FName>& OutFormats ) const
{
	static FName NAME_SF_VULKAN_ES31_ANDROID(TEXT("SF_VULKAN_ES31_ANDROID"));
	static FName NAME_GLSL_ES3_1_ANDROID(TEXT("GLSL_ES3_1_ANDROID"));
	static FName NAME_SF_VULKAN_SM5_ANDROID(TEXT("SF_VULKAN_SM5_ANDROID"));

	if (SupportsVulkan())
	{
		OutFormats.AddUnique(NAME_SF_VULKAN_ES31_ANDROID);	
	}

	if (SupportsVulkanSM5())
	{
		OutFormats.AddUnique(NAME_SF_VULKAN_SM5_ANDROID);
	}

	if (SupportsES31())
	{
		OutFormats.AddUnique(NAME_GLSL_ES3_1_ANDROID);
	}
}

void FAndroidTargetPlatform::GetAllTargetedShaderFormats( TArray<FName>& OutFormats ) const
{
	GetAllPossibleShaderFormats(OutFormats);
}


const FStaticMeshLODSettings& FAndroidTargetPlatform::GetStaticMeshLODSettings( ) const
{
	return StaticMeshLODSettings;
}


void FAndroidTargetPlatform::GetTextureFormats( const UTexture* InTexture, TArray< TArray<FName> >& OutFormats) const
{
#if WITH_EDITOR
	const int32 NumLayers = InTexture->Source.GetNumLayers();

	// Can always compress power-of-two, sometimes support non-POT compression
	const bool bIsCompressionValid = InTexture->Source.IsPowerOfTwo() || SupportsCompressedNonPOT();

	OutFormats.Reserve((int32)EAndroidTextureFormatCategory::Count);
	for (int32 FormatIndex = 0; FormatIndex < (int32)EAndroidTextureFormatCategory::Count; ++FormatIndex)
	{
		const EAndroidTextureFormatCategory FormatCategory = (EAndroidTextureFormatCategory)FormatIndex;
		if (!SupportsTextureFormatCategory(FormatCategory))
		{
			continue;
		}

		TArray<FName> FormatPerLayer;
		FormatPerLayer.SetNum(NumLayers);

		bool bValidFormat = true;
		for (int32 LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
		{
			FTextureFormatSettings LayerFormatSettings;
			InTexture->GetLayerFormatSettings(LayerIndex, LayerFormatSettings);

			const bool bNoCompression = LayerFormatSettings.CompressionNone				// Code wants the texture uncompressed.
				|| (InTexture->LODGroup == TEXTUREGROUP_ColorLookupTable)	// Textures in certain LOD groups should remain uncompressed.
				|| (InTexture->LODGroup == TEXTUREGROUP_Bokeh)
				|| (LayerFormatSettings.CompressionSettings == TC_EditorIcon)
				|| (InTexture->Source.GetSizeX() < 4)						// Don't compress textures smaller than the DXT block size.
				|| (InTexture->Source.GetSizeY() < 4)
				|| (InTexture->Source.GetSizeX() % 4 != 0)
				|| (InTexture->Source.GetSizeY() % 4 != 0);

			// Determine the pixel format of the compressed texture.
			if (InTexture->LODGroup == TEXTUREGROUP_Shadowmap)
			{
				// forward rendering only needs one channel for shadow maps
				FormatPerLayer[LayerIndex] = AndroidTexFormat::NameG8;
			}
			else if (bNoCompression && InTexture->HasHDRSource(LayerIndex))
			{
				FormatPerLayer[LayerIndex] = AndroidTexFormat::NameRGBA16F;
			}
			else if (bNoCompression)
			{
				FormatPerLayer[LayerIndex] = AndroidTexFormat::NameBGRA8;
			}
			else if (LayerFormatSettings.CompressionSettings == TC_EncodedReflectionCapture && !LayerFormatSettings.CompressionNone)
			{
				FormatPerLayer[LayerIndex] = AndroidTexFormat::NameETC2_RGBA;
			}
			else if (LayerFormatSettings.CompressionSettings == TC_HDR || LayerFormatSettings.CompressionSettings == TC_HDR_Compressed)
			{
				FormatPerLayer[LayerIndex] = AndroidTexFormat::NameRGBA16F;
			}
			else if (LayerFormatSettings.CompressionSettings == TC_Normalmap)
			{
				if(!bIsCompressionValid) FormatPerLayer[LayerIndex] = AndroidTexFormat::NamePOTERROR;
				else if (FormatCategory == EAndroidTextureFormatCategory::DXT) FormatPerLayer[LayerIndex] = AndroidTexFormat::NameDXT5;
				else if (FormatCategory == EAndroidTextureFormatCategory::ETC2) FormatPerLayer[LayerIndex] = AndroidTexFormat::NameETC2_RGB;
				else bValidFormat = false;
			}
			else if (LayerFormatSettings.CompressionSettings == TC_Displacementmap)
			{
				FormatPerLayer[LayerIndex] = AndroidTexFormat::NameRGBA16F;
			}
			else if (LayerFormatSettings.CompressionSettings == TC_VectorDisplacementmap)
			{
				FormatPerLayer[LayerIndex] = AndroidTexFormat::NameBGRA8;
			}
			else if (LayerFormatSettings.CompressionSettings == TC_Grayscale)
			{
				FormatPerLayer[LayerIndex] = AndroidTexFormat::NameG8;
			}
			else if (LayerFormatSettings.CompressionSettings == TC_Alpha)
			{
				FormatPerLayer[LayerIndex] = AndroidTexFormat::NameG8;
			}
			else if (LayerFormatSettings.CompressionSettings == TC_DistanceFieldFont)
			{
				FormatPerLayer[LayerIndex] = AndroidTexFormat::NameG8;
			}
			else if (LayerFormatSettings.CompressionSettings == TC_HalfFloat)
			{
				FormatPerLayer[LayerIndex] = AndroidTexFormat::NameR16F;
			}
			else if (LayerFormatSettings.CompressionSettings == TC_BC7)
			{
				if (!bIsCompressionValid) FormatPerLayer[LayerIndex] = AndroidTexFormat::NamePOTERROR;
				else if (FormatCategory == EAndroidTextureFormatCategory::DXT) FormatPerLayer[LayerIndex] = AndroidTexFormat::NameDXT5;
				else if (FormatCategory == EAndroidTextureFormatCategory::ETC2) FormatPerLayer[LayerIndex] = AndroidTexFormat::NameAutoETC2;
				else bValidFormat = false;
			}
			else if (LayerFormatSettings.CompressionNoAlpha)
			{
				if (!bIsCompressionValid) FormatPerLayer[LayerIndex] = AndroidTexFormat::NamePOTERROR;
				else if (FormatCategory == EAndroidTextureFormatCategory::DXT) FormatPerLayer[LayerIndex] = AndroidTexFormat::NameDXT1;
				else if (FormatCategory == EAndroidTextureFormatCategory::ETC2) FormatPerLayer[LayerIndex] = AndroidTexFormat::NameETC2_RGB;
				else bValidFormat = false;
			}
			else if (InTexture->bDitherMipMapAlpha)
			{
				if (!bIsCompressionValid) FormatPerLayer[LayerIndex] = AndroidTexFormat::NamePOTERROR;
				else if (FormatCategory == EAndroidTextureFormatCategory::DXT) FormatPerLayer[LayerIndex] = AndroidTexFormat::NameDXT5;
				else if (FormatCategory == EAndroidTextureFormatCategory::ETC2) FormatPerLayer[LayerIndex] = AndroidTexFormat::NameAutoETC2;
				else bValidFormat = false;
			}
			else
			{
				if (!bIsCompressionValid) FormatPerLayer[LayerIndex] = AndroidTexFormat::NamePOTERROR;
				else if (FormatCategory == EAndroidTextureFormatCategory::DXT) FormatPerLayer[LayerIndex] = AndroidTexFormat::NameAutoDXT;
				else if (FormatCategory == EAndroidTextureFormatCategory::ETC2) FormatPerLayer[LayerIndex] = AndroidTexFormat::NameAutoETC2;
				else bValidFormat = false;
			}
		}

		if (bValidFormat)
		{
			OutFormats.AddUnique(FormatPerLayer);
		}
	}
#endif // WITH_EDITOR
}

FName FAndroidTargetPlatform::FinalizeVirtualTextureLayerFormat(FName Format) const
{
#if WITH_EDITOR
	// Remap non-ETC variants to ETC
	const static FName ETCRemap[][2] =
	{
		{ { FName(TEXT("ASTC_RGB")) },			{ AndroidTexFormat::NameETC2_RGB } },
		{ { FName(TEXT("ASTC_RGBA")) },			{ AndroidTexFormat::NameETC2_RGBA } },
		{ { FName(TEXT("ASTC_RGBAuto")) },		{ AndroidTexFormat::NameAutoETC2 } },
		{ { FName(TEXT("ASTC_NormalAG")) },		{ AndroidTexFormat::NameETC2_RGB } },
		{ { FName(TEXT("ASTC_NormalRG")) },		{ AndroidTexFormat::NameETC2_RGB } },
		{ { AndroidTexFormat::NameDXT1 },		{ AndroidTexFormat::NameETC2_RGB } },
		{ { AndroidTexFormat::NameDXT5 },		{ AndroidTexFormat::NameAutoETC2 } },
		{ { AndroidTexFormat::NameAutoDXT },	{ AndroidTexFormat::NameAutoETC2 } }
	};

	for (int32 RemapIndex = 0; RemapIndex < UE_ARRAY_COUNT(ETCRemap); RemapIndex++)
	{
		if (ETCRemap[RemapIndex][0] == Format)
		{
			return ETCRemap[RemapIndex][1];
		}
	}
#endif
	return Format;
}

void FAndroidTargetPlatform::GetAllTextureFormats(TArray<FName>& OutFormats) const
{
	OutFormats.Add(AndroidTexFormat::NameG8);
	OutFormats.Add(AndroidTexFormat::NameRGBA16F);
	OutFormats.Add(AndroidTexFormat::NameBGRA8);
	OutFormats.Add(AndroidTexFormat::NameRGBA16F);
	OutFormats.Add(AndroidTexFormat::NameRGBA16F);
	OutFormats.Add(AndroidTexFormat::NameBGRA8);
	OutFormats.Add(AndroidTexFormat::NameG8);
	OutFormats.Add(AndroidTexFormat::NameG8);
	OutFormats.Add(AndroidTexFormat::NameG8);
	OutFormats.Add(AndroidTexFormat::NameR16F);

	auto AddAllTextureFormatIfSupports = [=, &OutFormats](bool bIsNonPOT)
	{
		AddTextureFormatIfSupports(AndroidTexFormat::NameAutoDXT, OutFormats, bIsNonPOT);
		AddTextureFormatIfSupports(AndroidTexFormat::NameDXT1, OutFormats, bIsNonPOT);
		AddTextureFormatIfSupports(AndroidTexFormat::NameDXT5, OutFormats, bIsNonPOT);
		AddTextureFormatIfSupports(AndroidTexFormat::NameAutoETC2, OutFormats, bIsNonPOT);
	};

	AddAllTextureFormatIfSupports(true);
	AddAllTextureFormatIfSupports(false);
}


void FAndroidTargetPlatform::GetReflectionCaptureFormats( TArray<FName>& OutFormats ) const
{
	static auto* MobileShadingPathCvar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.ShadingPath"));
	const bool bMobileDeferredShading = (MobileShadingPathCvar->GetValueOnAnyThread() == 1);
	
	if (SupportsVulkanSM5() || (SupportsVulkan() && bMobileDeferredShading))
	{
		// use Full HDR with SM5 and Mobile Deferred
		OutFormats.Add(FName(TEXT("FullHDR")));
	}

	// always emit encoded
	OutFormats.Add(FName(TEXT("EncodedHDR")));
}


const UTextureLODSettings& FAndroidTargetPlatform::GetTextureLODSettings() const
{
	return *TextureLODSettings;
}


FName FAndroidTargetPlatform::GetWaveFormat( const class USoundWave* Wave ) const
{
	static const FName NAME_ADPCM(TEXT("ADPCM"));
	static const FName NAME_OGG(TEXT("OGG"));

	static bool bFormatRead = false;
	static FName NAME_FORMAT;
	if (!bFormatRead)
	{
		bFormatRead = true;

		FName AudioSetting;
		{
			FString AudioSettingStr;
			if (!GConfig->GetString(TEXT("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings"), TEXT("AndroidAudio"), AudioSettingStr, GEngineIni))
			{
				AudioSetting = *AudioSettingStr;
			}
		}

#if WITH_OGGVORBIS
		if (AudioSetting == NAME_OGG || AudioSetting == NAME_None)
		{
			NAME_FORMAT = NAME_OGG;
		}
#else
		if (AudioSetting == NAME_OGG)
		{
			UE_LOG(LogAudio, Error, TEXT("Attemped to select Ogg Vorbis encoding when the cooker is built without Ogg Vorbis support."));
		}
#endif
		else
		{
			// Otherwise return ADPCM as it'll either be option '2' or 'default' depending on WITH_OGGVORBIS config
			NAME_FORMAT = NAME_ADPCM;
		}
	}

	if (Wave->IsSeekableStreaming())
	{
		return NAME_ADPCM;
	}

	return NAME_FORMAT;
}


void FAndroidTargetPlatform::GetAllWaveFormats(TArray<FName>& OutFormats) const
{
	static FName NAME_OGG(TEXT("OGG"));
	static FName NAME_ADPCM(TEXT("ADPCM"));

	OutFormats.Add(NAME_OGG);
	OutFormats.Add(NAME_ADPCM);
}

#endif //WITH_ENGINE

bool FAndroidTargetPlatform::SupportsVariants() const
{
	return true;
}

FText FAndroidTargetPlatform::GetVariantTitle() const
{
	return LOCTEXT("AndroidVariantTitle", "Texture Format");
}

/* FAndroidTargetPlatform implementation
 *****************************************************************************/

void FAndroidTargetPlatform::AddTextureFormatIfSupports( FName Format, TArray<FName>& OutFormats, bool bIsCompressedNonPOT ) const
{
	if (SupportsTextureFormat(Format))
	{
		if (bIsCompressedNonPOT && SupportsCompressedNonPOT() == false)
		{
			OutFormats.Add(AndroidTexFormat::NamePOTERROR);
		}
		else
		{
			OutFormats.Add(Format);
		}
	}
}

void FAndroidTargetPlatform::InitializeDeviceDetection()
{
	DeviceDetection = FModuleManager::LoadModuleChecked<IAndroidDeviceDetectionModule>("AndroidDeviceDetection").GetAndroidDeviceDetection();
	DeviceDetection->Initialize(TEXT("ANDROID_HOME"),
#if PLATFORM_WINDOWS
		TEXT("platform-tools\\adb.exe"),
#else
		TEXT("platform-tools/adb"),
#endif
		TEXT("shell getprop"), true);
}

bool FAndroidTargetPlatform::ShouldExpandTo32Bit(const uint16* Indices, const int32 NumIndices) const
{
	bool bIsMaliBugIndex = false;
	const uint16 MaliBugIndexMaxDiff = 16;
	uint16 LastIndex = Indices[0];
	for (int32 i = 1; i < NumIndices; ++i)
	{
		uint16 CurrentIndex = Indices[i];
		if ((FMath::Abs(LastIndex - CurrentIndex) > MaliBugIndexMaxDiff))
		{
			bIsMaliBugIndex = true;
			break;
		}
		else
		{
			LastIndex = CurrentIndex;
		}
	}
	return bIsMaliBugIndex;
}

/* FAndroidTargetPlatform callbacks
 *****************************************************************************/

bool FAndroidTargetPlatform::HandleTicker( float DeltaTime )
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FAndroidTargetPlatform_HandleTicker);

	if (DeviceDetection == nullptr)
	{
		InitializeDeviceDetection();
		checkf(DeviceDetection != nullptr, TEXT("A target platform didn't create a device detection object in InitializeDeviceDetection()!"));
	}

	TArray<FString> ConnectedDeviceIds;

	{
		FScopeLock ScopeLock(DeviceDetection->GetDeviceMapLock());

		auto DeviceIt = DeviceDetection->GetDeviceMap().CreateConstIterator();

		for (; DeviceIt; ++DeviceIt)
		{
			ConnectedDeviceIds.Add(DeviceIt.Key());

			const FAndroidDeviceInfo& DeviceInfo = DeviceIt.Value();

			// see if this device is already known
			if (Devices.Contains(DeviceIt.Key()))
			{
				FAndroidTargetDevicePtr TestDevice = Devices[DeviceIt.Key()];

				// ignore if authorization didn't change
				if (DeviceInfo.bAuthorizedDevice == TestDevice->IsAuthorized())
				{
					continue;
				}

				// remove it to add again
				TestDevice->SetConnected(false);
				Devices.Remove(DeviceIt.Key());

				DeviceLostEvent.Broadcast(TestDevice.ToSharedRef());
			}

			// check if this platform is supported by the extensions and version
			if (!SupportedByExtensionsString(DeviceInfo.GLESExtensions, DeviceInfo.GLESVersion))
			{
				continue;
			}

			// create target device
			FAndroidTargetDevicePtr& Device = Devices.Add(DeviceInfo.SerialNumber);

			Device = CreateTargetDevice(*this, DeviceInfo.SerialNumber, GetAndroidVariantName());

			Device->SetConnected(true);
			Device->SetModel(DeviceInfo.Model);
			Device->SetDeviceName(DeviceInfo.DeviceName);
			Device->SetAuthorized(DeviceInfo.bAuthorizedDevice);
			Device->SetVersions(DeviceInfo.SDKVersion, DeviceInfo.HumanAndroidVersion);

			DeviceDiscoveredEvent.Broadcast(Device.ToSharedRef());
		}
	}

	// remove disconnected devices
	for (auto Iter = Devices.CreateIterator(); Iter; ++Iter)
	{
		if (!ConnectedDeviceIds.Contains(Iter.Key()))
		{
			FAndroidTargetDevicePtr RemovedDevice = Iter.Value();
			RemovedDevice->SetConnected(false);

			Iter.RemoveCurrent();

			DeviceLostEvent.Broadcast(RemovedDevice.ToSharedRef());
		}
	}

	return true;
}

FAndroidTargetDeviceRef FAndroidTargetPlatform::CreateNewDevice(const FAndroidDeviceInfo &DeviceInfo)
{
	return MakeShareable(new FAndroidTargetDevice(*this, DeviceInfo.SerialNumber, GetAndroidVariantName()));
}

#undef LOCTEXT_NAMESPACE
