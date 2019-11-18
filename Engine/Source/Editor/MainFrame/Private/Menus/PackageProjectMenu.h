// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Textures/SlateIcon.h"
#include "Misc/Paths.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Frame/MainFrameActions.h"
#include "HAL/FileManager.h"
#include "GameProjectGenerationModule.h"
#include "PlatformInfo.h"
#include "Interfaces/IProjectTargetPlatformEditorModule.h"
#include "Interfaces/IProjectManager.h"
#include "InstalledPlatformInfo.h"
#include "Misc/DataDrivenPlatformInfoRegistry.h"
#include "DesktopPlatformModule.h"

#define LOCTEXT_NAMESPACE "FPackageProjectMenu"

/**
 * Static helper class for populating the "Package Project" menu.
 */
class FPackageProjectMenu
{
public:

	/**
	 * Creates the menu.
	 *
	 * @param MenuBuilder The builder for the menu that owns this menu.
	 */
	static void MakeMenu( FMenuBuilder& MenuBuilder )
	{
		TArray<FName> AllPlatformSubMenus;
		const TArray<FString>& ConfidentalPlatforms = FDataDrivenPlatformInfoRegistry::GetConfidentialPlatforms();

		TArray<PlatformInfo::FVanillaPlatformEntry> VanillaPlatforms = PlatformInfo::BuildPlatformHierarchy(PlatformInfo::EPlatformFilter::All);
		if (!VanillaPlatforms.Num())
		{
			return;
		}

		VanillaPlatforms.Sort([](const PlatformInfo::FVanillaPlatformEntry& One, const PlatformInfo::FVanillaPlatformEntry& Two) -> bool
		{
			return One.PlatformInfo->DisplayName.CompareTo(Two.PlatformInfo->DisplayName) < 0;
		});

		IProjectTargetPlatformEditorModule& ProjectTargetPlatformEditorModule = FModuleManager::LoadModuleChecked<IProjectTargetPlatformEditorModule>("ProjectTargetPlatformEditor");
		EProjectType ProjectType = FGameProjectGenerationModule::Get().ProjectHasCodeFiles() ? EProjectType::Code : EProjectType::Content;

		// Build up a menu from the tree of platforms
		for (const PlatformInfo::FVanillaPlatformEntry& VanillaPlatform : VanillaPlatforms)
		{
			check(VanillaPlatform.PlatformInfo->IsVanilla());

			// Only care about game targets
			if (VanillaPlatform.PlatformInfo->PlatformType != EBuildTargetType::Game || !VanillaPlatform.PlatformInfo->bEnabledForUse || !FInstalledPlatformInfo::Get().CanDisplayPlatform(VanillaPlatform.PlatformInfo->BinaryFolderName, ProjectType))
			{
				continue;
			}

			// Make sure we're able to run this platform
			if (VanillaPlatform.PlatformInfo->bIsConfidential && !ConfidentalPlatforms.Contains(VanillaPlatform.PlatformInfo->IniPlatformName))
			{
				continue;
			}

			// Check if this platform has a submenu entry
			if (VanillaPlatform.PlatformInfo->PlatformSubMenu != NAME_None)
			{
				TArray<const PlatformInfo::FPlatformInfo*> SubMenuEntries;
				const FName& PlatformSubMenu = VanillaPlatform.PlatformInfo->PlatformSubMenu;

				// Check if we've already added this submenu
				if (AllPlatformSubMenus.Find(PlatformSubMenu) != INDEX_NONE)
					continue;
				AllPlatformSubMenus.Add(PlatformSubMenu);

				// Go through all vanilla platforms looking for matching submenus
				for (const PlatformInfo::FVanillaPlatformEntry& SubMenuVanillaPlatform : VanillaPlatforms)
				{
					const PlatformInfo::FPlatformInfo* PlatformInfo = SubMenuVanillaPlatform.PlatformInfo;

					if ((PlatformInfo->PlatformType == EBuildTargetType::Game) && (PlatformInfo->PlatformSubMenu == PlatformSubMenu))
					{
						SubMenuEntries.Add(PlatformInfo);
					}
				}

				if (SubMenuEntries.Num())
				{
					const FText DisplayName = FText::FromName(PlatformSubMenu);

					MenuBuilder.AddSubMenu(
							ProjectTargetPlatformEditorModule.MakePlatformMenuItemWidget(*VanillaPlatform.PlatformInfo, false, DisplayName), 
							FNewMenuDelegate::CreateStatic(&FPackageProjectMenu::AddPlatformSubPlatformsToMenu, SubMenuEntries),
							false
							);
				}
			}
			else if (VanillaPlatform.PlatformFlavors.Num())
			{
				MenuBuilder.AddSubMenu(
					ProjectTargetPlatformEditorModule.MakePlatformMenuItemWidget(*VanillaPlatform.PlatformInfo), 
					FNewMenuDelegate::CreateStatic(&FPackageProjectMenu::AddPlatformSubPlatformsToMenu, VanillaPlatform.PlatformFlavors),
					false
					);
			}
			else
			{
				AddPlatformToMenu(MenuBuilder, *VanillaPlatform.PlatformInfo);
			}
		}

		MenuBuilder.AddMenuSeparator();
		MenuBuilder.AddMenuEntry(FMainFrameCommands::Get().ZipUpProject);

		MenuBuilder.AddMenuSeparator();
		MenuBuilder.AddSubMenu(
			LOCTEXT("PackageProjectBuildConfigurationSubMenuLabel", "Build Configuration"),
			LOCTEXT("PackageProjectBuildConfigurationSubMenuToolTip", "Select the build configuration to package the project with"),
			FNewMenuDelegate::CreateStatic(&FPackageProjectMenu::MakeBuildConfigurationsMenu)
		);

		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
		if (DesktopPlatform->GetTargetsForCurrentProject().Num() > 0)
		{
			MenuBuilder.AddSubMenu(
				LOCTEXT("PackageProjectBuildTargetSubMenuLabel", "Build Target"),
				LOCTEXT("PackageProjectBuildTargetSubMenuToolTip", "Select the build target to package"),
				FNewMenuDelegate::CreateStatic(&FPackageProjectMenu::MakeBuildTargetsMenu)
			);
		}

		MenuBuilder.AddMenuSeparator();
		MenuBuilder.AddMenuEntry(FMainFrameCommands::Get().PackagingSettings);

		ProjectTargetPlatformEditorModule.AddOpenProjectTargetPlatformEditorMenuItem(MenuBuilder);
	}

protected:

	/**
	 * Creates the platform menu entries.
	 *
	 * @param MenuBuilder The builder for the menu that owns this menu.
	 * @param Platform The target platform we allow packaging for
	 */
	static void AddPlatformToMenu(FMenuBuilder& MenuBuilder, const PlatformInfo::FPlatformInfo& PlatformInfo)
	{
		EProjectType ProjectType = FGameProjectGenerationModule::Get().ProjectHasCodeFiles() ? EProjectType::Code : EProjectType::Content;

		// don't add sub-platforms that can't be displayed in an installed build
		if (!FInstalledPlatformInfo::Get().CanDisplayPlatform(PlatformInfo.BinaryFolderName, ProjectType))
		{
			return;
		}

		IProjectTargetPlatformEditorModule& ProjectTargetPlatformEditorModule = FModuleManager::LoadModuleChecked<IProjectTargetPlatformEditorModule>("ProjectTargetPlatformEditor");

		FUIAction Action(
			FExecuteAction::CreateStatic(&FMainFrameActionCallbacks::PackageProject, PlatformInfo.PlatformInfoName),
			FCanExecuteAction::CreateStatic(&FMainFrameActionCallbacks::PackageProjectCanExecute, PlatformInfo.PlatformInfoName)
			);

		// ... generate tooltip text
		FFormatNamedArguments TooltipArguments;
		TooltipArguments.Add(TEXT("DisplayName"), PlatformInfo.DisplayName);
		FText Tooltip = FText::Format(LOCTEXT("PackageGameForPlatformTooltip", "Build, cook and package your game for the {DisplayName} platform"), TooltipArguments);

		FProjectStatus ProjectStatus;
		if (IProjectManager::Get().QueryStatusForCurrentProject(ProjectStatus) && !ProjectStatus.IsTargetPlatformSupported(PlatformInfo.VanillaPlatformName))
		{
			FText TooltipLine2 = FText::Format(LOCTEXT("PackageUnsupportedPlatformWarning", "{DisplayName} is not listed as a target platform for this project, so may not run as expected."), TooltipArguments);
			Tooltip = FText::Format(FText::FromString(TEXT("{0}\n\n{1}")), Tooltip, TooltipLine2);
		}

		// ... and add a menu entry
		MenuBuilder.AddMenuEntry(
			Action, 
			ProjectTargetPlatformEditorModule.MakePlatformMenuItemWidget(PlatformInfo), 
			NAME_None, 
			Tooltip
			);
	}

	/**
	 * Creates the platform menu entries for a given platforms sub-platforms.
	 * e.g. Windows has multiple sub-platforms - Win32 and Win64
	 *
	 * @param MenuBuilderThe builder for the menu that owns this menu.
	 * @param SubPlatformInfos The Sub-platform information
	 */
	static void AddPlatformSubPlatformsToMenu(FMenuBuilder& MenuBuilder, TArray<const PlatformInfo::FPlatformInfo*> SubPlatformInfos)
	{
		for (const PlatformInfo::FPlatformInfo* SubPlatformInfo : SubPlatformInfos)
		{
			if (SubPlatformInfo->PlatformType != EBuildTargetType::Game)
			{
				continue;
			}
			AddPlatformToMenu(MenuBuilder, *SubPlatformInfo);
		}
	}

	/**
	 * Creates a build configuration sub-menu.
	 *
	 * @param MenuBuilder The builder for the menu that owns this menu.
	 */
	static void MakeBuildConfigurationsMenu(FMenuBuilder& MenuBuilder)
	{
		EProjectType ProjectType = FGameProjectGenerationModule::Get().ProjectHasCodeFiles() ? EProjectType::Code : EProjectType::Content;

		TArray<EProjectPackagingBuildConfigurations> PackagingConfigurations = UProjectPackagingSettings::GetValidPackageConfigurations();
		for(EProjectPackagingBuildConfigurations PackagingConfiguration : PackagingConfigurations)
		{
			const UProjectPackagingSettings::FConfigurationInfo& Info = UProjectPackagingSettings::ConfigurationInfo[(int)PackagingConfiguration];
			if (FInstalledPlatformInfo::Get().IsValid(TOptional<EBuildTargetType>(), TOptional<FString>(), Info.Configuration, ProjectType, EInstalledPlatformState::Downloaded))
			{
				MenuBuilder.AddMenuEntry(
					Info.Name, 
					Info.ToolTip,
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateStatic(&FMainFrameActionCallbacks::PackageBuildConfiguration, PackagingConfiguration),
						FCanExecuteAction::CreateStatic(&FMainFrameActionCallbacks::CanPackageBuildConfiguration, PackagingConfiguration),
						FIsActionChecked::CreateStatic(&FMainFrameActionCallbacks::PackageBuildConfigurationIsChecked, PackagingConfiguration)
					),
					NAME_None,
					EUserInterfaceActionType::RadioButton
				);
			}
		}
	}

	/**
	 * Creates a build configuration sub-menu.
	 *
	 * @param MenuBuilder The builder for the menu that owns this menu.
	 */
	static void MakeBuildTargetsMenu(FMenuBuilder& MenuBuilder)
	{
		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();

		TArray<FTargetInfo> Targets = DesktopPlatform->GetTargetsForCurrentProject();
		Targets.Sort([](const FTargetInfo& A, const FTargetInfo& B){ return A.Name < B.Name; });

		for (const FTargetInfo& Target : Targets)
		{
			if (Target.Type == EBuildTargetType::Game || Target.Type == EBuildTargetType::Client || Target.Type == EBuildTargetType::Server)
			{
				MenuBuilder.AddMenuEntry(
					FText::FromString(Target.Name),
					FText::Format(LOCTEXT("PackageTargetName", "Package the '{0}' target."), FText::FromString(Target.Name)),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateStatic(&FMainFrameActionCallbacks::PackageBuildTarget, Target.Name),
						FCanExecuteAction(),
						FIsActionChecked::CreateStatic(&FMainFrameActionCallbacks::PackageBuildTargetIsChecked, Target.Name)
					),
					NAME_None,
					EUserInterfaceActionType::RadioButton
				);
			}
		}
	}
};


#undef LOCTEXT_NAMESPACE
