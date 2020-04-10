// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformFilemanager.h"
#include "Logging/LogMacros.h"
#include "Misc/ConfigCacheIni.h"
#include "SoundControlBusMix.h"
#include "SoundModulationValue.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"


namespace AudioModulation
{
	class FProfileSerializer
	{
		static bool ChannelValueToFloat(const FConfigSection& InSection, const FName& InMember, float& OutValue)
		{
			if (const FConfigValue* Value = InSection.Find(InMember))
			{
				OutValue = FCString::Atof(*Value->GetValue());
				return true;
			}

			return false;
		}

		static FString GetConfigDir()
		{
			return FPaths::GeneratedConfigDir() + TEXT("AudioModulation");
		}

		static bool GetConfigDirectory(const FString& InDir, bool bCreateDirIfMissing)
		{
			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
			if (!bCreateDirIfMissing && PlatformFile.DirectoryExists(*InDir))
			{
				return false;
			}

			TArray<FString> DirArray;
			InDir.ParseIntoArray(DirArray, TEXT("/"));

			FString SubPath = GetConfigDir();

			if (!PlatformFile.CreateDirectory(*SubPath))
			{
				return false;
			}

			for (FString& Dir : DirArray)
			{
				SubPath /= Dir;
				if (!PlatformFile.CreateDirectory(*SubPath))
				{
					return false;
				}
			}

			return true;
		}

		static bool GetMixProfileConfigPath(const FString& Name, const FString& Path, const int32 InProfileIndex, bool bInCreateDirIfMissing, FString& OutMixProfileConfig)
		{
			FString ConfigDir = FPaths::GetPath(Path);
			FString ConfigPath = GetConfigDir() + ConfigDir / Name;

			ConfigPath += InProfileIndex > 0
				? FString::Printf(TEXT("%i.ini"), InProfileIndex)
				: FString(TEXT(".ini"));

			if (!GConfig)
			{
				UE_LOG(LogAudioModulation, Error,
					TEXT("Failed to find mix profile '%s' config file. Config cache not initialized."),
					*Path, *ConfigPath);
				return false;
			}

			if (!GetConfigDirectory(ConfigDir, bInCreateDirIfMissing))
			{
				UE_LOG(LogAudioModulation, Error, TEXT("Failed to %s mix profile '%s' config directory."),
					bInCreateDirIfMissing ? TEXT("create") : TEXT("find"),
					*Path,
					*ConfigDir);
				return false;
			}

			OutMixProfileConfig = ConfigPath;
			return true;
		}

	public:
		static bool Serialize(const USoundControlBusMix& InBusMix, const int32 InProfileIndex, const FString* InMixPathOverride = nullptr)
		{
			check(IsInGameThread());

			const FString Name = InMixPathOverride ? FPaths::GetBaseFilename(*InMixPathOverride) : InBusMix.GetName();
			const FString Path = InMixPathOverride ? *InMixPathOverride : InBusMix.GetPathName();

			FString ConfigPath;
			if (!GetMixProfileConfigPath(Name, Path, InProfileIndex, true /* bCreateDir */, ConfigPath))
			{
				return false;
			}

			FConfigFile& ConfigFile = GConfig->FindOrAdd(ConfigPath);
			ConfigFile.Reset();

			for (const FSoundControlBusMixChannel& Channel : InBusMix.Channels)
			{
				if (Channel.Bus)
				{
					FConfigSection& ConfigSection = ConfigFile.Add(Channel.Bus->GetPathName());
					ConfigSection.Add(GET_MEMBER_NAME_CHECKED(FSoundModulationValue, AttackTime), FConfigValue(FString::Printf(TEXT("%f"), Channel.Value.AttackTime)));
					ConfigSection.Add(GET_MEMBER_NAME_CHECKED(FSoundModulationValue, ReleaseTime), FConfigValue(FString::Printf(TEXT("%f"), Channel.Value.ReleaseTime)));
					ConfigSection.Add(GET_MEMBER_NAME_CHECKED(FSoundModulationValue, TargetValue), FConfigValue(FString::Printf(TEXT("%f"), Channel.Value.TargetValue)));
				}
			}
			ConfigFile.Dirty = true;

			UE_LOG(LogAudioModulation, Display, TEXT("Serialized mix to '%s'"), *Path);
			GConfig->Flush(false, ConfigPath);

			return true;
		}

		static bool Deserialize(int32 InProfileIndex, USoundControlBusMix& InOutBusMix, const FString* InMixPathOverride = nullptr)
		{
			check(IsInGameThread());

			const FString Name = InMixPathOverride ? FPaths::GetBaseFilename(*InMixPathOverride) : InOutBusMix.GetName();
			const FString Path = InMixPathOverride ? *InMixPathOverride : InOutBusMix.GetPathName();

			FString ConfigPath;
			if (!GetMixProfileConfigPath(Name, Path, InProfileIndex, false /* bCreateDir */, ConfigPath))
			{
				return false;
			}

			FConfigFile* ConfigFile = GConfig->Find(ConfigPath, false /* CreateIfNotFound */);
			if (!ConfigFile)
			{
				UE_LOG(LogAudioModulation, Warning, TEXT("Config '%s' not found. Mix '%s' not loaded from config profile."), *ConfigPath , *Name);
				return false;
			}

			bool bMarkDirty = false;
			TSet<FString> SectionsProcessed;
			TArray<FSoundControlBusMixChannel>& Channels = InOutBusMix.Channels;
			for (int32 i = Channels.Num() - 1; i >= 0; --i)
			{
				FSoundControlBusMixChannel& Channel = Channels[i];
				if (Channel.Bus)
				{
					FString PathName = Channel.Bus->GetPathName();
					if (FConfigSection* ConfigSection = ConfigFile->Find(PathName))
					{
						ChannelValueToFloat(*ConfigSection, GET_MEMBER_NAME_CHECKED(FSoundModulationValue, AttackTime), Channel.Value.AttackTime);
						ChannelValueToFloat(*ConfigSection, GET_MEMBER_NAME_CHECKED(FSoundModulationValue, ReleaseTime), Channel.Value.ReleaseTime);
						ChannelValueToFloat(*ConfigSection, GET_MEMBER_NAME_CHECKED(FSoundModulationValue, TargetValue), Channel.Value.TargetValue);
						SectionsProcessed.Emplace(MoveTemp(PathName));
						bMarkDirty = true;
					}
					else
					{
						Channels.RemoveAt(i);
					}
				}
				else
				{
					Channels.RemoveAt(i);
				}
			}

			TArray<FString> ConfigSectionNames;
			ConfigFile->GetKeys(ConfigSectionNames);
			for (FString& SectionName : ConfigSectionNames)
			{
				if (!SectionsProcessed.Contains(SectionName))
				{
					const FConfigSection& ConfigSection = ConfigFile->FindChecked(SectionName);
					UObject* BusObj = FSoftObjectPath(SectionName).TryLoad();
					if (USoundControlBusBase* Bus = Cast<USoundControlBusBase>(BusObj))
					{
						FSoundControlBusMixChannel Channel;
						Channel.Bus = Bus;

						ChannelValueToFloat(ConfigSection, GET_MEMBER_NAME_CHECKED(FSoundModulationValue, AttackTime),  Channel.Value.AttackTime);
						ChannelValueToFloat(ConfigSection, GET_MEMBER_NAME_CHECKED(FSoundModulationValue, ReleaseTime), Channel.Value.ReleaseTime);
						ChannelValueToFloat(ConfigSection, GET_MEMBER_NAME_CHECKED(FSoundModulationValue, TargetValue), Channel.Value.TargetValue);

						Channels.Emplace(MoveTemp(Channel));

						bMarkDirty = true;
					}
					else
					{
						UE_LOG(LogAudioModulation, Warning,
							TEXT("Bus missing or invalid type at '%s'. Profile '%s' channel not added/updated for mix '%s'"),
							*SectionName,
							*ConfigPath,
							*Path);
					}
				}
			}

			if (bMarkDirty)
			{
				InOutBusMix.MarkPackageDirty();
			}

			UE_LOG(LogAudioModulation, Display, TEXT("Deserialized mix from '%s'"), *ConfigPath);
			return true;
		}
	};
} // namespace AudioModulation