// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "OculusPlatformToolSettings.generated.h"

UENUM()
enum class EOculusPlatformTarget : uint8
{
	Rift UMETA(DisplayName="Rift"),
	Quest UMETA(DisplayName="Quest"),
	Mobile UMETA(DisplayName="Oculus Go | Gear VR"),
	Length UMETA(DisplayName="Invalid")
};

/**
 * 
 */
UCLASS(config=Editor)
class OCULUSEDITOR_API UOculusPlatformToolSettings : public UObject
{
	GENERATED_BODY()

public:
	UOculusPlatformToolSettings();

	uint8 GetTargetPlatform()
	{
		return (uint8)OculusTargetPlatform;
	}
	void SetTargetPlatform(uint8 i)
	{
		OculusTargetPlatform = (EOculusPlatformTarget)i;
	}

	FString GetApplicationID()
	{
		return OculusTargetPlatform < EOculusPlatformTarget::Length ? OculusApplicationID[(uint8)OculusTargetPlatform] : "";
	}
	void SetApplicationID(FString s)
	{
		if (OculusTargetPlatform < EOculusPlatformTarget::Length)
		{
			OculusApplicationID[(uint8)OculusTargetPlatform] = s;
		}
	}

	FString GetApplicationToken()
	{
		return OculusTargetPlatform < EOculusPlatformTarget::Length ? OculusApplicationToken[(uint8)OculusTargetPlatform] : "";
	}
	void SetApplicationToken(FString s)
	{
		if (OculusTargetPlatform < EOculusPlatformTarget::Length)
		{
			OculusApplicationToken[(uint8)OculusTargetPlatform] = s;
		}
	}

	FString GetReleaseChannel()
	{
		return OculusTargetPlatform < EOculusPlatformTarget::Length ? OculusReleaseChannel[(uint8)OculusTargetPlatform] : "Alpha";
	}
	void SetReleaseChannel(FString s)
	{
		if (OculusTargetPlatform < EOculusPlatformTarget::Length)
		{
			OculusReleaseChannel[(uint8)OculusTargetPlatform] = s;
		}
	}

	FString GetReleaseNote()
	{
		return OculusTargetPlatform < EOculusPlatformTarget::Length ? OculusReleaseNote[(uint8)OculusTargetPlatform] : "";
	}
	void SetReleaseNote(FString s)
	{
		if (OculusTargetPlatform < EOculusPlatformTarget::Length)
		{
			OculusReleaseNote[(uint8)OculusTargetPlatform] = s;
		}
	}

	FString GetLaunchFilePath()
	{
		return OculusTargetPlatform < EOculusPlatformTarget::Length ? OculusLaunchFilePath[(uint8)OculusTargetPlatform] : "";
	}
	void SetLaunchFilePath(FString s)
	{
		if (OculusTargetPlatform < EOculusPlatformTarget::Length)
		{
			OculusLaunchFilePath[(uint8)OculusTargetPlatform] = s;
		}
	}

	UPROPERTY(config, EditAnywhere, Category = Oculus)
		FString OculusRiftBuildDirectory;

	UPROPERTY(config, EditAnywhere, Category = Oculus)
		FString OculusRiftBuildVersion;

private:
	UPROPERTY(config, EditAnywhere, Category = Oculus)
	EOculusPlatformTarget OculusTargetPlatform;

	UPROPERTY(config, EditAnywhere, Category = Oculus)
	TArray<FString> OculusApplicationID;

	UPROPERTY(config, EditAnywhere, Category = Oculus)
	TArray<FString> OculusApplicationToken;

	UPROPERTY(config, EditAnywhere, Category = Oculus)
	TArray<FString> OculusReleaseChannel;

	UPROPERTY(config, EditAnywhere, Category = Oculus)
	TArray<FString> OculusReleaseNote;

	UPROPERTY(config, EditAnywhere, Category = Oculus)
	TArray<FString> OculusLaunchFilePath;
};
