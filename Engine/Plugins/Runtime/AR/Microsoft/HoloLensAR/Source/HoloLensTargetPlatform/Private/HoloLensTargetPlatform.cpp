// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HoloLensTargetPlatform.cpp: Implements the FHoloLensTargetPlatform class.
=============================================================================*/

#include "HoloLensTargetPlatform.h"
#include "HoloLensTargetDevice.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ScopeLock.h"
#include "HttpModule.h"
#include "PlatformHttp.h"
#include "Interfaces/IHttpResponse.h"
#include "HoloLensPlatformEditor.h"
#include "GeneralProjectSettings.h"

DEFINE_LOG_CATEGORY_STATIC(LogHoloLensTargetPlatform, Log, All);

FHoloLensTargetPlatform::FHoloLensTargetPlatform()
	: HoloLensDeviceDetectorModule(IHoloLensDeviceDetectorModule::Get())
{
#if WITH_ENGINE
	FConfigCacheIni::LoadLocalIniFile(EngineSettings, TEXT("Engine"), true, *PlatformName());
	TextureLODSettings = nullptr; // These are registered by the device profile system.
	StaticMeshLODSettings.Initialize(EngineSettings);
#endif

	DeviceDetectedRegistration = HoloLensDeviceDetectorModule.OnDeviceDetected().AddRaw(this, &FHoloLensTargetPlatform::OnDeviceDetected);
}

FHoloLensTargetPlatform::~FHoloLensTargetPlatform()
{
	IHoloLensDeviceDetectorModule::Get().OnDeviceDetected().Remove(DeviceDetectedRegistration);
}

void FHoloLensTargetPlatform::GetAllDevices(TArray<ITargetDevicePtr>& OutDevices) const
{
	HoloLensDeviceDetectorModule.StartDeviceDetection();

	OutDevices.Reset();
	FScopeLock Lock(&DevicesLock);
	OutDevices = Devices;
}

ITargetDevicePtr FHoloLensTargetPlatform::GetDevice(const FTargetDeviceId& DeviceId)
{
	if (PlatformName() == DeviceId.GetPlatformName())
	{
		IHoloLensDeviceDetectorModule::Get().StartDeviceDetection();

		FScopeLock Lock(&DevicesLock);
		for (ITargetDevicePtr Device : Devices)
		{
			if (DeviceId == Device->GetId())
			{
				return Device;
			}
		}
	}

	return nullptr;
}

ITargetDevicePtr FHoloLensTargetPlatform::GetDefaultDevice() const
{
	IHoloLensDeviceDetectorModule::Get().StartDeviceDetection();

	FScopeLock Lock(&DevicesLock);
	for (ITargetDevicePtr RemoteDevice : Devices)
	{
		if (RemoteDevice->IsDefault())
		{
			return RemoteDevice;
		}
	}

	return nullptr;
}

bool FHoloLensTargetPlatform::SupportsFeature(ETargetPlatformFeatures Feature) const
{
	if (Feature == ETargetPlatformFeatures::Packaging)
	{
		return true;
	}

	return TTargetPlatformBase<FHoloLensPlatformProperties>::SupportsFeature(Feature);
}

#if WITH_ENGINE

void FHoloLensTargetPlatform::GetReflectionCaptureFormats(TArray<FName>& OutFormats) const
{
	OutFormats.Add(FName(TEXT("FullHDR")));
	OutFormats.Add(FName(TEXT("EncodedHDR")));
}

const FPlatformAudioCookOverrides* FHoloLensTargetPlatform::GetAudioCompressionSettings() const
{
	return nullptr;
}

void FHoloLensTargetPlatform::GetTextureFormats(const UTexture* InTexture, TArray< TArray<FName> >& OutFormats) const
{
	GetDefaultTextureFormatNamePerLayer(OutFormats.AddDefaulted_GetRef(), this, InTexture, EngineSettings, false);
}

void FHoloLensTargetPlatform::GetAllTextureFormats(TArray<FName>& OutFormats) const
{
	GetAllDefaultTextureFormats(this, OutFormats, false);
}

static FName NAME_PCD3D_ES3_1(TEXT("PCD3D_ES31"));
static FName NAME_PCD3D_SM4(TEXT("PCD3D_SM4"));
static FName NAME_PCD3D_SM5(TEXT("PCD3D_SM5"));

void FHoloLensTargetPlatform::GetAllPossibleShaderFormats(TArray<FName>& OutFormats) const
{
	OutFormats.AddUnique(NAME_PCD3D_ES3_1);
	OutFormats.AddUnique(NAME_PCD3D_SM5);
	OutFormats.AddUnique(NAME_PCD3D_SM4);
}

void FHoloLensTargetPlatform::GetAllTargetedShaderFormats(TArray<FName>& OutFormats) const
{
	OutFormats.AddUnique(NAME_PCD3D_ES3_1);
	OutFormats.AddUnique(NAME_PCD3D_SM5);
	OutFormats.AddUnique(NAME_PCD3D_SM4);
}

#endif

void FHoloLensTargetPlatform::OnDeviceDetected(const FHoloLensDeviceInfo& Info)
{
	if (SupportsDevice(Info.DeviceTypeName, Info.bIs64Bit))
	{
		// Don't automatically add remote devices that require credentials.  They
		// must be manually added by the user so that we can collect those credentials.
		if (Info.IsLocal() || !Info.bRequiresCredentials)
		{
			FHoloLensDevicePtr NewDevice = MakeShared<FHoloLensTargetDevice, ESPMode::ThreadSafe>(*this, Info);
			{
				FScopeLock Lock(&DevicesLock);
				Devices.Add(NewDevice);
			}
			DeviceDiscoveredEvent.Broadcast(NewDevice.ToSharedRef());
		}
	}
}

bool FHoloLensTargetPlatform::SupportsBuildTarget(EBuildTargets::Type BuildTarget) const
{
	return BuildTarget == EBuildTargets::Game;
}

bool FHoloLensTargetPlatform::IsSdkInstalled(bool bProjectHasCode, FString& OutDocumentationPath) const
{
	OutDocumentationPath = TEXT("Platforms/HoloLens/GettingStarted");

	const TArray<FHoloLensSDKVersion>& SDKVersions = FHoloLensSDKVersion::GetSDKVersions();
	return SDKVersions.Num() > 0;
}

int32 FHoloLensTargetPlatform::CheckRequirements(const FString& ProjectPath, bool bProjectHasCode, FString& OutTutorialPath, FString& OutDocumentationPath, FText& CustomizedLogMessage) const
{
	OutDocumentationPath = TEXT("Platforms/HoloLens/GettingStarted");
	FString LocalErrors;

	int32 BuildStatus = ETargetPlatformReadyStatus::Ready;
	if (!IsSdkInstalled(bProjectHasCode, OutTutorialPath))
	{
		BuildStatus |= ETargetPlatformReadyStatus::SDKNotFound;
	}
	FString PublisherIdentityName = GetDefault<UGeneralProjectSettings>()->CompanyDistinguishedName;
	if (PublisherIdentityName.IsEmpty())
	{
		LocalErrors += TEXT("Missing Company Distinguished Name (See Project Settings).");
		BuildStatus |= ETargetPlatformReadyStatus::SigningKeyNotFound;
	}
	else
	{
		if (PublisherIdentityName.Contains(TEXT("CN=")) && PublisherIdentityName.Len() == 3)
		{
			LocalErrors += TEXT(" Malformed Company Distinguished Name (See Project Settings).");
			BuildStatus |= ETargetPlatformReadyStatus::SigningKeyNotFound;
		}
	}
	FString ProjectName = GetDefault<UGeneralProjectSettings>()->ProjectName;
	if (ProjectName.IsEmpty())
	{
		LocalErrors += TEXT(" Missing Project Name (See Project Settings).");
		BuildStatus |= ETargetPlatformReadyStatus::SigningKeyNotFound;
	}

	// Set the path if missing any of the bits needed for signing
	if (BuildStatus & ETargetPlatformReadyStatus::SigningKeyNotFound)
	{
		OutDocumentationPath = TEXT("Platforms/HoloLens/Signing");
	}

	if (BuildStatus != ETargetPlatformReadyStatus::Ready)
	{
		UE_LOG(LogHoloLensTargetPlatform, Warning, TEXT("FHoloLensTargetPlatform::CheckRequirements found these problems: %s"), *LocalErrors);
	}

	return BuildStatus;
}
