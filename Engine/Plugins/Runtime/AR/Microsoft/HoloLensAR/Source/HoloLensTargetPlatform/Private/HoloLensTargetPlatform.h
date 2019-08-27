// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HoloLensTargetPlatform.h: Declares the FXboxOneTargetPlatform class.
=============================================================================*/

#pragma once

#include "Common/TargetPlatformBase.h"
#include "Runtime/Core/Public/HoloLens/HoloLensPlatformProperties.h"
#include "Misc/ConfigCacheIni.h"
#include "HoloLensTargetDevice.h"
#include "Misc/ScopeLock.h"
#include "IHoloLensDeviceDetectorModule.h"

#if PLATFORM_WINDOWS || PLATFORM_HOLOLENS
#include "Windows/AllowWindowsPlatformTypes.h"
#endif

#if WITH_ENGINE
#include "StaticMeshResources.h"
#endif // WITH_ENGINE

#define LOCTEXT_NAMESPACE "HoloLensTargetPlatform"

/**
 * FHoloLensTargetPlatform, abstraction for cooking HoloLens platforms
 */
class HOLOLENSTARGETPLATFORM_API FHoloLensTargetPlatform
	: public TTargetPlatformBase<FHoloLensPlatformProperties>
{
public:

	/**
	 * Default constructor.
	 */
	FHoloLensTargetPlatform();

	/**
	 * Destructor.
	 */
	virtual ~FHoloLensTargetPlatform();

public:

	//~ Begin ITargetPlatform Interface

	virtual bool AddDevice(const FString& DeviceName, bool bDefault) override { return false; }

	virtual void EnableDeviceCheck(bool OnOff) override {}

	virtual void GetAllDevices(TArray<ITargetDevicePtr>& OutDevices) const override;

	virtual ITargetDevicePtr GetDefaultDevice() const override;

	virtual ITargetDevicePtr GetDevice(const FTargetDeviceId& DeviceId) override;

	//virtual ECompressionFlags GetBaseCompressionMethod() const override { return ECompressionFlags::COMPRESS_ZLIB; }

	virtual bool GenerateStreamingInstallManifest(const TMultiMap<FString, int32>& ChunkMap, const TSet<int32>& ChunkIDsInUse) const override { return true; }

	virtual bool IsRunningPlatform() const override { return false; }

	virtual bool SupportsFeature(ETargetPlatformFeatures Feature) const override;

	virtual bool SupportsBuildTarget(EBuildTargetType TargetType) const override;

#if WITH_ENGINE
	virtual void GetReflectionCaptureFormats(TArray<FName>& OutFormats) const override;

	virtual const FPlatformAudioCookOverrides* GetAudioCompressionSettings() const override;

	virtual const class FStaticMeshLODSettings& GetStaticMeshLODSettings() const override { return StaticMeshLODSettings; }

	virtual void GetTextureFormats(const UTexture* InTexture, TArray< TArray<FName> >& OutFormats) const override;

	virtual const UTextureLODSettings& GetTextureLODSettings() const override { return *TextureLODSettings; }

	virtual void GetAllTextureFormats(TArray<FName>& OutFormats) const override;

	virtual void RegisterTextureLODSettings(const UTextureLODSettings* InTextureLODSettings) override
	{
		TextureLODSettings = InTextureLODSettings;
	}

	virtual void GetAllPossibleShaderFormats(TArray<FName>& OutFormats) const override;

	virtual void GetAllTargetedShaderFormats(TArray<FName>& OutFormats) const override;
	
	//virtual void GetAllCachedShaderFormats( TArray<FName>& OutFormats ) const override {}

	virtual FName GetWaveFormat( const class USoundWave* Wave ) const override
	{
		static FName NAME_OGG(TEXT("OGG"));

		return NAME_OGG;
	}

	virtual void GetAllWaveFormats(TArray<FName>& OutFormats) const override
	{
		static FName NAME_OGG(TEXT("OGG"));
		static FName NAME_OPUS(TEXT("OPUS"));
		OutFormats.Add(NAME_OGG);
		OutFormats.Add(NAME_OPUS);
	}

#endif //WITH_ENGINE

	DECLARE_DERIVED_EVENT(FHoloLensTargetPlatform, ITargetPlatform::FOnTargetDeviceDiscovered, FOnTargetDeviceDiscovered);
	virtual FOnTargetDeviceDiscovered& OnDeviceDiscovered( ) override
	{
		return DeviceDiscoveredEvent;
	}

	DECLARE_DERIVED_EVENT(FHoloLensTargetPlatform, ITargetPlatform::FOnTargetDeviceLost, FOnTargetDeviceLost);
	virtual FOnTargetDeviceLost& OnDeviceLost( ) override
	{
		return DeviceLostEvent;
	}

	virtual bool RequiresUserCredentials() const override
	{
		return true;
	}

	virtual bool SupportsVariants() const override
	{
		return true;
	}

	virtual FText GetVariantTitle() const override
	{
		return LOCTEXT("HoloLensVariantTitle", "Build Type");
	}

	virtual bool IsSdkInstalled(bool bProjectHasCode, FString& OutDocumentationPath) const override;
	virtual int32 CheckRequirements(const FString& ProjectPath, bool bProjectHasCode, FString& OutTutorialPath, FString& OutDocumentationPath, FText& CustomizedLogMessage) const override;
	//~ End ITargetPlatform Interface

protected:

	virtual bool SupportsDevice(FName DeviceType, bool DeviceIs64Bits) = 0;

private:

	void OnDeviceDetected(const FHoloLensDeviceInfo& Info);

	mutable FCriticalSection DevicesLock;
	TArray<ITargetDevicePtr> Devices;

	FDelegateHandle DeviceDetectedRegistration;

#if WITH_ENGINE
	// Holds the Engine INI settings (for quick access).
	FConfigFile EngineSettings;

	// Holds a cache of the target LOD settings.
	const UTextureLODSettings* TextureLODSettings;

	// Holds the static mesh LOD settings.
	FStaticMeshLODSettings StaticMeshLODSettings;
#endif //WITH_ENGINE

private:

	// Holds an event delegate that is executed when a new target device has been discovered.
	FOnTargetDeviceDiscovered DeviceDiscoveredEvent;

	// Holds an event delegate that is executed when a target device has been lost, i.e. disconnected or timed out.
	FOnTargetDeviceLost DeviceLostEvent;

	IHoloLensDeviceDetectorModule& HoloLensDeviceDetectorModule;
};


#undef LOCTEXT_NAMESPACE

#include "Windows/HideWindowsPlatformTypes.h"
