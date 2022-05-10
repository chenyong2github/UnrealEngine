// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/ConfigContext.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreMisc.h"
#include "Misc/MessageDialog.h"
#include "Misc/CommandLine.h"
#include "Misc/RemoteConfigIni.h"
#include "Misc/Paths.h"


FConfigContext::FConfigContext(FConfigCacheIni* InConfigSystem, bool InIsHierarchicalConfig, const FString& InPlatform, FConfigFile* DestConfigFile)
	: ConfigSystem(InConfigSystem)
	, Platform(InPlatform)
	, bIsHierarchicalConfig(InIsHierarchicalConfig)
{
	if (DestConfigFile != nullptr)
	{
		ConfigFile = DestConfigFile;
		bDoNotResetConfigFile = true;
	}

	if (Platform.IsEmpty())
	{
		// read from, for instance Windows
		Platform = FPlatformProperties::IniPlatformName();
		// but save Generated ini files to, say, WindowsEditor
		SavePlatform = FPlatformProperties::PlatformName();
	}
	else if (Platform == FPlatformProperties::IniPlatformName())
	{
		// but save Generated ini files to, say, WindowsEditor
		SavePlatform = FPlatformProperties::PlatformName();
	}
	else
	{
		SavePlatform = Platform;
	}


	// now set to defaults anything not already set
	EngineConfigDir = FPaths::EngineConfigDir();
	ProjectConfigDir = FPaths::SourceConfigDir();

	// set settings that apply when using GConfig
	if (ConfigSystem != nullptr && ConfigSystem == GConfig)
	{
		bWriteDestIni = true;
		bUseHierarchyCache = true;
		bAllowGeneratedIniWhenCooked = true;
		bAllowRemoteConfig = true;
	}
}

void FConfigContext::CachePaths()
{
	// these are needed for single ini files
	if (bIsHierarchicalConfig)
	{
		// for the hierarchy replacements, we need to have a directory called Config - or we will have to do extra processing for these non-standard cases
		check(EngineConfigDir.EndsWith(TEXT("Config/")));
		check(ProjectConfigDir.EndsWith(TEXT("Config/")));

		EngineRootDir = FPaths::GetPath(FPaths::GetPath(EngineConfigDir));
		ProjectRootDir = FPaths::GetPath(FPaths::GetPath(ProjectConfigDir));

		if (FPaths::IsUnderDirectory(ProjectRootDir, EngineRootDir))
		{
			FString RelativeDir = ProjectRootDir;
			FPaths::MakePathRelativeTo(RelativeDir, *EngineRootDir);
			ProjectNotForLicenseesDir = FPaths::Combine(EngineRootDir, TEXT("Restricted/NotForLicensees"), RelativeDir);
			ProjectNoRedistDir = FPaths::Combine(EngineRootDir, TEXT("Restricted/NoRedist"), RelativeDir);
		}
		else
		{
			ProjectNotForLicenseesDir = FPaths::Combine(ProjectRootDir, TEXT("Restricted/NotForLicensees"));
			ProjectNoRedistDir = FPaths::Combine(ProjectRootDir, TEXT("Restricted/NoRedist"));
		}
	}
}

FConfigContext& FConfigContext::ResetBaseIni(const TCHAR* InBaseIniName)
{
	// for now, there's nothing that needs to be updated, other than the name here
	BaseIniName = InBaseIniName;

	if (!bDoNotResetConfigFile)
	{
		ConfigFile = nullptr;
	}

	return *this;
}

const FConfigContext::FPerPlatformDirs& FConfigContext::GetPerPlatformDirs(const FString& PlatformName)
{
	FConfigContext::FPerPlatformDirs* Dirs = FConfigContext::PerPlatformDirs.Find(PlatformName);
	if (Dirs == nullptr)
	{
		Dirs = &PerPlatformDirs.Emplace(PlatformName, FConfigContext::FPerPlatformDirs
			{
				// PlatformExtensionEngineDir
				FPaths::Combine(*FPaths::EnginePlatformExtensionsDir(), *PlatformName).Replace(*FPaths::EngineDir(), *(EngineRootDir + "/")),
				// PlatformExtensionProjectDir
				FPaths::Combine(*FPaths::ProjectPlatformExtensionsDir(), *PlatformName).Replace(*FPaths::ProjectDir(), *(ProjectRootDir + "/"))
			});
	}
	return *Dirs;
}

bool FConfigContext::Load(const TCHAR* InBaseIniName, FString& OutFinalFilename)
{
	if (bCacheOnNextLoad || BaseIniName != InBaseIniName)
	{
		ResetBaseIni(InBaseIniName);
		CachePaths();
		bCacheOnNextLoad = false;
	}


	bool bPerformLoad;
	if (!PrepareForLoad(bPerformLoad))
	{
		return false;
	}

	// if we are reloading a known ini file (where OutFinalIniFilename already has a value), then we need to leave the OutFinalFilename alone until we can remove LoadGlobalIniFile completely
	if (OutFinalFilename.Len() > 0 && OutFinalFilename == BaseIniName)
	{
		// do nothing
	}
	else
	{
		check(!bWriteDestIni || !DestIniFilename.IsEmpty());

		OutFinalFilename = DestIniFilename;
	}

	// now load if we need (PrepareForLoad may find an existing file and just use it)
	return bPerformLoad ? PerformLoad() : true;
}

bool FConfigContext::Load(const TCHAR* InBaseIniName)
{
	FString Discard;
	return Load(InBaseIniName, Discard);
}


bool FConfigContext::PrepareForLoad(bool& bPerformLoad)
{
	checkf(ConfigSystem != nullptr || ConfigFile != nullptr, TEXT("Loading config expects to either have a ConfigFile already passed in, or have a ConfigSystem passed in"));

	if (bForceReload)
	{
		// re-use an existing ConfigFile's Engine/Project directories if we have a config system to look in,
		// or no config system and the platform matches current platform (which will look in GConfig)
		if (ConfigSystem != nullptr || (Platform == FPlatformProperties::IniPlatformName() && GConfig != nullptr))
		{
			bool bNeedRecache = false;
			FConfigCacheIni* SearchSystem = ConfigSystem == nullptr ? GConfig : ConfigSystem;
			FConfigFile* BaseConfigFile = SearchSystem->FindConfigFileWithBaseName(*BaseIniName);
			if (BaseConfigFile != nullptr)
			{
				if (!BaseConfigFile->SourceEngineConfigDir.IsEmpty() && BaseConfigFile->SourceEngineConfigDir != EngineConfigDir)
				{
					EngineConfigDir = BaseConfigFile->SourceEngineConfigDir;
					bNeedRecache = true;
				}
				if (!BaseConfigFile->SourceProjectConfigDir.IsEmpty() && BaseConfigFile->SourceProjectConfigDir != ProjectConfigDir)
				{
					ProjectConfigDir = BaseConfigFile->SourceProjectConfigDir;
					bNeedRecache = true;
				}
				if (bNeedRecache)
				{
					CachePaths();
				}
			}
		}

	}

	// setup for writing out later on
	if (bWriteDestIni || bAllowGeneratedIniWhenCooked || FPlatformProperties::RequiresCookedData())
	{
		// delay filling out GeneratedConfigDir because some early configs can be read in that set -savedir, and 
		// FPaths::GeneratedConfigDir() will permanently cache the value
		if (GeneratedConfigDir.IsEmpty())
		{
			GeneratedConfigDir = FPaths::GeneratedConfigDir();
		}

		// calculate where this file will be saved/generated to (or at least the key to look up in the ConfigSystem)
		DestIniFilename = FConfigCacheIni::GetDestIniFilename(*BaseIniName, *SavePlatform, *GeneratedConfigDir);

		if (bAllowRemoteConfig)
		{
			// Start the loading process for the remote config file when appropriate
			if (FRemoteConfig::Get()->ShouldReadRemoteFile(*DestIniFilename))
			{
				FRemoteConfig::Get()->Read(*DestIniFilename, *BaseIniName);
			}

			FRemoteConfigAsyncIOInfo* RemoteInfo = FRemoteConfig::Get()->FindConfig(*DestIniFilename);
			if (RemoteInfo && (!RemoteInfo->bWasProcessed || !FRemoteConfig::Get()->IsFinished(*DestIniFilename)))
			{
				// Defer processing this remote config file to until it has finish its IO operation
				bPerformLoad = false;
				return false;
			}
		}
	}

	// we can re-use an existing file if:
	//   we are not loading into an existing ConfigFile
	//   we don't want to reload
	//   we found an existing file in the ConfigSystem
	//   the existing file has entries (because Known config files are always going to be found, but they will be empty)
	bool bLookForExistingFile = ConfigFile == nullptr && !bForceReload && ConfigSystem != nullptr;
	if (bLookForExistingFile)
	{
		// look up a file that already exists and matches the name
		FConfigFile* FoundConfigFile = ConfigSystem->KnownFiles.GetMutableFile(*BaseIniName);
		if (FoundConfigFile == nullptr)
		{
			FoundConfigFile = ConfigSystem->FindConfigFile(*DestIniFilename);
			//// @todo: this is test to see if we can simplify this to FindConfigFileWithBaseName always (if it never fires, we can)
			//check(FoundConfigFile == nullptr || FoundConfigFile == ConfigSystem->FindConfigFileWithBaseName(*BaseIniName))
		}

		if (FoundConfigFile != nullptr && FoundConfigFile->Num() > 0)
		{
			ConfigFile = FoundConfigFile;
			bPerformLoad = false;
			return true;
		}
	}

	// setup ConfigFile to read into if one isn't already set
	if (ConfigFile == nullptr)
	{
		// first look for a KnownFile
		ConfigFile = ConfigSystem->KnownFiles.GetMutableFile(*BaseIniName);
		if (ConfigFile == nullptr)
		{
			check(!DestIniFilename.IsEmpty());

			ConfigFile = &ConfigSystem->Add(DestIniFilename, FConfigFile());
		}
	}

	bPerformLoad = true;
	return true;
}

bool FConfigContext::PerformLoad()
{
	LLM_SCOPE(ELLMTag::ConfigSystem);

	// if bIsBaseIniName is false, that means the .ini is a ready-to-go .ini file, and just needs to be loaded into the FConfigFile
	if (!bIsHierarchicalConfig)
	{
		// generate path to the .ini file (not a Default ini, IniName is the complete name of the file, without path)
		DestIniFilename = FString::Printf(TEXT("%s/%s.ini"), *ProjectConfigDir, *BaseIniName);

		// load the .ini file straight up
		LoadAnIniFile(*DestIniFilename, *ConfigFile);

		ConfigFile->Name = FName(*BaseIniName);
		ConfigFile->PlatformName.Reset();
		ConfigFile->bHasPlatformName = false;
	}
	else
	{
#if DISABLE_GENERATED_INI_WHEN_COOKED
		if (BaseIniName == TEXT("GameUserSettings"))
		{
			// If we asked to disable ini when cooked, disable all ini files except GameUserSettings, which stores user preferences
			bAllowGeneratedIniWhenCooked = false;
			if (FPlatformProperties::RequiresCookedData())
			{
				ConfigFile->NoSave = true;
			}
		}
#endif

		// generate the whole standard ini hierarchy
		ConfigFile->AddStaticLayersToHierarchy(*this);

		// clear previous source config file and reset
		delete ConfigFile->SourceConfigFile;
		ConfigFile->SourceConfigFile = new FConfigFile();

		// now generate and make sure it's up to date (using IniName as a Base for an ini filename)
		// @todo This bNeedsWrite afaict is always true even if it loaded a completely valid generated/final .ini, and the write below will
		// just write out the exact same thing it read in!
		bool bNeedsWrite = GenerateDestIniFile(*this);

		ConfigFile->Name = FName(*BaseIniName);
		ConfigFile->PlatformName = Platform;
		ConfigFile->bHasPlatformName = true;

		// don't write anything to disk in cooked builds - we will always use re-generated INI files anyway.
		// Note: Unfortunately bAllowGeneratedIniWhenCooked is often true even in shipping builds with cooked data
		// due to default parameters. We don't dare change this now.
		//
		// Check GIsInitialLoad since no INI changes that should be persisted could have occurred this early.
		// INI changes from code, environment variables, CLI parameters, etc should not be persisted. 
		if (!GIsInitialLoad && bWriteDestIni && (!FPlatformProperties::RequiresCookedData() || bAllowGeneratedIniWhenCooked)
			// We shouldn't save config files when in multiprocess mode,
			// otherwise we get file contention in XGE shader builds.
			&& !FParse::Param(FCommandLine::Get(), TEXT("Multiprocess")))
		{
			// Check the config system for any changes made to defaults and propagate through to the saved.
			ConfigFile->ProcessSourceAndCheckAgainstBackup();

			if (bNeedsWrite)
			{
				// if it was dirtied during the above function, save it out now
				ConfigFile->Write(DestIniFilename);
			}
		}
	}

	// GenerateDestIniFile returns true if nothing is loaded, so check if we actually loaded something
	return ConfigFile->Num() > 0;
}


