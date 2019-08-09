// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "HoloLensTargetDevice.h"

#include "Misc/Paths.h"

#if PLATFORM_WINDOWS || PLATFORM_HOLOLENS
#include "Windows/AllowWindowsPlatformTypes.h"
#endif

#include "Windows/ComPointer.h"
#include "HoloLensBuildLib.h"

WindowsMixedReality::HoloLensBuildLib hlLib;

bool FHoloLensTargetDevice::Deploy(const FString& SourceFolder, FString& OutAppId)
{
	return Info.bCanDeployTo;
}

bool FHoloLensTargetDevice::Launch(const FString& AppId, EBuildConfiguration BuildConfiguration, EBuildTargetType BuildTarget, const FString& Params, uint32* OutProcessId)
{
	return false;
}

bool FHoloLensTargetDevice::Run(const FString& ExecutablePath, const FString& Params, uint32* OutProcessId)
{
	HRESULT hr = CoInitialize(nullptr);
	if (FAILED(hr))
	{
		UE_LOG(LogTemp, Warning, TEXT("FHoloLensTargetDevice::Run - CoInitialize() failed with hr = 0x(%x)"), hr);
	}

	// Currently even packaged builds get an exe name in here which kind of works because we 
	// don't yet support remote deployment and so the loose structure the package was created 
	// from is probably in place on this machine.  So the code will read the manifest from the
	// loose version, but actually launch the package (since that's what's registered).
	bool PathIsActuallyPackage = FPaths::GetExtension(ExecutablePath) == TEXT("appx");
	FString StreamPath;
	if (PathIsActuallyPackage)
	{
		StreamPath = ExecutablePath;
	}
	else
	{
		StreamPath = FPaths::Combine(*FPaths::GetPath(ExecutablePath), TEXT("../../.."));
		StreamPath = FPaths::Combine(*StreamPath, TEXT("AppxManifest.xml"));
	}

	return hlLib.PackageProject(*StreamPath, PathIsActuallyPackage, *Params, OutProcessId);
}

#include "Windows/HideWindowsPlatformTypes.h"
