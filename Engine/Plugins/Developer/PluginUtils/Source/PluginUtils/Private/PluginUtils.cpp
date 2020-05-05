// Copyright Epic Games, Inc. All Rights Reserved.

#include "PluginUtils.h"

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
			FailReason = FText::Format(LOCTEXT("InvalidPluginTemplateFolder", "Plugin template folder '{0}' doesn't exist"), FText::FromString(FPaths::ConvertRelativePathToFull(SourceDir)));
			return false;
		}

		// Destination directory exists already or can be created ?
		if (!PlatformFile.DirectoryExists(*DestDir) && !PlatformFile.CreateDirectoryTree(*DestDir))
		{
			FailReason = FText::Format(LOCTEXT("FailedToCreateDestinationFolder", "Failed to create destination folder '{0}'"), FText::FromString(FPaths::ConvertRelativePathToFull(DestDir)));
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
						FailReason = FText::Format(LOCTEXT("FailedToCreatePluginSubFolder", "Failed to create plugin subfolder '{0}'"), FText::FromString(FPaths::ConvertRelativePathToFull(NewName)));
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
								FailReason = FText::Format(LOCTEXT("FailedToReadPluginTemplateFile", "Failed to read plugin template file '{0}'"), FText::FromString(FPaths::ConvertRelativePathToFull(FilenameOrDirectory)));
								return false;
							}

							OutFileContents = OutFileContents.Replace(*PLUGIN_NAME, *PluginName, ESearchCase::CaseSensitive);

							// For some content, we also want to export a PLUGIN_NAME_API text macro, which requires that the plugin name
							// be all capitalized

							FString PluginNameAPI = PluginName + TEXT("_API");

							OutFileContents = OutFileContents.Replace(*PluginNameAPI, *PluginNameAPI.ToUpper(), ESearchCase::CaseSensitive);

							if (!FFileHelper::SaveStringToFile(OutFileContents, *NewName))
							{
								FailReason = FText::Format(LOCTEXT("FailedToWritePluginFile", "Failed to write plugin file '{0}'"), FText::FromString(FPaths::ConvertRelativePathToFull(NewName)));
								return false;
							}
						}
						else
						{
							// Copy file from source
							if (!PlatformFile.CopyFile(*NewName, FilenameOrDirectory))
							{
								// Not all files could be copied
								FailReason = FText::Format(LOCTEXT("FailedToCopyPluginTemplateFile", "Failed to copy plugin template file '{0}' to '{1}'"), FText::FromString(FPaths::ConvertRelativePathToFull(FilenameOrDirectory)), FText::FromString(FPaths::ConvertRelativePathToFull(NewName)));
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
							FString AssetName = Asset.AssetName.ToString().Replace(*PLUGIN_NAME, *PluginName, ESearchCase::CaseSensitive);
							FString AssetPath = Asset.PackagePath.ToString().Replace(*PLUGIN_NAME, *PluginName, ESearchCase::CaseSensitive);

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

bool FPluginUtils::CreateAndMountNewPlugin(const FNewPluginParams& Params, FText& FailReason)
{
	// Early validations on new plugin params
	if (Params.PluginName.IsEmpty())
	{
		FailReason = LOCTEXT("CreateNewPluginParam_NoPluginName", "Missing plugin name");
		return false;
	}
	if (Params.PluginLocation.IsEmpty())
	{
		FailReason = LOCTEXT("CreateNewPluginParam_NoPluginLocation", "Missing plugin location");
		return false;
	}
	if (Params.bHasModules && Params.TemplateFolders.Num() == 0)
	{
		FailReason = LOCTEXT("CreateNewPluginParam_NoTemplateFolder", "A template folder must be specified to create a plugin with code");
		return false;
	}

	const FString PluginFolder = FPluginUtils::GetPluginFolder(Params.PluginLocation, Params.PluginName, /*bFullPath*/ true);

	bool bSucceeded = true;
	do 
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		if (!PlatformFile.DirectoryExists(*PluginFolder) && !PlatformFile.CreateDirectoryTree(*PluginFolder))
		{
			FailReason = FText::Format(LOCTEXT("FailedToCreatePluginFolder", "Failed to create plugin folder '{0}'"), FText::FromString(PluginFolder));
			bSucceeded = false;
			break;
		}

		if (Params.bCanContainContent)
		{
			const FString PluginContentFolder = FPluginUtils::GetPluginContentFolder(Params.PluginLocation, Params.PluginName, /*bFullPath*/ true);
			if (!PlatformFile.DirectoryExists(*PluginContentFolder) && !PlatformFile.CreateDirectory(*PluginContentFolder))
			{
				FailReason = FText::Format(LOCTEXT("FailedToCreatePluginContentFolder", "Failed to create plugin Content folder '{0}'"), FText::FromString(PluginContentFolder));
				bSucceeded = false;
				break;
			}
		}

		FPluginDescriptor Descriptor;
		Descriptor.FriendlyName = Params.PluginName;
		Descriptor.Version = 1;
		Descriptor.VersionName = TEXT("1.0");
		Descriptor.Category = TEXT("Other");
		Descriptor.CreatedBy = Params.CreatedBy;
		Descriptor.CreatedByURL = Params.CreatedByURL;
		Descriptor.Description = Params.Description;
		Descriptor.bIsBetaVersion = Params.bIsBetaVersion;
		Descriptor.bCanContainContent = Params.bCanContainContent;
		if (Params.bHasModules)
		{
			Descriptor.Modules.Add(FModuleDescriptor(*Params.PluginName, Params.ModuleDescriptorType, Params.LoadingPhase));
		}

		// Write the uplugin file
		const FString PluginFilePath = FPluginUtils::GetPluginFilePath(Params.PluginLocation, Params.PluginName, /*bFullPath*/ true);
		if (!Descriptor.Save(PluginFilePath, FailReason))
		{
			bSucceeded = false;
			break;
		}

		if (!Params.PluginIconPath.IsEmpty())
		{
			const FString ResourcesFolder = FPluginUtils::GetPluginResourcesFolder(Params.PluginLocation, Params.PluginName, /*bFullPath*/ true);
			const FString DestinationPluginIconPath = FPaths::Combine(ResourcesFolder, TEXT("Icon128.png"));
			if (IFileManager::Get().Copy(*DestinationPluginIconPath, *Params.PluginIconPath, /*bReplaceExisting=*/ false) != COPY_OK)
			{
				FailReason = FText::Format(LOCTEXT("FailedToCopyPluginIcon", "Failed to copy plugin icon from '{0}' to '{1}'"), FText::FromString(FPaths::ConvertRelativePathToFull(Params.PluginIconPath)), FText::FromString(DestinationPluginIconPath));
				bSucceeded = false;
				break;
			}
		}

		GWarn->BeginSlowTask(LOCTEXT("CopyingPluginTemplate", "Copying plugin template files..."), /*ShowProgressDialog*/ true, /*bShowCancelButton*/ false);
		for (const FString& TemplateFolder : Params.TemplateFolders)
		{
			if (!PluginUtils::CopyPluginTemplateFolder(*PluginFolder, *TemplateFolder, Params.PluginName, FailReason))
			{
				FailReason = FText::Format(LOCTEXT("FailedToCopyPluginTemplate", "Failed to copy plugin template files from '{0}' to '{1}'. {2}"), FText::FromString(FPaths::ConvertRelativePathToFull(TemplateFolder)), FText::FromString(PluginFolder), FailReason);
				bSucceeded = false;
				break;
			}
		}
		GWarn->EndSlowTask();
		if (!bSucceeded)
		{
			break;
		}

		if (Params.bHasModules)
		{
			const FString ProjectFileName = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*FPaths::GetProjectFilePath());
			const FString Arguments = FString::Printf(TEXT("%s %s %s -Plugin=\"%s\" -Project=\"%s\" -Progress -NoHotReloadFromIDE"), FPlatformMisc::GetUBTTargetName(), FModuleManager::Get().GetUBTConfiguration(), FPlatformMisc::GetUBTPlatform(), *PluginFilePath, *ProjectFileName);
			if (!FDesktopPlatformModule::Get()->RunUnrealBuildTool(FText::Format(LOCTEXT("CompilingPlugin", "Compiling {0} plugin..."), FText::FromString(Params.PluginName)), FPaths::RootDir(), Arguments, GWarn))
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

		if (Params.bUpdateProjectAddtitionalPluginDirectories)
		{
			// If this path isn't in the Engine/Plugins dir and isn't in Project/Plugins dir,
			// add the directory to the list of ones we additionally scan.
			FString PluginLocationFull = FPaths::ConvertRelativePathToFull(Params.PluginLocation);
			FPaths::MakePlatformFilename(PluginLocationFull);
			FString AbsoluteEnginePluginPath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*FPaths::EnginePluginsDir());
			FPaths::MakePlatformFilename(AbsoluteEnginePluginPath);
			if (!PluginLocationFull.StartsWith(AbsoluteEnginePluginPath))
			{
				// There have been issues with ProjectDir can be relative and PluginLocation absolute, causing our
				// tests to fail below. We now normalize on absolute paths prior to performing the check to ensure
				// that we don't add the folder to the additional plugin directory unnecessarily (which can result in build failures).
				FString ProjectDirFull = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
				FPaths::MakePlatformFilename(ProjectDirFull);
				if (!PluginLocationFull.StartsWith(ProjectDirFull))
				{
					GameProjectUtils::UpdateAdditionalPluginDirectory(PluginLocationFull, true);
				}
			}
		}

		// Update the list of known plugins
		IPluginManager::Get().RefreshPluginsList();

		// Enable this plugin in the project
		if (!IProjectManager::Get().SetPluginEnabled(Params.PluginName, true, FailReason))
		{
			FailReason = FText::Format(LOCTEXT("FailedToEnablePlugin", "Failed to enable plugin: {0}"), FailReason);
			bSucceeded = false;
			break;
		}

		// Mount the plugin
		GWarn->BeginSlowTask(LOCTEXT("MountingPluginFiles", "Mounting plugin files..."), /*ShowProgressDialog*/ true, /*bShowCancelButton*/ false);
		IPluginManager::Get().MountNewlyCreatedPlugin(Params.PluginName);
		GWarn->EndSlowTask();

		if (Params.bCanContainContent)
		{	
			// Attempt to fix any content that was added by the plugin
			GWarn->BeginSlowTask(LOCTEXT("LoadingContent", "Loading Content..."), /*ShowProgressDialog*/ true, /*bShowCancelButton*/ false);
			PluginUtils::FixupPluginTemplateAssets(Params.PluginName);
			GWarn->EndSlowTask();
		}
	} while (false);

	if (!bSucceeded)
	{
		IFileManager::Get().DeleteDirectory(*PluginFolder, /*RequireExists*/ false, /*Tree*/ true);
	}

	return bSucceeded;
}

#undef LOCTEXT_NAMESPACE
