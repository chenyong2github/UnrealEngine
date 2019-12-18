// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PluginDescriptor.h"
#include "Interfaces/IPluginManager.h"

struct FProjectDescriptor;

/**
 * Instance of a plugin in memory
 */
class FPlugin final : public IPlugin
{
public:
	/** The name of the plugin */
	FString Name;

	/** The filename that the plugin was loaded from */
	FString FileName;

	/** The plugin's settings */
	FPluginDescriptor Descriptor;

	/** Type of plugin */
	EPluginType Type;

	/** True if the plugin is marked as enabled */
	bool bEnabled;

	/**
	 * FPlugin constructor
	 */
	FPlugin(const FString &FileName, const FPluginDescriptor& InDescriptor, EPluginType InType);

	/**
	 * Destructor.
	 */
	virtual ~FPlugin();

	/* IPluginInfo interface */
	virtual const FString& GetName() const override
	{
		return Name;
	}

	virtual const FString& GetDescriptorFileName() const override
	{
		return FileName;
	}

	virtual FString GetBaseDir() const override;
	virtual FString GetContentDir() const override;
	virtual FString GetMountedAssetPath() const override;

	virtual bool IsEnabled() const override
	{
		return bEnabled;
	}

	virtual bool IsEnabledByDefault() const override;

	virtual bool IsHidden() const override
	{
		return Descriptor.bIsHidden;
	}

	virtual bool CanContainContent() const override
	{
		return Descriptor.bCanContainContent;
	}

	virtual EPluginType GetType() const override
	{
		return Type;
	}

	virtual EPluginLoadedFrom GetLoadedFrom() const override;
	virtual const FPluginDescriptor& GetDescriptor() const override;
	virtual bool UpdateDescriptor(const FPluginDescriptor& NewDescriptor, FText& OutFailReason) override;
};

PRAGMA_DISABLE_DEPRECATION_WARNINGS
/**
 * FPluginManager manages available code and content extensions (both loaded and not loaded.)
 */
class FPluginManager final : public IPluginManager
{
public:
	/** Constructor */
	FPluginManager();

	/** Destructor */
	~FPluginManager();

	/** IPluginManager interface */
	virtual void RefreshPluginsList() override;
	virtual bool LoadModulesForEnabledPlugins( const ELoadingPhase::Type LoadingPhase ) override;
	virtual void GetLocalizationPathsForEnabledPlugins( TArray<FString>& OutLocResPaths ) override;
	virtual void SetRegisterMountPointDelegate( const FRegisterMountPointDelegate& Delegate ) override;
	virtual bool AreRequiredPluginsAvailable() override;
#if !IS_MONOLITHIC
	virtual bool CheckModuleCompatibility( TArray<FString>& OutIncompatibleModules ) override;
#endif
	virtual TSharedPtr<IPlugin> FindPlugin(const FString& Name) override;
	virtual TArray<TSharedRef<IPlugin>> GetEnabledPlugins() override;
	virtual TArray<TSharedRef<IPlugin>> GetEnabledPluginsWithContent() const override;
	virtual TArray<TSharedRef<IPlugin>> GetDiscoveredPlugins() override;
	virtual TArray< FPluginStatus > QueryStatusForAllPlugins() const override;
	virtual void AddPluginSearchPath(const FString& ExtraDiscoveryPath, bool bRefresh = true) override;
	virtual TArray<TSharedRef<IPlugin>> GetPluginsWithPakFile() const override;
	virtual FNewPluginMountedEvent& OnNewPluginCreated() override;
	virtual FNewPluginMountedEvent& OnNewPluginMounted() override;
	virtual void MountNewlyCreatedPlugin(const FString& PluginName) override;
	virtual void MountExplicitlyLoadedPlugin(const FString& PluginName) override;
	virtual FName PackageNameFromModuleName(FName ModuleName) override;
	virtual bool RequiresTempTargetForCodePlugin(const FProjectDescriptor* ProjectDescriptor, const FString& Platform, EBuildConfiguration Configuration, EBuildTargetType TargetType, FText& OutReason) override;

private:

	/** Searches for all plugins on disk and builds up the array of plugin objects.  Doesn't load any plugins. 
	    This is called when the plugin manager singleton is first accessed. */
	void DiscoverAllPlugins();

	/** Reads all the plugin descriptors */
	static void ReadAllPlugins(TMap<FString, TSharedRef<FPlugin>>& Plugins, const TSet<FString>& ExtraSearchPaths);

	/** Reads all the plugin descriptors from disk */
	static void ReadPluginsInDirectory(const FString& PluginsDirectory, const EPluginType Type, TMap<FString, TSharedRef<FPlugin>>& Plugins, TArray<TSharedRef<FPlugin>>& ChildPlugins);

	/** Creates a FPlugin object and adds it to the given map */
	static void CreatePluginObject(const FString& FileName, const FPluginDescriptor& Descriptor, const EPluginType Type, TMap<FString, TSharedRef<FPlugin>>& Plugins, TArray<TSharedRef<FPlugin>>& ChildPlugins);

	/** Finds all the plugin descriptors underneath a given directory */
	static void FindPluginsInDirectory(const FString& PluginsDirectory, TArray<FString>& FileNames);

	/** Finds all the plugin manifests in a given directory */
	static void FindPluginManifestsInDirectory(const FString& PluginManifestDirectory, TArray<FString>& FileNames);

	/** Gets all the code plugins that are enabled for a content only project */
	static bool GetCodePluginsForProject(const FProjectDescriptor* ProjectDescriptor, const FString& Platform, EBuildConfiguration Configuration, EBuildTargetType TargetType, const TMap<FString, TSharedRef<FPlugin>>& AllPlugins, TSet<FString>& CodePluginNames, const FPluginReferenceDescriptor*& OutMissingPlugin);

	/** Sets the bPluginEnabled flag on all plugins found from DiscoverAllPlugins that are enabled in config */
	bool ConfigureEnabledPlugins();

	/** Adds a single enabled plugin, and all its dependencies */
	bool ConfigureEnabledPluginForCurrentTarget(const FPluginReferenceDescriptor& FirstReference, TMap<FString, FPlugin*>& EnabledPlugins);

	/** Adds a single enabled plugin and all its dependencies. */
	static bool ConfigureEnabledPluginForTarget(const FPluginReferenceDescriptor& FirstReference, const FProjectDescriptor* ProjectDescriptor, const FString& TargetName, const FString& Platform, EBuildConfiguration Configuration, EBuildTargetType TargetType, bool bLoadPluginsForTargetPlatforms, const TMap<FString, TSharedRef<FPlugin>>& AllPlugins, TMap<FString, FPlugin*>& EnabledPlugins, const FPluginReferenceDescriptor*& OutMissingPlugin);

	/** Prompts the user to download a missing plugin from the given URL */
	static bool PromptToDownloadPlugin(const FString& PluginName, const FString& MarketplaceURL);

	/** Prompts the user to disable a plugin */
	static bool PromptToDisableMissingPlugin(const FString& PluginName, const FString& MissingPluginName);

	/** Prompts the user to disable a plugin */
	static bool PromptToDisableIncompatiblePlugin(const FString& PluginName, const FString& IncompatiblePluginName);

	/** Prompts the user to disable a plugin */
	static bool PromptToDisablePlugin(const FText& Caption, const FText& Message, const FString& PluginName);

	/** Checks whether a plugin is compatible with the current engine version */
	static bool IsPluginCompatible(const FPlugin& Plugin);

	/** Prompts the user to disable a plugin */
	static bool PromptToLoadIncompatiblePlugin(const FPlugin& Plugin);

	/** Gets the instance of a given plugin */
	TSharedPtr<FPlugin> FindPluginInstance(const FString& Name);

	/** Mounts a plugin that was requested to be mounted from external code (either by MountNewlyCreatedPlugin or MountExplicitlyLoadedPlugin) */
	void MountPluginFromExternalSource(const TSharedRef<FPlugin>& Plugin);

private:
	/** All of the plugins that we know about */
	TMap< FString, TSharedRef< FPlugin > > AllPlugins;

	TArray<TSharedRef<IPlugin>> PluginsWithPakFile;

	/** Delegate for mounting content paths.  Bound by FPackageName code in CoreUObject, so that we can access
	    content path mounting functionality from Core. */
	FRegisterMountPointDelegate RegisterMountPointDelegate;

	/** Set when all the appropriate plugins have been marked as enabled */
	bool bHaveConfiguredEnabledPlugins;

	/** Set if all the required plugins are available */
	bool bHaveAllRequiredPlugins;

	/** List of additional directory paths to search for plugins within */
	TSet<FString> PluginDiscoveryPaths;

	/** Callback for notifications that a new plugin was mounted */
	FNewPluginMountedEvent NewPluginCreatedEvent;
	FNewPluginMountedEvent NewPluginMountedEvent;
};
PRAGMA_ENABLE_DEPRECATION_WARNINGS


