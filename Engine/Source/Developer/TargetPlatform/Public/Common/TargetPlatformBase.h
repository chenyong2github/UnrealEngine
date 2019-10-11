// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/ITargetPlatform.h"
#include "PlatformInfo.h"

/**
 * Base class for target platforms.
 */
class TARGETPLATFORM_VTABLE FTargetPlatformBase
	: public ITargetPlatform
{
public:

	// ITargetPlatform interface

	virtual bool AddDevice( const FString& DeviceName, bool bDefault ) override
	{
		return false;
	}

	virtual FText DisplayName() const override
	{
		return PlatformInfo->DisplayName;
	}

	virtual const PlatformInfo::FPlatformInfo& GetPlatformInfo() const override
	{
		return *PlatformInfo;
	}

	TARGETPLATFORM_API virtual bool UsesForwardShading() const override;

	TARGETPLATFORM_API virtual bool UsesDBuffer() const override;

	TARGETPLATFORM_API virtual bool UsesBasePassVelocity() const override;

	TARGETPLATFORM_API virtual bool UsesSelectiveBasePassOutputs() const override;
	
	TARGETPLATFORM_API virtual bool UsesDistanceFields() const override;

	TARGETPLATFORM_API virtual float GetDownSampleMeshDistanceFieldDivider() const override;

#if WITH_ENGINE
	virtual void GetReflectionCaptureFormats( TArray<FName>& OutFormats ) const override
	{
		OutFormats.Add(FName(TEXT("FullHDR")));
	}

	virtual FName GetVirtualTextureLayerFormat(
		int32 SourceFormat,
		bool bAllowCompression, bool bNoAlpha,
		bool bSupportDX11TextureFormats, int32 Settings) const override
	{
		return FName();
	}
#endif //WITH_ENGINE

	virtual bool PackageBuild( const FString& InPackgeDirectory ) override
	{
		return true;
	}

	virtual bool CanSupportXGEShaderCompile() const override
	{
		return true;
	}

	virtual bool IsSdkInstalled(bool bProjectHasCode, FString& OutDocumentationPath) const override
	{
		return true;
	}

	virtual int32 CheckRequirements(bool bProjectHasCode, EBuildConfiguration Configuration, bool bRequiresAssetNativization, FString& OutTutorialPath, FString& OutDocumentationPath, FText& CustomizedLogMessage) const override
	{
		int32 bReadyToBuild = ETargetPlatformReadyStatus::Ready; // @todo How do we check that the iOS SDK is installed when building from Windows? Is that even possible?
		if (!IsSdkInstalled(bProjectHasCode, OutTutorialPath))
		{
			bReadyToBuild |= ETargetPlatformReadyStatus::SDKNotFound;
		}
		return bReadyToBuild;
	}

	TARGETPLATFORM_API virtual bool RequiresTempTarget(bool bProjectHasCode, EBuildConfiguration Configuration, bool bRequiresAssetNativization, FText& OutReason) const override;

	virtual bool SupportsValueForType(FName SupportedType, FName RequiredSupportedValue) const override
	{
#if WITH_ENGINE
		// check if the given shader format is returned by this TargetPlatform
		if (SupportedType == TEXT("ShaderFormat"))
		{
			TArray<FName> AllPossibleShaderFormats;
			GetAllPossibleShaderFormats(AllPossibleShaderFormats);
			return AllPossibleShaderFormats.Contains(RequiredSupportedValue);
		}
#endif
		return false;
	}

	virtual bool SupportsVariants() const override
	{
		return false;
	}

	virtual FText GetVariantDisplayName() const override
	{
		return FText();
	}

	virtual FText GetVariantTitle() const override
	{
		return FText();
	}

	virtual float GetVariantPriority() const override
	{
		return IsClientOnly() ? 0.0f : 0.2f;
	}

	virtual bool SendLowerCaseFilePaths() const override
	{
		return false;
	}

	virtual void GetBuildProjectSettingKeys(FString& OutSection, TArray<FString>& InBoolKeys, TArray<FString>& InIntKeys, TArray<FString>& InStringKeys) const override
	{
		// do nothing in the base class
	}

	virtual void RefreshSettings() override
	{
	}

	virtual int32 GetPlatformOrdinal() const override
	{
		return PlatformOrdinal;
	}

	TARGETPLATFORM_API virtual TSharedPtr<IDeviceManagerCustomPlatformWidgetCreator> GetCustomWidgetCreator() const override;

protected:

	FTargetPlatformBase(const PlatformInfo::FPlatformInfo *const InPlatformInfo)
		: PlatformInfo(InPlatformInfo)
	{
		checkf(PlatformInfo, TEXT("Null PlatformInfo was passed to FTargetPlatformBase. Check the static IsUsable function before creating this object. See FWindowsTargetPlatformModule::GetTargetPlatform()"));

		PlatformOrdinal = AssignPlatformOrdinal(*this);
	}

	/** Information about this platform */
	const PlatformInfo::FPlatformInfo *PlatformInfo;
	int32 PlatformOrdinal;

private:
	bool HasDefaultBuildSettings() const;
	static bool DoProjectSettingsMatchDefault(const FString& InPlatformName, const FString& InSection, const TArray<FString>* InBoolKeys, const TArray<FString>* InIntKeys, const TArray<FString>* InStringKeys);
};


/**
 * Template for target platforms.
 *
 * @param TPlatformProperties Type of platform properties.
 */
template<typename TPlatformProperties>
class TTargetPlatformBase
	: public FTargetPlatformBase
{
public:

	/**
	 * Returns true if the target platform will be able to be  initialized with an FPlatformInfo. Because FPlatformInfo now comes from a .ini file,
	 * it's possible that the .dll exists, but the .ini does not (should be uncommon, but is necessary to be handled)
	 */
	static bool IsUsable()
	{
		return PlatformInfo::FindPlatformInfo(TPlatformProperties::PlatformName()) != nullptr;
	}
	
	/** Default constructor. */
	TTargetPlatformBase()
		: FTargetPlatformBase( PlatformInfo::FindPlatformInfo(TPlatformProperties::PlatformName()) )
	{
		// HasEditorOnlyData and RequiresCookedData are mutually exclusive.
		check(TPlatformProperties::HasEditorOnlyData() != TPlatformProperties::RequiresCookedData());
	}

public:

	// ITargetPlatform interface

	virtual bool HasEditorOnlyData() const override
	{
		return TPlatformProperties::HasEditorOnlyData();
	}

	virtual bool IsLittleEndian() const override
	{
		return TPlatformProperties::IsLittleEndian();
	}

	virtual bool IsServerOnly() const override
	{
		return TPlatformProperties::IsServerOnly();
	}

	virtual bool IsClientOnly() const override
	{
		return TPlatformProperties::IsClientOnly();
	}

	virtual FString PlatformName() const override
	{
		return FString(TPlatformProperties::PlatformName());
	}

	virtual FString IniPlatformName() const override
	{
		return FString(TPlatformProperties::IniPlatformName());
	}

	virtual bool RequiresCookedData() const override
	{
		return TPlatformProperties::RequiresCookedData();
	}

	virtual bool HasSecurePackageFormat() const override
	{
		return TPlatformProperties::HasSecurePackageFormat();
	}

	virtual bool RequiresUserCredentials() const override
	{
		return TPlatformProperties::RequiresUserCredentials();
	}

	virtual bool SupportsBuildTarget( EBuildTargetType TargetType ) const override
	{
		return TPlatformProperties::SupportsBuildTarget(TargetType);
	}

	virtual bool SupportsAutoSDK() const override
	{
		return TPlatformProperties::SupportsAutoSDK();
	}

	virtual bool SupportsFeature( ETargetPlatformFeatures Feature ) const override
	{
		switch (Feature)
		{
		case ETargetPlatformFeatures::AudioStreaming:
			return TPlatformProperties::SupportsAudioStreaming();

		case ETargetPlatformFeatures::DistanceFieldShadows:
			return TPlatformProperties::SupportsDistanceFieldShadows();

		case ETargetPlatformFeatures::DistanceFieldAO:
			return TPlatformProperties::SupportsDistanceFieldAO();

		case ETargetPlatformFeatures::GrayscaleSRGB:
			return TPlatformProperties::SupportsGrayscaleSRGB();

		case ETargetPlatformFeatures::HighQualityLightmaps:
			return TPlatformProperties::SupportsHighQualityLightmaps();

		case ETargetPlatformFeatures::LowQualityLightmaps:
			return TPlatformProperties::SupportsLowQualityLightmaps();

		case ETargetPlatformFeatures::MultipleGameInstances:
			return TPlatformProperties::SupportsMultipleGameInstances();

		case ETargetPlatformFeatures::Packaging:
			return false;

		case ETargetPlatformFeatures::Tessellation:
			return TPlatformProperties::SupportsTessellation();

		case ETargetPlatformFeatures::TextureStreaming:
			return TPlatformProperties::SupportsTextureStreaming();
		case ETargetPlatformFeatures::MeshLODStreaming:
			return TPlatformProperties::SupportsMeshLODStreaming();

		case ETargetPlatformFeatures::MemoryMappedFiles:
			return TPlatformProperties::SupportsMemoryMappedFiles();

		case ETargetPlatformFeatures::MemoryMappedAudio:
			return TPlatformProperties::SupportsMemoryMappedAudio();
		case ETargetPlatformFeatures::MemoryMappedAnimation:
			return TPlatformProperties::SupportsMemoryMappedAnimation();

		case ETargetPlatformFeatures::VirtualTextureStreaming:
			return TPlatformProperties::SupportsVirtualTextureStreaming();

		case ETargetPlatformFeatures::SdkConnectDisconnect:
		case ETargetPlatformFeatures::UserCredentials:
			break;

		case ETargetPlatformFeatures::MobileRendering:
			return false;
		case ETargetPlatformFeatures::DeferredRendering:
			return true;

		case ETargetPlatformFeatures::ShouldSplitPaksIntoSmallerSizes :
			return false;

		case ETargetPlatformFeatures::HalfFloatVertexFormat:
			return true;
		}

		return false;
	}
	virtual FName GetZlibReplacementFormat() const override
	{
		return TPlatformProperties::GetZlibReplacementFormat() != nullptr ? FName(TPlatformProperties::GetZlibReplacementFormat()) : NAME_Zlib;
	}

	virtual int32 GetMemoryMappingAlignment() const override
	{
		return TPlatformProperties::GetMemoryMappingAlignment();
	}


#if WITH_ENGINE
	virtual FName GetPhysicsFormat( class UBodySetup* Body ) const override
	{
		return FName(TPlatformProperties::GetPhysicsFormat());
	}
#endif // WITH_ENGINE
};
