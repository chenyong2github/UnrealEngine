// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Menus/MainMenu.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxDefs.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Interfaces/IMainFrameModule.h"
#include "DesktopPlatformModule.h"
#include "ISourceControlModule.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Toolkits/GlobalEditorCommonCommands.h"
#include "SourceCodeNavigation.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "EditorStyleSet.h"
#include "Classes/EditorStyleSettings.h"
#include "Editor/UnrealEdEngine.h"
#include "Settings/EditorExperimentalSettings.h"
#include "UnrealEdGlobals.h"
#include "Frame/MainFrameActions.h"
#include "Menus/PackageProjectMenu.h"
#include "Menus/RecentProjectsMenu.h"
#include "Menus/SettingsMenu.h"
#include "Menus/MainFrameTranslationEditorMenu.h"

#include "EditorMenuSubsystem.h"

#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Features/EditorFeatures.h"
#include "Features/IModularFeatures.h"
#include "UndoHistoryModule.h"
#include "Framework/Commands/GenericCommands.h"
#include "ToolboxModule.h"


#define LOCTEXT_NAMESPACE "MainFileMenu"


void FMainMenu::RegisterFileMenu()
{
	UEditorMenuSubsystem* EditorMenus = UEditorMenuSubsystem::Get();
	UEditorMenu* FileMenu = EditorMenus->RegisterMenu("MainFrame.MainMenu.File");

	FEditorMenuSection& FileLoadAndSaveSection = FileMenu->AddSection("FileLoadAndSave", LOCTEXT("LoadSandSaveHeading", "Load and Save"));
	{
		// Open Asset...
		FileLoadAndSaveSection.AddMenuEntry(FGlobalEditorCommonCommands::Get().SummonOpenAssetDialog);

		// Save All
		FileLoadAndSaveSection.AddMenuEntry(FMainFrameCommands::Get().SaveAll);

		// Choose specific files to save
		FileLoadAndSaveSection.AddMenuEntry(FMainFrameCommands::Get().ChooseFilesToSave);

		FileLoadAndSaveSection.AddDynamicEntry("SourceControl", FNewEditorMenuSectionDelegate::CreateLambda([](FEditorMenuSection& InSection)
		{
			if (ISourceControlModule::Get().IsEnabled() && ISourceControlModule::Get().GetProvider().IsAvailable())
			{
				// Choose specific files to submit
				InSection.AddMenuEntry(FMainFrameCommands::Get().ChooseFilesToCheckIn);
			}
			else
			{
				InSection.AddMenuEntry(FMainFrameCommands::Get().ConnectToSourceControl);
			}
		}));
	}

	RegisterFileProjectMenu();
	RegisterRecentFileAndExitMenuItems();
}

#undef LOCTEXT_NAMESPACE


#define LOCTEXT_NAMESPACE "MainEditMenu"

void FMainMenu::RegisterEditMenu()
{
	UEditorMenu* EditMenu = UEditorMenuSubsystem::Get()->RegisterMenu("MainFrame.MainMenu.Edit");
	{
		FEditorMenuSection& Section = EditMenu->AddSection("EditHistory", LOCTEXT("HistoryHeading", "History"));
		struct Local
		{
			/** @return Returns a dynamic text string for Undo that contains the name of the action */
			static FText GetUndoLabelText()
			{
				return FText::Format(LOCTEXT("DynamicUndoLabel", "Undo {0}"), GUnrealEd->Trans->GetUndoContext().Title);
			}

			/** @return Returns a dynamic text string for Redo that contains the name of the action */
			static FText GetRedoLabelText()
			{
				return FText::Format(LOCTEXT("DynamicRedoLabel", "Redo {0}"), GUnrealEd->Trans->GetRedoContext().Title);
			}
		};

		// Undo
		TAttribute<FText> DynamicUndoLabel;
		DynamicUndoLabel.BindStatic(&Local::GetUndoLabelText);
		Section.AddMenuEntry(FGenericCommands::Get().Undo, DynamicUndoLabel); // TAttribute< FString >::Create( &Local::GetUndoLabelText ) );

		// Redo
		TAttribute< FText > DynamicRedoLabel;
		DynamicRedoLabel.BindStatic( &Local::GetRedoLabelText );
		Section.AddMenuEntry(FGenericCommands::Get().Redo, DynamicRedoLabel); // TAttribute< FString >::Create( &Local::GetRedoLabelText ) );

		// Show undo history
		Section.AddMenuEntry(
			"UndoHistory",
			LOCTEXT("UndoHistoryTabTitle", "Undo History"),
			LOCTEXT("UndoHistoryTooltipText", "View the entire undo history."),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "UndoHistory.TabIcon"),
			FUIAction(FExecuteAction::CreateStatic(&FUndoHistoryModule::ExecuteOpenUndoHistory))
			);
	}

	{
		FEditorMenuSection& Section = EditMenu->AddSection("EditLocalTabSpawners", LOCTEXT("ConfigurationHeading", "Configuration"));
		if (GetDefault<UEditorExperimentalSettings>()->bToolbarCustomization)
		{
			FUIAction ToggleMultiBoxEditMode(
				FExecuteAction::CreateStatic(&FMultiBoxSettings::ToggleToolbarEditing),
				FCanExecuteAction(),
				FIsActionChecked::CreateStatic(&FMultiBoxSettings::IsInToolbarEditMode)
			);
		
			Section.AddMenuEntry(
				"EditToolbars",
				LOCTEXT("EditToolbarsLabel", "Edit Toolbars"),
				LOCTEXT("EditToolbarsToolTip", "Allows customization of each toolbar"),
				FSlateIcon(),
				ToggleMultiBoxEditMode,
				EUserInterfaceActionType::ToggleButton
			);

			Section.AddDynamicEntry("TabManager", FNewEditorMenuDelegateLegacy::CreateLambda([](FMenuBuilder& MenuBuilder, UEditorMenu* MenuData)
			{
				if (USlateTabManagerContext* TabManagerContext = MenuData->FindContext<USlateTabManagerContext>())
				{
					if (TabManagerContext->TabManager.IsValid())
					{
						// Automatically populate tab spawners from TabManager
						const IWorkspaceMenuStructure& MenuStructure = WorkspaceMenu::GetMenuStructure();
						TabManagerContext->TabManager.Pin()->PopulateTabSpawnerMenu(MenuBuilder, MenuStructure.GetEditOptions());
					}
				}
			}));
		}

		if (GetDefault<UEditorStyleSettings>()->bExpandConfigurationMenus)
		{
			Section.AddEntry(FEditorMenuEntry::InitSubMenu(
				EditMenu->GetMenuName(),
				"EditorPreferencesSubMenu",
				LOCTEXT("EditorPreferencesSubMenuLabel", "Editor Preferences"),
				LOCTEXT("EditorPreferencesSubMenuToolTip", "Configure the behavior and features of this Editor"),
				FNewEditorMenuDelegate::CreateStatic(&FSettingsMenu::MakeMenu, FName("Editor")),
				false,
				FSlateIcon(FEditorStyle::GetStyleSetName(), "EditorPreferences.TabIcon")
			));

			Section.AddEntry(FEditorMenuEntry::InitSubMenu(
				EditMenu->GetMenuName(),
				"ProjectSettingsSubMenu",
				LOCTEXT("ProjectSettingsSubMenuLabel", "Project Settings"),
				LOCTEXT("ProjectSettingsSubMenuToolTip", "Change the settings of the currently loaded project"),
				FNewEditorMenuDelegate::CreateStatic(&FSettingsMenu::MakeMenu, FName("Project")),
				false,
				FSlateIcon(FEditorStyle::GetStyleSetName(), "ProjectSettings.TabIcon")
			));
		}
		else
		{
#if !PLATFORM_MAC // Handled by app's menu in menu bar
			Section.AddMenuEntry(
				"EditorPreferencesMenu",
				LOCTEXT("EditorPreferencesMenuLabel", "Editor Preferences..."),
				LOCTEXT("EditorPreferencesMenuToolTip", "Configure the behavior and features of the Unreal Editor."),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "EditorPreferences.TabIcon"),
				FUIAction(FExecuteAction::CreateStatic(&FSettingsMenu::OpenSettings, FName("Editor"), FName("General"), FName("Appearance")))
			);
#endif

			Section.AddMenuEntry(
				"ProjectSettingsMenu",
				LOCTEXT("ProjectSettingsMenuLabel", "Project Settings..."),
				LOCTEXT("ProjectSettingsMenuToolTip", "Change the settings of the currently loaded project."),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "ProjectSettings.TabIcon"),
				FUIAction(FExecuteAction::CreateStatic(&FSettingsMenu::OpenSettings, FName("Project"), FName("Project"), FName("General")))
			);
		}

		Section.AddDynamicEntry("PluginsEditor", FNewEditorMenuDelegateLegacy::CreateLambda([](FMenuBuilder& InBuilder, UEditorMenu* InData)
		{
			//@todo The tab system needs to be able to be extendable by plugins [9/3/2013 Justin.Sargent]
			if (IModularFeatures::Get().IsModularFeatureAvailable(EditorFeatures::PluginsEditor))
			{
				FGlobalTabmanager::Get()->PopulateTabSpawnerMenu(InBuilder, "PluginsEditor");
			}
		}));
	}
}

#undef LOCTEXT_NAMESPACE

#define LOCTEXT_NAMESPACE "MainWindowMenu"

void FMainMenu::RegisterWindowMenu()
{
	UEditorMenu* Menu = UEditorMenuSubsystem::Get()->RegisterMenu("MainFrame.MainMenu.Window");

	// Automatically populate tab spawners from TabManager
	Menu->AddDynamicSection("TabManagerSection", FNewEditorMenuDelegateLegacy::CreateLambda([](FMenuBuilder& InBuilder, UEditorMenu* InData)
	{
		if (USlateTabManagerContext* TabManagerContext = InData->FindContext<USlateTabManagerContext>())
		{
			TSharedPtr<FTabManager> TabManager = TabManagerContext->TabManager.Pin();
			if (TabManager.IsValid())
			{
				// Local editor tabs
				TabManager->PopulateLocalTabSpawnerMenu(InBuilder);

				// General tabs
				const IWorkspaceMenuStructure& MenuStructure = WorkspaceMenu::GetMenuStructure();
				TabManager->PopulateTabSpawnerMenu(InBuilder, MenuStructure.GetStructureRoot());
			}
		}
	}));

	{
		FEditorMenuSection& Section = Menu->AddSection("WindowGlobalTabSpawners");
		Section.AddMenuEntry(
			"ProjectLauncher",
			LOCTEXT("ProjectLauncherLabel", "Project Launcher"),
			LOCTEXT("ProjectLauncherToolTip", "The Project Launcher provides advanced workflows for packaging, deploying and launching your projects."),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Launcher.TabIcon"),
			FUIAction(FExecuteAction::CreateStatic(&FMainMenu::OpenProjectLauncher))
			);
	}

	{
		// This is a temporary home for the spawners of experimental features that must be explicitly enabled.
		// When the feature becomes permanent and need not check a flag, register a nomad spawner for it in the proper WorkspaceMenu category
		bool bLocalizationDashboard = GetDefault<UEditorExperimentalSettings>()->bEnableLocalizationDashboard;
		bool bTranslationPicker = GetDefault<UEditorExperimentalSettings>()->bEnableTranslationPicker;

		// Make sure at least one is enabled before creating the section
		if (bLocalizationDashboard || bTranslationPicker)
		{
			FEditorMenuSection& Section = Menu->AddSection("ExperimentalTabSpawners", LOCTEXT("ExperimentalTabSpawnersHeading", "Experimental"));
			{
				// Localization Dashboard
				if (bLocalizationDashboard)
				{
					Section.AddMenuEntry(
						"LocalizationDashboard",
						LOCTEXT("LocalizationDashboardLabel", "Localization Dashboard"),
						LOCTEXT("LocalizationDashboardToolTip", "Open the Localization Dashboard for this Project."),
						FSlateIcon(),
						FUIAction(FExecuteAction::CreateStatic(&FMainMenu::OpenLocalizationDashboard))
						);
				}

				// Translation Picker
				if (bTranslationPicker)
				{
					Section.AddMenuEntry(
						"TranslationPicker",
						LOCTEXT("TranslationPickerMenuItem", "Translation Picker"),
						LOCTEXT("TranslationPickerMenuItemToolTip", "Launch the Translation Picker to Modify Editor Translations"),
						FSlateIcon(),
						FUIAction(FExecuteAction::CreateStatic(&FMainFrameTranslationEditorMenu::HandleOpenTranslationPicker))
						);
				}
			}
		}
	}

	{
		FEditorMenuSection& Section = Menu->AddSection("WindowLayout", NSLOCTEXT("MainAppMenu", "LayoutManagementHeader", "Layout"));
		Section.AddMenuEntry(FMainFrameCommands::Get().ResetLayout);
		Section.AddMenuEntry(FMainFrameCommands::Get().SaveLayout);
#if !PLATFORM_MAC && !PLATFORM_LINUX // On Mac/Linux windowed fullscreen mode in the editor is currently unavailable
		Section.AddMenuEntry(FMainFrameCommands::Get().ToggleFullscreen);
#endif
	}
}

#undef LOCTEXT_NAMESPACE


#define LOCTEXT_NAMESPACE "MainHelpMenu"

void FMainMenu::RegisterHelpMenu()
{
	UEditorMenu* Menu = UEditorMenuSubsystem::Get()->RegisterMenu("MainFrame.MainMenu.Help");
	FEditorMenuSection& BugReportingSection = Menu->AddSection("BugReporting", NSLOCTEXT("MainHelpMenu", "BugsReporting", "Bugs"));
	{
		BugReportingSection.AddMenuEntry(FMainFrameCommands::Get().ReportABug);
		BugReportingSection.AddMenuEntry(FMainFrameCommands::Get().OpenIssueTracker);
	}

	FEditorMenuSection& HelpOnlineSection = Menu->AddSection("HelpOnline", NSLOCTEXT("MainHelpMenu", "Online", "Help Online"));
	{
		HelpOnlineSection.AddMenuEntry(FMainFrameCommands::Get().VisitSupportWebSite);
		HelpOnlineSection.AddMenuEntry(FMainFrameCommands::Get().VisitForums);
		HelpOnlineSection.AddMenuEntry(FMainFrameCommands::Get().VisitSearchForAnswersPage);
		HelpOnlineSection.AddMenuEntry(FMainFrameCommands::Get().VisitWiki);


		const FText SupportWebSiteLabel = NSLOCTEXT("MainHelpMenu", "VisitUnrealEngineSupportWebSite", "Unreal Engine Support Web Site...");

		HelpOnlineSection.AddMenuSeparator("EpicGamesHelp");
		HelpOnlineSection.AddMenuEntry(FMainFrameCommands::Get().VisitEpicGamesDotCom);

		HelpOnlineSection.AddMenuSeparator("Credits");
		HelpOnlineSection.AddMenuEntry(FMainFrameCommands::Get().CreditsUnrealEd);
	}

#if !PLATFORM_MAC // Handled by app's menu in menu bar
	FEditorMenuSection& HelpApplicationSection = Menu->AddSection("HelpApplication", NSLOCTEXT("MainHelpMenu", "Application", "Application"));
	{
		const FText AboutWindowTitle = NSLOCTEXT("MainHelpMenu", "AboutUnrealEditor", "About Unreal Editor...");

		HelpApplicationSection.AddMenuEntry(FMainFrameCommands::Get().AboutUnrealEd, AboutWindowTitle);
	}
#endif
}

#undef LOCTEXT_NAMESPACE

TSharedRef<SWidget> FMainMenu::MakeMainMenu(const TSharedPtr<FTabManager>& TabManager, const FName MenuName, FEditorMenuContext& EditorMenuContext)
{
	// Cache all project names once
	FMainFrameActionCallbacks::CacheProjectNames();

	FMainMenu::RegisterMainMenu();

	EditorMenuContext.AppendCommandList(FMainFrameCommands::ActionList);

	USlateTabManagerContext* ContextObject = NewObject<USlateTabManagerContext>();
	ContextObject->TabManager = TabManager;
	EditorMenuContext.AddObject(ContextObject);

	// Create the menu bar!
	TSharedRef<SWidget> MenuBarWidget = UEditorMenuSubsystem::Get()->GenerateWidget(MenuName, EditorMenuContext);
	if (MenuBarWidget != SNullWidget::NullWidget)
	{
		// Tell tab-manager about the multi-box for platforms with a global menu bar
		TSharedRef<SMultiBoxWidget> MultiBoxWidget = StaticCastSharedRef<SMultiBoxWidget>(MenuBarWidget);
		TabManager->SetMenuMultiBox(ConstCastSharedRef<FMultiBox>(MultiBoxWidget->GetMultiBox()));
	}

	return MenuBarWidget;
}

void FMainMenu::RegisterMainMenu()
{
#define LOCTEXT_NAMESPACE "MainMenu"

	static const FName MainMenuName("MainFrame.MainMenu");
	UEditorMenuSubsystem* EditorMenus = UEditorMenuSubsystem::Get();
	if (EditorMenus->IsMenuRegistered(MainMenuName))
	{
		return;
	}

	RegisterFileMenu();
	RegisterEditMenu();
	RegisterWindowMenu();
	RegisterHelpMenu();

	UEditorMenu* MenuBar = EditorMenus->RegisterMenu(MainMenuName, NAME_None, EMultiBoxType::MenuBar);

	MenuBar->AddSubMenu(
		"MainMenu",
		NAME_None,
		"File",
		LOCTEXT("FileMenu", "File"),
		LOCTEXT("FileMenu_ToolTip", "Open the file menu")
	);

	MenuBar->AddSubMenu(
		"MainMenu",
		NAME_None,
		"Edit",
		LOCTEXT("EditMenu", "Edit"),
		LOCTEXT("EditMenu_ToolTip", "Open the edit menu")
		);

	MenuBar->AddSubMenu(
		"MainMenu",
		NAME_None,
		"Window",
		LOCTEXT("WindowMenu", "Window"),
		LOCTEXT("WindowMenu_ToolTip", "Open new windows or tabs.")
		);

	MenuBar->AddSubMenu(
		"MainMenu",
		NAME_None,
		"Help",
		LOCTEXT("HelpMenu", "Help"),
		LOCTEXT("HelpMenu_ToolTip", "Open the help menu")
	);
}

#undef LOCTEXT_NAMESPACE


#define LOCTEXT_NAMESPACE "MainTabMenu"

void FMainMenu::RegisterFileProjectMenu()
{
	if (!GetDefault<UEditorStyleSettings>()->bShowProjectMenus)
	{
		return;
	}

	UEditorMenuSubsystem* EditorMenus = UEditorMenuSubsystem::Get();
	UEditorMenu* MainTabFileMenu = EditorMenus->ExtendMenu("MainFrame.MainTabMenu.File");
	FEditorMenuSection& Section = MainTabFileMenu->AddSection("FileProject", LOCTEXT("ProjectHeading", "Project"), FEditorMenuInsert("FileLoadAndSave", EEditorMenuInsertType::After));

	Section.AddMenuEntry( FMainFrameCommands::Get().NewProject );
	Section.AddMenuEntry( FMainFrameCommands::Get().OpenProject );

	FText ShortIDEName = FSourceCodeNavigation::GetSelectedSourceCodeIDE();

	Section.AddMenuEntry( FMainFrameCommands::Get().AddCodeToProject,
		TAttribute<FText>(),
		FText::Format(LOCTEXT("AddCodeToProjectTooltip", "Adds C++ code to the project. The code can only be compiled if you have {0} installed."), ShortIDEName)
	);

	Section.AddEntry(FEditorMenuEntry::InitSubMenu(
		"MainFrame.MainTabMenu.File",
		"PackageProject",
		LOCTEXT("PackageProjectSubMenuLabel", "Package Project"),
		LOCTEXT("PackageProjectSubMenuToolTip", "Compile, cook and package your project and its content for distribution."),
		FNewMenuDelegate::CreateStatic( &FPackageProjectMenu::MakeMenu ), false, FSlateIcon(FEditorStyle::GetStyleSetName(), "MainFrame.PackageProject")
	));

	/*
	MenuBuilder.AddMenuEntry( FMainFrameCommands::Get().LocalizeProject,
		NAME_None,
		TAttribute<FText>(),
		LOCTEXT("LocalizeProjectToolTip", "Gather text from your project and import/export translations.")
		);
		*/
	/*
	MenuBuilder.AddSubMenu(
		LOCTEXT("CookProjectSubMenuLabel", "Cook Project"),
		LOCTEXT("CookProjectSubMenuToolTip", "Cook your project content for debugging"),
		FNewMenuDelegate::CreateStatic( &FCookContentMenu::MakeMenu ), false, FSlateIcon()
	);
	*/

	FString SolutionPath;
	if (FSourceCodeNavigation::DoesModuleSolutionExist())
	{
		Section.AddMenuEntry( FMainFrameCommands::Get().RefreshCodeProject,
			FText::Format(LOCTEXT("RefreshCodeProjectLabel", "Refresh {0} Project"), ShortIDEName),
			FText::Format(LOCTEXT("RefreshCodeProjectTooltip", "Refreshes your C++ code project in {0}."), ShortIDEName)
		);

		Section.AddMenuEntry( FMainFrameCommands::Get().OpenIDE,
			FText::Format(LOCTEXT("OpenIDELabel", "Open {0}"), ShortIDEName),
			FText::Format(LOCTEXT("OpenIDETooltip", "Opens your C++ code in {0}."), ShortIDEName)
		);
	}
	else
	{
		Section.AddMenuEntry( FMainFrameCommands::Get().RefreshCodeProject,
			FText::Format(LOCTEXT("GenerateCodeProjectLabel", "Generate {0} Project"), ShortIDEName),
			FText::Format(LOCTEXT("GenerateCodeProjectTooltip", "Generates your C++ code project in {0}."), ShortIDEName)
		);
	}

	Section.AddDynamicEntry("CookContentForPlatform", FNewEditorMenuSectionDelegate::CreateLambda([](FEditorMenuSection& InSection)
	{
		// @hack GDC: this should be moved somewhere else and be less hacky
		ITargetPlatform* RunningTargetPlatform = GetTargetPlatformManager()->GetRunningTargetPlatform();

		if (RunningTargetPlatform != nullptr)
		{
			const FName CookedPlatformName = *(RunningTargetPlatform->PlatformName() + TEXT("NoEditor"));
			const FText CookedPlatformText = FText::FromString(RunningTargetPlatform->PlatformName());

			FUIAction Action(
				FExecuteAction::CreateStatic(&FMainFrameActionCallbacks::CookContent, CookedPlatformName),
				FCanExecuteAction::CreateStatic(&FMainFrameActionCallbacks::CookContentCanExecute, CookedPlatformName)
			);

			InSection.AddMenuEntry(
				"CookContentForPlatform",
				FText::Format(LOCTEXT("CookContentForPlatform", "Cook Content for {0}"), CookedPlatformText),
				FText::Format(LOCTEXT("CookContentForPlatformTooltip", "Cook your game content for debugging on the {0} platform"), CookedPlatformText),
				FSlateIcon(),
				Action
			);
		}
	}));
}

void FMainMenu::RegisterRecentFileAndExitMenuItems()
{
	UEditorMenuSubsystem* EditorMenus = UEditorMenuSubsystem::Get();
	UEditorMenu* MainTabFileMenu = EditorMenus->RegisterMenu("MainFrame.MainTabMenu.File", "MainFrame.MainMenu.File");

	{
		FEditorMenuSection& Section = MainTabFileMenu->AddSection("FileRecentFiles");
		if (GetDefault<UEditorStyleSettings>()->bShowProjectMenus && FMainFrameActionCallbacks::ProjectNames.Num() > 0)
		{
			Section.AddEntry(FEditorMenuEntry::InitSubMenu(
				"MainFrame.MainTabMenu.File",
				"RecentProjects",
				LOCTEXT("SwitchProjectSubMenu", "Recent Projects"),
				LOCTEXT("SwitchProjectSubMenu_ToolTip", "Select a project to switch to"),
				FNewEditorMenuDelegate::CreateStatic(&FRecentProjectsMenu::MakeMenu),
				false,
				FSlateIcon(FEditorStyle::GetStyleSetName(), "MainFrame.RecentProjects")
			));
		}
	}

#if !PLATFORM_MAC // Handled by app's menu in menu bar
	{
		FEditorMenuSection& Section = MainTabFileMenu->AddSection("Exit");
		Section.AddMenuEntry( FMainFrameCommands::Get().Exit );
	}
#endif
}

TSharedRef< SWidget > FMainMenu::MakeMainTabMenu( const TSharedPtr<FTabManager>& TabManager, const FName MenuName, FEditorMenuContext& EditorMenuContext )
{
	if (GetDefault<UEditorStyleSettings>()->bShowProjectMenus)
	{
		IMainFrameModule& MainFrameModule = FModuleManager::GetModuleChecked<IMainFrameModule>("MainFrame");
		EditorMenuContext.AppendCommandList(MainFrameModule.GetMainFrameCommandBindings());
	}


	TSharedRef< SWidget > MenuBarWidget = FMainMenu::MakeMainMenu( TabManager, MenuName, EditorMenuContext );

	return MenuBarWidget;
}


#undef LOCTEXT_NAMESPACE
