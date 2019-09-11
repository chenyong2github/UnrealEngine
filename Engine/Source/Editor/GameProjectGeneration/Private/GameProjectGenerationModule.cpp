// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GameProjectGenerationModule.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "EditorStyleSet.h"
#include "GameProjectGenerationLog.h"
#include "GameProjectUtils.h"
#include "SGameProjectDialog.h"
#include "SNewClassDialog.h"
#include "TemplateCategory.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "TemplateProjectDefs.h"
#include "Internationalization/Culture.h"

IMPLEMENT_MODULE( FGameProjectGenerationModule, GameProjectGeneration );
DEFINE_LOG_CATEGORY(LogGameProjectGeneration);

#define LOCTEXT_NAMESPACE "GameProjectGeneration"

void FGameProjectGenerationModule::StartupModule()
{
	LoadTemplateCategories();
}

void FGameProjectGenerationModule::ShutdownModule()
{
	
}

TSharedRef<class SWidget> FGameProjectGenerationModule::CreateGameProjectDialog(bool bAllowProjectOpening, bool bAllowProjectCreate)
{
	ensure(bAllowProjectOpening || bAllowProjectCreate);

	SGameProjectDialog::EMode Mode = SGameProjectDialog::EMode::Both;
	
	if (bAllowProjectOpening && !bAllowProjectCreate)
	{
		Mode = SGameProjectDialog::EMode::Open;
	}
	else if (bAllowProjectCreate && !bAllowProjectOpening)
	{
		Mode = SGameProjectDialog::EMode::New;
	}

	return SNew(SGameProjectDialog, Mode);
}


TSharedRef<class SWidget> FGameProjectGenerationModule::CreateNewClassDialog(const UClass* InClass)
{
	return SNew(SNewClassDialog).Class(InClass);
}

void FGameProjectGenerationModule::OpenAddCodeToProjectDialog(const FAddToProjectConfig& Config)
{
	GameProjectUtils::OpenAddToProjectDialog(Config, EClassDomain::Native);
	AddCodeToProjectDialogOpenedEvent.Broadcast();
}

void FGameProjectGenerationModule::OpenAddBlueprintToProjectDialog(const FAddToProjectConfig& Config)
{
	GameProjectUtils::OpenAddToProjectDialog(Config, EClassDomain::Blueprint);
}

void FGameProjectGenerationModule::TryMakeProjectFileWriteable(const FString& ProjectFile)
{
	GameProjectUtils::TryMakeProjectFileWriteable(ProjectFile);
}

void FGameProjectGenerationModule::CheckForOutOfDateGameProjectFile()
{
	GameProjectUtils::CheckForOutOfDateGameProjectFile();
}


bool FGameProjectGenerationModule::UpdateGameProject(const FString& ProjectFile, const FString& EngineIdentifier, FText& OutFailReason)
{
	return GameProjectUtils::UpdateGameProject(ProjectFile, EngineIdentifier, OutFailReason);
}


bool FGameProjectGenerationModule::UpdateCodeProject(FText& OutFailReason, FText& OutFailLog)
{
	FScopedSlowTask SlowTask(0, LOCTEXT( "UpdatingCodeProject", "Updating code project..." ) );
	SlowTask.MakeDialog();

	return GameProjectUtils::GenerateCodeProjectFiles(FPaths::GetProjectFilePath(), OutFailReason, OutFailLog);
}

bool FGameProjectGenerationModule::GenerateBasicSourceCode(TArray<FString>& OutCreatedFiles, FText& OutFailReason)
{
	return GameProjectUtils::GenerateBasicSourceCode(OutCreatedFiles, OutFailReason);
}

bool FGameProjectGenerationModule::ProjectHasCodeFiles()
{
	return GameProjectUtils::ProjectHasCodeFiles();
}

FString FGameProjectGenerationModule::DetermineModuleIncludePath(const FModuleContextInfo& ModuleInfo, const FString& FileRelativeTo)
{
	return GameProjectUtils::DetermineModuleIncludePath(ModuleInfo, FileRelativeTo);
}

const TArray<FModuleContextInfo>& FGameProjectGenerationModule::GetCurrentProjectModules()
{
	return GameProjectUtils::GetCurrentProjectModules();
}

bool FGameProjectGenerationModule::IsValidBaseClassForCreation(const UClass* InClass, const FModuleContextInfo& InModuleInfo)
{
	return GameProjectUtils::IsValidBaseClassForCreation(InClass, InModuleInfo);
}

bool FGameProjectGenerationModule::IsValidBaseClassForCreation(const UClass* InClass, const TArray<FModuleContextInfo>& InModuleInfoArray)
{
	return GameProjectUtils::IsValidBaseClassForCreation(InClass, InModuleInfoArray);
}

void FGameProjectGenerationModule::GetProjectSourceDirectoryInfo(int32& OutNumFiles, int64& OutDirectorySize)
{
	GameProjectUtils::GetProjectSourceDirectoryInfo(OutNumFiles, OutDirectorySize);
}

void FGameProjectGenerationModule::CheckAndWarnProjectFilenameValid()
{
	GameProjectUtils::CheckAndWarnProjectFilenameValid();
}


void FGameProjectGenerationModule::UpdateSupportedTargetPlatforms(const FName& InPlatformName, const bool bIsSupported)
{
	GameProjectUtils::UpdateSupportedTargetPlatforms(InPlatformName, bIsSupported);
}

void FGameProjectGenerationModule::ClearSupportedTargetPlatforms()
{
	GameProjectUtils::ClearSupportedTargetPlatforms();
}

void FGameProjectGenerationModule::LoadTemplateCategories()
{
	// Now discover and all data driven templates
	TArray<FString> TemplateRootFolders;

	// @todo rocket make template folder locations extensible.
	TemplateRootFolders.Add(FPaths::RootDir() / TEXT("Templates"));

	// Add the Enterprise templates
	TemplateRootFolders.Add(FPaths::EnterpriseDir() / TEXT("Templates"));

	// allow plugins to define templates
	TArray<TSharedRef<IPlugin>> Plugins = IPluginManager::Get().GetEnabledPlugins();
	for (const TSharedRef<IPlugin>& Plugin : Plugins)
	{
		FString PluginDirectory = Plugin->GetBaseDir();
		if (!PluginDirectory.IsEmpty())
		{
			const FString PluginTemplatesDirectory = PluginDirectory / TEXT("Templates");

			if (IFileManager::Get().DirectoryExists(*PluginTemplatesDirectory))
			{
				TemplateRootFolders.Add(PluginTemplatesDirectory);
			}
		}
	}

	for (const FString& Root : TemplateRootFolders)
	{
		UTemplateCategories* CategoryDefs = GameProjectUtils::LoadTemplateCategories(Root);
		if (CategoryDefs != nullptr)
		{
			for (const FTemplateCategoryDef& Category : CategoryDefs->Categories)
			{
				TSharedPtr<FTemplateCategory> TemplateCategory;

				TSharedPtr<FTemplateCategory>* Existing = TemplateCategories.Find(Category.Key);
				if (Existing == nullptr)
				{
					TemplateCategory = TemplateCategories.Add(Category.Key, MakeShareable(new FTemplateCategory));
				}
				else
				{
					TemplateCategory = *Existing;
				}

				TemplateCategory->Key = Category.Key;
				TemplateCategory->DisplayName = FLocalizedTemplateString::GetLocalizedText(Category.LocalizedDisplayNames);
				TemplateCategory->Description = FLocalizedTemplateString::GetLocalizedText(Category.LocalizedDescriptions);
				
				const FName BrushName(*Category.Icon);
				TemplateCategory->Icon = new FSlateDynamicImageBrush(BrushName, FVector2D(128, 128));

				TemplateCategory->IsMajor = Category.IsMajorCategory;
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
