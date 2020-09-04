// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Engine/EngineTypes.h"
#include "LuminRuntimeSettings.generated.h"

UENUM(BlueprintType)
enum class ELuminComponentSubElementType : uint8
{
	FileExtension,
	MimeType,
	Mode,
	MusicAttribute,
	Schema
};

UENUM(BlueprintType)
enum class ELuminComponentType : uint8
{
	Universe,
	Fullscreen,
	SearchProvider,
	MusicService,
	Console,
	SystemUI,
};

USTRUCT(BlueprintType)
struct FLuminComponentSubElement
{
	GENERATED_BODY()

	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Runtime", Meta = (DisplayName = "Component sub-node type"))
	ELuminComponentSubElementType ElementType;

	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Runtime", Meta = (DisplayName = "Component sub-node value"))
	FString Value;
};

USTRUCT(BlueprintType)
struct FLuminComponentElement
{
	GENERATED_BODY()

	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Runtime", Meta = (DisplayName = "Name"))
	FString Name;

	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Runtime", Meta = (DisplayName = "Visiable name"))
	FString VisibleName;

	/** Name of the executable for this component. This binary should be packaged into the bin folder of the mpk. Refer to ResonanceAudio_LPL.xml for an example. */
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Runtime", Meta = (DisplayName = "Executable name"))
	FString ExecutableName;

	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Runtime", Meta = (DisplayName = "Component type"))
	ELuminComponentType ComponentType;

	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Runtime", Meta = (DisplayName = "Extra sub-elements"))
	TArray<FLuminComponentSubElement> ExtraComponentSubElements;
};

UENUM(BlueprintType)
enum class ELuminFrameTimingHint : uint8
{
	/* Default rate is unspecified, adjusted based on system conditions. */
	Unspecified,
	/* Run at the maximum rate allowed by the system. */
	Maximum,
	/* Run at a specified rate of 60Hz (i.e. one frame every ~16.67 ms). */
	FPS_60,
	/* Run at a specified rate of 120Hz (i.e. one frame every ~8.33 ms). */
	FPS_120
};

UENUM(BlueprintType)
enum class ELuminPrivilege : uint8
{
	Invalid,
	BatteryInfo,
	CameraCapture,
	ComputerVision,
	WorldReconstruction,
	InAppPurchase,
	AudioCaptureMic,
	DrmCertificates,
	Occlusion,
	LowLatencyLightwear,
	Internet,
	IdentityRead,
	BackgroundDownload,
	BackgroundUpload,
	MediaDrm,
	Media,
	MediaMetadata,
	PowerInfo,
	LocalAreaNetwork,
	VoiceInput,
	Documents,
	ConnectBackgroundMusicService,
	RegisterBackgroundMusicService,
	PcfRead,
	PwFoundObjRead = ELuminPrivilege::PcfRead,
	NormalNotificationsUsage,
	MusicService,
	ControllerPose,
	GesturesSubscribe,
	GesturesConfig,
	AddressBookRead,
	AddressBookWrite,
	AddressBookBasicAccess,
	CoarseLocation,
	FineLocation,
	HandMesh,
	WifiStatusRead,
	SocialConnectionsInvitesAccess,
	SocialConnectionsSelectAccess,
	SecureBrowserWindow,
	BluetoothAdapterExternalApp,
	BluetoothAdapterUser,
	BluetoothGattWrite,
};

USTRUCT(BlueprintType)
struct FLocalizedAppName
{
	GENERATED_BODY()
	
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Localization", Meta = (DisplayName = "Language Code"))
	FString LanguageCode;

	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Localization", Meta = (DisplayName = "App Name"))
	FString AppName;
};

USTRUCT(BlueprintType)
struct FLocalizedIconInfo
{
	GENERATED_BODY()

	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Localization", Meta = (DisplayName = "Language Code"))
	FString LanguageCode;

	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Localization", Meta = (DisplayName = "Icon Model Path"))
	FDirectoryPath IconModelPath;

	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Localization", Meta = (DisplayName = "Icon Portal Path"))
	FDirectoryPath IconPortalPath;
};

USTRUCT(BlueprintType)
struct FLocalizedIconInfos 
{
	GENERATED_BODY()

	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Localization", Meta = (DisplayName = "Icon Data"))
	TArray<FLocalizedIconInfo> IconData;
};

/**
 * IMPORTANT!! Add a default value for every new UPROPERTY in the ULuminRuntimeSettings class in <UnrealEngine>/Engine/Config/BaseEngine.ini
 */

/**
 * Implements the settings for the Lumin runtime platform.
 */
UCLASS(config=Engine, defaultconfig)
class LUMINRUNTIMESETTINGS_API ULuminRuntimeSettings : public UObject
{
public:
	GENERATED_BODY()

	/** The official name of the project. Note: Must have at least 2 sections separated by a period and be unique! */
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "MPK Packaging", Meta = (DisplayName = "Magic Leap Package Name ('com.Company.Project', [PROJECT] is replaced with project name)"))
	FString PackageName;

	/** The visual application name displayed for end users. */
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "MPK Packaging", Meta = (DisplayName = "Application Display Name (project name if blank)"))
	FString ApplicationDisplayName;

	/** Indicates to the Lumin OS what the application's target framerate is, to improve prediction and reprojection */
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Runtime", Meta = (DisplayName = "Frame timing hint"))
	ELuminFrameTimingHint FrameTimingHint;

	/** Content for this app is protected and should not be recorded or captured outside the graphics system. */
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Runtime", Meta = (DisplayName = "Protected Content"))
	bool bProtectedContent;

	/**
	 * Check this if you wish to manually control when the start up loading animation is dismissed.
	 * @note If this is checked, the developer MUST call MagicLeapHMDFunctionLibrary::SetAppReady
	 *       in order for their application to finish booting.
	 */
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Runtime", Meta = (DisplayName = "Manual call to 'set ready indication'"))
	bool bManualCallToAppReady;

	/** If checked, use Mobile Rendering. Otherwise, use Desktop Rendering. */
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Build", Meta = (DisplayName = "Use Mobile Rendering (otherwise, Desktop Rendering)"))
	bool bUseMobileRendering;

	UPROPERTY(GlobalConfig, Meta = (DisplayName = "Use Vulkan (otherwise, OpenGL)"))
	bool bUseVulkan;

	/** Certificate File used to sign builds for distribution. */
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Distribution Signing", Meta = (DisplayName = "Certificate File Path"))
	FFilePath Certificate;
	
	/** Folder containing the assets (FBX / OBJ / MTL / PNG files) used for the Magic Leap App Icon model. */
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Magic Leap App Tile", Meta = (DisplayName = "Icon Model"))
	FDirectoryPath IconModelPath;

	/** Folder containing the assets (FBX / OBJ / MTL / PNG files) used for the Magic Leap App Icon portal. */
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Magic Leap App Tile", Meta = (DisplayName = "Icon Portal"))
	FDirectoryPath IconPortalPath;

	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Magic Leap App Tile", Meta = (DisplayName = "Localized Icon Infos"))
	FLocalizedIconInfos LocalizedIconInfos;

	/** Used as an internal version number. This number is used only to determine whether one version is more recent than another, with higher numbers indicating more recent versions. This is not the version number shown to users. */
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Advanced MPK Packaging", Meta = (DisplayName = "Version Code", ClampMin = 0))
	int32 VersionCode;

	/** Minimum API level required based on which APIs have been integrated into the base engine. Developers can set higher API levels if they are implementing newer APIs. */
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Advanced MPK Packaging", Meta = (DisplayName = "Minimum API Level", ClampMin = 2))
	int32 MinimumAPILevel;

	/** Any privileges your app needs. */
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Advanced MPK Packaging", Meta = (DisplayName = "App Privileges"))
	TArray<ELuminPrivilege> AppPrivileges;

	/** Extra nodes under the <component> node like <mime-type>, <schema> etc. */
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Advanced MPK Packaging", Meta = (DisplayName = "Extra nodes under the <component> node"))
	TArray<FLuminComponentSubElement> ExtraComponentSubElements;

	/** Extra component elements. */
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Advanced MPK Packaging", Meta = (DisplayName = "Extra <component> elements."))
	TArray<FLuminComponentElement> ExtraComponentElements;

	/** Which of the currently enabled spatialization plugins to use on Lumin. */
	UPROPERTY(config, EditAnywhere, Category = "Audio")
	FString SpatializationPlugin;

	/** Which of the currently enabled reverb plugins to use on Lumin. */
	UPROPERTY(config, EditAnywhere, Category = "Audio")
	FString ReverbPlugin;

	/** Which of the currently enabled occlusion plugins to use on Lumin. */
	UPROPERTY(config, EditAnywhere, Category = "Audio")
	FString OcclusionPlugin;

	/** Quality Level to COOK SoundCues at (if set, all other levels will be stripped by the cooker). */
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Audio|CookOverrides", meta = (DisplayName = "Sound Cue Cook Quality"))
	int32 SoundCueCookQualityIndex = INDEX_NONE;

	// Strip debug symbols from packaged builds even if they aren't shipping builds.
	UPROPERTY(GlobalConfig, EditAnywhere, Category = AdvancedBuild, meta = (DisplayName = "Strip debug symbols from packaged builds even if they aren't shipping builds"))
	bool bRemoveDebugInfo;

	/** Folder containing the libraries required for vulkan validation layers. Can be found under %NDKROOT%/sources/third_party/vulkan/src/build-android/jniLibs/arm64-v8a */
	UPROPERTY(GlobalConfig, EditAnywhere, Category = AdvancedBuild, Meta = (DisplayName = "Vulkan Validation Layer libs"))
	FDirectoryPath VulkanValidationLayerLibs;

	/** Render frame vignette. */
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Rendering", Meta = (DisplayName = "Render frame vignette"))
	bool bFrameVignette;

	UPROPERTY(GlobalConfig, EditAnywhere, Category = "Advanced MPK Packaging", Meta = (DisplayName = "Localized App Names"))
	TArray<FLocalizedAppName> LocalizedAppNames;

	TArray<FString> ExtraApplicationNodes_DEPRECATED;
	TArray<FString> ExtraComponentNodes_DEPRECATED;
};
