// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterConfigurationMgr.h"

#include "Formats/IDisplayClusterConfigurationDataParser.h"
#include "Formats/JSON/DisplayClusterConfigurationJsonParser.h"
#include "Formats/Text/DisplayClusterConfigurationTextParser.h"

#include "DisplayClusterConfigurationTypes.h"
#include "DisplayClusterConfigurationStrings.h"
#include "DisplayClusterConfigurationLog.h"

#include "Misc/DisplayClusterHelpers.h"
#include "Misc/Paths.h"


FDisplayClusterConfigurationMgr::FDisplayClusterConfigurationMgr()
{
}

FDisplayClusterConfigurationMgr::~FDisplayClusterConfigurationMgr()
{
}

FDisplayClusterConfigurationMgr& FDisplayClusterConfigurationMgr::Get()
{
	static FDisplayClusterConfigurationMgr Instance;
	return Instance;
}


UDisplayClusterConfigurationData* FDisplayClusterConfigurationMgr::LoadConfig(const FString& FilePath, UObject* Owner)
{
	FString ConfigFile = FilePath.TrimStartAndEnd();

	if (FPaths::IsRelative(ConfigFile))
	{
		ConfigFile = DisplayClusterHelpers::filesystem::GetFullPathForConfig(ConfigFile);
	}

	if (!FPaths::FileExists(ConfigFile))
	{
		UE_LOG(LogDisplayClusterConfiguration, Error, TEXT("File not found: %s"), *ConfigFile);
		return nullptr;
	}

	// Instantiate appropriate parser
	TUniquePtr<IDisplayClusterConfigurationDataParser> Parser;
	switch (GetConfigFileType(ConfigFile))
	{
	case EConfigFileType::Text:
		Parser = MakeUnique<FDisplayClusterConfigurationTextParser>();
		break;

	case EConfigFileType::Json:
		Parser = MakeUnique<FDisplayClusterConfigurationJsonParser>();
		break;

	default:
		UE_LOG(LogDisplayClusterConfiguration, Error, TEXT("Unknown config type"));
		return nullptr;
	}

	return Parser->LoadData(ConfigFile);
}

bool FDisplayClusterConfigurationMgr::SaveConfig(const UDisplayClusterConfigurationData* Config, const FString& FilePath)
{
	// Save to json only
	TUniquePtr<IDisplayClusterConfigurationDataParser> Parser = MakeUnique<FDisplayClusterConfigurationJsonParser>();
	check(Parser);
	return Parser->SaveData(Config, FilePath);
}

UDisplayClusterConfigurationData* FDisplayClusterConfigurationMgr::CreateDefaultStandaloneConfigData()
{
	// Not implemented yet
	return nullptr;
}

FDisplayClusterConfigurationMgr::EConfigFileType FDisplayClusterConfigurationMgr::GetConfigFileType(const FString& InConfigPath) const
{
	const FString Extension = FPaths::GetExtension(InConfigPath).ToLower();
	if (Extension.Equals(FString(DisplayClusterConfigurationStrings::file::FileExtJson), ESearchCase::IgnoreCase))
	{
		UE_LOG(LogDisplayClusterConfiguration, Log, TEXT("JSON config: %s"), *InConfigPath);
		return EConfigFileType::Json;
	}
	else if (Extension.Equals(FString(DisplayClusterConfigurationStrings::file::FileExtCfg), ESearchCase::IgnoreCase))
	{
		UE_LOG(LogDisplayClusterConfiguration, Log, TEXT("TXT config: %s"), *InConfigPath);
		return EConfigFileType::Text;
	}

	UE_LOG(LogDisplayClusterConfiguration, Warning, TEXT("Unknown file extension: %s"), *Extension);
	return EConfigFileType::Unknown;
}
