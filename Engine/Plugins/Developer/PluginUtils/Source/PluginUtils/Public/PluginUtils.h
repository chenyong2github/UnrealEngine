// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PluginDescriptor.h"
#include "ModuleDescriptor.h"

class IPlugin;

class PLUGINUTILS_API FPluginUtils
{
public:
	/**
	 * Returns the plugin folder.
	 * @param PluginLocation directory that contains the plugin folder
	 * @param PluginName name of the plugin
	 * @param bFullPath ensures a full path is returned
	 */
	static FString GetPluginFolder(const FString& PluginLocation, const FString& PluginName, bool bFullPath = false);

	/** 
	 * Returns the uplugin file path.
	 * @param PluginLocation directory that contains the plugin folder
	 * @param PluginName name of the plugin
	 * @param bFullPath ensures a full path is returned
	 */
	static FString GetPluginFilePath(const FString& PluginLocation, const FString& PluginName, bool bFullPath = false);

	/**
	 * Returns the plugin Content folder.
	 * @param PluginLocation directory that contains the plugin folder
	 * @param PluginName name of the plugin
	 * @param bFullPath ensures a full path is returned
	 */
	static FString GetPluginContentFolder(const FString& PluginLocation, const FString& PluginName, bool bFullPath = false);

	/**
	 * Returns the plugin Resources folder.
	 * @param PluginLocation directory that contains the plugin folder
	 * @param PluginName name of the plugin
	 * @param bFullPath ensures a full path is returned
	 */
	static FString GetPluginResourcesFolder(const FString& PluginLocation, const FString& PluginName, bool bFullPath = false);

	/**
	 * Parameters for creating a new plugin.
	 */
	struct FNewPluginParams
	{
		/** The author of this plugin */
		FString CreatedBy;

		/** Hyperlink for the author's website  */
		FString CreatedByURL;

		/** A description for this plugin */
		FString Description;

		/** Path to plugin icon to copy in the plugin resources folder */
		FString PluginIconPath;

		/** 
		 * Folders containing template files to copy into the plugin folder (Required if bHasModules).
		 * Occurrences of the string PLUGIN_NAME in the filename or file content will be replaced by the plugin name. 
		 */
		TArray<FString> TemplateFolders;

		/** Marks this content as being in beta */
		bool bIsBetaVersion = false;

		/** Can this plugin contain content */
		bool bCanContainContent = false;

		/** Does this plugin have Source files? */
		bool bHasModules = false;

		/**
		 * When true, this plugin's modules will not be loaded automatically nor will it's content be mounted automatically.
		 * It will load/mount when explicitly requested and LoadingPhases will be ignored.
		 */
		bool bExplicitelyLoaded = false;

		/** Whether this plugin should be enabled/disabled by default for any project. */
		EPluginEnabledByDefault EnabledByDefault = EPluginEnabledByDefault::Unspecified;

		/** If this plugin has Source, what is the type of Source included (so it can potentially be excluded in the right builds) */
		EHostType::Type ModuleDescriptorType = EHostType::Runtime;

		/** If this plugin has Source, when should the module be loaded (may need to be earlier than default if used in blueprints) */
		ELoadingPhase::Type LoadingPhase = ELoadingPhase::Default;
	};

	/**
	 * Parameters for mounting a plugin.
	 */
	struct FMountPluginParams
	{
		/** Whether to enable the plugin in the current project config. */
		bool bEnablePluginInProject = true;

		/**
		 * Whether to update the project additional plugin directories (persistently saved in uproject file)
		 * if the plugin location is not under the engine or project plugin folder.
		 * Otherwise the plugin search path gets updated for the process lifetime only.
		 */
		bool bUpdateProjectPluginSearchPath = true;

		/** Whether to select the plugin Content folder (if any) in the content browser. */
		bool bSelectInContentBrowser = true;
	};

	/**
	 * Helper to create and mount a new plugin.
	 * @param PluginName Plugin name
	 * @param PluginLocation Directory that contains the plugin folder
	 * @param CreationParams Plugin creation parameters
	 * @param MountParams Plugin mounting parameters
	 * @param FailReason Reason the plugin creation/mount failed
	 * @return The newly created plugin. If something goes wrong during the creation process, the plugin folder gets deleted and null is returned.
	 * @note Will fail if the plugin already exists
	 */
	static TSharedPtr<IPlugin> CreateAndMountNewPlugin(const FString& PluginName, const FString& PluginLocation, const FNewPluginParams& CreationParams, const FMountPluginParams& MountParams, FText& FailReason);

	/**
	 * Load/mount the specified plugin.
	 * @param PluginName Plugin name
	 * @param PluginLocation Directory that contains the plugin folder
	 * @note the plugin search path will get updated if necessary
	 * @param MountParams Plugin mounting parameters
	 * @param FailReason Reason the plugin failed to load
	 * @return The mounted plugin or null on failure
	 */
	static TSharedPtr<IPlugin> MountPlugin(const FString& PluginName, const FString& PluginLocation, const FMountPluginParams& MountParams, FText& FailReason);

	/**
	 * Adds a directory to the list of paths that are recursively searched for plugins, 
	 * if that directory isn't already under the search paths.
	 * @param Dir Directory to add (doesn't have to be an absolute or normalized path)
	 * @param bRefreshPlugins Whether to refresh plugins if the search path list gets modified
	 * @param bUpdateProjectFile Whether to update the project additional plugin directories (persistently saved in uproject file) if needed
	 * @return Whether the plugin search path was modified
	 */
	static bool AddToPluginSearchPathIfNeeded(const FString& Dir, bool bRefreshPlugins = false, bool bUpdateProjectFile = false);

	/**
	 * Validate that the plugin name is valid, that the name isn't already used by a registered plugin
	 * and optionally that there isn't an unregistered plugin with that name that exists at the specified location.
	 * @param PluginName Plugin name
	 * @param PluginLocation Optional directory in which to look for a plugin that might not be registered
	 * @param FailReason Optional output text describing why the validation failed
	 * @return
	 */
	static bool ValidateNewPluginNameAndLocation(const FString& PluginName, const FString& PluginLocation = FString(), FText* FailReason = nullptr);

	/**
	 * Returns whether the specified plugin name is valid, regardless of whether it's already used
	 * @param PluginName Plugin name
	 * @param FailReason Optional output text specifying what is wrong with the plugin name
	 */
	static bool IsValidPluginName(const FString& PluginName, FText* FailReason = nullptr);
};
