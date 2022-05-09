// Copyright Epic Games, Inc. All Rights Reserved.
#include "AssetImporters/DHIImport.h"
#include "MSAssetImportData.h"
#include "Utilities/MiscUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/ScopedSlowTask.h"
#include "EditorAssetLibrary.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/IProjectManager.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Internationalization/Text.h"
#include "UObject/Object.h"

TSharedPtr<FImportDHI> FImportDHI::ImportDHIInst;

TSharedPtr<FDHIData> FImportDHI::ParseDHIData(TSharedPtr<FJsonObject> AssetImportJson)
{
    TSharedPtr<FDHIData> DHIImportData = MakeShareable(new FDHIData);
    DHIImportData->CharacterPath = AssetImportJson->GetStringField(TEXT("characterPath"));
    DHIImportData->CharacterName = AssetImportJson->GetStringField(TEXT("folderName"));
    DHIImportData->CommonPath = AssetImportJson->GetStringField(TEXT("commonPath"));
    DHIImportData->CharacterPath = FPaths::Combine(DHIImportData->CharacterPath, DHIImportData->CharacterName);
    return DHIImportData;
}

TSharedPtr<FImportDHI> FImportDHI::Get()
{
    if (!ImportDHIInst.IsValid())
    {
        ImportDHIInst = MakeShareable(new FImportDHI);
    }
    return ImportDHIInst;
}

void FImportDHI::ImportAsset(TSharedPtr<FJsonObject> AssetImportJson)
{

    TSharedPtr<FDHIData> CharacterSourceData = ParseDHIData(AssetImportJson);
    IPlatformFile &PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

    bool bIsCharacterUE5 = false;

    if (PlatformFile.FileExists(*FPaths::Combine(CharacterSourceData->CharacterPath, TEXT("VersionInfo.txt"))))
    {
        bIsCharacterUE5 = true;
    }

    TArray<FString> IncompatibleCharacters = CheckVersionCompatibilty();

    if (IncompatibleCharacters.Num() > 0 && bIsCharacterUE5)
    {
        FString CharacterOutputString = TEXT("");
        for (FString CharacterName : IncompatibleCharacters)
        {
            CharacterOutputString += TEXT("\n") + CharacterName;
        }

        FString BaseMessage = TEXT("This project contains UE4 MetaHumans, which are incompatible with UE5 MetaHumans in the same project. Adding a UE5 MH can result in breaking the existing UE4 MH until each has been replaced with UE5. \nContinue anyway.?");

        EAppReturnType::Type ContinueImport = FMessageDialog::Open(EAppMsgType::YesNo, FText(FText::FromString(BaseMessage)));
        if (ContinueImport == EAppReturnType::No)
            return;
    }

    
    FAssetRegistryModule &AssetRegistryModule = FModuleManager::GetModuleChecked<FAssetRegistryModule>("AssetRegistry");
    TArray<FString> AssetsBasePath;
    AssetsBasePath.Add(TEXT("/Game/MetaHumans"));

    FString CommonDestinationPath = FPaths::ProjectContentDir();
    FString MetaHumansRoot = FPaths::Combine(CommonDestinationPath, TEXT("MetaHumans"));
    CommonDestinationPath = FPaths::Combine(MetaHumansRoot, TEXT("Common"));
    FString RootFolder = TEXT("/Game/MetaHumans");
    FString CommonFolder = FPaths::Combine(RootFolder, TEXT("Common"));
    FString CharacterName = CharacterSourceData->CharacterName;
    FString CharacterDestination = FPaths::Combine(MetaHumansRoot, CharacterName);

    if (PlatformFile.DirectoryExists(*CharacterDestination))
    {
        return;

        EAppReturnType::Type ContinueImport = FMessageDialog::Open(EAppMsgType::YesNo, FText(FText::FromString("The character you are trying to import already exists. Do you want to overwrite it.")));
        if (ContinueImport == EAppReturnType::No)
            return;

        // Delete existing asset
        FString CharacterPath = FPaths::Combine(TEXT("/Game/MetaHumans"), CharacterSourceData->CharacterName);
        AssetUtils::DeleteDirectory(CharacterPath);
    }

    TArray<FString> SourceCommonFiles;
    PlatformFile.FindFilesRecursively(SourceCommonFiles, *CharacterSourceData->CommonPath, NULL);
    TArray<FString> ExistingAssets = UEditorAssetLibrary::ListAssets(TEXT("/Game/MetaHumans/Common"));

    FString ProjectCommonPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("MetaHumans"), TEXT("Common")));
    FString SourceCommonPath = CharacterSourceData->CommonPath;
    FPaths::NormalizeDirectoryName(SourceCommonPath);

    TArray<FString> ProjectCommonFiles;
    PlatformFile.FindFilesRecursively(ProjectCommonFiles, *ProjectCommonPath, TEXT("uasset"));
    TArray<FString> ExistingAssetStrippedPaths;

    for (FString ProjectCommonFile : ProjectCommonFiles)
    {
        ExistingAssetStrippedPaths.Add(ProjectCommonFile.Replace(*ProjectCommonPath, TEXT("")));
    }

    int8 CommonFilesCount = 0;

    for (FString FileToCopy : SourceCommonFiles)
    {
        FString NormalizedSourceFile = FileToCopy;
        FPaths::NormalizeFilename(NormalizedSourceFile);
        FString StrippedSourcePath = NormalizedSourceFile.Replace(*SourceCommonPath, TEXT(""));

        if (!ExistingAssetStrippedPaths.Contains(StrippedSourcePath))
        {

            CommonFilesCount += 1;
        }
    }

    TArray<FString> FilesToCopy;
    FString CommonCopyMsg = TEXT("Importing Common Assets.");
    FText CommonCopyMsgDialogMessage = FText::FromString(CommonCopyMsg);
    FScopedSlowTask AssetLoadprogress(CommonFilesCount, CommonCopyMsgDialogMessage, true);
    AssetLoadprogress.MakeDialog();
    
    for (FString FileToCopy : SourceCommonFiles)
    {

        FString NormalizedSourceFile = FileToCopy;
        FPaths::NormalizeFilename(NormalizedSourceFile);
        FString StrippedSourcePath = NormalizedSourceFile.Replace(*SourceCommonPath, TEXT(""));

        if (!ExistingAssetStrippedPaths.Contains(StrippedSourcePath))
        {

            FString CommonFileDestination = FPaths::Combine(CommonDestinationPath, FileToCopy.Replace(*CharacterSourceData->CommonPath, TEXT("")));

            FString FileDirectory = FPaths::GetPath(CommonFileDestination);

            PlatformFile.CreateDirectoryTree(*FileDirectory);
			AssetLoadprogress.EnterProgressFrame();
            PlatformFile.CopyFile(*CommonFileDestination, *FileToCopy);
        }
    }

	

    AssetRegistryModule.Get().ScanPathsSynchronous(AssetsBasePath, true);

    PlatformFile.CreateDirectoryTree(*MetaHumansRoot);

    AssetRegistryModule.Get().ScanPathsSynchronous(AssetsBasePath, true);

    PlatformFile.CreateDirectoryTree(*CharacterDestination);
    AssetsBasePath.Add("/Game/MetaHumans/" + CharacterSourceData->CharacterName);

    AssetRegistryModule.Get().ScanPathsSynchronous(AssetsBasePath, true);

    FString CharacterCopyMsg = TEXT("Importing : ") + CharacterName;
    FText CharacterCopyMsgDialogMessage = FText::FromString(CharacterCopyMsg);
    FScopedSlowTask CharacterLoadprogress(1.0f, CharacterCopyMsgDialogMessage, true);
    CharacterLoadprogress.MakeDialog();
    CharacterLoadprogress.EnterProgressFrame(1.0f);
    PlatformFile.CopyDirectoryTree(*CharacterDestination, *CharacterSourceData->CharacterPath, true);

    AssetRegistryModule.Get().ScanPathsSynchronous(AssetsBasePath, true);

    FString BPName = TEXT("BP_") + CharacterSourceData->CharacterName;
    BPName += TEXT(".") + BPName;
    FString BPPath = FPaths::Combine(TEXT("/Game/MetaHumans/"), CharacterSourceData->CharacterName, BPName);

    FAssetData CharacterAssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FName(*BPPath));

    // NOTE: the RigLogic plugin (and maybe others) must be loaded and added to the project before loading the asset
    // otherwise we get rid of the RigLogic nodes, resulting in leaving the asset in an undefined state. In the context
    // of ControlRig assets, graphs will remove the RigLogic nodes if the plugin is not enabled because the
    // FRigUnit_RigLogic_Data won't be available
    EnableMissingPlugins();

    // As part of our discussion to fix this, here was the explanation about why the asset has to be loaded:
    // "In UE4 we came across this issue where even if we did call the syncfolder on content browser, it would still
    // not show the character in content browser. So a workaround to make the character appear was to just load
    // the character."
    UObject *CharacterObject = CharacterAssetData.GetAsset();

    AssetUtils::FocusOnSelected(CharacterDestination);
}

void FImportDHI::EnableMissingPlugins()
{
    // TODO we should find a way to retrieve the required plugins from the metadata as RigLogic might not be the only one
    static const TArray<FString> NeededPluginNames({TEXT("RigLogic")});

    IPluginManager &PluginManager = IPluginManager::Get();
    IProjectManager &ProjectManager = IProjectManager::Get();

    for (const FString &PluginName : NeededPluginNames)
    {
        TSharedPtr<IPlugin> NeededPlugin = PluginManager.FindPlugin(PluginName);
        if (NeededPlugin.IsValid() && !NeededPlugin->IsEnabled())
        {
            FText FailMessage;
            bool bPluginEnabled = ProjectManager.SetPluginEnabled(NeededPlugin->GetName(), true, FailMessage);

            if (bPluginEnabled && ProjectManager.IsCurrentProjectDirty())
            {
                bPluginEnabled = ProjectManager.SaveCurrentProjectToDisk(FailMessage);
            }

            if (bPluginEnabled)
            {
                PluginManager.MountNewlyCreatedPlugin(NeededPlugin->GetName());
            }
            else
            {
                FMessageDialog::Open(EAppMsgType::Ok, FailMessage);
            }
        }
    }
}

// Check if the project contains any UE4 metahuman characters
TArray<FString> FImportDHI::CheckVersionCompatibilty()
{
    TArray<FString> IncompatibleCharacters;
    TArray<FString> DirectoryList = UEditorAssetLibrary::ListAssets(TEXT("/Game/Metahumans"), false, true);

    FString ProjectMetahumanPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("MetaHumans")));

    IPlatformFile &PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

    for (FString FoundAsset : DirectoryList)
    {
        if (UEditorAssetLibrary::DoesDirectoryExist(FoundAsset))
        {
            FString CharacterName = FoundAsset.Replace(TEXT("/Game/Metahumans/"), TEXT(""));

            if (CharacterName == TEXT("Common") || CharacterName == TEXT("Common/"))
            {
                continue;
            }

            FString VersionFilePath = FPaths::Combine(ProjectMetahumanPath, CharacterName, TEXT("VersionInfo.txt"));

            if (!PlatformFile.FileExists(*VersionFilePath))
            {
                IncompatibleCharacters.Add(CharacterName);
            }
        }
    }

    return IncompatibleCharacters;
}
