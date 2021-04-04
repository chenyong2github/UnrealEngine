// Copyright Epic Games, Inc. All Rights Reserved.

#include "PluginManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/FileManager.h"
#include "Misc/DataDrivenPlatformInfoRegistry.h"
#include "Misc/MessageDialog.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/CoreDelegates.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/FeedbackContext.h"
#include "ProjectDescriptor.h"
#include "Interfaces/IProjectManager.h"
#include "Modules/ModuleManager.h"
#include "ProjectManager.h"
#include "PluginManifest.h"
#include "HAL/PlatformTime.h"
#include "Async/ParallelFor.h"
#if READ_TARGET_ENABLED_PLUGINS_FROM_RECEIPT
#include "TargetReceipt.h"
#endif

DEFINE_LOG_CATEGORY_STATIC( LogPluginManager, Log, All );

#define LOCTEXT_NAMESPACE "PluginManager"


namespace PluginSystemDefs
{
	/** File extension of plugin descriptor files.
	    NOTE: This constant exists in UnrealBuildTool code as well. */
	static const TCHAR PluginDescriptorFileExtension[] = TEXT( ".uplugin" );

	/**
	 * Parsing the command line and loads any foreign plugins that were
	 * specified using the -PLUGIN= command.
	 *
	 * @param  CommandLine    The commandline used to launch the editor.
	 * @param  SearchPathsOut 
	 * @return The number of plugins that were specified using the -PLUGIN param.
	 */
	static int32 GetAdditionalPluginPaths(TSet<FString>& PluginPathsOut)
	{
		const TCHAR* SwitchStr = TEXT("PLUGIN=");
		const int32  SwitchLen = FCString::Strlen(SwitchStr);

		int32 PluginCount = 0;

		const TCHAR* SearchStr = FCommandLine::Get();
		do
		{
			FString PluginPath;

			SearchStr = FCString::Strifind(SearchStr, SwitchStr);
			if (FParse::Value(SearchStr, SwitchStr, PluginPath))
			{
				FString PluginDir = FPaths::GetPath(PluginPath);
				PluginPathsOut.Add(PluginDir);

				++PluginCount;
				SearchStr += SwitchLen + PluginPath.Len();
			}
			else
			{
				break;
			}
		} while (SearchStr != nullptr);

		return PluginCount;
	}
}

FPlugin::FPlugin(const FString& InFileName, const FPluginDescriptor& InDescriptor, EPluginType InType)
	: Name(FPaths::GetBaseFilename(InFileName))
	, FileName(InFileName)
	, Descriptor(InDescriptor)
	, Type(InType)
	, bEnabled(false)
{

}

FPlugin::~FPlugin()
{
}

FString FPlugin::GetBaseDir() const
{
	return FPaths::GetPath(FileName);
}

FString FPlugin::GetContentDir() const
{
	return FPaths::GetPath(FileName) / TEXT("Content");
}

FString FPlugin::GetMountedAssetPath() const
{
	FString Path;
	Path.Reserve(Name.Len() + 2);
	Path.AppendChar('/');
	Path.Append(Name);
	Path.AppendChar('/');
	return Path;
}

bool FPlugin::IsEnabledByDefault(const bool bAllowEnginePluginsEnabledByDefault) const
{
	if (Descriptor.EnabledByDefault == EPluginEnabledByDefault::Enabled)
	{
		return (GetLoadedFrom() == EPluginLoadedFrom::Project ? true : bAllowEnginePluginsEnabledByDefault);
	}
	else if (Descriptor.EnabledByDefault == EPluginEnabledByDefault::Disabled)
	{
		return false;
	}
	else
	{
		return GetLoadedFrom() == EPluginLoadedFrom::Project;
	}
}

EPluginLoadedFrom FPlugin::GetLoadedFrom() const
{
	if(Type == EPluginType::Engine || Type == EPluginType::Enterprise)
	{
		return EPluginLoadedFrom::Engine;
	}
	else
	{
		return EPluginLoadedFrom::Project;
	}
}

const FPluginDescriptor& FPlugin::GetDescriptor() const
{
	return Descriptor;
}

bool FPlugin::UpdateDescriptor(const FPluginDescriptor& NewDescriptor, FText& OutFailReason)
{
	if(!NewDescriptor.UpdatePluginFile(FileName, OutFailReason))
	{
		return false;
	}

	Descriptor = NewDescriptor;
	return true;
}

#if WITH_EDITOR
const TSharedPtr<FJsonObject>& FPlugin::GetDescriptorJson()
{
	return Descriptor.CachedJson;
}
#endif // WITH_EDITOR

FPluginManager::FPluginManager()
{
	SCOPED_BOOT_TIMING("DiscoverAllPlugins");
	DiscoverAllPlugins();
}

FPluginManager::~FPluginManager()
{
	// NOTE: All plugins and modules should be cleaned up or abandoned by this point

	// @todo plugin: Really, we should "reboot" module manager's unloading code so that it remembers at which startup phase
	//  modules were loaded in, so that we can shut groups of modules down (in reverse-load order) at the various counterpart
	//  shutdown phases.  This will fix issues where modules that are loaded after game modules are shutdown AFTER many engine
	//  systems are already killed (like GConfig.)  Currently the only workaround is to listen to global exit events, or to
	//  explicitly unload your module somewhere.  We should be able to handle most cases automatically though!
}

void FPluginManager::RefreshPluginsList()
{
	// Read a new list of all plugins
	TMap<FString, TSharedRef<FPlugin>> NewPlugins;
	ReadAllPlugins(NewPlugins, PluginDiscoveryPaths);

	// Build a list of filenames for plugins which are enabled, and remove the rest
	TArray<FString> EnabledPluginFileNames;
	for(TMap<FString, TSharedRef<FPlugin>>::TIterator Iter(AllPlugins); Iter; ++Iter)
	{
		const TSharedRef<FPlugin>& Plugin = Iter.Value();
		if(Plugin->bEnabled)
		{
			EnabledPluginFileNames.Add(Plugin->FileName);
		}
		else
		{
			Iter.RemoveCurrent();
		}
	}

	// Add all the plugins which aren't already enabled
	for(const TPair<FString, TSharedRef<FPlugin>>& NewPluginPair: NewPlugins)
	{
		const TSharedRef<FPlugin>& NewPlugin = NewPluginPair.Value;
		if(!EnabledPluginFileNames.Contains(NewPlugin->FileName))
		{
			uint32 PluginNameHash = GetTypeHash(NewPlugin->GetName());
			AllPlugins.AddByHash(PluginNameHash, NewPlugin->GetName(), NewPlugin);
			PluginsToConfigure.AddByHash(PluginNameHash, NewPlugin->GetName());
		}
	}
}

bool FPluginManager::AddToPluginsList(const FString& PluginFilename)
{
#if (WITH_ENGINE && !IS_PROGRAM) || WITH_PLUGIN_SUPPORT
	// No need to readd if it already exists
	FString PluginName = FPaths::GetBaseFilename(PluginFilename);
	if (AllPlugins.Contains(PluginName))
	{
		return true;
	}

	// Read the plugin and load it
	FPluginDescriptor Descriptor;
	FText FailureReason;
	if (Descriptor.Load(PluginFilename, FailureReason))
	{
		// Determine the plugin type
		EPluginType PluginType = EPluginType::External;
		if (PluginFilename.StartsWith(FPaths::EngineDir()))
		{
			PluginType = EPluginType::Engine;
		}
		else if (PluginFilename.StartsWith(FPaths::EnterpriseDir()))
		{
			PluginType = EPluginType::Enterprise;
		}
		else if (PluginFilename.StartsWith(FPaths::ProjectModsDir()))
		{
			PluginType = EPluginType::Mod;
		}
		else if (PluginFilename.StartsWith(FPaths::GetPath(FPaths::GetProjectFilePath())))
		{
			PluginType = EPluginType::Project;
		}

		// Create the plugin
		TMap<FString, TSharedRef<FPlugin>> NewPlugins;
		TArray<TSharedRef<FPlugin>> ChildPlugins;
		CreatePluginObject(PluginFilename, Descriptor, PluginType, NewPlugins, ChildPlugins);
		ensureMsgf(ChildPlugins.Num() == 0, TEXT("AddToPluginsList does not allow plugins with bIsPluginExtension set to true. Plugin: %s"), *PluginFilename);
		ensure(NewPlugins.Num() == 1);
		
		// Add the loaded plugin
		TSharedRef<FPlugin>* NewPlugin = NewPlugins.Find(PluginName);
		if (ensure(NewPlugin))
		{
			AllPlugins.Add(PluginName, *NewPlugin);
		}

		return true;
	}
	else
	{
		UE_LOG(LogPluginManager, Warning, TEXT("AddToPluginsList failed to load plugin %s. Reason: %s"), *PluginFilename, *FailureReason.ToString());
	}
#endif

	return false;
}

void FPluginManager::DiscoverAllPlugins()
{
	ensure( AllPlugins.Num() == 0 );		// Should not have already been initialized!

	PluginSystemDefs::GetAdditionalPluginPaths(PluginDiscoveryPaths);
	ReadAllPlugins(AllPlugins, PluginDiscoveryPaths);

	PluginsToConfigure.Reserve(AllPlugins.Num());
	for (const TPair<FString, TSharedRef<FPlugin>>& PluginPair : AllPlugins)
	{
		PluginsToConfigure.Add(PluginPair.Key);
	}
}

void FPluginManager::ReadAllPlugins(TMap<FString, TSharedRef<FPlugin>>& Plugins, const TSet<FString>& ExtraSearchPaths)
{
#if (WITH_ENGINE && !IS_PROGRAM) || WITH_PLUGIN_SUPPORT
	const FProjectDescriptor* Project = IProjectManager::Get().GetCurrentProject();

	// Find any plugin manifest files. These give us the plugin list (and their descriptors) without needing to scour the directory tree.
	TArray<FString> ManifestFileNames;
#if !WITH_EDITOR
	if (Project != nullptr)
	{
		FindPluginManifestsInDirectory(*FPaths::ProjectPluginsDir(), ManifestFileNames);
	}
#endif // !WITH_EDITOR

	// track child plugins that don't want to go into main plugin set
	TArray<TSharedRef<FPlugin>> ChildPlugins;

	// If we didn't find any manifests, do a recursive search for plugins
	if (ManifestFileNames.Num() == 0)
	{
		// Find "built-in" plugins.  That is, plugins situated right within the Engine directory.
		TArray<FString> EnginePluginDirs = FPaths::GetExtensionDirs(FPaths::EngineDir(), TEXT("Plugins"));
		for (const FString& EnginePluginDir : EnginePluginDirs)
		{
			ReadPluginsInDirectory(EnginePluginDir, EPluginType::Engine, Plugins, ChildPlugins);
		}

		// Find plugins in the game project directory (<MyGameProject>/Plugins). If there are any engine plugins matching the name of a game plugin,
		// assume that the game plugin version is preferred.
		if (Project != nullptr)
		{
			TArray<FString> ProjectPluginDirs = FPaths::GetExtensionDirs(FPaths::GetPath(FPaths::GetProjectFilePath()), TEXT("Plugins"));
			for (const FString& ProjectPluginDir : ProjectPluginDirs)
			{
				ReadPluginsInDirectory(ProjectPluginDir, EPluginType::Project, Plugins, ChildPlugins);
			}
		}
	}
	else
	{
		// Add plugins from each of the manifests
		for (const FString& ManifestFileName : ManifestFileNames)
		{
			UE_LOG(LogPluginManager, Verbose, TEXT("Reading plugin manifest: %s"), *ManifestFileName);
			FPluginManifest Manifest;

			// Try to load the manifest. We only expect manifests in a cooked game, so failing to load them is a hard error.
			FText FailReason;
			if (!Manifest.Load(ManifestFileName, FailReason))
			{
				UE_LOG(LogPluginManager, Fatal, TEXT("%s"), *FailReason.ToString());
			}

			// Get all the standard plugin directories
			const FString EngineDir = FPaths::EngineDir();
			const FString PlatformExtensionEngineDir = FPaths::EnginePlatformExtensionsDir();
			const FString EnterpriseDir = FPaths::EnterpriseDir();
			const FString ProjectModsDir = FPaths::ProjectModsDir();

			// Create all the plugins inside it
			for (const FPluginManifestEntry& Entry : Manifest.Contents)
			{
				EPluginType Type;
				if (Entry.File.StartsWith(EngineDir) || Entry.File.StartsWith(PlatformExtensionEngineDir))
				{
					Type = EPluginType::Engine;
				}
				else if (Entry.File.StartsWith(EnterpriseDir))
				{
					Type = EPluginType::Enterprise;
				}
				else if (Entry.File.StartsWith(ProjectModsDir))
				{
					Type = EPluginType::Mod;
				}
				else
				{
					Type = EPluginType::Project;
				}
				CreatePluginObject(Entry.File, Entry.Descriptor, Type, Plugins, ChildPlugins);
			}
		}
	}

	if (Project != nullptr)
	{
		// Always add the mods from the loose directory without using manifests, because they're not packaged together.
		ReadPluginsInDirectory(FPaths::ProjectModsDir(), EPluginType::Mod, Plugins, ChildPlugins);

		// If they have a list of additional directories to check, add those plugins too
		for (const FString& Dir : Project->GetAdditionalPluginDirectories())
		{
			ReadPluginsInDirectory(Dir, EPluginType::External, Plugins, ChildPlugins);
		}

		// Add plugins from FPaths::EnterprisePluginsDir if it exists
		if (FPaths::DirectoryExists(FPaths::EnterprisePluginsDir()))
		{
			ReadPluginsInDirectory(FPaths::EnterprisePluginsDir(), EPluginType::Enterprise, Plugins, ChildPlugins);
		}
	}

	for (const FString& ExtraSearchPath : ExtraSearchPaths)
	{
		ReadPluginsInDirectory(ExtraSearchPath, EPluginType::External, Plugins, ChildPlugins);
	}

	// now that we have all the plugins, merge child plugins
	for (TSharedRef<FPlugin> Child : ChildPlugins)
	{
		// find the parent
		TArray<FString> Tokens;
		FPaths::GetCleanFilename(Child->GetDescriptorFileName()).ParseIntoArray(Tokens, TEXT("_"), true);
		TSharedRef<FPlugin>* ParentPtr = nullptr;
		if (Tokens.Num() == 2)
		{
			FString ParentPluginName = Tokens[0];
			ParentPtr = Plugins.Find(ParentPluginName);
		}
		if (ParentPtr != nullptr)
		{
			TSharedRef<FPlugin>& Parent = *ParentPtr;
			for (const FModuleDescriptor& ChildModule : Child->GetDescriptor().Modules)
			{
				// look for a matching parent
				for (FModuleDescriptor& ParentModule : Parent->Descriptor.Modules)
				{
					if (ParentModule.Name == ChildModule.Name && ParentModule.Type == ChildModule.Type)
					{
						// we only need to whitelist the platform if the parent had a whitelist (otherwise, we could mistakenly remove all other platforms)
						if (ParentModule.WhitelistPlatforms.Num() > 0)
						{
							ParentModule.WhitelistPlatforms.Append(ChildModule.WhitelistPlatforms);
						}

						// if we want to blacklist a platform, add it even if the parent didn't have a blacklist. this won't cause problems with other platforms
						ParentModule.BlacklistPlatforms.Append(ChildModule.BlacklistPlatforms);
					}
				}
			}

			if (Parent->GetDescriptor().SupportedTargetPlatforms.Num() != 0)
			{
				for (const FString& SupportedTargetPlatform : Child->GetDescriptor().SupportedTargetPlatforms)
				{
					Parent->Descriptor.SupportedTargetPlatforms.AddUnique(SupportedTargetPlatform);
				}
			}
		}
		else
		{
			UE_LOG(LogPluginManager, Error, TEXT("Child plugin %s was not named properly. It should be in the form <ParentPlugin>_<Platform>.uplugin."), *Child->GetDescriptorFileName());
		}
	}

#endif
}

void FPluginManager::ReadPluginsInDirectory(const FString& PluginsDirectory, const EPluginType Type, TMap<FString, TSharedRef<FPlugin>>& Plugins, TArray<TSharedRef<FPlugin>>& ChildPlugins)
{
	// Make sure the directory even exists
	if(FPlatformFileManager::Get().GetPlatformFile().DirectoryExists(*PluginsDirectory))
	{
		TArray<FString> FileNames;
		FindPluginsInDirectory(PluginsDirectory, FileNames);

		struct FLoadContext
		{
			FPluginDescriptor Descriptor;
			FText FailureReason;
			bool bResult = false;
		};

		TArray<FLoadContext> Contexts;
		Contexts.SetNum(FileNames.Num());

		ParallelFor(
			FileNames.Num(),
			[&Contexts, &FileNames](int32 Index)
			{
				FLoadContext& Context = Contexts[Index];
				Context.bResult = Context.Descriptor.Load(FileNames[Index], Context.FailureReason);
			},
			EParallelForFlags::Unbalanced
		);

		for (int32 Index = 0, Num = FileNames.Num(); Index < Num; ++Index)
		{
			const FString& FileName = FileNames[Index];
			FLoadContext& Context = Contexts[Index];
			
			if (Context.bResult)
			{
				CreatePluginObject(FileName, Context.Descriptor, Type, Plugins, ChildPlugins);
			}
			else
			{
				// NOTE: Even though loading of this plugin failed, we'll keep processing other plugins
				FString FullPath = FPaths::ConvertRelativePathToFull(FileName);
				FText FailureMessage = FText::Format(LOCTEXT("FailureFormat", "{0} ({1})"), Context.FailureReason, FText::FromString(FullPath));
				FText DialogTitle = LOCTEXT("PluginFailureTitle", "Failed to load Plugin");
				UE_LOG(LogPluginManager, Error, TEXT("%s"), *FailureMessage.ToString());
				FMessageDialog::Open(EAppMsgType::Ok, FailureMessage, &DialogTitle);
			}
		}
	}
}

void FPluginManager::FindPluginsInDirectory(const FString& PluginsDirectory, TArray<FString>& FileNames)
{
	FPlatformFileManager::Get().GetPlatformFile().FindFilesRecursively(FileNames, *PluginsDirectory, TEXT(".uplugin"));
}

void FPluginManager::FindPluginManifestsInDirectory(const FString& PluginManifestDirectory, TArray<FString>& FileNames)
{
	class FManifestVisitor : public IPlatformFile::FDirectoryVisitor
	{
	public:
		TArray<FString>& ManifestFileNames;

		FManifestVisitor(TArray<FString>& InManifestFileNames) : ManifestFileNames(InManifestFileNames)
		{
		}

		virtual bool Visit(const TCHAR* FileNameOrDirectory, bool bIsDirectory)
		{
			if (!bIsDirectory)
			{
				FStringView FileName(FileNameOrDirectory);
				if (FileName.EndsWith(TEXT(".upluginmanifest")))
				{
					ManifestFileNames.Emplace(FileName);
				}
			}
			return true;
		}
	};

	FManifestVisitor Visitor(FileNames);
	IFileManager::Get().IterateDirectory(*PluginManifestDirectory, Visitor);
}

void FPluginManager::CreatePluginObject(const FString& FileName, const FPluginDescriptor& Descriptor, const EPluginType Type, TMap<FString, TSharedRef<FPlugin>>& Plugins, TArray<TSharedRef<FPlugin>>& ChildPlugins)
{
	TSharedRef<FPlugin> Plugin = MakeShareable(new FPlugin(FileName, Descriptor, Type));

	// children plugins are gathered and used later
	if (Plugin->GetDescriptor().bIsPluginExtension)
	{
		ChildPlugins.Add(Plugin);
		return;
	}

	FString FullPath = FPaths::ConvertRelativePathToFull(FileName);
	UE_LOG(LogPluginManager, Verbose, TEXT("Read plugin descriptor for %s, from %s"), *Plugin->GetName(), *FullPath);

	const TSharedRef<FPlugin>* ExistingPlugin = Plugins.Find(Plugin->GetName());
	if (ExistingPlugin == nullptr)
	{
		Plugins.Add(Plugin->GetName(), Plugin);
	}
	else if ((*ExistingPlugin)->Type == EPluginType::Engine && Type == EPluginType::Project)
	{
		Plugins[Plugin->GetName()] = Plugin;
		UE_LOG(LogPluginManager, Verbose, TEXT("Replacing engine version of '%s' plugin with game version"), *Plugin->GetName());
	}
	else if (((*ExistingPlugin)->Type != EPluginType::Project || Type != EPluginType::Engine) && (*ExistingPlugin)->FileName != Plugin->FileName)
	{
		UE_LOG(LogPluginManager, Warning, TEXT("Plugin '%s' exists at '%s' and '%s' - second location will be ignored"), *Plugin->GetName(), *(*ExistingPlugin)->FileName, *Plugin->FileName);
	}
}

// Helper class to find all pak files.
class FPakFileSearchVisitor : public IPlatformFile::FDirectoryVisitor
{
	TArray<FString>& FoundFiles;
public:
	FPakFileSearchVisitor(TArray<FString>& InFoundFiles)
		: FoundFiles(InFoundFiles)
	{}
	virtual bool Visit(const TCHAR* FilenameOrDirectory, bool bIsDirectory)
	{
		if (bIsDirectory == false)
		{
			FString Filename(FilenameOrDirectory);
			if (Filename.MatchesWildcard(TEXT("*.pak")) && !FoundFiles.Contains(Filename))
			{
				FoundFiles.Add(Filename);
			}
		}
		return true;
	}
};

bool FPluginManager::IntegratePluginsIntoConfig(FConfigCacheIni& ConfigSystem, const TCHAR* EngineIniName, const TCHAR* PlatformName, const TCHAR* StagedPluginsFile)
{
	TArray<FString> PluginList;
	if (!FFileHelper::LoadFileToStringArray(PluginList, StagedPluginsFile))
	{
		return false;
	}

	// track which plugins were staged and are in the binary config - so at runtime, we will still look at other 
	TArray<FString> IntegratedPlugins;

	// loop over each one
	for (FString PluginFile : PluginList)
	{
		FPaths::MakeStandardFilename(PluginFile);

		FPluginDescriptor Descriptor;
		FText FailureReason;
		if (Descriptor.Load(PluginFile, FailureReason))
		{
			// @todo: The type isn't quite right here
			FPlugin Plugin(PluginFile, Descriptor, FPaths::IsUnderDirectory(PluginFile, FPaths::EngineDir()) ? EPluginType::Engine : EPluginType::Project);

			// we perform Mod plugin processing at runtime
			if (Plugin.GetType() == EPluginType::Mod)
			{
				continue;
			}

			// mark that we have processed this plugin, so runtime will not scan it again
			IntegratedPlugins.Add(Plugin.Name);

			FString PluginConfigDir = FPaths::GetPath(Plugin.FileName) / TEXT("Config/");

			// override config cache entries with plugin configs (Engine.ini, Game.ini, etc in <PluginDir>\Config\)
			TArray<FString> PluginConfigs;
			IFileManager::Get().FindFiles(PluginConfigs, *PluginConfigDir, TEXT("ini"));
			for (const FString& ConfigFile : PluginConfigs)
			{
				// Use GetDestIniFilename to find the proper config file to combine into, since it manages command line overrides and path sanitization
				FString PluginConfigFilename = FConfigCacheIni::GetDestIniFilename(*FPaths::GetBaseFilename(ConfigFile), PlatformName, *FPaths::GeneratedConfigDir());
				FConfigFile* FoundConfig = ConfigSystem.Find(PluginConfigFilename, false);
				if (FoundConfig != nullptr)
				{
					UE_LOG(LogPluginManager, Log, TEXT("Found config from plugin[%s] %s"), *Plugin.GetName(), *PluginConfigFilename);

					FoundConfig->AddDynamicLayerToHeirarchy(FPaths::Combine(PluginConfigDir, ConfigFile));
				}
			}

			if (Descriptor.bCanContainContent)
			{
				// we need to look up the section each time because other loops could add entries
				FConfigFile* EngineConfigFile = ConfigSystem.Find(EngineIniName, false);
				FConfigSection* CoreSystemSection = EngineConfigFile->FindOrAddSection(TEXT("Core.System"));
				CoreSystemSection->AddUnique("Paths", Plugin.GetContentDir());
			}
		}
	}

	// record in the config that the plugin inis have been inserted (so we can know at runtime if we have to load plugins or not)
	FConfigFile* EngineConfigFile = ConfigSystem.Find(EngineIniName, false);
	EngineConfigFile->SetArray(TEXT("BinaryConfig"), TEXT("BinaryConfigPlugins"), IntegratedPlugins);

	return true;
}

bool FPluginManager::ConfigureEnabledPlugins()
{
#if (WITH_ENGINE && !IS_PROGRAM) || WITH_PLUGIN_SUPPORT
	if(PluginsToConfigure.Num() > 0)
	{
		SCOPED_BOOT_TIMING("FPluginManager::ConfigureEnabledPlugins");

		bHaveAllRequiredPlugins = false;

		// Set of all the plugins which have been enabled
		TMap<FString, FPlugin*> EnabledPlugins;

		// Keep a set of all the plugin names that have been configured. We read configuration data from different places, but only configure a plugin from the first place that it's referenced.
		TSet<FString> ConfiguredPluginNames;

		// Check which plugins have been enabled or excluded via the command line
		{
			auto ParsePluginsList = [](const TCHAR* InListKey) -> TArray<FString>
			{
				TArray<FString> PluginsList;
				FString PluginsListStr;
				FParse::Value(FCommandLine::Get(), InListKey, PluginsListStr, false);
				PluginsListStr.ParseIntoArray(PluginsList, TEXT(","));
				return PluginsList;
			};

			// Which extra plugins should be enabled?
			bAllPluginsEnabledViaCommandLine = FParse::Param(FCommandLine::Get(), TEXT("EnableAllPlugins"));
			TArray<FString> ExtraPluginsToEnable;
			if (bAllPluginsEnabledViaCommandLine)
			{
				ExtraPluginsToEnable = PluginsToConfigure.Array();
			}
			else
			{
				ExtraPluginsToEnable = ParsePluginsList(TEXT("EnablePlugins="));
			}
			if (ExtraPluginsToEnable.Num() > 0)
			{
				const TArray<FString> ExceptPlugins = ParsePluginsList(TEXT("ExceptPlugins="));
				for (const FString& EnablePluginName : ExtraPluginsToEnable)
				{
					if (!ConfiguredPluginNames.Contains(EnablePluginName) && !ExceptPlugins.Contains(EnablePluginName))
					{
						if (!ConfigureEnabledPluginForCurrentTarget(FPluginReferenceDescriptor(EnablePluginName, true), EnabledPlugins))
						{
							if (bAllPluginsEnabledViaCommandLine)
							{
								// Plugins may legitimately fail to enable when running with -EnableAllPlugins, but this shouldn't be considered a fatal error
								continue;
							}
							return false;
						}
						ConfiguredPluginNames.Add(EnablePluginName);
					}
				}
			}

			// Which extra plugins should be disabled?
			TArray<FString> ExtraPluginsToDisable;
			ExtraPluginsToDisable = ParsePluginsList(TEXT("DisablePlugins="));
			for (const FString& DisablePluginName : ExtraPluginsToDisable)
			{
				if (!ConfiguredPluginNames.Contains(DisablePluginName))
				{
					if (!ConfigureEnabledPluginForCurrentTarget(FPluginReferenceDescriptor(DisablePluginName, false), EnabledPlugins))
					{
						return false;
					}
					ConfiguredPluginNames.Add(DisablePluginName);
				}
			}
		}

		if (!FParse::Param(FCommandLine::Get(), TEXT("NoEnginePlugins")))
		{
#if READ_TARGET_ENABLED_PLUGINS_FROM_RECEIPT
			// Configure the plugins that were enabled or disabled from the target file using the target receipt file
			FString DefaultEditorTarget;
			GConfig->GetString(TEXT("/Script/BuildSettings.BuildSettings"), TEXT("DefaultEditorTarget"), DefaultEditorTarget, GEngineIni);

			auto ConfigurePluginsFromFirstMatchingTargetFile = [this, &ConfiguredPluginNames, &EnabledPlugins, &DefaultEditorTarget](const TCHAR* BaseDir, bool& bOutError) -> bool
			{
				TArray<FString> AllTargetFilesWithoutPath;
				const FString ReceiptWildcard = FTargetReceipt::GetDefaultPath(BaseDir, TEXT("*"), FPlatformProcess::GetBinariesSubdirectory(), FApp::GetBuildConfiguration(), nullptr);
				const FString ReceiptPath = FPaths::GetPath(ReceiptWildcard);
				IFileManager::Get().FindFiles(AllTargetFilesWithoutPath, *ReceiptWildcard, true, false);
				for (const FString& TargetFileWithoutPath : AllTargetFilesWithoutPath)
				{
					const FString TargetFile = FPaths::Combine(ReceiptPath, TargetFileWithoutPath);
					FTargetReceipt Receipt;
					if (Receipt.Read(TargetFile))
					{
						if (Receipt.TargetType == FApp::GetBuildTargetType() && Receipt.Configuration == FApp::GetBuildConfiguration())
						{
							bool bIsDefaultTarget = Receipt.TargetType != EBuildTargetType::Editor || (DefaultEditorTarget.Len() == 0) || (DefaultEditorTarget == Receipt.TargetName);

							if (bIsDefaultTarget)
							{
								for (const TPair<FString, bool>& Pair : Receipt.PluginNameToEnabledState)
								{
									const FString& PluginName = Pair.Key;
									const bool bEnabled = Pair.Value;
									if (!ConfiguredPluginNames.Contains(PluginName))
									{
										if (!ConfigureEnabledPluginForCurrentTarget(FPluginReferenceDescriptor(PluginName, bEnabled), EnabledPlugins))
										{
											bOutError = true;
											break;
										}
										ConfiguredPluginNames.Add(PluginName);
									}
								}

								return true;
							}
						}
					}
				}

				return false;
			};

			{
				bool bErrorConfiguring = false;
				if (!ConfigurePluginsFromFirstMatchingTargetFile(FPlatformMisc::ProjectDir(), bErrorConfiguring))
				{
					ConfigurePluginsFromFirstMatchingTargetFile(FPlatformMisc::EngineDir(), bErrorConfiguring);
				}
				if (bErrorConfiguring)
				{
					return false;
				}
			}
#else
			// Configure the plugins that were enabled from the target file using defines
			TArray<FString> TargetEnabledPlugins = { UBT_TARGET_ENABLED_PLUGINS };
			for (const FString& TargetEnabledPlugin : TargetEnabledPlugins)
			{
				if (!ConfiguredPluginNames.Contains(TargetEnabledPlugin))
				{
					if (!ConfigureEnabledPluginForCurrentTarget(FPluginReferenceDescriptor(TargetEnabledPlugin, true), EnabledPlugins))
					{
						return false;
					}
					ConfiguredPluginNames.Add(TargetEnabledPlugin);
				}
			}

			// Configure the plugins that were disabled from the target file using defines
			TArray<FString> TargetDisabledPlugins = { UBT_TARGET_DISABLED_PLUGINS };
			for (const FString& TargetDisabledPlugin : TargetDisabledPlugins)
			{
				if (!ConfiguredPluginNames.Contains(TargetDisabledPlugin))
				{
					if (!ConfigureEnabledPluginForCurrentTarget(FPluginReferenceDescriptor(TargetDisabledPlugin, false), EnabledPlugins))
					{
						return false;
					}
					ConfiguredPluginNames.Add(TargetDisabledPlugin);
				}
			}
#endif // READ_TARGET_ENABLED_PLUGINS_FROM_RECEIPT

			bool bAllowEnginePluginsEnabledByDefault = true;
			// Find all the plugin references in the project file
			const FProjectDescriptor* ProjectDescriptor = IProjectManager::Get().GetCurrentProject();
			{
				SCOPED_BOOT_TIMING("ConfigureEnabledPluginForCurrentTarget");
				if (ProjectDescriptor != nullptr)
				{

					bAllowEnginePluginsEnabledByDefault = !ProjectDescriptor->bDisableEnginePluginsByDefault;

					// Copy the plugin references, since we may modify the project if any plugins are missing
					TArray<FPluginReferenceDescriptor> PluginReferences(ProjectDescriptor->Plugins);
					for (const FPluginReferenceDescriptor& PluginReference : PluginReferences)
					{
						if (!ConfiguredPluginNames.Contains(PluginReference.Name))
						{
							if (!ConfigureEnabledPluginForCurrentTarget(PluginReference, EnabledPlugins))
							{
								return false;
							}
							ConfiguredPluginNames.Add(PluginReference.Name);
						}
					}
				}
			}

			// Add the plugins which are enabled by default
			for(const FString& PluginName : PluginsToConfigure)
			{
				const TSharedRef<FPlugin>& Plugin = AllPlugins.FindChecked(PluginName);
				if (Plugin->IsEnabledByDefault(bAllowEnginePluginsEnabledByDefault) && !ConfiguredPluginNames.Contains(PluginName))
				{
					if (!ConfigureEnabledPluginForCurrentTarget(FPluginReferenceDescriptor(PluginName, true), EnabledPlugins))
					{
						return false;
					}
					ConfiguredPluginNames.Add(PluginName);
				}
			}
		}
#if IS_PROGRAM
		// Programs can also define the list of enabled plugins in ini
		TArray<FString> ProgramPluginNames;
		GConfig->GetArray(TEXT("Plugins"), TEXT("ProgramEnabledPlugins"), ProgramPluginNames, GEngineIni);

		for (const FString& PluginName : ProgramPluginNames)
		{
			if (!ConfiguredPluginNames.Contains(PluginName))
			{
				if (!ConfigureEnabledPluginForCurrentTarget(FPluginReferenceDescriptor(PluginName, true), EnabledPlugins))
				{
					return false;
				}
				ConfiguredPluginNames.Add(PluginName);
			}
		}
#endif

		// Mark all the plugins as enabled
		for (TPair<FString, FPlugin*>& Pair : EnabledPlugins)
		{
			FPlugin& Plugin = *Pair.Value;

#if !IS_MONOLITHIC
			// Mount the binaries directory, and check the modules are valid
			if (Plugin.Descriptor.Modules.Num() > 0)
			{
				// Mount the binaries directory
				const FString PluginBinariesPath = FPaths::Combine(*FPaths::GetPath(Plugin.FileName), TEXT("Binaries"), FPlatformProcess::GetBinariesSubdirectory());
				FModuleManager::Get().AddBinariesDirectory(*PluginBinariesPath, Plugin.GetLoadedFrom() == EPluginLoadedFrom::Project);
			}

			// Check the declared engine version. This is a soft requirement, so allow the user to skip over it.
			if (!IsPluginCompatible(Plugin) && !PromptToLoadIncompatiblePlugin(Plugin))
			{
				UE_LOG(LogPluginManager, Display, TEXT("Skipping load of '%s'."), *Plugin.Name);
				continue;
			}
#endif
			Plugin.bEnabled = true;
		}

		// If we made it here, we have all the required plugins
		bHaveAllRequiredPlugins = true;

		// check if the config already contais the plugin inis - if so, we don't need to scan anything, just use the ini to find paks to mount
		TArray<FString> BinaryConfigPlugins;
		if (GConfig->GetArray(TEXT("BinaryConfig"), TEXT("BinaryConfigPlugins"), BinaryConfigPlugins, GEngineIni) && BinaryConfigPlugins.Num() > 0)
		{
			SCOPED_BOOT_TIMING("QuickMountingPaks");

			TArray<FString> PluginPaks;
			GConfig->GetArray(TEXT("Core.System"), TEXT("PluginPaks"), PluginPaks, GEngineIni);
			if (FCoreDelegates::MountPak.IsBound())
			{
				for (FString& PakPathEntry : PluginPaks)
				{
					int32 PipeLocation;
					if (PakPathEntry.FindChar(TEXT('|'), PipeLocation))
					{
						// split the string in twain
						FString PluginName = PakPathEntry.Left(PipeLocation);
						FString PakPath = PakPathEntry.Mid(PipeLocation + 1);

						// look for the existing plugin
						FPlugin* FoundPlugin = EnabledPlugins.FindRef(PluginName);
						if (FoundPlugin != nullptr)
						{
							PluginsWithPakFile.AddUnique(TSharedRef<IPlugin>(FoundPlugin));
							// and finally mount the plugin's pak
							FCoreDelegates::MountPak.Execute(PakPath, 0);
						}
						
					}
				}
			}
			else
			{
				UE_LOG(LogPluginManager, Warning, TEXT("Plugin Pak files could not be mounted because MountPak is not bound"));
			}
		}


		// even if we had plugins in the Config already, we need to process Mod plugins
		{
			SCOPED_BOOT_TIMING("ParallelPluginEnabling");

			// generate optimal list of plugins to process
			TArray<TSharedRef<FPlugin>> PluginsArray;
			for (const FString& PluginName : PluginsToConfigure)
			{
				TSharedRef<FPlugin> Plugin = AllPlugins.FindChecked(PluginName);
				// check all plugins that were not in a BinaryConfig
				if (!BinaryConfigPlugins.Contains(PluginName))
				{
					// only process enabled plugins
					if (Plugin->bEnabled && !Plugin->Descriptor.bExplicitlyLoaded)
					{
						PluginsArray.Add(Plugin);
					}
				}
			}

			FCriticalSection ConfigCS;
			FCriticalSection PluginPakCS;
			// Mount all the enabled plugins
			ParallelFor(PluginsArray.Num(), [&PluginsArray, &ConfigCS, &PluginPakCS, this](int32 Index)
			{
				FString PlatformName = FPlatformProperties::PlatformName();
				FPlugin& Plugin = *PluginsArray[Index];
				UE_LOG(LogPluginManager, Log, TEXT("Mounting plugin %s"), *Plugin.GetName());

				// Load <PluginName>.ini config file if it exists
				FString PluginConfigDir = FPaths::GetPath(Plugin.FileName) / TEXT("Config/");
				FString EngineConfigDir = FPaths::EngineConfigDir();
				FString SourceConfigDir = FPaths::SourceConfigDir();

				// Load Engine plugins out of BasePluginName.ini and the engine directory, game plugins out of DefaultPluginName.ini
				if (Plugin.GetLoadedFrom() == EPluginLoadedFrom::Engine)
				{
					EngineConfigDir = PluginConfigDir;
				}
				else
				{
					SourceConfigDir = PluginConfigDir;
				}

				FString PluginConfigFilename = FString::Printf(TEXT("%s%s/%s.ini"), *FPaths::GeneratedConfigDir(), *PlatformName, *Plugin.Name);
				FPaths::MakeStandardFilename(PluginConfigFilename); // This needs to match what we do in ConfigCacheIni.cpp's GetDestIniFilename method. Otherwise, the hash results will differ and the plugin's version will be overwritten later.
				{
					FScopeLock Locker(&ConfigCS);

					FConfigFile& PluginConfig = GConfig->Add(PluginConfigFilename, FConfigFile());

					// This will write out an ini to PluginConfigFilename
					if (!FConfigCacheIni::LoadExternalIniFile(PluginConfig, *Plugin.Name, *EngineConfigDir, *SourceConfigDir, true, nullptr, false, true))
					{
						// Nothing to add, remove from map
						GConfig->Remove(PluginConfigFilename);
					}
				}

				//@note: This function is called too early for `GIsEditor` to be true and hence not go through this scope
				if (!GIsEditor)
				{
					// override config cache entries with plugin configs (Engine.ini, Game.ini, etc in <PluginDir>\Config\)
					TArray<FString> PluginConfigs;
					IFileManager::Get().FindFiles(PluginConfigs, *PluginConfigDir, TEXT("ini"));
					for (const FString& ConfigFile : PluginConfigs)
					{
						// Use GetDestIniFilename to find the proper config file to combine into, since it manages command line overrides and path sanitization
						PluginConfigFilename = FConfigCacheIni::GetDestIniFilename(*FPaths::GetBaseFilename(ConfigFile), *PlatformName, *FPaths::GeneratedConfigDir());
						{
							FScopeLock Locker(&ConfigCS);
							FConfigFile* FoundConfig = GConfig->Find(PluginConfigFilename, false);
							if (FoundConfig != nullptr)
							{
								UE_LOG(LogPluginManager, Log, TEXT("Found config from plugin[%s] %s"), *Plugin.GetName(), *PluginConfigFilename);

								FoundConfig->AddDynamicLayerToHeirarchy(FPaths::Combine(PluginConfigDir, ConfigFile));

#if ALLOW_INI_OVERRIDE_FROM_COMMANDLINE
								// Don't allow plugins to stomp command line overrides, so re-apply them
								FConfigFile::OverrideFromCommandline(FoundConfig, PluginConfigFilename);
#endif // ALLOW_INI_OVERRIDE_FROM_COMMANDLINE
							}
						}
					}
				}

				// Build the list of content folders
				if (Plugin.Descriptor.bCanContainContent)
				{
					{
						FScopeLock Locker(&ConfigCS);

						// we need to look up the section each time because other loops could add entries
						if (FConfigFile* EngineConfigFile = GConfig->Find(GEngineIni, false))
						{
							if (FConfigSection* CoreSystemSection = EngineConfigFile->Find(TEXT("Core.System")))
							{
								CoreSystemSection->AddUnique("Paths", Plugin.GetContentDir());
							}
						}
					}

					TArray<FString>	FoundPaks;
					FPakFileSearchVisitor PakVisitor(FoundPaks);
					IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

					// Pak files are loaded from <PluginName>/Content/Paks/<PlatformName>
					if (FPlatformProperties::RequiresCookedData())
					{
						PlatformFile.IterateDirectoryRecursively(*(Plugin.GetContentDir() / TEXT("Paks") / FPlatformProperties::PlatformName()), PakVisitor);

						for (const FString& PakPath : FoundPaks)
						{
							FScopeLock Locker(&PluginPakCS);
							if (FCoreDelegates::MountPak.IsBound())
							{
								FCoreDelegates::MountPak.Execute(PakPath, 0);
								PluginsWithPakFile.AddUnique(PluginsArray[Index]);
							}
							else
							{
								UE_LOG(LogPluginManager, Warning, TEXT("PAK file (%s) could not be mounted because MountPak is not bound"), *PakPath)
							}
						}
					}
				}
			}, true); // @todo disable parallelism for now as it's causing hard to track problems
		}

		for (TSharedRef<IPlugin> Plugin: GetEnabledPluginsWithContent())
		{
			if (Plugin->GetDescriptor().bExplicitlyLoaded)
			{
				continue;
			}

			if (NewPluginMountedEvent.IsBound())
			{
				NewPluginMountedEvent.Broadcast(*Plugin);
			}

			if (ensure(RegisterMountPointDelegate.IsBound()))
			{
				FString ContentDir = Plugin->GetContentDir();
				RegisterMountPointDelegate.Execute(Plugin->GetMountedAssetPath(), ContentDir);
			}
		}

		PluginsToConfigure.Empty();
	}
	else 
	{
		bHaveAllRequiredPlugins = true;
	}
	return bHaveAllRequiredPlugins;
#else
	return true;
#endif
}

bool FPluginManager::RequiresTempTargetForCodePlugin(const FProjectDescriptor* ProjectDescriptor, const FString& Platform, EBuildConfiguration Configuration, EBuildTargetType TargetType, FText& OutReason)
{
	const FPluginReferenceDescriptor* MissingPlugin;

	TSet<FString> ProjectCodePlugins;
	if (!GetCodePluginsForProject(ProjectDescriptor, Platform, Configuration, TargetType, AllPlugins, ProjectCodePlugins, MissingPlugin))
	{
		OutReason = FText::Format(LOCTEXT("TempTarget_MissingPluginForTarget", "{0} plugin is referenced by target but not found"), FText::FromString(MissingPlugin->Name));
		return true;
	}

	TSet<FString> DefaultCodePlugins;
	if (!GetCodePluginsForProject(nullptr, Platform, Configuration, TargetType, AllPlugins, DefaultCodePlugins, MissingPlugin))
	{
		OutReason = FText::Format(LOCTEXT("TempTarget_MissingPluginForDefaultTarget", "{0} plugin is referenced by the default target but not found"), FText::FromString(MissingPlugin->Name));
		return true;
	}

	for (const FString& ProjectCodePlugin : ProjectCodePlugins)
	{
		if (!DefaultCodePlugins.Contains(ProjectCodePlugin))
		{
			OutReason = FText::Format(LOCTEXT("TempTarget_PluginEnabled", "{0} plugin is enabled"), FText::FromString(ProjectCodePlugin));
			return true;
		}
	}

	for (const FString& DefaultCodePlugin : DefaultCodePlugins)
	{
		if (!ProjectCodePlugins.Contains(DefaultCodePlugin))
		{
			OutReason = FText::Format(LOCTEXT("TempTarget_PluginDisabled", "{0} plugin is disabled"), FText::FromString(DefaultCodePlugin));
			return true;
		}
	}

	return false;
}

bool FPluginManager::GetCodePluginsForProject(const FProjectDescriptor* ProjectDescriptor, const FString& Platform, EBuildConfiguration Configuration, EBuildTargetType TargetType, const TMap<FString, TSharedRef<FPlugin>>& AllPlugins, TSet<FString>& CodePluginNames, const FPluginReferenceDescriptor*& OutMissingPlugin)
{
	// Can only check the current project at the moment, since we won't have enumerated them otherwise
	check(ProjectDescriptor == nullptr || ProjectDescriptor == IProjectManager::Get().GetCurrentProject());

	// Always false for content-only projects
	const bool bLoadPluginsForTargetPlatforms = (TargetType == EBuildTargetType::Editor);

	// Map of all enabled plugins
	TMap<FString, FPlugin*> EnabledPlugins;

	// Keep a set of all the plugin names that have been configured. We read configuration data from different places, but only configure a plugin from the first place that it's referenced.
	TSet<FString> ConfiguredPluginNames;

	// Find all the plugin references in the project file
	bool bAllowEnginePluginsEnabledByDefault = true;
	if (ProjectDescriptor != nullptr)
	{
		bAllowEnginePluginsEnabledByDefault = !ProjectDescriptor->bDisableEnginePluginsByDefault;

		// Copy the plugin references, since we may modify the project if any plugins are missing
		TArray<FPluginReferenceDescriptor> PluginReferences(ProjectDescriptor->Plugins);
		for (const FPluginReferenceDescriptor& PluginReference : PluginReferences)
		{
			if(!ConfiguredPluginNames.Contains(PluginReference.Name))
			{
				if (!ConfigureEnabledPluginForTarget(PluginReference, ProjectDescriptor, FString(), Platform, Configuration, TargetType, bLoadPluginsForTargetPlatforms, AllPlugins, EnabledPlugins, OutMissingPlugin))
				{
					return false;
				}
				ConfiguredPluginNames.Add(PluginReference.Name);
			}
		}
	}

	// Add the plugins which are enabled by default
	for (const TPair<FString, TSharedRef<FPlugin>>& PluginPair : AllPlugins)
	{
		if (PluginPair.Value->IsEnabledByDefault(bAllowEnginePluginsEnabledByDefault) && !ConfiguredPluginNames.Contains(PluginPair.Key))
		{
			if (!ConfigureEnabledPluginForTarget(FPluginReferenceDescriptor(PluginPair.Key, true), ProjectDescriptor, FString(), Platform, Configuration, TargetType, bLoadPluginsForTargetPlatforms, AllPlugins, EnabledPlugins, OutMissingPlugin))
			{
				return false;
			}
			ConfiguredPluginNames.Add(PluginPair.Key);
		}
	}

	// Figure out which plugins have code 
	bool bBuildDeveloperTools = (TargetType == EBuildTargetType::Editor || TargetType == EBuildTargetType::Program || (Configuration != EBuildConfiguration::Test && Configuration != EBuildConfiguration::Shipping));
	bool bRequiresCookedData = (TargetType != EBuildTargetType::Editor);
	for (const TPair<FString, FPlugin*>& Pair : EnabledPlugins)
	{
		for (const FModuleDescriptor& Module : Pair.Value->GetDescriptor().Modules)
		{
			if (Module.IsCompiledInConfiguration(Platform, Configuration, FString(), TargetType, bBuildDeveloperTools, bRequiresCookedData))
			{
				CodePluginNames.Add(Pair.Key);
				break;
			}
		}
	}

	return true;
}

bool FPluginManager::ConfigureEnabledPluginForCurrentTarget(const FPluginReferenceDescriptor& FirstReference, TMap<FString, FPlugin*>& EnabledPlugins)
{
	SCOPED_BOOT_TIMING("ConfigureEnabledPluginForCurrentTarget");

	const FPluginReferenceDescriptor* MissingPlugin;
	if (!ConfigureEnabledPluginForTarget(FirstReference, IProjectManager::Get().GetCurrentProject(), UE_APP_NAME, FPlatformMisc::GetUBTPlatform(), FApp::GetBuildConfiguration(), FApp::GetBuildTargetType(), (bool)LOAD_PLUGINS_FOR_TARGET_PLATFORMS, AllPlugins, EnabledPlugins, MissingPlugin))
	{
		// If we're in unattended mode, don't open any windows and fatal out
		if (FApp::IsUnattended())
		{
			UE_LOG(LogPluginManager, Fatal, TEXT("This project requires the '%s' plugin. Install it and try again, or remove it from the project's required plugin list."), *MissingPlugin->Name);
			return false;
		}

#if !IS_MONOLITHIC
		// Try to download it from the marketplace
		if (MissingPlugin->MarketplaceURL.Len() > 0 && PromptToDownloadPlugin(MissingPlugin->Name, MissingPlugin->MarketplaceURL))
		{
			UE_LOG(LogPluginManager, Display, TEXT("Downloading '%s' plugin from marketplace (%s)."), *MissingPlugin->Name, *MissingPlugin->MarketplaceURL);
			return false;
		}

		// Prompt to disable it in the project file, if possible
		if (PromptToDisableMissingPlugin(FirstReference.Name, MissingPlugin->Name))
		{
			UE_LOG(LogPluginManager, Display, TEXT("Disabled plugin '%s', continuing."), *FirstReference.Name);
			return true;
		}
#endif

		// Unable to continue
		UE_LOG(LogPluginManager, Error, TEXT("Unable to load plugin '%s'. Aborting."), *MissingPlugin->Name);
		return false;
	}
	return true;
}

bool FPluginManager::ConfigureEnabledPluginForTarget(const FPluginReferenceDescriptor& FirstReference, const FProjectDescriptor* ProjectDescriptor, const FString& TargetName, const FString& Platform, EBuildConfiguration Configuration, EBuildTargetType TargetType, bool bLoadPluginsForTargetPlatforms, const TMap<FString, TSharedRef<FPlugin>>& AllPlugins, TMap<FString, FPlugin*>& EnabledPlugins, const FPluginReferenceDescriptor*& OutMissingPlugin)
{
	if (!EnabledPlugins.Contains(FirstReference.Name))
	{
		// Set of plugin names we've added to the queue for processing
		TSet<FString> NewPluginNames;
		NewPluginNames.Add(FirstReference.Name);

		// Queue of plugin references to consider
		TArray<const FPluginReferenceDescriptor*> NewPluginReferences;
		NewPluginReferences.Add(&FirstReference);

		// Loop through the queue of plugin references that need to be enabled, queuing more items as we go
		TArray<TSharedRef<FPlugin>> NewPlugins;
		for (int32 Idx = 0; Idx < NewPluginReferences.Num(); Idx++)
		{
			const FPluginReferenceDescriptor& Reference = *NewPluginReferences[Idx];

			// Check if the plugin is required for this platform
			if(!Reference.IsEnabledForPlatform(Platform) || !Reference.IsEnabledForTargetConfiguration(Configuration) || !Reference.IsEnabledForTarget(TargetType))
			{
				UE_LOG(LogPluginManager, Verbose, TEXT("Ignoring plugin '%s' for platform/configuration"), *Reference.Name);
				continue;
			}

			// Check if the plugin is required for this platform
			if(!bLoadPluginsForTargetPlatforms && !Reference.IsSupportedTargetPlatform(Platform))
			{
				UE_LOG(LogPluginManager, Verbose, TEXT("Ignoring plugin '%s' due to unsupported platform"), *Reference.Name);
				continue;
			}

			// Find the plugin being enabled
			const TSharedRef<FPlugin>* PluginPtr = AllPlugins.Find(Reference.Name);
			if (PluginPtr == nullptr)
			{
				// Ignore any optional plugins
				if (Reference.bOptional)
				{
					UE_LOG(LogPluginManager, Verbose, TEXT("Ignored optional reference to '%s' plugin; plugin was not found."), *Reference.Name);
					continue;
				}

				// Add it to the missing list
				OutMissingPlugin = &Reference;
				return false;
			}

			// Check the plugin is not disabled by the platform
			FPlugin& Plugin = PluginPtr->Get();

			// Allow the platform to disable it
			if (FPlatformMisc::ShouldDisablePluginAtRuntime(Plugin.Name))
			{
				UE_LOG(LogPluginManager, Verbose, TEXT("Plugin '%s' was disabled by platform."), *Reference.Name);
				continue;
			}

			// Check the plugin supports this platform
			if(!bLoadPluginsForTargetPlatforms && !Plugin.Descriptor.SupportsTargetPlatform(Platform))
			{
				UE_LOG(LogPluginManager, Verbose, TEXT("Ignoring plugin '%s' due to unsupported platform in plugin descriptor"), *Reference.Name);
				continue;
			}

			// Check that this plugin supports the current program
			if (TargetType == EBuildTargetType::Program && !Plugin.Descriptor.SupportedPrograms.Contains(TargetName))
			{
				UE_LOG(LogPluginManager, Verbose, TEXT("Ignoring plugin '%s' due to absence from the supported programs list"), *Reference.Name);
				continue;
			}

			// Skip loading Enterprise plugins when project is not an Enterprise project
			if (Plugin.Type == EPluginType::Enterprise && ProjectDescriptor != nullptr && !ProjectDescriptor->bIsEnterpriseProject)
			{
				UE_LOG(LogPluginManager, Verbose, TEXT("Ignoring plugin '%s' due to not being an Enterpise project"), *Reference.Name);
				continue;
			}

			// Add references to all its dependencies
			for (const FPluginReferenceDescriptor& NextReference : Plugin.Descriptor.Plugins)
			{
				if (!EnabledPlugins.Contains(NextReference.Name) && !NewPluginNames.Contains(NextReference.Name))
				{
					NewPluginNames.Add(NextReference.Name);
					NewPluginReferences.Add(&NextReference);
				}
			}

			// Add the plugin
			EnabledPlugins.Add(Plugin.GetName(), &Plugin);
		}
	}
	return true;
}

bool FPluginManager::PromptToDownloadPlugin(const FString& PluginName, const FString& MarketplaceURL)
{
	FText Caption = FText::Format(LOCTEXT("DownloadPluginCaption", "Missing {0} Plugin"), FText::FromString(PluginName));
	FText Message = FText::Format(LOCTEXT("DownloadPluginMessage", "This project requires the {0} plugin.\n\nWould you like to download it from the Unreal Engine Marketplace?"), FText::FromString(PluginName));
	if(FMessageDialog::Open(EAppMsgType::YesNo, Message, &Caption) == EAppReturnType::Yes)
	{
		FString Error;
		FPlatformProcess::LaunchURL(*MarketplaceURL, nullptr, &Error);
		if (Error.Len() == 0)
		{
			return true;
		}
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Error));
	}
	return false;
}

bool FPluginManager::PromptToDisableMissingPlugin(const FString& PluginName, const FString& MissingPluginName)
{
	FText Message;
	if (PluginName == MissingPluginName)
	{
		Message = FText::Format(LOCTEXT("DisablePluginMessage_NotFound", "This project requires the '{0}' plugin, which could not be found. Would you like to disable it and continue?\n\nIf you do, you will no longer be able to open any assets created with it. If not, the application will close."), FText::FromString(PluginName));
	}
	else
	{
		Message = FText::Format(LOCTEXT("DisablePluginMessage_MissingDependency", "This project requires the '{0}' plugin, which has a missing dependency on the '{1}' plugin.\n\nWould you like to disable it?\n\nIf you do, you will no longer be able to open any assets created with it. If not, the application will close."), FText::FromString(PluginName), FText::FromString(MissingPluginName));
	}

	FText Caption(LOCTEXT("DisablePluginCaption", "Missing Plugin"));
	return PromptToDisablePlugin(Caption, Message, PluginName);
}

bool FPluginManager::PromptToDisableIncompatiblePlugin(const FString& PluginName, const FString& IncompatiblePluginName)
{
	FText Message;
	if (PluginName == IncompatiblePluginName)
	{
		Message = FText::Format(LOCTEXT("DisablePluginMessage_MissingOrIncompatibleEngineVersion", "Binaries for the '{0}' plugin are missing or incompatible with the current engine version.\n\nWould you like to disable it? You will no longer be able to open assets that were created with it."), FText::FromString(PluginName));
	}
	else
	{
		Message = FText::Format(LOCTEXT("DisablePluginMessage_MissingOrIncompatibleDependency", "Binaries for the '{0}' plugin (a dependency of '{1}') are missing or incompatible with the current engine version.\n\nWould you like to disable it? You will no longer be able to open assets that were created with it."), FText::FromString(IncompatiblePluginName), FText::FromString(PluginName));
	}

	FText Caption(LOCTEXT("DisablePluginCaption", "Missing Plugin"));
	return PromptToDisablePlugin(Caption, Message, PluginName);
}

bool FPluginManager::PromptToDisablePlugin(const FText& Caption, const FText& Message, const FString& PluginName)
{
	// Check we have a project file. If this is a missing engine/program plugin referenced by something, we can't disable it through this method.
	if (IProjectManager::Get().GetCurrentProject() != nullptr)
	{
		if (FMessageDialog::Open(EAppMsgType::YesNo, Message, &Caption) == EAppReturnType::Yes)
		{
			FText FailReason;
			if (IProjectManager::Get().SetPluginEnabled(*PluginName, false, FailReason))
			{
				return true;
			}
			FMessageDialog::Open(EAppMsgType::Ok, FailReason);
		}
	}
	return false;
}

bool FPluginManager::IsPluginCompatible(const FPlugin& Plugin)
{
	if (Plugin.Descriptor.EngineVersion.Len() > 0)
	{
		FEngineVersion Version;
		if (!FEngineVersion::Parse(Plugin.Descriptor.EngineVersion, Version))
		{
			UE_LOG(LogPluginManager, Warning, TEXT("Engine version string in %s could not be parsed (\"%s\")"), *Plugin.FileName, *Plugin.Descriptor.EngineVersion);
			return true;
		}

		EVersionComparison Comparison = FEngineVersion::GetNewest(FEngineVersion::CompatibleWith(), Version, nullptr);
		if (Comparison != EVersionComparison::Neither)
		{
			UE_LOG(LogPluginManager, Warning, TEXT("Plugin '%s' is not compatible with the current engine version (%s)"), *Plugin.Name, *Plugin.Descriptor.EngineVersion);
			return false;
		}
	}
	return true;
}

bool FPluginManager::PromptToLoadIncompatiblePlugin(const FPlugin& Plugin)
{
	// Format the message dependning on whether the plugin is referenced directly, or as a dependency
	FText Message = FText::Format(LOCTEXT("LoadIncompatiblePlugin", "The '{0}' plugin was designed for build {1}. Attempt to load it anyway?"), FText::FromString(Plugin.Name), FText::FromString(Plugin.Descriptor.EngineVersion));
	FText Caption = FText::Format(LOCTEXT("IncompatiblePluginCaption", "'{0}' is Incompatible"), FText::FromString(Plugin.Name));
	return FMessageDialog::Open(EAppMsgType::YesNo, Message, &Caption) == EAppReturnType::Yes;
}

TSharedPtr<FPlugin> FPluginManager::FindPluginInstance(const FString& Name)
{
	const TSharedRef<FPlugin>* Instance = AllPlugins.Find(Name);
	if (Instance == nullptr)
	{
		return TSharedPtr<FPlugin>();
	}
	else
	{
		return TSharedPtr<FPlugin>(*Instance);
	}
}


bool FPluginManager::TryLoadModulesForPlugin( const FPlugin& Plugin, const ELoadingPhase::Type LoadingPhase ) const
{
	TMap<FName, EModuleLoadResult> ModuleLoadFailures;
	FModuleDescriptor::LoadModulesForPhase(LoadingPhase, Plugin.Descriptor.Modules, ModuleLoadFailures);

	FText FailureMessage;
	for( auto FailureIt( ModuleLoadFailures.CreateConstIterator() ); FailureIt; ++FailureIt )
	{
		const FName ModuleNameThatFailedToLoad = FailureIt.Key();
		const EModuleLoadResult FailureReason = FailureIt.Value();

		if( FailureReason != EModuleLoadResult::Success )
		{
			const FText PluginNameText = FText::FromString(Plugin.Name);
			const FText TextModuleName = FText::FromName(FailureIt.Key());

			if ( FailureReason == EModuleLoadResult::FileNotFound )
			{
				FailureMessage = FText::Format( LOCTEXT("PluginModuleNotFound", "Plugin '{0}' failed to load because module '{1}' could not be found.  Please ensure the plugin is properly installed, otherwise consider disabling the plugin for this project."), PluginNameText, TextModuleName );
			}
			else if ( FailureReason == EModuleLoadResult::FileIncompatible )
			{
				FailureMessage = FText::Format( LOCTEXT("PluginModuleIncompatible", "Plugin '{0}' failed to load because module '{1}' does not appear to be compatible with the current version of the engine.  The plugin may need to be recompiled."), PluginNameText, TextModuleName );
			}
			else if ( FailureReason == EModuleLoadResult::CouldNotBeLoadedByOS )
			{
				FailureMessage = FText::Format( LOCTEXT("PluginModuleCouldntBeLoaded", "Plugin '{0}' failed to load because module '{1}' could not be loaded.  There may be an operating system error or the module may not be properly set up."), PluginNameText, TextModuleName );
			}
			else if ( FailureReason == EModuleLoadResult::FailedToInitialize )
			{
				FailureMessage = FText::Format( LOCTEXT("PluginModuleFailedToInitialize", "Plugin '{0}' failed to load because module '{1}' could not be initialized successfully after it was loaded."), PluginNameText, TextModuleName );
			}
			else 
			{
				ensure(0);	// If this goes off, the error handling code should be updated for the new enum values!
				FailureMessage = FText::Format( LOCTEXT("PluginGenericLoadFailure", "Plugin '{0}' failed to load because module '{1}' could not be loaded for an unspecified reason.  This plugin's functionality will not be available.  Please report this error."), PluginNameText, TextModuleName );
			}

			// Don't need to display more than one module load error per plugin that failed to load
			break;
		}
	}

	if( !FailureMessage.IsEmpty() )
	{
		if (bAllPluginsEnabledViaCommandLine)
		{
			UE_LOG(LogPluginManager, Error, TEXT("%s"), *FailureMessage.ToString());
		}
		else
		{
			FMessageDialog::Open(EAppMsgType::Ok, FailureMessage);
			return false;
		}
	}

	return true;
}

bool FPluginManager::LoadModulesForEnabledPlugins( const ELoadingPhase::Type LoadingPhase )
{
	// Figure out which plugins are enabled
	bool bSuccess = true;
	if (!ConfigureEnabledPlugins())
	{
		bSuccess = false;
	}
	else
	{
		FScopedSlowTask SlowTask(AllPlugins.Num());

		// Load plugins!
		for (const TPair<FString, TSharedRef< FPlugin >>& PluginPair : AllPlugins)
		{
			const TSharedRef<FPlugin>& Plugin = PluginPair.Value;

			SlowTask.EnterProgressFrame(1);

			if (Plugin->bEnabled && !Plugin->Descriptor.bExplicitlyLoaded)
			{
				if (!TryLoadModulesForPlugin(Plugin.Get(), LoadingPhase))
				{
					bSuccess = false;
					break;
				}
			}
		}
	}
	// Some phases such as ELoadingPhase::PreEarlyLoadingScreen are potentially called multiple times,
	// but we do not return to an earlier phase after calling LoadModulesForEnabledPlugins on a later phase
	UE_CLOG(LastCompletedLoadingPhase != ELoadingPhase::None && LastCompletedLoadingPhase > LoadingPhase,
		LogPluginManager, Error, TEXT("LoadModulesForEnabledPlugins called on phase %d after already being called on later phase %d."),
		static_cast<int32>(LoadingPhase), static_cast<int32>(LastCompletedLoadingPhase));

	// We send the broadcast event each time, even if this function is called multiple times with the same phase
	LastCompletedLoadingPhase = LoadingPhase;
	LoadingPhaseCompleteEvent.Broadcast(LoadingPhase, bSuccess);
	return bSuccess;
}

IPluginManager::FLoadingModulesForPhaseEvent& FPluginManager::OnLoadingPhaseComplete()
{
	return LoadingPhaseCompleteEvent;
}

ELoadingPhase::Type FPluginManager::GetLastCompletedLoadingPhase() const
{
	return LastCompletedLoadingPhase;
}

void FPluginManager::GetLocalizationPathsForEnabledPlugins( TArray<FString>& OutLocResPaths )
{
	// Figure out which plugins are enabled
	if (!ConfigureEnabledPlugins())
	{
		return;
	}

	// Gather the paths from all plugins that have localization targets that are loaded based on the current runtime environment
	for (const TPair<FString, TSharedRef<FPlugin>>& PluginPair : AllPlugins)
	{
		const TSharedRef<FPlugin>& Plugin = PluginPair.Value;
		if (!Plugin->bEnabled || Plugin->GetDescriptor().LocalizationTargets.Num() == 0)
		{
			continue;
		}
		
		const FString PluginLocDir = Plugin->GetContentDir() / TEXT("Localization");
		for (const FLocalizationTargetDescriptor& LocTargetDesc : Plugin->GetDescriptor().LocalizationTargets)
		{
			if (LocTargetDesc.ShouldLoadLocalizationTarget())
			{
				OutLocResPaths.Add(PluginLocDir / LocTargetDesc.Name);
			}
		}
	}
}

void FPluginManager::SetRegisterMountPointDelegate( const FRegisterMountPointDelegate& Delegate )
{
	RegisterMountPointDelegate = Delegate;
}

void FPluginManager::SetUnRegisterMountPointDelegate( const FRegisterMountPointDelegate& Delegate )
{
	UnRegisterMountPointDelegate = Delegate;
}

void FPluginManager::SetUpdatePackageLocalizationCacheDelegate( const FUpdatePackageLocalizationCacheDelegate& Delegate )
{
	UpdatePackageLocalizationCacheDelegate = Delegate;
}

bool FPluginManager::AreRequiredPluginsAvailable()
{
	return ConfigureEnabledPlugins();
}

#if !IS_MONOLITHIC
bool FPluginManager::CheckModuleCompatibility(TArray<FString>& OutIncompatibleModules, TArray<FString>& OutIncompatibleEngineModules)
{
	if(!ConfigureEnabledPlugins())
	{
		return false;
	}

	bool bResult = true;
	for(const TPair<FString, TSharedRef<FPlugin>>& PluginPair : AllPlugins)
	{
		const TSharedRef< FPlugin > &Plugin = PluginPair.Value;

		TArray<FString> IncompatibleModules;
		if (Plugin->bEnabled && !FModuleDescriptor::CheckModuleCompatibility(Plugin->Descriptor.Modules, IncompatibleModules))
		{
			OutIncompatibleModules.Append(IncompatibleModules);
			if (Plugin->GetLoadedFrom() == EPluginLoadedFrom::Engine)
			{
				OutIncompatibleEngineModules.Append(IncompatibleModules);
			}
			bResult = false;
		}
	}
	return bResult;
}
#endif

IPluginManager& IPluginManager::Get()
{
	// Single instance of manager, allocated on demand and destroyed on program exit.
	static FPluginManager PluginManager;
	return PluginManager;
}

TSharedPtr<IPlugin> FPluginManager::FindPlugin(const FString& Name)
{
	const TSharedRef<FPlugin>* Instance = AllPlugins.Find(Name);
	if (Instance == nullptr)
	{
		return TSharedPtr<IPlugin>();
	}
	else
	{
		return TSharedPtr<IPlugin>(*Instance);
	}
}

TArray<TSharedRef<IPlugin>> FPluginManager::GetEnabledPlugins()
{
	TArray<TSharedRef<IPlugin>> Plugins;
	for(TPair<FString, TSharedRef<FPlugin>>& PluginPair : AllPlugins)
	{
		TSharedRef<FPlugin>& PossiblePlugin = PluginPair.Value;
		if(PossiblePlugin->bEnabled)
		{
			Plugins.Add(PossiblePlugin);
		}
	}
	return Plugins;
}

TArray<TSharedRef<IPlugin>> FPluginManager::GetEnabledPluginsWithContent() const
{
	TArray<TSharedRef<IPlugin>> Plugins;
	for (const TPair<FString, TSharedRef<FPlugin>>& PluginPair : AllPlugins)
	{
		const TSharedRef<FPlugin>& PluginRef = PluginPair.Value;
		const FPlugin& Plugin = *PluginRef;
		if (Plugin.IsEnabled() && Plugin.CanContainContent())
		{
			Plugins.Add(PluginRef);
		}
	}
	return Plugins;
}

TArray<TSharedRef<IPlugin>> FPluginManager::GetDiscoveredPlugins()
{
	TArray<TSharedRef<IPlugin>> Plugins;
	Plugins.Reserve(AllPlugins.Num());
	for (TPair<FString, TSharedRef<FPlugin>>& PluginPair : AllPlugins)
	{
		Plugins.Add(PluginPair.Value);
	}
	return Plugins;
}

TArray< FPluginStatus > FPluginManager::QueryStatusForAllPlugins() const
{
	TArray< FPluginStatus > PluginStatuses;

	for( const TPair<FString, TSharedRef<FPlugin>>& PluginPair : AllPlugins )
	{
		const TSharedRef< FPlugin >& Plugin = PluginPair.Value;
		
		FPluginStatus PluginStatus;
		PluginStatus.Name = Plugin->Name;
		PluginStatus.PluginDirectory = FPaths::GetPath(Plugin->FileName);
		PluginStatus.bIsEnabled = Plugin->bEnabled;
		PluginStatus.Descriptor = Plugin->Descriptor;
		PluginStatus.LoadedFrom = Plugin->GetLoadedFrom();

		PluginStatuses.Add( PluginStatus );
	}

	return PluginStatuses;
}

bool FPluginManager::AddPluginSearchPath(const FString& ExtraDiscoveryPath, bool bRefresh)
{
	bool bAlreadyExists = false;
	PluginDiscoveryPaths.Add(FPaths::ConvertRelativePathToFull(ExtraDiscoveryPath), &bAlreadyExists);
	if (bRefresh)
	{
		RefreshPluginsList();
	}
	return !bAlreadyExists;
}

const TSet<FString>& FPluginManager::GetAdditionalPluginSearchPaths() const
{
	return PluginDiscoveryPaths;
}

TArray<TSharedRef<IPlugin>> FPluginManager::GetPluginsWithPakFile() const
{
	return PluginsWithPakFile;
}

IPluginManager::FNewPluginMountedEvent& FPluginManager::OnNewPluginCreated()
{
	return NewPluginCreatedEvent;
}

IPluginManager::FNewPluginMountedEvent& FPluginManager::OnNewPluginMounted()
{
	return NewPluginMountedEvent;
}

void FPluginManager::MountNewlyCreatedPlugin(const FString& PluginName)
{
	TSharedPtr<FPlugin> Plugin = FindPluginInstance(PluginName);
	if (Plugin.IsValid())
	{
		MountPluginFromExternalSource(Plugin.ToSharedRef());

		// Notify any listeners that a new plugin has been mounted
		if (NewPluginCreatedEvent.IsBound())
		{
			NewPluginCreatedEvent.Broadcast(*Plugin);
		}
	}
}

void FPluginManager::MountExplicitlyLoadedPlugin(const FString& PluginName)
{
	TSharedPtr<FPlugin> Plugin = FindPluginInstance(PluginName);
	if (Plugin.IsValid() && Plugin->Descriptor.bExplicitlyLoaded)
	{
		MountPluginFromExternalSource(Plugin.ToSharedRef());
	}
}

void FPluginManager::MountPluginFromExternalSource(const TSharedRef<FPlugin>& Plugin)
{
	if (GWarn)
	{
		GWarn->BeginSlowTask(FText::Format(LOCTEXT("MountingPluginFiles", "Mounting plugin {0}..."), FText::FromString(Plugin->GetFriendlyName())), /*ShowProgressDialog*/ true, /*bShowCancelButton*/ false);
	}

	// Mark the plugin as enabled
	Plugin->bEnabled = true;

	// Mount the plugin content directory
	if (Plugin->CanContainContent() && ensure(RegisterMountPointDelegate.IsBound()))
	{
		if (NewPluginMountedEvent.IsBound())
		{
			NewPluginMountedEvent.Broadcast(*Plugin);
		}

		FString ContentDir = Plugin->GetContentDir();
		RegisterMountPointDelegate.Execute(Plugin->GetMountedAssetPath(), ContentDir);

		// Register this plugin's path with the list of content directories that the editor will search
		if (FConfigFile* EngineConfigFile = GConfig->Find(GEngineIni, false))
		{
			if (FConfigSection* CoreSystemSection = EngineConfigFile->Find(TEXT("Core.System")))
			{
				CoreSystemSection->AddUnique("Paths", MoveTemp(ContentDir));
			}
		}

		// Update the localization cache for the newly added content directory
		UpdatePackageLocalizationCacheDelegate.ExecuteIfBound();
	}

	// If it's a code module, also load the modules for it
	if (Plugin->Descriptor.Modules.Num() > 0)
	{
		// Add the plugin binaries directory
		const FString PluginBinariesPath = FPaths::Combine(*FPaths::GetPath(Plugin->FileName), TEXT("Binaries"), FPlatformProcess::GetBinariesSubdirectory());
		FModuleManager::Get().AddBinariesDirectory(*PluginBinariesPath, Plugin->GetLoadedFrom() == EPluginLoadedFrom::Project);

		// Load all the plugin modules
		for (ELoadingPhase::Type LoadingPhase = (ELoadingPhase::Type)0; LoadingPhase < ELoadingPhase::Max; LoadingPhase = (ELoadingPhase::Type)(LoadingPhase + 1))
		{
			if (LoadingPhase != ELoadingPhase::None)
			{
				TryLoadModulesForPlugin(Plugin.Get(), LoadingPhase);
			}
		}
	}

	if (GWarn)
	{
		GWarn->EndSlowTask();
	}
}

bool FPluginManager::UnmountExplicitlyLoadedPlugin(const FString& PluginName, FText* OutReason)
{
	TSharedPtr<FPlugin> Plugin = FindPluginInstance(PluginName);
	return UnmountPluginFromExternalSource(Plugin, OutReason);
}

bool FPluginManager::UnmountPluginFromExternalSource(const TSharedPtr<FPlugin>& Plugin, FText* OutReason)
{
	if (!Plugin.IsValid() || Plugin->bEnabled == false)
	{
		// Does not exist or is not loaded
		return true;
	}

	if (!Plugin->Descriptor.bExplicitlyLoaded)
	{
		if (OutReason)
		{
			*OutReason = LOCTEXT("UnloadPluginNotExplicitlyLoaded", "Plugin was not explicitly loaded");
		}
		return false;
	}

	if (Plugin->Descriptor.Modules.Num() > 0)
	{
		if (OutReason)
		{
			*OutReason = LOCTEXT("UnloadPluginContainedModules", "Plugin contains modules and may be unsafe to unload");
		}
		return false;
	}

	if (Plugin->CanContainContent() && ensure(UnRegisterMountPointDelegate.IsBound()))
	{
		UnRegisterMountPointDelegate.Execute(Plugin->GetMountedAssetPath(), Plugin->GetContentDir());
	}

	Plugin->bEnabled = false;

	return true;
}

FName FPluginManager::PackageNameFromModuleName(FName ModuleName)
{
	FName Result = ModuleName;
	for (TMap<FString, TSharedRef<FPlugin>>::TIterator Iter(AllPlugins); Iter; ++Iter)
	{
		const TSharedRef<FPlugin>& Plugin = Iter.Value();
		const TArray<FModuleDescriptor>& Modules = Plugin->Descriptor.Modules;
		for (int Idx = 0; Idx < Modules.Num(); Idx++)
		{
			const FModuleDescriptor& Descriptor = Modules[Idx];
			if (Descriptor.Name == ModuleName)
			{
				UE_LOG(LogPluginManager, Log, TEXT("Module %s belongs to Plugin %s and we assume that is the name of the package with the UObjects is /Script/%s"), *ModuleName.ToString(), *Plugin->Name, *Plugin->Name);
				return FName(*Plugin->Name);
			}
		}
	}
	return Result;
}


#undef LOCTEXT_NAMESPACE
