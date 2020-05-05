// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ModuleDescriptor.h"

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
		FNewPluginParams(const FString& InPluginName, const FString& InPluginLocation)
			: PluginName(InPluginName)
			, PluginLocation(InPluginLocation)
		{}

		/** Plugin name (Required) */
		FString PluginName;
		
		/** Directory in which to create the plugin folder (Required) */
		FString PluginLocation;

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
		 * Whether to update the project additional plugin directories (persistently saved in uproject file)
		 * if the specified plugin location is outside the engine or game plugin folders. 
		 */
		bool bUpdateProjectAddtitionalPluginDirectories = true;

		/** If this plugin has Source, what is the type of Source included (so it can potentially be excluded in the right builds) */
		EHostType::Type ModuleDescriptorType = EHostType::Runtime;

		/** If this plugin has Source, when should the module be loaded (may need to be earlier than default if used in blueprints) */
		ELoadingPhase::Type LoadingPhase = ELoadingPhase::Default;
	};

	/**
	 * Helper to create and mount a new unreal plugin.
	 * @param FailReason Reason the plugin creation failed.
	 * @return Whether the plugin was successfully created. If something goes wrong during the creation process, the plugin folder gets deleted.
	 */
	static bool CreateAndMountNewPlugin(const FNewPluginParams& Params, FText& FailReason);
};
