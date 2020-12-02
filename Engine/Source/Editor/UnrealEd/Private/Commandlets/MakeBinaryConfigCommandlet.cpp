// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commandlets/MakeBinaryConfigCommandlet.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"

UMakeBinaryConfigCommandlet::UMakeBinaryConfigCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

int32 UMakeBinaryConfigCommandlet::Main(const FString& Params)
{
	FString OutputFile;
	if (!FParse::Value(FCommandLine::Get(), TEXT("OutputFile="), OutputFile))
	{
		UE_LOG(LogTemp, Fatal, TEXT("OutputFile= parameter required"));
		return -1;
	}

	FString StagedPluginsFile;
	if (!FParse::Value(FCommandLine::Get(), TEXT("StagedPluginsFile="), StagedPluginsFile))
	{
		UE_LOG(LogTemp, Fatal, TEXT("StagedPluginsFile= parameter required"));
		return -1;
	}

	// only expecting one targetplatform
	const TArray<ITargetPlatform*>& Platforms = GetTargetPlatformManagerRef().GetActiveTargetPlatforms();
	check(Platforms.Num() == 1);
	FString PlatformName = Platforms[0]->IniPlatformName();

	FConfigCacheIni Config(EConfigCacheType::Temporary);
	FConfigCacheIni::FConfigNamesForAllPlatforms FinalConfigFilenames;
	Config.InitializePlatformConfigSystem(*PlatformName, FinalConfigFilenames);

	// removing for now, because this causes issues with some plugins not getting ini files merged in
//	IPluginManager::Get().IntegratePluginsIntoConfig(Config, *FinalConfigFilenames.EngineIni, *PlatformName, *StagedPluginsFile);

	// pull out black list entries

	TArray<FString> BlacklistKeyStrings;
	TArray<FString> BlacklistSections;
	GConfig->GetArray(TEXT("/Script/UnrealEd.ProjectPackagingSettings"), TEXT("IniKeyBlacklist"), BlacklistKeyStrings, GGameIni);
	GConfig->GetArray(TEXT("/Script/UnrealEd.ProjectPackagingSettings"), TEXT("IniSectionBlacklist"), BlacklistSections, GGameIni);
	TArray<FName> BlacklistKeys;
	for (FString Key : BlacklistKeyStrings)
	{
		BlacklistKeys.Add(FName(*Key));
	}

	for (TPair<FString, FConfigFile>& FilePair : Config)
	{
		FConfigFile& File = FilePair.Value;

		delete File.SourceConfigFile;
		File.SourceConfigFile = nullptr;

		for (FString Section : BlacklistSections)
		{
			File.Remove(Section);
		}

		// now go over any remaining sections and remove keys
		for (TPair<FString, FConfigSection>& SectionPair : File)
		{
			FConfigSection& Section = SectionPair.Value;
			for (FName Key : BlacklistKeys)
			{
				Section.Remove(Key);
			}
		}
	}

	// check the blacklist removed itself
	BlacklistKeyStrings.Empty();
	Config.GetArray(TEXT("/Script/UnrealEd.ProjectPackagingSettings"), TEXT("IniKeyBlacklist"), BlacklistKeyStrings, GGameIni);
	check(BlacklistKeyStrings.Num() == 0);

	// allow delegates to modify the config data with some tagged binary data
	FCoreDelegates::FExtraBinaryConfigData ExtraData(Config, true);
	FCoreDelegates::AccessExtraBinaryConfigData.Broadcast(ExtraData);

	// write it all out!
	TArray<uint8> FileContent;
	{
		// Use FMemoryWriter because FileManager::CreateFileWriter doesn't serialize FName as string and is not overridable
		FMemoryWriter MemoryWriter(FileContent, true);

		Config.Serialize(MemoryWriter);
		MemoryWriter << FinalConfigFilenames;
		MemoryWriter << ExtraData.Data;
	}

	if (!FFileHelper::SaveArrayToFile(FileContent, *OutputFile))
	{
		UE_LOG(LogTemp, Fatal, TEXT("Failed to create Config.bin file"));
		return -1;
	}

	return 0;
}
