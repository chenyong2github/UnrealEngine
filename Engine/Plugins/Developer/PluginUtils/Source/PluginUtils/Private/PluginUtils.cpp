// Copyright Epic Games, Inc. All Rights Reserved.

#include "PluginUtils.h"

#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "GameProjectUtils.h"
#include "Interfaces/IProjectManager.h"
#include "Interfaces/IPluginManager.h"
#include "PluginDescriptor.h"
#include "IAssetRegistry.h"
#include "AssetRegistryModule.h"
#include "AssetData.h"
#include "IAssetTools.h"
#include "AssetToolsModule.h"
#include "DesktopPlatformModule.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/FeedbackContext.h"

#define LOCTEXT_NAMESPACE "PluginUtils"

namespace PluginUtils
{
	// The text macro to replace with the actual plugin name when copying files
	const FString PLUGIN_NAME = TEXT("PLUGIN_NAME");

	bool CopyPluginTemplateFolder(const TCHAR* DestinationDirectory, const TCHAR* Source, const FString& PluginName, FText& FailReason)
	{
		check(DestinationDirectory);
		check(Source);

		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

		FString DestDir(DestinationDirectory);
		FPaths::NormalizeDirectoryName(DestDir);

		FString SourceDir(Source);
		FPaths::NormalizeDirectoryName(SourceDir);

		// Does Source dir exist?
		if (!PlatformFile.DirectoryExists(*SourceDir))
		{
			FailReason = FText::Format(LOCTEXT("InvalidPluginTemplateFolder", "Plugin template folder doesn't exist\n{0}"), FText::FromString(FPaths::ConvertRelativePathToFull(SourceDir)));
			return false;
		}

		// Destination directory exists already or can be created ?
		if (!PlatformFile.DirectoryExists(*DestDir) && !PlatformFile.CreateDirectoryTree(*DestDir))
		{
			FailReason = FText::Format(LOCTEXT("FailedToCreateDestinationFolder", "Failed to create destination folder\n{0}"), FText::FromString(FPaths::ConvertRelativePathToFull(DestDir)));
			return false;
		}

		// Copy all files and directories, renaming specific sections to the plugin name
		struct FCopyPluginFilesAndDirs : public IPlatformFile::FDirectoryVisitor
		{
			IPlatformFile& PlatformFile;
			const TCHAR* SourceRoot;
			const TCHAR* DestRoot;
			const FString& PluginName;
			TArray<FString> NameReplacementFileTypes;
			TArray<FString> IgnoredFileTypes;
			TArray<FString> CopyUnmodifiedFileTypes;
			FText& FailReason;

			FCopyPluginFilesAndDirs(IPlatformFile& InPlatformFile, const TCHAR* InSourceRoot, const TCHAR* InDestRoot, const FString& InPluginName, FText& InFailReason)
				: PlatformFile(InPlatformFile)
				, SourceRoot(InSourceRoot)
				, DestRoot(InDestRoot)
				, PluginName(InPluginName)
				, FailReason(InFailReason)
			{
				// Which file types we want to replace instances of PLUGIN_NAME with the new Plugin Name
				NameReplacementFileTypes.Add(TEXT("cs"));
				NameReplacementFileTypes.Add(TEXT("cpp"));
				NameReplacementFileTypes.Add(TEXT("h"));
				NameReplacementFileTypes.Add(TEXT("vcxproj"));

				// Which file types do we want to ignore
				IgnoredFileTypes.Add(TEXT("opensdf"));
				IgnoredFileTypes.Add(TEXT("sdf"));
				IgnoredFileTypes.Add(TEXT("user"));
				IgnoredFileTypes.Add(TEXT("suo"));

				// Which file types do we want to copy completely unmodified
				CopyUnmodifiedFileTypes.Add(TEXT("uasset"));
				CopyUnmodifiedFileTypes.Add(TEXT("umap"));
			}

			virtual bool Visit(const TCHAR* FilenameOrDirectory, bool bIsDirectory)
			{
				FString NewName(FilenameOrDirectory);
				// change the root and rename paths/files
				NewName.RemoveFromStart(SourceRoot);
				NewName = NewName.Replace(*PLUGIN_NAME, *PluginName, ESearchCase::CaseSensitive);
				NewName = FPaths::Combine(DestRoot, *NewName);

				if (bIsDirectory)
				{
					// create new directory structure
					if (!PlatformFile.CreateDirectoryTree(*NewName) && !PlatformFile.DirectoryExists(*NewName))
					{
						FailReason = FText::Format(LOCTEXT("FailedToCreatePluginSubFolder", "Failed to create plugin subfolder\n{0}"), FText::FromString(FPaths::ConvertRelativePathToFull(NewName)));
						return false;
					}
				}
				else
				{
					FString NewExt = FPaths::GetExtension(FilenameOrDirectory);

					if (!IgnoredFileTypes.Contains(NewExt))
					{
						if (CopyUnmodifiedFileTypes.Contains(NewExt))
						{
							// Copy unmodified files with the original name, but do rename the directories
							FString CleanFilename = FPaths::GetCleanFilename(FilenameOrDirectory);
							FString CopyToPath = FPaths::GetPath(NewName);

							NewName = FPaths::Combine(CopyToPath, CleanFilename);
						}

						if (PlatformFile.FileExists(*NewName))
						{
							// Delete destination file if it exists
							PlatformFile.DeleteFile(*NewName);
						}

						// If file of specified extension - open the file as text and replace PLUGIN_NAME in there before saving
						if (NameReplacementFileTypes.Contains(NewExt))
						{
							FString OutFileContents;
							if (!FFileHelper::LoadFileToString(OutFileContents, FilenameOrDirectory))
							{
								FailReason = FText::Format(LOCTEXT("FailedToReadPluginTemplateFile", "Failed to read plugin template file\n{0}"), FText::FromString(FPaths::ConvertRelativePathToFull(FilenameOrDirectory)));
								return false;
							}

							OutFileContents = OutFileContents.Replace(*PLUGIN_NAME, *PluginName, ESearchCase::CaseSensitive);

							// For some content, we also want to export a PLUGIN_NAME_API text macro, which requires that the plugin name
							// be all capitalized

							FString PluginNameAPI = PluginName + TEXT("_API");

							OutFileContents = OutFileContents.Replace(*PluginNameAPI, *PluginNameAPI.ToUpper(), ESearchCase::CaseSensitive);

							if (!FFileHelper::SaveStringToFile(OutFileContents, *NewName))
							{
								FailReason = FText::Format(LOCTEXT("FailedToWritePluginFile", "Failed to write plugin file\n{0}"), FText::FromString(FPaths::ConvertRelativePathToFull(NewName)));
								return false;
							}
						}
						else
						{
							// Copy file from source
							if (!PlatformFile.CopyFile(*NewName, FilenameOrDirectory))
							{
								// Not all files could be copied
								FailReason = FText::Format(LOCTEXT("FailedToCopyPluginTemplateFile", "Failed to copy plugin template file\nFrom: {0}\nTo: {1}"), FText::FromString(FPaths::ConvertRelativePathToFull(FilenameOrDirectory)), FText::FromString(FPaths::ConvertRelativePathToFull(NewName)));
								return false;
							}
						}
					}
				}
				return true; // continue searching
			}
		};

		// copy plugin files and directories visitor
		FCopyPluginFilesAndDirs CopyFilesAndDirs(PlatformFile, *SourceDir, *DestDir, PluginName, FailReason);

		// create all files subdirectories and files in subdirectories!
		return PlatformFile.IterateDirectoryRecursively(*SourceDir, CopyFilesAndDirs);
	}

	void FixupPluginTemplateAssets(const FString& PluginName)
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName);

		struct FFixupPluginAssets : public IPlatformFile::FDirectoryVisitor
		{
			IPlatformFile& PlatformFile;
			const FString& PluginName;
			const FString& PluginBaseDir;

			TArray<FString> FilesToScan;

			FFixupPluginAssets(IPlatformFile& InPlatformFile, const FString& InPluginName, const FString& InPluginBaseDir)
				: PlatformFile(InPlatformFile)
				, PluginName(InPluginName)
				, PluginBaseDir(InPluginBaseDir)
			{
			}

			virtual bool Visit(const TCHAR* FilenameOrDirectory, bool bIsDirectory) override
			{
				if (!bIsDirectory)
				{
					FString Extension = FPaths::GetExtension(FilenameOrDirectory);

					// Only interested in fixing up uassets and umaps...anything else we leave alone
					if (Extension == TEXT("uasset") || Extension == TEXT("umap"))
					{
						FilesToScan.Add(FilenameOrDirectory);
					}
				}

				return true;
			}

			/**
			 * Fixes up any assets that contain the PLUGIN_NAME text macro, since those need to be renamed by the engine for the change to
			 * stick (as opposed to just renaming the file)
			 */
			void PerformFixup()
			{
				TArray<FAssetRenameData> AssetRenameData;

				if (FilesToScan.Num() > 0)
				{
					IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
					AssetRegistry.ScanFilesSynchronous(FilesToScan);

					for (const FString& File : FilesToScan)
					{
						TArray<FAssetData> Assets;

						FString PackageName;
						if (FPackageName::TryConvertFilenameToLongPackageName(File, PackageName))
						{
							AssetRegistry.GetAssetsByPackageName(*PackageName, Assets);
						}

						for (FAssetData Asset : Assets)
						{
							const FString AssetName = Asset.AssetName.ToString().Replace(*PLUGIN_NAME, *PluginName, ESearchCase::CaseSensitive);
							const FString AssetPath = Asset.PackagePath.ToString().Replace(*PLUGIN_NAME, *PluginName, ESearchCase::CaseSensitive);

							FAssetRenameData RenameData(Asset.GetAsset(), AssetPath, AssetName);

							AssetRenameData.Add(RenameData);
						}
					}

					if (AssetRenameData.Num() > 0)
					{
						FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
						AssetToolsModule.Get().RenameAssetsWithDialog(AssetRenameData);
					}
				}
			}
		};

		if (Plugin.IsValid())
		{
			const FString PluginBaseDir = Plugin->GetBaseDir();
			FFixupPluginAssets FixupPluginAssets(PlatformFile, PluginName, PluginBaseDir);
			PlatformFile.IterateDirectoryRecursively(*PluginBaseDir, FixupPluginAssets);
			FixupPluginAssets.PerformFixup();
		}
	}

	TSharedPtr<IPlugin> MountPluginInternal(const FString& PluginName, const FString& PluginLocation, const FPluginUtils::FMountPluginParams& MountParams, FText& FailReason, const bool bIsNewPlugin)
	{
		ensure(!PluginLocation.IsEmpty());

		FPluginUtils::AddToPluginSearchPathIfNeeded(PluginLocation, /*bRefreshPlugins*/ false, MountParams.bUpdateProjectPluginSearchPath);

		IPluginManager::Get().RefreshPluginsList();

		// Find the plugin in the manager.
		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName);
		if (!Plugin)
		{
			FailReason = FText::Format(LOCTEXT("FailedToRegisterPlugin", "Failed to register plugin\n{0}"), FText::FromString(FPluginUtils::GetPluginFilePath(PluginLocation, PluginName, /*bFullPath*/ true)));
			return nullptr;
		}

		// Double check the path matches
		const FString PluginFilePath = FPluginUtils::GetPluginFilePath(PluginLocation, PluginName, /*bFullPath*/ true);
		if (!FPaths::IsSamePath(Plugin->GetDescriptorFileName(), PluginFilePath))
		{
			const FString PluginFilePathFull = FPaths::ConvertRelativePathToFull(Plugin->GetDescriptorFileName());
			FailReason = FText::Format(LOCTEXT("PluginNameAlreadyUsed", "There's already a plugin named {0} at this location:\n{1}"), FText::FromString(PluginName), FText::FromString(PluginFilePathFull));
			return nullptr;
		}

		// Enable this plugin in the project
		if (MountParams.bEnablePluginInProject && !IProjectManager::Get().SetPluginEnabled(PluginName, true, FailReason))
		{
			FailReason = FText::Format(LOCTEXT("FailedToEnablePlugin", "Failed to enable plugin\n{0}"), FailReason);
			return nullptr;
		}

		// Mount the new plugin (mount content folder if any and load modules if any)
		if (bIsNewPlugin)
		{
			IPluginManager::Get().MountNewlyCreatedPlugin(PluginName);
		}
		else
		{
			IPluginManager::Get().MountExplicitlyLoadedPlugin(PluginName);
		}

		// Select plugin Content folder in content browser
		if (MountParams.bSelectInContentBrowser && Plugin->CanContainContent() && !IsRunningCommandlet())
		{
			IContentBrowserSingleton& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();
			const bool bIsEnginePlugin = FPaths::IsUnderDirectory(PluginLocation, FPaths::EnginePluginsDir());
			ContentBrowser.ForceShowPluginContent(bIsEnginePlugin);
			ContentBrowser.SetSelectedPaths({ Plugin->GetMountedAssetPath() }, /*bNeedsRefresh*/ true);
		}

		return Plugin;
	}
}

FString FPluginUtils::GetPluginFolder(const FString& PluginLocation, const FString& PluginName, bool bFullPath)
{
	FString PluginFolder = FPaths::Combine(PluginLocation, PluginName);
	if (bFullPath)
	{
		PluginFolder = FPaths::ConvertRelativePathToFull(PluginFolder);
	}
	FPaths::MakePlatformFilename(PluginFolder);
	return PluginFolder;
}

FString FPluginUtils::GetPluginFilePath(const FString& PluginLocation, const FString& PluginName, bool bFullPath)
{
	FString PluginFilePath = FPaths::Combine(PluginLocation, PluginName, (PluginName + TEXT(".uplugin")));
	if (bFullPath)
	{
		PluginFilePath = FPaths::ConvertRelativePathToFull(PluginFilePath);
	}
	FPaths::MakePlatformFilename(PluginFilePath);
	return PluginFilePath;
}

FString FPluginUtils::GetPluginContentFolder(const FString& PluginLocation, const FString& PluginName, bool bFullPath)
{
	FString PluginContentFolder = FPaths::Combine(PluginLocation, PluginName, TEXT("Content"));
	if (bFullPath)
	{
		PluginContentFolder = FPaths::ConvertRelativePathToFull(PluginContentFolder);
	}
	FPaths::MakePlatformFilename(PluginContentFolder);
	return PluginContentFolder;
}

FString FPluginUtils::GetPluginResourcesFolder(const FString& PluginLocation, const FString& PluginName, bool bFullPath)
{
	FString PluginResourcesFolder = FPaths::Combine(PluginLocation, PluginName, TEXT("Resources"));
	if (bFullPath)
	{
		PluginResourcesFolder = FPaths::ConvertRelativePathToFull(PluginResourcesFolder);
	}
	FPaths::MakePlatformFilename(PluginResourcesFolder);
	return PluginResourcesFolder;
}

TSharedPtr<IPlugin> FPluginUtils::CreateAndMountNewPlugin(const FString& PluginName, const FString& PluginLocation, const FNewPluginParams& CreationParams, const FMountPluginParams& MountParams, FText& FailReason)
{
	// Early validations on new plugin params
	if (PluginName.IsEmpty())
	{
		FailReason = LOCTEXT("CreateNewPluginParam_NoPluginName", "Missing plugin name");
		return nullptr;
	}

	if (PluginLocation.IsEmpty())
	{
		FailReason = LOCTEXT("CreateNewPluginParam_NoPluginLocation", "Missing plugin location");
		return nullptr;
	}

	if (CreationParams.bHasModules && CreationParams.TemplateFolders.Num() == 0)
	{
		FailReason = LOCTEXT("CreateNewPluginParam_NoTemplateFolder", "A template folder must be specified to create a plugin with code");
		return nullptr;
	}

	if (!FPluginUtils::ValidateNewPluginNameAndLocation(PluginName, PluginLocation, &FailReason))
	{
		return nullptr;
	}

	const FString PluginFolder = FPluginUtils::GetPluginFolder(PluginLocation, PluginName, /*bFullPath*/ true);

	TSharedPtr<IPlugin> NewPlugin;
	bool bSucceeded = true;
	do
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

		if (!PlatformFile.DirectoryExists(*PluginFolder) && !PlatformFile.CreateDirectoryTree(*PluginFolder))
		{
			FailReason = FText::Format(LOCTEXT("FailedToCreatePluginFolder", "Failed to create plugin folder\n{0}"), FText::FromString(PluginFolder));
			bSucceeded = false;
			break;
		}

		if (CreationParams.bCanContainContent)
		{
			const FString PluginContentFolder = FPluginUtils::GetPluginContentFolder(PluginLocation, PluginName, /*bFullPath*/ true);
			if (!PlatformFile.DirectoryExists(*PluginContentFolder) && !PlatformFile.CreateDirectory(*PluginContentFolder))
			{
				FailReason = FText::Format(LOCTEXT("FailedToCreatePluginContentFolder", "Failed to create plugin Content folder\n{0}"), FText::FromString(PluginContentFolder));
				bSucceeded = false;
				break;
			}
		}

		FPluginDescriptor Descriptor;
		Descriptor.FriendlyName = PluginName;
		Descriptor.Version = 1;
		Descriptor.VersionName = TEXT("1.0");
		Descriptor.Category = TEXT("Other");
		Descriptor.CreatedBy = CreationParams.CreatedBy;
		Descriptor.CreatedByURL = CreationParams.CreatedByURL;
		Descriptor.Description = CreationParams.Description;
		Descriptor.bIsBetaVersion = CreationParams.bIsBetaVersion;
		Descriptor.bCanContainContent = CreationParams.bCanContainContent;
		Descriptor.EnabledByDefault = CreationParams.EnabledByDefault;
		Descriptor.bExplicitlyLoaded = CreationParams.bExplicitelyLoaded;
		if (CreationParams.bHasModules)
		{
			Descriptor.Modules.Add(FModuleDescriptor(*PluginName, CreationParams.ModuleDescriptorType, CreationParams.LoadingPhase));
		}

		// Write the uplugin file
		const FString PluginFilePath = FPluginUtils::GetPluginFilePath(PluginLocation, PluginName, /*bFullPath*/ true);
		if (!Descriptor.Save(PluginFilePath, FailReason))
		{
			bSucceeded = false;
			break;
		}

		// Copy plugin icon
		if (!CreationParams.PluginIconPath.IsEmpty())
		{
			const FString ResourcesFolder = FPluginUtils::GetPluginResourcesFolder(PluginLocation, PluginName, /*bFullPath*/ true);
			const FString DestinationPluginIconPath = FPaths::Combine(ResourcesFolder, TEXT("Icon128.png"));
			if (IFileManager::Get().Copy(*DestinationPluginIconPath, *CreationParams.PluginIconPath, /*bReplaceExisting=*/ false) != COPY_OK)
			{
				FailReason = FText::Format(LOCTEXT("FailedToCopyPluginIcon", "Failed to copy plugin icon\nFrom: {0}\nTo: {1}"), FText::FromString(FPaths::ConvertRelativePathToFull(CreationParams.PluginIconPath)), FText::FromString(DestinationPluginIconPath));
				bSucceeded = false;
				break;
			}
		}

		// Copy template files
		GWarn->BeginSlowTask(LOCTEXT("CopyingPluginTemplate", "Copying plugin template files..."), /*ShowProgressDialog*/ true, /*bShowCancelButton*/ false);
		for (const FString& TemplateFolder : CreationParams.TemplateFolders)
		{
			if (!PluginUtils::CopyPluginTemplateFolder(*PluginFolder, *TemplateFolder, PluginName, FailReason))
			{
				FailReason = FText::Format(LOCTEXT("FailedToCopyPluginTemplate", "Failed to copy plugin template files\nFrom: {0}\nTo: {1}\n{2}"), FText::FromString(FPaths::ConvertRelativePathToFull(TemplateFolder)), FText::FromString(PluginFolder), FailReason);
				bSucceeded = false;
				break;
			}
		}
		GWarn->EndSlowTask();
		if (!bSucceeded)
		{
			break;
		}

		// Compile plugin code
		if (CreationParams.bHasModules)
		{
			const FString ProjectFileName = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*FPaths::GetProjectFilePath());
			const FString Arguments = FString::Printf(TEXT("%s %s %s -Plugin=\"%s\" -Project=\"%s\" -Progress -NoHotReloadFromIDE"), FPlatformMisc::GetUBTTargetName(), FModuleManager::Get().GetUBTConfiguration(), FPlatformMisc::GetUBTPlatform(), *PluginFilePath, *ProjectFileName);
			if (!FDesktopPlatformModule::Get()->RunUnrealBuildTool(FText::Format(LOCTEXT("CompilingPlugin", "Compiling {0} plugin..."), FText::FromString(PluginName)), FPaths::RootDir(), Arguments, GWarn))
			{
				FailReason = LOCTEXT("FailedToCompilePlugin", "Failed to compile plugin source code");
				bSucceeded = false;
				break;
			}

			// Reset the module paths cache. For unique build environments, the modules may be generated to the project binaries directory.
			FModuleManager::Get().ResetModulePathsCache();

			// Generate project files if we happen to be using a project file.
			if (!FDesktopPlatformModule::Get()->GenerateProjectFiles(FPaths::RootDir(), FPaths::GetProjectFilePath(), GWarn))
			{
				FailReason = LOCTEXT("FailedToGenerateProjectFiles", "Failed to generate project files");
				bSucceeded = false;
				break;
			}
		}

		// Mount the new plugin
		NewPlugin = PluginUtils::MountPluginInternal(PluginName, PluginLocation, MountParams, FailReason, /*bIsNewPlugin*/ true);
		if (!NewPlugin)
		{
			bSucceeded = false;
			break;
		}

		// Fix any content that was added to the plugin
		if (CreationParams.bCanContainContent)
		{	
			GWarn->BeginSlowTask(LOCTEXT("LoadingContent", "Loading Content..."), /*ShowProgressDialog*/ true, /*bShowCancelButton*/ false);
			PluginUtils::FixupPluginTemplateAssets(PluginName);
			GWarn->EndSlowTask();
		}
	} while (false);

	if (!bSucceeded)
	{
		// Delete the plugin folder is something goes wrong during the plugin creation.
		IFileManager::Get().DeleteDirectory(*PluginFolder, /*RequireExists*/ false, /*Tree*/ true);
		if (NewPlugin)
		{
			// Refresh plugins if the new plugin was registered, but we decide to delete its files.
			IPluginManager::Get().RefreshPluginsList();
			NewPlugin.Reset();
			ensure(!IPluginManager::Get().FindPlugin(PluginName));
		}
	}

	return NewPlugin;
}

TSharedPtr<IPlugin> FPluginUtils::MountPlugin(const FString& PluginName, const FString& PluginLocation, const FMountPluginParams& MountParams, FText& FailReason)
{
	// Valide that the uplugin file exists.
	const FString PluginFilePath = FPluginUtils::GetPluginFilePath(PluginLocation, PluginName, /*bFullPath*/ true);
	if (!FPaths::FileExists(PluginFilePath))
	{
		FailReason = FText::Format(LOCTEXT("PluginFileDoesNotExist", "Plugin file does not exist\n{0}"), FText::FromString(PluginFilePath));
		return nullptr;
	}

	if (!IsValidPluginName(PluginName, &FailReason))
	{
		return nullptr;
	}

	return PluginUtils::MountPluginInternal(PluginName, PluginLocation, MountParams, FailReason, /*bIsNewPlugin*/ false);
}

bool FPluginUtils::AddToPluginSearchPathIfNeeded(const FString& Dir, bool bRefreshPlugins, bool bUpdateProjectFile)
{
	bool bSearchPathChanged = false;

	const bool bIsEnginePlugin = FPaths::IsUnderDirectory(Dir, FPaths::EnginePluginsDir());
	const bool bIsProjectPlugin = FPaths::IsUnderDirectory(Dir, FPaths::ProjectPluginsDir());
	if (!bIsEnginePlugin && !bIsProjectPlugin)
	{
		if (bUpdateProjectFile)
		{
			bool bNeedToUpdate = true;
			for (const FString& AdditionalDir : IProjectManager::Get().GetAdditionalPluginDirectories())
			{
				if (FPaths::IsUnderDirectory(Dir, AdditionalDir))
				{
					bNeedToUpdate = false;
					break;
				}
			}

			if (bNeedToUpdate)
			{
				bSearchPathChanged = GameProjectUtils::UpdateAdditionalPluginDirectory(Dir, /*bAdd*/ true);
			}
		}
		else
		{
			bool bNeedToUpdate = true;
			for (const FString& AdditionalDir : IPluginManager::Get().GetAdditionalPluginSearchPaths())
			{
				if (FPaths::IsUnderDirectory(Dir, AdditionalDir))
				{
					bNeedToUpdate = false;
					break;
				}
			}			

			if (bNeedToUpdate)
			{
				bSearchPathChanged = IPluginManager::Get().AddPluginSearchPath(Dir, /*bShouldRefresh*/ false);
			}
		}

		if (bSearchPathChanged && bRefreshPlugins)
		{
			IPluginManager::Get().RefreshPluginsList();
		}
	}

	return bSearchPathChanged;
}

bool FPluginUtils::ValidateNewPluginNameAndLocation(const FString& PluginName, const FString& PluginLocation /*= FString()*/, FText* FailReason /*= nullptr*/)
{
	// Check whether the plugin name is valid
	if (!IsValidPluginName(PluginName, FailReason))
	{
		return false;
	}

	if (!PluginLocation.IsEmpty())
	{
		// Check if a .uplugin file exists at the specified location (if any)
		{
			const FString PluginFilePath = FPluginUtils::GetPluginFilePath(PluginLocation, PluginName);

			if (!PluginFilePath.IsEmpty() && FPaths::FileExists(*PluginFilePath))
			{
				if (FailReason)
				{
					*FailReason = FText::Format(LOCTEXT("PluginPathExists", "Plugin already exists at this location\n{0}"), FText::FromString(FPaths::ConvertRelativePathToFull(PluginFilePath)));
				}
				return false;
			}
		}

		// Check that the plugin location is a valid path (it doesn't have to exist; it will be created if needed)
		if (!FPaths::ValidatePath(PluginLocation, FailReason))
		{
			if (FailReason)
			{
				*FailReason = FText::Format(LOCTEXT("PluginLocationIsNotValidPath", "Plugin location is not a valid path\n{0}"), *FailReason);
			}
			return false;
		}

		// Check there isn't an existing file along the plugin folder path that would prevent creating the directory tree
		{
			FString ExistingFilePath = FPluginUtils::GetPluginFolder(PluginLocation, PluginName, true /*bFullPath*/);
			while (!ExistingFilePath.IsEmpty())
			{
				if (FPaths::FileExists(ExistingFilePath))
				{
					break;
				}
				ExistingFilePath = FPaths::GetPath(ExistingFilePath);
			}
			
			if (!ExistingFilePath.IsEmpty())
			{
				if (FailReason)
				{
					*FailReason = FText::Format(LOCTEXT("PluginLocationIsFile", "Plugin location is invalid because a file exists at this path\n{0}"), FText::FromString(ExistingFilePath));
				}
				return false;
			}
		}
	}

	// Check to see if a discovered plugin with this name exists (at any path)
	if (TSharedPtr<IPlugin> ExistingPlugin = IPluginManager::Get().FindPlugin(PluginName))
	{
		if (FailReason)
		{
			*FailReason = FText::Format(LOCTEXT("PluginNameAlreadyInUse", "Plugin name is already in use\n{0}"), FText::FromString(FPaths::ConvertRelativePathToFull(ExistingPlugin->GetDescriptorFileName())));
		}
		return false;
	}

	return true;
}

bool FPluginUtils::IsValidPluginName(const FString& PluginName, FText* FailReason/* = nullptr*/)
{
	bool bIsNameValid = true;

	// Cannot be empty
	if (PluginName.IsEmpty())
	{
		bIsNameValid = false;
		if (FailReason)
		{
			*FailReason = LOCTEXT("PluginNameIsEmpty", "Plugin name cannot be empty");
		}
	}

	// Must begin with an alphabetic character
	if (bIsNameValid && !FChar::IsAlpha(PluginName[0]))
	{
		bIsNameValid = false;
		if (FailReason)
		{
			*FailReason = LOCTEXT("PluginNameMustBeginWithAlphabetic", "Plugin name must begin with an alphabetic character");
		}
	}

	// Only allow alphanumeric characters and underscore in the name
	if (bIsNameValid)
	{
		FString IllegalCharacters;
		for (int32 CharIdx = 0; CharIdx < PluginName.Len(); ++CharIdx)
		{
			const FString& Char = PluginName.Mid(CharIdx, 1);
			if (!FChar::IsAlnum(Char[0]) && Char != TEXT("_") && Char != TEXT("-"))
			{
				if (!IllegalCharacters.Contains(Char))
				{
					IllegalCharacters += Char;
				}
			}
		}
		
		if (IllegalCharacters.Len() > 0)
		{
			bIsNameValid = false;
			if (FailReason)
			{
				FFormatNamedArguments Args;
				Args.Add(TEXT("IllegalCharacters"), FText::FromString(IllegalCharacters));
				*FailReason = FText::Format(LOCTEXT("PluginNameContainsIllegalCharacters", "Plugin name cannot contain characters such as \"{IllegalCharacters}\""), Args);
			}
		}
	}

	return bIsNameValid;
}

#undef LOCTEXT_NAMESPACE
