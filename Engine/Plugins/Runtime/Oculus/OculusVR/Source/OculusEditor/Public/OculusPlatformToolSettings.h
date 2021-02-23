// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "OculusPlatformToolSettings.generated.h"

UENUM()
enum class EOculusPlatformTarget : uint8
{
	Rift UMETA(DisplayName="Rift"),
	Quest UMETA(DisplayName="Quest"),
	Length UMETA(DisplayName="Invalid")
};

UENUM()
enum class EOculusGamepadEmulation : uint8
{
	Off UMETA(DisplayName="Off"),
	Twinstick UMETA(DisplayName = "Twinstick"),
	RightDPad UMETA(DisplayName = "Right D Pad"),
	LeftDPad UMETA(DisplayName = "Left D Pad"),
	Length UMETA(DisplayName = "Invalid")
};

UENUM()
enum class EOculusAssetType : uint8
{
	Default UMETA(DisplayName="Default"),
	Store UMETA(DisplayName="Store"),
	Language_Pack UMETA(DisplayName="Language Pack"),
	Length UMETA(DisplayName="Invlaid"),
};

USTRUCT()
struct FRedistPackage
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(config, EditAnywhere, Category = Oculus)
	bool Included = false;

	UPROPERTY(config, EditAnywhere, Category = Oculus)
	FString Name;

	UPROPERTY(config, EditAnywhere, Category = Oculus)
	FString Id;
};

USTRUCT()
struct FAssetConfig
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(config, EditAnywhere, Category = Oculus)
	EOculusAssetType AssetType = EOculusAssetType::Default;

	UPROPERTY(config, EditAnywhere, Category = Oculus)
	bool Required = false;

	UPROPERTY(config, EditAnywhere, Category = Oculus)
	FString Name;

	UPROPERTY(config, EditAnywhere, Category = Oculus)
	FString Sku;
};

USTRUCT()
struct FAssetConfigArray
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(config, EditAnywhere, Category = Oculus)
	TArray<FAssetConfig> ConfigArray;
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
		return (uint8)OculusTargetPlatform < OculusApplicationID.Num() ? OculusApplicationID[(uint8)OculusTargetPlatform] : "";
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
		return (uint8)OculusTargetPlatform < OculusApplicationToken.Num() ? OculusApplicationToken[(uint8)OculusTargetPlatform] : "";
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
		return (uint8)OculusTargetPlatform < OculusReleaseChannel.Num() ? OculusReleaseChannel[(uint8)OculusTargetPlatform] : "Alpha";
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
		return (uint8)OculusTargetPlatform < OculusReleaseNote.Num() ? OculusReleaseNote[(uint8)OculusTargetPlatform] : "";
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
		return (uint8)OculusTargetPlatform < OculusLaunchFilePath.Num() ? OculusLaunchFilePath[(uint8)OculusTargetPlatform] : "";
	}
	void SetLaunchFilePath(FString s)
	{
		if (OculusTargetPlatform < EOculusPlatformTarget::Length)
		{
			OculusLaunchFilePath[(uint8)OculusTargetPlatform] = s;
		}
	}

	EOculusGamepadEmulation GetRiftGamepadEmulation()
	{
		return OculusRiftGamepadEmulation;
	}
	void SetRiftGamepadEmulation(uint8 i)
	{
		OculusRiftGamepadEmulation = (EOculusGamepadEmulation)i;
	}

	FString GetLanguagePacksPath()
	{
		return (uint8)OculusTargetPlatform < OculusLanguagePacksPath.Num() ? OculusLanguagePacksPath[(uint8)OculusTargetPlatform] : "";
	}
	void SetLanguagePacksPath(FString s)
	{
		if (OculusTargetPlatform < EOculusPlatformTarget::Length)
		{
			OculusLanguagePacksPath[(uint8)OculusTargetPlatform] = s;
		}
	}

	FString GetExpansionFilesPath()
	{
		return (uint8)OculusTargetPlatform < OculusExpansionFilesPath.Num() ? OculusExpansionFilesPath[(uint8)OculusTargetPlatform] : "";
	}
	void SetExpansionFilesPath(FString s)
	{
		if (OculusTargetPlatform < EOculusPlatformTarget::Length)
		{
			OculusExpansionFilesPath[(uint8)OculusTargetPlatform] = s;
		}
	}

	FString GetSymbolDirPath()
	{
		return (uint8)OculusTargetPlatform < OculusSymbolDirPath.Num() ? OculusSymbolDirPath[(uint8)OculusTargetPlatform] : "";
	}
	void SetSymbolDirPath(FString s)
	{
		if (OculusTargetPlatform < EOculusPlatformTarget::Length)
		{
			OculusSymbolDirPath[(uint8)OculusTargetPlatform] = s;
		}
	}

	TArray<FAssetConfig>* GetAssetConfigs()
	{
		return (uint8)OculusTargetPlatform < OculusAssetConfigs.Num() ? &OculusAssetConfigs[(uint8)OculusTargetPlatform].ConfigArray : NULL;
	}

	UPROPERTY(config, EditAnywhere, Category = Oculus)
	FString OculusRiftBuildDirectory;

	UPROPERTY(config, EditAnywhere, Category = Oculus)
	FString OculusRiftBuildVersion;

	UPROPERTY(config, EditAnywhere, Category = Oculus)
	FString OculusRiftLaunchParams;

	UPROPERTY(config, EditAnywhere, Category = Oculus)
	bool OculusRiftFireWallException;

	UPROPERTY(config, EditAnywhere, Category = Oculus)
	FString OculusRift2DLaunchPath;

	UPROPERTY(config, EditAnywhere, Category = Oculus)
	FString OculusRift2DLaunchParams;

	UPROPERTY(config, EditAnywhere, Category = Oculus)
	TArray<FRedistPackage> OculusRedistPackages;

	UPROPERTY(config, EditAnywhere, Category = Oculus)
	bool UploadDebugSymbols;

	UPROPERTY(config, EditAnywhere, Category = Oculus)
	bool DebugSymbolsOnly;

	UPROPERTY(config, EditAnywhere, Category = Oculus)
	FString BuildID;

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

	UPROPERTY(config, EditAnywhere, Category = Oculus)
	EOculusGamepadEmulation OculusRiftGamepadEmulation;

	UPROPERTY(config, EditAnywhere, Category = Oculus)
	TArray<FString> OculusLanguagePacksPath;

	UPROPERTY(config, EditAnywhere, Category = Oculus)
	TArray<FString> OculusExpansionFilesPath;

	UPROPERTY(config, EditAnywhere, Category = Oculus)
	TArray<FString> OculusSymbolDirPath;

	UPROPERTY(config, EditAnywhere, Category = Oculus)
	TArray<FAssetConfigArray> OculusAssetConfigs;
};
