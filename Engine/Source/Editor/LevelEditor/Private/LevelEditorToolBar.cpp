// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LevelEditorToolBar.h"
#include "Misc/MessageDialog.h"
#include "HAL/FileManager.h"
#include "Modules/ModuleManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Framework/MultiBox/MultiBoxDefs.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SSlider.h"
#include "EditorStyleSet.h"
#include "ISourceControlOperation.h"
#include "SourceControlOperations.h"
#include "ISourceControlProvider.h"
#include "ISourceControlModule.h"
#include "Settings/EditorExperimentalSettings.h"
#include "GameMapsSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/TextureStreamingTypes.h"

#include "LevelEditor.h"
#include "LevelEditorActions.h"
#include "SourceCodeNavigation.h"
#include "Kismet2/DebuggerCommands.h"
#include "SceneOutlinerPublicTypes.h"
#include "SceneOutlinerModule.h"
#include "SScalabilitySettings.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "Matinee/MatineeActor.h"
#include "LevelSequenceActor.h"
#include "Engine/LevelScriptBlueprint.h"
#include "ISettingsCategory.h"
#include "ISettingsContainer.h"
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "ClassViewerModule.h"
#include "ClassViewerFilter.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Widgets/Input/SVolumeControl.h"
#include "Features/IModularFeatures.h"
#include "EngineUtils.h"
#include "ScopedTransaction.h"
#include "Features/EditorFeatures.h"
#include "Misc/ConfigCacheIni.h"
#include "ILauncherPlatform.h"
#include "LauncherPlatformModule.h"
#include "Misc/ScopedSlowTask.h"
#include "MaterialShaderQualitySettings.h"
#include "RHIShaderPlatformDefinitions.inl"
#include "LevelEditorMenuContext.h"
#include "ToolMenus.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "LevelEditorModesActions.h"

static TAutoConsoleVariable<int32> CVarAllowMatineeActors(
	TEXT("Matinee.AllowMatineeActors"),
	0,
	TEXT("Toggles whether matinee actors should appear in the cinematics menu so that they can be edited."));

namespace LevelEditorActionHelpers
{
	/** Filters out any classes for the Class Picker when creating or selecting classes in the Blueprints dropdown */
	class FBlueprintParentFilter_MapModeSettings : public IClassViewerFilter
	{
	public:
		/** Classes to not allow any children of into the Class Viewer/Picker. */
		TSet< const UClass* > AllowedChildrenOfClasses;

		virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs ) override
		{
			return InFilterFuncs->IfInChildOfClassesSet(AllowedChildrenOfClasses, InClass) == EFilterReturn::Passed;
		}

		virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef< const IUnloadedBlueprintData > InUnloadedClassData, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
		{
			return InFilterFuncs->IfInChildOfClassesSet(AllowedChildrenOfClasses, InUnloadedClassData) == EFilterReturn::Passed;
		}
	};

	/**
	 * Retrieves the GameMode class
	 *
	 * @param InLevelEditor				The editor to extract the world from
	 * @param bInIsProjectSettings		TRUE if retrieving the game mode from the project settings
	 * @return							The GameMode class in the Project Settings or World Settings
	 */
	static UClass* GetGameModeClass(TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings);

	/** Callback for the label to display for the GameMode menu selection */
	static FText GetOpenGameModeBlueprintLabel(TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings);

	/** Callback when selecting a GameMode class, assigns it to the world */
	static void OnSelectGameModeClassPicked(UClass* InChosenClass, TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings);

	/** Callback when creating a new GameMode class, creates the Blueprint and assigns it to the world */
	static void OnCreateGameModeClassPicked(UClass* InChosenClass, TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings);

	/**
	 * Retrieves the active GameState class from
	 *
	 * @param InLevelEditor		The editor to extract the world from
	 * @param bInIsProjectSettings		TRUE if retrieving the game mode from the project settings
	 * @return					The active GameState class in the World Settings
	 */
	static UClass* GetGameStateClass(TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings);

	/** Callback for the label to display for the GameState menu selection */
	static FText GetOpenGameStateBlueprintLabel(TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings);
	
	/** Callback when selecting a GameState class, assigns it to the world */
	static void OnSelectGameStateClassPicked(UClass* InChosenClass, TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings);

	/** Callback when creating a new GameState class, creates the Blueprint and assigns it to the world */
	static void OnCreateGameStateClassPicked(UClass* InChosenClass, TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings);

	/**
	 * Retrieves the active Pawn class from
	 *
	 * @param InLevelEditor		The editor to extract the world from
	 * @return					The active Pawn class in the World Settings
	 */
	static UClass* GetPawnClass(TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings);

	/** Callback for the label to display for the Pawn menu selection */
	static FText GetOpenPawnBlueprintLabel(TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings);

	/** Callback for the tooltip to display for the Pawn menu selection */
	static FText GetOpenPawnBlueprintTooltip(TWeakPtr< SLevelEditor > InLevelEditor);

	/** Callback when selecting a Pawn class, assigns it to the world */
	static void OnSelectPawnClassPicked(UClass* InChosenClass, TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings);

	/** Callback when creating a new Pawn class, creates the Blueprint and assigns it to the world */
	static void OnCreatePawnClassPicked(UClass* InChosenClass, TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings);

	/**
	 * Retrieves the active HUD class from
	 *
	 * @param InLevelEditor		The editor to extract the world from
	 * @param bInIsProjectSettings		TRUE if retrieving the game mode from the project settings
	 * @return					The active HUD class in the World Settings
	 */
	static UClass* GetHUDClass(TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings);

	/** Callback for the label to display for the HUD menu selection */
	static FText GetOpenHUDBlueprintLabel(TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings);
	
	/** Callback when selecting a HUD class, assigns it to the world */
	static void OnSelectHUDClassPicked(UClass* InChosenClass, TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings);

	/** Callback when creating a new HUD class, creates the Blueprint and assigns it to the world */
	static void OnCreateHUDClassPicked(UClass* InChosenClass, TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings);

	/**
	 * Retrieves the active PlayerController class from
	 *
	 * @param InLevelEditor		The editor to extract the world from
	 * @param bInIsProjectSettings		TRUE if retrieving the game mode from the project settings
	 * @return					The active PlayerController class in the World Settings
	 */
	static UClass* GetPlayerControllerClass(TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings);

	/** Callback for the label to display for the PlayerController menu selection */
	static FText GetOpenPlayerControllerBlueprintLabel(TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings);

	/** Callback when selecting a PlayerController class, assigns it to the world */
	static void OnSelectPlayerControllerClassPicked(UClass* InChosenClass, TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings);

	/** Callback when creating a new PlayerController class, creates the Blueprint and assigns it to the world */
	static void OnCreatePlayerControllerClassPicked(UClass* InChosenClass, TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings);

	/** Opens a native class's header file if the compiler is available. */
	static void OpenNativeClass(UClass* InClass)
	{
		if(InClass->HasAllClassFlags(CLASS_Native) && FSourceCodeNavigation::IsCompilerAvailable())
		{
			FString NativeParentClassHeaderPath;
			const bool bFileFound = FSourceCodeNavigation::FindClassHeaderPath(InClass, NativeParentClassHeaderPath) 
				&& (IFileManager::Get().FileSize(*NativeParentClassHeaderPath) != INDEX_NONE);
			if (bFileFound)
			{
				const FString AbsoluteHeaderPath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*NativeParentClassHeaderPath);
				FSourceCodeNavigation::OpenSourceFile( AbsoluteHeaderPath );
			}
		}
	}

	/** Open the game mode blueprint, in the project settings or world settings */
	static void OpenGameModeBlueprint( TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings )
	{
		if(UClass* GameModeClass = GetGameModeClass(InLevelEditor, bInIsProjectSettings))
		{
			if(UBlueprint* BlueprintClass = Cast<UBlueprint>(GameModeClass->ClassGeneratedBy))
			{
				// @todo Re-enable once world centric works
				const bool bOpenWorldCentric = false;
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(
					BlueprintClass,
					bOpenWorldCentric ? EToolkitMode::WorldCentric : EToolkitMode::Standalone,
					InLevelEditor.Pin() );
			}
			else
			{
				OpenNativeClass(GameModeClass);
			}
		}
	}

	/** Open the game state blueprint, in the project settings or world settings */
	static void OpenGameStateBlueprint( TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings )
	{
		if(UClass* GameStateClass = GetGameStateClass(InLevelEditor, bInIsProjectSettings))
		{
			if(UBlueprint* BlueprintClass = Cast<UBlueprint>(GameStateClass->ClassGeneratedBy))
			{
				// @todo Re-enable once world centric works
				const bool bOpenWorldCentric = false;
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(
					BlueprintClass,
					bOpenWorldCentric ? EToolkitMode::WorldCentric : EToolkitMode::Standalone,
					InLevelEditor.Pin() );
			}
			else
			{
				OpenNativeClass(GameStateClass);
			}
		}
	}

	/** Open the default pawn blueprint, in the project settings or world settings */
	static void OpenDefaultPawnBlueprint( TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings )
	{
		if(UClass* DefaultPawnClass = GetPawnClass(InLevelEditor, bInIsProjectSettings))
		{
			if(UBlueprint* BlueprintClass = Cast<UBlueprint>(DefaultPawnClass->ClassGeneratedBy))
			{
				// @todo Re-enable once world centric works
				const bool bOpenWorldCentric = false;
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(
					BlueprintClass,
					bOpenWorldCentric ? EToolkitMode::WorldCentric : EToolkitMode::Standalone,
					InLevelEditor.Pin() );
			}
			else
			{
				OpenNativeClass(DefaultPawnClass);
			}
		}
	}

	/** Open the HUD blueprint, in the project settings or world settings */
	static void OpenHUDBlueprint( TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings )
	{
		if(UClass* DefaultHUDClass = GetHUDClass(InLevelEditor, bInIsProjectSettings))
		{
			if(UBlueprint* BlueprintClass = Cast<UBlueprint>(DefaultHUDClass->ClassGeneratedBy))
			{
				// @todo Re-enable once world centric works
				const bool bOpenWorldCentric = false;
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(
					BlueprintClass,
					bOpenWorldCentric ? EToolkitMode::WorldCentric : EToolkitMode::Standalone,
					InLevelEditor.Pin() );
			}
			else
			{
				OpenNativeClass(DefaultHUDClass);
			}
		}
	}

	/** Open the player controller blueprint, in the project settings or world settings */
	static void OpenPlayerControllerBlueprint( TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings )
	{
		if(UClass* PlayerControllerClass = GetPlayerControllerClass(InLevelEditor, bInIsProjectSettings))
		{
			if(UBlueprint* BlueprintClass = Cast<UBlueprint>(PlayerControllerClass->ClassGeneratedBy))
			{
				// @todo Re-enable once world centric works
				const bool bOpenWorldCentric = false;
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(
					BlueprintClass,
					bOpenWorldCentric ? EToolkitMode::WorldCentric : EToolkitMode::Standalone,
					InLevelEditor.Pin() );
			}
			else
			{
				OpenNativeClass(PlayerControllerClass);
			}
		}
	}

	/**
	 * Builds a sub-menu for selecting a class
	 *
	 * @param InMenu		Object to append menu items/widgets to
	 * @param InRootClass		The root class to filter the Class Viewer by to only show children of
	 * @param InOnClassPicked	Callback delegate to fire when a class is picked
	 */
	void GetSelectSettingsClassSubMenu(UToolMenu* InMenu, UClass* InRootClass, FOnClassPicked InOnClassPicked)
	{
		FClassViewerInitializationOptions Options;
		Options.Mode = EClassViewerMode::ClassPicker;
		Options.DisplayMode = EClassViewerDisplayMode::ListView;
		Options.bShowObjectRootClass = true;
		Options.bShowNoneOption = true;

		// Only want blueprint actor base classes.
		Options.bIsBlueprintBaseOnly = true;

		// This will allow unloaded blueprints to be shown.
		Options.bShowUnloadedBlueprints = true;

		TSharedPtr< FBlueprintParentFilter_MapModeSettings > Filter = MakeShareable(new FBlueprintParentFilter_MapModeSettings);
		Filter->AllowedChildrenOfClasses.Add(InRootClass);
		Options.ClassFilter = Filter;

		FText RootClassName = FText::FromString(InRootClass->GetName());
		TSharedRef<SWidget> ClassViewer = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer").CreateClassViewer(Options, InOnClassPicked);
		FFormatNamedArguments FormatArgs;
		FormatArgs.Add(TEXT("RootClass"), RootClassName);
		FToolMenuSection& Section = InMenu->AddSection("SelectSettingsClass", FText::Format(NSLOCTEXT("LevelToolBarViewMenu", "SelectGameModeLabel", "Select {RootClass} class"), FormatArgs));
		Section.AddEntry(FToolMenuEntry::InitWidget("ClassViewer", ClassViewer, FText::GetEmpty(), true));
	}

	/**
	 * Builds a sub-menu for creating a class
	 *
	 * @param InMenu		Object to append menu items/widgets to
	 * @param InRootClass		The root class to filter the Class Viewer by to only show children of
	 * @param InOnClassPicked	Callback delegate to fire when a class is picked
	 */
	void GetCreateSettingsClassSubMenu(UToolMenu* InMenu, UClass* InRootClass, FOnClassPicked InOnClassPicked)
	{
		FClassViewerInitializationOptions Options;
		Options.Mode = EClassViewerMode::ClassPicker;
		Options.DisplayMode = EClassViewerDisplayMode::ListView;
		Options.bShowObjectRootClass = true;

		// Only want blueprint actor base classes.
		Options.bIsBlueprintBaseOnly = true;

		// This will allow unloaded blueprints to be shown.
		Options.bShowUnloadedBlueprints = true;

		TSharedPtr< FBlueprintParentFilter_MapModeSettings > Filter = MakeShareable(new FBlueprintParentFilter_MapModeSettings);
		Filter->AllowedChildrenOfClasses.Add(InRootClass);
		Options.ClassFilter = Filter;

		FText RootClassName = FText::FromString(InRootClass->GetName());
		TSharedRef<SWidget> ClassViewer = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer").CreateClassViewer(Options, InOnClassPicked);
		FFormatNamedArguments FormatArgs;
		FormatArgs.Add(TEXT("RootClass"), RootClassName);
		FToolMenuSection& Section = InMenu->AddSection("CreateSettingsClass", FText::Format(NSLOCTEXT("LevelToolBarViewMenu", "CreateGameModeLabel", "Select {RootClass} parent class"), FormatArgs));
		Section.AddEntry(FToolMenuEntry::InitWidget("ClassViewer", ClassViewer, FText::GetEmpty(), true));
	}

	/** Helper struct for passing all required data to the GetBlueprintSettingsSubMenu function */
	struct FBlueprintMenuSettings
	{
		/** The UI command for editing the Blueprint class associated with the menu */
		FUIAction EditCommand;

		/** Current class associated with the menu */
		UClass* CurrentClass;

		/** Root class that defines what class children can be set through the menu */
		UClass* RootClass;

		/** Callback when a class is picked, to assign the new class */
		FOnClassPicked OnSelectClassPicked;

		/** Callback when a class is picked, to create a new child class of and assign */
		FOnClassPicked OnCreateClassPicked;

		/** Level Editor these menu settings are for */
		TWeakPtr< SLevelEditor > LevelEditor;

		/** TRUE if these represent Project Settings, FALSE if they represent World Settings */
		bool bIsProjectSettings;
	};

	/** Returns the label of the "Check Out" option based on if source control is present or not */
	FText GetCheckOutLabel()
	{
#define LOCTEXT_NAMESPACE "LevelToolBarViewMenu"
		if(ISourceControlModule::Get().IsEnabled())
		{
			return LOCTEXT("CheckoutMenuLabel", "Check Out");
		}
		else
		{
			return LOCTEXT("MakeWritableLabel", "Make Writable");
		}
#undef LOCTEXT_NAMESPACE
	}

	/** Returns the tooltip of the "Check Out" option based on if source control is present or not */
	FText GetCheckOutTooltip()
	{
#define LOCTEXT_NAMESPACE "LevelToolBarViewMenu"
		if(ISourceControlModule::Get().IsEnabled())
		{
			return LOCTEXT("CheckoutMenuTooltip", "Checks out the project settings config file so the game mode can be set.");
		}
		else
		{
			return LOCTEXT("MakeWritableTooltip", "Forces the project settings config file to be writable so the game mode can be set.");
		}
#undef LOCTEXT_NAMESPACE
	}

	/**
	 * A sub-menu for the Blueprints dropdown, facilitates all the sub-menu actions such as creating, editing, and selecting classes for the world settings game mode.
	 *
	 * @param InMenu		Object to append menu items/widgets to
	 * @param InCommandList		Commandlist for menu items
	 * @param InSettingsData	All the data needed to create the menu actions
	 */
	void GetBlueprintSettingsSubMenu(UToolMenu* InMenu, FBlueprintMenuSettings InSettingsData);

	/** Returns TRUE if the class can be edited, always TRUE for Blueprints and for native classes a compiler must be present */
	bool CanEditClass(UClass* InClass)
	{
		// For native classes, we can only edit them if a compiler is available
		if(InClass && InClass->HasAllClassFlags(CLASS_Native))
		{
			return FSourceCodeNavigation::IsCompilerAvailable();
		}
		return true;
	}

	/** Returns TRUE if the GameMode's sub-class can be created or selected */
	bool CanCreateSelectSubClass(UClass* InGameModeClass, bool bInIsProjectSettings)
	{
		// Can never create or select project settings sub-classes if the config file is not checked out
		if(bInIsProjectSettings && !FLevelEditorActionCallbacks::CanSelectGameModeBlueprint())
		{
			return false;
		}

		// If the game mode class is native, we cannot set the sub class
		if(!InGameModeClass || InGameModeClass->HasAllClassFlags(CLASS_Native))
		{
			return false;
		}
		return true;
	}

	/** Creates a tooltip for a submenu */
	FText GetSubMenuTooltip(UClass* InClass, UClass* InRootClass, bool bInIsProjectSettings)
	{
#define LOCTEXT_NAMESPACE "LevelToolBarViewMenu"
		FFormatNamedArguments Args;
		Args.Add(TEXT("Class"), FText::FromString(InRootClass->GetName()));
		Args.Add(TEXT("TargetLocation"), bInIsProjectSettings? LOCTEXT("Project", "project") : LOCTEXT("World", "world"));
		return FText::Format(LOCTEXT("ClassSubmenu_Tooltip", "Select, edit, or create a new {Class} blueprint for the {TargetLocation}"), Args);
#undef LOCTEXT_NAMESPACE
	}

	/** Creates a tooltip for the create class submenu */
	FText GetCreateMenuTooltip(UClass* InGameModeClass, UClass* InRootClass, bool bInIsProjectSettings)
	{
#define LOCTEXT_NAMESPACE "LevelToolBarViewMenu"
		FText ResultText;

		// Game modes can always be created and selected (providing the config is checked out, handled separately)
		if(InRootClass != AGameModeBase::StaticClass() && InGameModeClass->HasAllClassFlags(CLASS_Native))
		{
			ResultText = LOCTEXT("CannotCreateClasses", "Cannot create classes when the game mode is a native class!");
		}
		else if(bInIsProjectSettings && !FLevelEditorActionCallbacks::CanSelectGameModeBlueprint())
		{
			ResultText = LOCTEXT("CannotCreateClasses_NeedsCheckOut", "Cannot create classes when the config file is not writable!");
		}
		else
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("RootClass"), FText::FromString(InRootClass->GetName()));
			Args.Add(TEXT("TargetLocation"), bInIsProjectSettings? LOCTEXT("Project", "project") : LOCTEXT("World", "world"));
			ResultText = FText::Format( LOCTEXT("CreateClass_Tooltip", "Create a new {RootClass} based on a selected class and auto-assign it to the {TargetLocation}"), Args );
		}

		return ResultText;
#undef LOCTEXT_NAMESPACE
	}

	/** Creates a tooltip for the select class submenu */
	FText GetSelectMenuTooltip(UClass* InGameModeClass, UClass* InRootClass, bool bInIsProjectSettings)
	{
#define LOCTEXT_NAMESPACE "LevelToolBarViewMenu"
		FText ResultText;

		// Game modes can always be created and selected (providing the config is checked out, handled separately)
		if(InRootClass != AGameModeBase::StaticClass() && InGameModeClass->HasAllClassFlags(CLASS_Native))
		{
			ResultText = LOCTEXT("CannotSelectClasses", "Cannot select classes when the game mode is a native class!");
		}
		else if(bInIsProjectSettings && !FLevelEditorActionCallbacks::CanSelectGameModeBlueprint())
		{
			ResultText = LOCTEXT("CannotSelectClasses_NeedsCheckOut", "Cannot select classes when the config file is not writable!");
		}
		else
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("RootClass"), FText::FromString(InRootClass->GetName()));
			Args.Add(TEXT("TargetLocation"), bInIsProjectSettings? LOCTEXT("Project", "project") : LOCTEXT("World", "world"));
			ResultText = FText::Format( LOCTEXT("SelectClass_Tooltip", "Select a new {RootClass} based on a selected class and auto-assign it to the {TargetLocation}"), Args );
		}
		return ResultText;
#undef LOCTEXT_NAMESPACE
	}

	void CreateGameModeSubMenu(FToolMenuSection& Section, const FName InName, bool bInProjectSettings)
	{
#define LOCTEXT_NAMESPACE "LevelToolBarViewMenu"
		Section.AddDynamicEntry(InName, FNewToolMenuSectionDelegate::CreateLambda([=](FToolMenuSection& InSection)
		{
			ULevelEditorMenuContext* Context = InSection.FindContext<ULevelEditorMenuContext>();
			if (Context && Context->LevelEditor.IsValid())
			{
				LevelEditorActionHelpers::FBlueprintMenuSettings GameModeMenuSettings;
				GameModeMenuSettings.EditCommand =
					FUIAction(
						FExecuteAction::CreateStatic< TWeakPtr< SLevelEditor > >(&OpenGameModeBlueprint, Context->LevelEditor, bInProjectSettings)
					);
				GameModeMenuSettings.OnCreateClassPicked = FOnClassPicked::CreateStatic(&LevelEditorActionHelpers::OnCreateGameModeClassPicked, Context->LevelEditor, bInProjectSettings);
				GameModeMenuSettings.OnSelectClassPicked = FOnClassPicked::CreateStatic(&LevelEditorActionHelpers::OnSelectGameModeClassPicked, Context->LevelEditor, bInProjectSettings);
				GameModeMenuSettings.CurrentClass = LevelEditorActionHelpers::GetGameModeClass(Context->LevelEditor, bInProjectSettings);
				GameModeMenuSettings.RootClass = AGameModeBase::StaticClass();
				GameModeMenuSettings.LevelEditor = Context->LevelEditor;
				GameModeMenuSettings.bIsProjectSettings = bInProjectSettings;

				auto IsGameModeActive = [](TWeakPtr< SLevelEditor > InLevelEditorPtr, bool bInProjSettings)->bool
				{
					UClass* WorldSettingsGameMode = LevelEditorActionHelpers::GetGameModeClass(InLevelEditorPtr, false);
					if ((WorldSettingsGameMode == nullptr) ^ bInProjSettings) //(WorldSettingsGameMode && !bInProjectSettings) || (!WorldSettingsGameMode && bInProjectSettings) )
					{
						return false;
					}
					return true;
				};

				InSection.AddSubMenu(InName, LevelEditorActionHelpers::GetOpenGameModeBlueprintLabel(Context->LevelEditor, bInProjectSettings),
					GetSubMenuTooltip(GameModeMenuSettings.CurrentClass, GameModeMenuSettings.RootClass, bInProjectSettings),
					FNewToolMenuDelegate::CreateStatic(&LevelEditorActionHelpers::GetBlueprintSettingsSubMenu, GameModeMenuSettings),
					FUIAction(FExecuteAction(), FCanExecuteAction(), FIsActionChecked::CreateStatic(IsGameModeActive, Context->LevelEditor, bInProjectSettings)),
					EUserInterfaceActionType::RadioButton);
			}
		}));
#undef LOCTEXT_NAMESPACE
	}

	/**
	 * Builds the game mode's sub menu objects
	 *
	 * @param InSection			Object to append menu items/widgets to
	 * @param InCommandList		Commandlist for menu items
	 * @param InSettingsData	All the data needed to create the menu actions
	 */
	void GetGameModeSubMenu(FToolMenuSection& InSection, const FBlueprintMenuSettings& InSettingsData)
	{
#define LOCTEXT_NAMESPACE "LevelToolBarViewMenu"
		// Game State
		LevelEditorActionHelpers::FBlueprintMenuSettings GameStateMenuSettings;
		GameStateMenuSettings.EditCommand = 
			FUIAction(
				FExecuteAction::CreateStatic< TWeakPtr< SLevelEditor > >( &OpenGameStateBlueprint, InSettingsData.LevelEditor, InSettingsData.bIsProjectSettings )
			);
		GameStateMenuSettings.OnCreateClassPicked = FOnClassPicked::CreateStatic( &LevelEditorActionHelpers::OnCreateGameStateClassPicked, InSettingsData.LevelEditor, InSettingsData.bIsProjectSettings );
		GameStateMenuSettings.OnSelectClassPicked = FOnClassPicked::CreateStatic( &LevelEditorActionHelpers::OnSelectGameStateClassPicked, InSettingsData.LevelEditor, InSettingsData.bIsProjectSettings );
		GameStateMenuSettings.CurrentClass = LevelEditorActionHelpers::GetGameStateClass(InSettingsData.LevelEditor, InSettingsData.bIsProjectSettings);
		GameStateMenuSettings.RootClass = AGameStateBase::StaticClass();
		GameStateMenuSettings.LevelEditor = InSettingsData.LevelEditor;
		GameStateMenuSettings.bIsProjectSettings = InSettingsData.bIsProjectSettings;

		InSection.AddSubMenu("OpenGameStateBlueprint", LevelEditorActionHelpers::GetOpenGameStateBlueprintLabel(InSettingsData.LevelEditor, InSettingsData.bIsProjectSettings),
			GetSubMenuTooltip(GameStateMenuSettings.CurrentClass, GameStateMenuSettings.RootClass, InSettingsData.bIsProjectSettings),
			FNewToolMenuDelegate::CreateStatic( &LevelEditorActionHelpers::GetBlueprintSettingsSubMenu, GameStateMenuSettings )
		);

		// Pawn
		LevelEditorActionHelpers::FBlueprintMenuSettings PawnMenuSettings;
		PawnMenuSettings.EditCommand = 
			FUIAction(
				FExecuteAction::CreateStatic< TWeakPtr< SLevelEditor > >( &OpenDefaultPawnBlueprint, InSettingsData.LevelEditor, InSettingsData.bIsProjectSettings )
			);
		PawnMenuSettings.OnCreateClassPicked = FOnClassPicked::CreateStatic( &LevelEditorActionHelpers::OnCreatePawnClassPicked, InSettingsData.LevelEditor, InSettingsData.bIsProjectSettings );
		PawnMenuSettings.OnSelectClassPicked = FOnClassPicked::CreateStatic( &LevelEditorActionHelpers::OnSelectPawnClassPicked, InSettingsData.LevelEditor, InSettingsData.bIsProjectSettings );
		PawnMenuSettings.CurrentClass = LevelEditorActionHelpers::GetPawnClass(InSettingsData.LevelEditor, InSettingsData.bIsProjectSettings);
		PawnMenuSettings.RootClass = APawn::StaticClass();
		PawnMenuSettings.LevelEditor = InSettingsData.LevelEditor;
		PawnMenuSettings.bIsProjectSettings = InSettingsData.bIsProjectSettings;

		InSection.AddSubMenu("OpenPawnBlueprint", LevelEditorActionHelpers::GetOpenPawnBlueprintLabel(InSettingsData.LevelEditor, InSettingsData.bIsProjectSettings),
			GetSubMenuTooltip(PawnMenuSettings.CurrentClass, PawnMenuSettings.RootClass, InSettingsData.bIsProjectSettings),
			FNewToolMenuDelegate::CreateStatic( &LevelEditorActionHelpers::GetBlueprintSettingsSubMenu, PawnMenuSettings )
		);

		// HUD
		LevelEditorActionHelpers::FBlueprintMenuSettings HUDMenuSettings;
		HUDMenuSettings.EditCommand = 
			FUIAction(
				FExecuteAction::CreateStatic< TWeakPtr< SLevelEditor > >( &OpenHUDBlueprint, InSettingsData.LevelEditor, InSettingsData.bIsProjectSettings )
			);
		HUDMenuSettings.OnCreateClassPicked = FOnClassPicked::CreateStatic( &LevelEditorActionHelpers::OnCreateHUDClassPicked, InSettingsData.LevelEditor, InSettingsData.bIsProjectSettings );
		HUDMenuSettings.OnSelectClassPicked = FOnClassPicked::CreateStatic( &LevelEditorActionHelpers::OnSelectHUDClassPicked, InSettingsData.LevelEditor, InSettingsData.bIsProjectSettings );
		HUDMenuSettings.CurrentClass = LevelEditorActionHelpers::GetHUDClass(InSettingsData.LevelEditor, InSettingsData.bIsProjectSettings);
		HUDMenuSettings.RootClass = AHUD::StaticClass();
		HUDMenuSettings.LevelEditor = InSettingsData.LevelEditor;
		HUDMenuSettings.bIsProjectSettings = InSettingsData.bIsProjectSettings;

		InSection.AddSubMenu("OpenHUDBlueprint", LevelEditorActionHelpers::GetOpenHUDBlueprintLabel(InSettingsData.LevelEditor, InSettingsData.bIsProjectSettings),
			GetSubMenuTooltip(HUDMenuSettings.CurrentClass, HUDMenuSettings.RootClass, InSettingsData.bIsProjectSettings),
			FNewToolMenuDelegate::CreateStatic( &LevelEditorActionHelpers::GetBlueprintSettingsSubMenu, HUDMenuSettings )
		);

		// Player Controller
		LevelEditorActionHelpers::FBlueprintMenuSettings PlayerControllerMenuSettings;
		PlayerControllerMenuSettings.EditCommand = 
			FUIAction(
				FExecuteAction::CreateStatic< TWeakPtr< SLevelEditor > >( &OpenPlayerControllerBlueprint, InSettingsData.LevelEditor, InSettingsData.bIsProjectSettings )
			);
		PlayerControllerMenuSettings.OnCreateClassPicked = FOnClassPicked::CreateStatic( &LevelEditorActionHelpers::OnCreatePlayerControllerClassPicked, InSettingsData.LevelEditor, InSettingsData.bIsProjectSettings );
		PlayerControllerMenuSettings.OnSelectClassPicked = FOnClassPicked::CreateStatic( &LevelEditorActionHelpers::OnSelectPlayerControllerClassPicked, InSettingsData.LevelEditor, InSettingsData.bIsProjectSettings );
		PlayerControllerMenuSettings.CurrentClass = LevelEditorActionHelpers::GetPlayerControllerClass(InSettingsData.LevelEditor, InSettingsData.bIsProjectSettings);
		PlayerControllerMenuSettings.RootClass = APlayerController::StaticClass();
		PlayerControllerMenuSettings.LevelEditor = InSettingsData.LevelEditor;
		PlayerControllerMenuSettings.bIsProjectSettings = InSettingsData.bIsProjectSettings;

		InSection.AddSubMenu("OpenPlayerControllerBlueprint", LevelEditorActionHelpers::GetOpenPlayerControllerBlueprintLabel(InSettingsData.LevelEditor, InSettingsData.bIsProjectSettings),
			GetSubMenuTooltip(PlayerControllerMenuSettings.CurrentClass, PlayerControllerMenuSettings.RootClass, InSettingsData.bIsProjectSettings),
			FNewToolMenuDelegate::CreateStatic( &LevelEditorActionHelpers::GetBlueprintSettingsSubMenu, PlayerControllerMenuSettings )
		);
#undef LOCTEXT_NAMESPACE
	}

	struct FLevelSortByName
	{
		bool operator ()(const ULevel* LHS, const ULevel* RHS) const
		{
			if (LHS != NULL && LHS->GetOutermost() != NULL && RHS != NULL && RHS->GetOutermost() != NULL)
			{
				return FPaths::GetCleanFilename(LHS->GetOutermost()->GetName()) < FPaths::GetCleanFilename(RHS->GetOutermost()->GetName());
			}
			else
			{
				return false;
			}
		}
	};
}

void LevelEditorActionHelpers::GetBlueprintSettingsSubMenu(UToolMenu* Menu, FBlueprintMenuSettings InSettingsData)
{
#define LOCTEXT_NAMESPACE "LevelToolBarViewMenu"

	FSlateIcon EditBPIcon(FEditorStyle::Get().GetStyleSetName(), TEXT("PropertyWindow.Button_Edit"));
	FSlateIcon NewBPIcon(FEditorStyle::Get().GetStyleSetName(), TEXT("PropertyWindow.Button_AddToArray"));
	FText RootClassName = FText::FromString(InSettingsData.RootClass->GetName());

	// If there is currently a valid GameMode Blueprint, offer to edit the Blueprint
	if(InSettingsData.CurrentClass)
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("RootClass"), RootClassName);
		Args.Add(TEXT("TargetLocation"), InSettingsData.bIsProjectSettings? LOCTEXT("Project", "project") : LOCTEXT("World", "world"));

		FToolMenuSection& Section = Menu->AddSection("EditBlueprintOrClass");
		if(InSettingsData.CurrentClass->ClassGeneratedBy)
		{
			FText BlueprintName = FText::FromString(InSettingsData.CurrentClass->ClassGeneratedBy->GetName());
			Args.Add(TEXT("Blueprint"), BlueprintName);
			Section.AddMenuEntry("EditBlueprint", FText::Format( LOCTEXT("EditBlueprint", "Edit {Blueprint}"), Args), FText::Format( LOCTEXT("EditBlueprint_Tooltip", "Open the {TargetLocation}'s assigned {RootClass} blueprint"), Args), EditBPIcon, InSettingsData.EditCommand );
		}
		else
		{
			FText ClassName = FText::FromString(InSettingsData.CurrentClass->GetName());
			Args.Add(TEXT("Class"), ClassName);

			FText MenuDescription = FText::Format( LOCTEXT("EditNativeClass", "Edit {Class}.h"), Args);
			if(FSourceCodeNavigation::IsCompilerAvailable())
			{
				Section.AddMenuEntry("EditNativeClass", MenuDescription, FText::Format( LOCTEXT("EditNativeClass_Tooltip", "Open the {TargetLocation}'s assigned {RootClass} header"), Args), EditBPIcon, InSettingsData.EditCommand );
			}
			else
			{
				auto CannotEditClass = []() -> bool
				{
					return false;
				};

				// There is no compiler present, this is always disabled with a tooltip to explain why
				Section.AddMenuEntry("EditNativeClass", MenuDescription, FText::Format( LOCTEXT("CannotEditNativeClass_Tooltip", "Cannot edit the {TargetLocation}'s assigned {RootClass} header because no compiler is present!"), Args), EditBPIcon, FUIAction(FExecuteAction(), FCanExecuteAction::CreateStatic(CannotEditClass)) );
			}
		}
	}

	if(InSettingsData.bIsProjectSettings && InSettingsData.CurrentClass && InSettingsData.CurrentClass->IsChildOf(AGameModeBase::StaticClass()) && !FLevelEditorActionCallbacks::CanSelectGameModeBlueprint())
	{
		FToolMenuSection& Section = Menu->AddSection("CheckoutSection", LOCTEXT("CheckoutSection", "Check Out Project Settings") );
		TAttribute<FText> CheckOutLabel;
		CheckOutLabel.BindStatic(&GetCheckOutLabel);

		TAttribute<FText> CheckOutTooltip;
		CheckOutTooltip.BindStatic(&GetCheckOutTooltip);
		Section.AddMenuEntry(FLevelEditorCommands::Get().CheckOutProjectSettingsConfig, CheckOutLabel, CheckOutTooltip, FSlateIcon(FEditorStyle::Get().GetStyleSetName(), TEXT("Icons.Error")));
	}

	auto CannotCreateSelectNativeProjectGameMode = [](bool bInIsProjectSettings) -> bool
	{
		// For the project settings, we can only create/select the game mode class if the config is writable
		if(bInIsProjectSettings)
		{
			return FLevelEditorActionCallbacks::CanSelectGameModeBlueprint();
		}
		return true;
	};

	FToolMenuSection& Section = Menu->AddSection("CreateBlueprint");

	// Create a new GameMode, this is always available so the user can easily create a new one
	Section.AddSubMenu("CreateBlueprint", LOCTEXT("CreateBlueprint", "Create..."),
		GetCreateMenuTooltip(GetGameModeClass(InSettingsData.LevelEditor, InSettingsData.bIsProjectSettings), InSettingsData.RootClass, InSettingsData.bIsProjectSettings),
		FNewToolMenuDelegate::CreateStatic( &LevelEditorActionHelpers::GetCreateSettingsClassSubMenu, InSettingsData.RootClass, InSettingsData.OnCreateClassPicked ),
		FUIAction(
			FExecuteAction(), 
			InSettingsData.RootClass == AGameModeBase::StaticClass()? 
				FCanExecuteAction::CreateStatic(CannotCreateSelectNativeProjectGameMode, InSettingsData.bIsProjectSettings) 
				: FCanExecuteAction::CreateStatic( &CanCreateSelectSubClass, GetGameModeClass(InSettingsData.LevelEditor, InSettingsData.bIsProjectSettings), InSettingsData.bIsProjectSettings )
		),
		EUserInterfaceActionType::Button, false, NewBPIcon
	);

	// Select a game mode, this is always available so the user can switch his selection
	FFormatNamedArguments Args;
	Args.Add(TEXT("RootClass"), RootClassName);
	Section.AddSubMenu("SelectGameModeClass", FText::Format(LOCTEXT("SelectGameModeClass", "Select {RootClass} Class"), Args),
		GetSelectMenuTooltip(GetGameModeClass(InSettingsData.LevelEditor, InSettingsData.bIsProjectSettings), InSettingsData.RootClass, InSettingsData.bIsProjectSettings),
		FNewToolMenuDelegate::CreateStatic( &LevelEditorActionHelpers::GetSelectSettingsClassSubMenu, InSettingsData.RootClass, InSettingsData.OnSelectClassPicked ),
		FUIAction(
			FExecuteAction(), 
			InSettingsData.RootClass == AGameModeBase::StaticClass()?
				FCanExecuteAction::CreateStatic(CannotCreateSelectNativeProjectGameMode, InSettingsData.bIsProjectSettings) 
				: FCanExecuteAction::CreateStatic( &CanCreateSelectSubClass, GetGameModeClass(InSettingsData.LevelEditor, InSettingsData.bIsProjectSettings), InSettingsData.bIsProjectSettings )
		),
		EUserInterfaceActionType::Button
	);

	// For GameMode classes only, there are some sub-classes we need to add to the menu
	if(InSettingsData.RootClass == AGameModeBase::StaticClass())
	{
		FToolMenuSection& GameModeClassesSection = Menu->AddSection("GameModeClasses", LOCTEXT("GameModeClasses", "Game Mode Classes"));
		if(InSettingsData.CurrentClass)
		{
			GetGameModeSubMenu(GameModeClassesSection, InSettingsData);
		}
	}

#undef LOCTEXT_NAMESPACE
}

UClass* LevelEditorActionHelpers::GetGameModeClass(TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings)
{
	UClass* GameModeClass = nullptr;
	if(bInIsProjectSettings)
	{
		UObject* GameModeObject = LoadObject<UObject>(nullptr, *UGameMapsSettings::GetGlobalDefaultGameMode());
		if(UBlueprint* GameModeAsBlueprint = Cast<UBlueprint>(GameModeObject))
		{
			GameModeClass = GameModeAsBlueprint->GeneratedClass;
		}
		else
		{
			GameModeClass = FindObject<UClass>(nullptr, *UGameMapsSettings::GetGlobalDefaultGameMode());
		}
	}
	else
	{
		AWorldSettings* WorldSettings = InLevelEditor.Pin()->GetWorld()->GetWorldSettings();
		if(WorldSettings->DefaultGameMode)
		{
			GameModeClass = WorldSettings->DefaultGameMode;
		}
	}
	return GameModeClass;
}

FText LevelEditorActionHelpers::GetOpenGameModeBlueprintLabel(TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings)
{
#define LOCTEXT_NAMESPACE "LevelToolBarViewMenu"
	if(UClass* GameModeClass = GetGameModeClass(InLevelEditor, bInIsProjectSettings))
	{
		if(GameModeClass->ClassGeneratedBy)
		{
			return FText::Format( LOCTEXT("GameModeEditBlueprint", "GameMode: Edit {0}"), FText::FromString(GameModeClass->ClassGeneratedBy->GetName()));
		}

		return FText::Format( LOCTEXT("GameModeBlueprint", "GameMode: {0}"), FText::FromString(GameModeClass->GetName()));
	}

	if(bInIsProjectSettings)
	{
		return LOCTEXT("GameModeCreateBlueprint", "GameMode: New...");
	}

	// For World Settings, we want to inform the user that they are not overridding the Project Settings
	return LOCTEXT("GameModeNotOverridden", "GameMode: Not overridden!");

#undef LOCTEXT_NAMESPACE
}

void LevelEditorActionHelpers::OnCreateGameModeClassPicked(UClass* InChosenClass, TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings)
{
	if(InChosenClass)
	{
		const FString NewBPName(TEXT("NewGameMode"));
		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprintFromClass(NSLOCTEXT("LevelEditorCommands", "CreateGameModeBlueprint_Title", "Create GameMode Blueprint"), InChosenClass, NewBPName);

		if(Blueprint)
		{
			// @todo Re-enable once world centric works
			const bool bOpenWorldCentric = false;
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(
				Blueprint,
				bOpenWorldCentric ? EToolkitMode::WorldCentric : EToolkitMode::Standalone,
				InLevelEditor.Pin()  );
			OnSelectGameModeClassPicked(Blueprint->GeneratedClass, InLevelEditor, bInIsProjectSettings);
		}
	}
	FSlateApplication::Get().DismissAllMenus();
}

void LevelEditorActionHelpers::OnSelectGameModeClassPicked(UClass* InChosenClass, TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings)
{
	if(bInIsProjectSettings)
	{
		UGameMapsSettings::SetGlobalDefaultGameMode(InChosenClass? InChosenClass->GetPathName() : FString());

		ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
		if (SettingsModule != nullptr)
		{
			ISettingsContainerPtr SettingsContainer = SettingsModule->GetContainer("Project");

			if (SettingsContainer.IsValid())
			{
				ISettingsCategoryPtr SettingsCategory = SettingsContainer->GetCategory("Project");

				if(SettingsCategory.IsValid())
				{
					SettingsCategory->GetSection("Maps")->Save();
				}
			}
		}
	}
	else
	{
		const FScopedTransaction Transaction( NSLOCTEXT("LevelEditorCommands", "SelectGameModeClassAction", "Set Override Game Mode Class") );

		AWorldSettings* WorldSettings = InLevelEditor.Pin()->GetWorld()->GetWorldSettings();
		WorldSettings->Modify();
		WorldSettings->DefaultGameMode = InChosenClass;
	}
	FSlateApplication::Get().DismissAllMenus();
}

UClass* LevelEditorActionHelpers::GetGameStateClass(TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings)
{
	if(UClass* GameModeClass = GetGameModeClass(InLevelEditor, bInIsProjectSettings))
	{
		AGameModeBase* ActiveGameMode = Cast<AGameModeBase>(GameModeClass->GetDefaultObject());
		if(ActiveGameMode)
		{
			return ActiveGameMode->GameStateClass;
		}
	}
	return NULL;
}

FText LevelEditorActionHelpers::GetOpenGameStateBlueprintLabel(TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings)
{
#define LOCTEXT_NAMESPACE "LevelToolBarViewMenu"
	if(UClass* GameStateClass = GetGameStateClass(InLevelEditor, bInIsProjectSettings))
	{
		FFormatNamedArguments FormatArgs;
		if(GameStateClass->ClassGeneratedBy)
		{
			FormatArgs.Add(TEXT("GameStateName"), FText::FromString(GameStateClass->ClassGeneratedBy->GetName()));
			return FText::Format(LOCTEXT("GameStateEditBlueprint", "GameState: Edit {GameStateName}"), FormatArgs);
		}

		FormatArgs.Add(TEXT("GameStateName"), FText::FromString(GameStateClass->GetName()));
		return FText::Format(LOCTEXT("GameStateBlueprint", "GameState: {GameStateName}"), FormatArgs);
	}

	return LOCTEXT("GameStateCreateBlueprint", "GameState: New...");
#undef LOCTEXT_NAMESPACE
}

void LevelEditorActionHelpers::OnCreateGameStateClassPicked(UClass* InChosenClass, TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings)
{
	if(InChosenClass)
	{
		const FString NewBPName(TEXT("NewGameState"));
		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprintFromClass(NSLOCTEXT("LevelEditorCommands", "CreateGameStateBlueprint_Title", "Create GameState Blueprint"), InChosenClass, NewBPName);

		if(Blueprint)
		{
			// @todo Re-enable once world centric works
			const bool bOpenWorldCentric = false;
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(
				Blueprint,
				bOpenWorldCentric ? EToolkitMode::WorldCentric : EToolkitMode::Standalone,
				InLevelEditor.Pin()  );

			OnSelectGameStateClassPicked(Blueprint->GeneratedClass, InLevelEditor, bInIsProjectSettings);
		}
	}
	FSlateApplication::Get().DismissAllMenus();
}

void LevelEditorActionHelpers::OnSelectGameStateClassPicked(UClass* InChosenClass, TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings)
{
	if(UClass* GameModeClass = GetGameModeClass(InLevelEditor, bInIsProjectSettings))
	{
		const FScopedTransaction Transaction( NSLOCTEXT("LevelEditorCommands", "SelectGameStateClassAction", "Set Game State Class") );
		AGameModeBase* ActiveGameMode = Cast<AGameModeBase>(GameModeClass->GetDefaultObject());
		ActiveGameMode->GameStateClass = InChosenClass;

		UBlueprint* Blueprint = Cast<UBlueprint>(GameModeClass->ClassGeneratedBy);
		if (ensure(Blueprint))
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		}
	}

	FSlateApplication::Get().DismissAllMenus();
}

UClass* LevelEditorActionHelpers::GetPawnClass(TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings)
{
	if(UClass* GameModeClass = GetGameModeClass(InLevelEditor, bInIsProjectSettings))
	{
		AGameModeBase* ActiveGameMode = Cast<AGameModeBase>(GameModeClass->GetDefaultObject());

		if(ActiveGameMode)
		{
			return ActiveGameMode->DefaultPawnClass;
		}
	}
	return NULL;
}

FText LevelEditorActionHelpers::GetOpenPawnBlueprintLabel(TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings)
{
#define LOCTEXT_NAMESPACE "LevelToolBarViewMenu"
	if(UClass* PawnClass = GetPawnClass(InLevelEditor, bInIsProjectSettings))
	{
		FFormatNamedArguments FormatArgs;
		if(PawnClass->ClassGeneratedBy)
		{
			FormatArgs.Add(TEXT("PawnName"), FText::FromString(PawnClass->ClassGeneratedBy->GetName()));
			return FText::Format(LOCTEXT("PawnEditBlueprint", "Pawn: Edit {PawnName}"), FormatArgs);
		}

		FormatArgs.Add(TEXT("PawnName"), FText::FromString(PawnClass->GetName()));
		return FText::Format(LOCTEXT("PawnBlueprint", "Pawn: {PawnName}"), FormatArgs);
	}

	return LOCTEXT("PawnCreateBlueprint", "Pawn: New...");
#undef LOCTEXT_NAMESPACE
}

void LevelEditorActionHelpers::OnCreatePawnClassPicked(UClass* InChosenClass, TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings)
{
	if(InChosenClass)
	{
		const FString NewBPName(TEXT("NewPawn"));
		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprintFromClass(NSLOCTEXT("LevelEditorCommands", "CreatePawnBlueprint_Title", "Create Pawn Blueprint"), InChosenClass, NewBPName);

		if(Blueprint)
		{
			// @todo Re-enable once world centric works
			const bool bOpenWorldCentric = false;
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(
				Blueprint,
				bOpenWorldCentric ? EToolkitMode::WorldCentric : EToolkitMode::Standalone,
				InLevelEditor.Pin()  );

			OnSelectPawnClassPicked(Blueprint->GeneratedClass, InLevelEditor, bInIsProjectSettings);
		}
	}
	FSlateApplication::Get().DismissAllMenus();
}

void LevelEditorActionHelpers::OnSelectPawnClassPicked(UClass* InChosenClass, TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings)
{
	if(UClass* GameModeClass = GetGameModeClass(InLevelEditor, bInIsProjectSettings))
	{
		const FScopedTransaction Transaction( NSLOCTEXT("LevelEditorCommands", "SelectPawnClassAction", "Set Pawn Class") );

		AGameModeBase* ActiveGameMode = Cast<AGameModeBase>(GameModeClass->GetDefaultObject());
		ActiveGameMode->DefaultPawnClass = InChosenClass;

		UBlueprint* Blueprint = Cast<UBlueprint>(GameModeClass->ClassGeneratedBy);
		if (ensure(Blueprint))
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		}
	}

	FSlateApplication::Get().DismissAllMenus();
}

UClass* LevelEditorActionHelpers::GetHUDClass(TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings)
{
	if(UClass* GameModeClass = GetGameModeClass(InLevelEditor, bInIsProjectSettings))
	{
		AGameModeBase* ActiveGameMode = Cast<AGameModeBase>(GameModeClass->GetDefaultObject());
		if(ActiveGameMode)
		{
			return ActiveGameMode->HUDClass;
		}
	}
	return NULL;
}

FText LevelEditorActionHelpers::GetOpenHUDBlueprintLabel(TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings)
{
#define LOCTEXT_NAMESPACE "LevelToolBarViewMenu"
	if(UClass* HUDClass = GetHUDClass(InLevelEditor, bInIsProjectSettings))
	{
		FFormatNamedArguments FormatArgs;
		if (HUDClass->ClassGeneratedBy)
		{
			FormatArgs.Add(TEXT("HUDName"), FText::FromString(HUDClass->ClassGeneratedBy->GetName()));
			return FText::Format(LOCTEXT("HUDEditBlueprint", "HUD: Edit {HUDName}"), FormatArgs);
		}

		FormatArgs.Add(TEXT("HUDName"), FText::FromString(HUDClass->GetName()));
		return FText::Format(LOCTEXT("HUDBlueprint", "HUD: {HUDName}"), FormatArgs);
	}

	return LOCTEXT("HUDCreateBlueprint", "HUD: New...");
#undef LOCTEXT_NAMESPACE
}

void LevelEditorActionHelpers::OnCreateHUDClassPicked(UClass* InChosenClass, TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings)
{
	if(InChosenClass)
	{
		const FString NewBPName(TEXT("NewHUD"));
		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprintFromClass(NSLOCTEXT("LevelEditorCommands", "CreateHUDBlueprint_Title", "Create HUD Blueprint"), InChosenClass, NewBPName);

		if(Blueprint)
		{
			// @todo Re-enable once world centric works
			const bool bOpenWorldCentric = false;
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(
				Blueprint,
				bOpenWorldCentric ? EToolkitMode::WorldCentric : EToolkitMode::Standalone,
				InLevelEditor.Pin()  );

			OnSelectHUDClassPicked(Blueprint->GeneratedClass, InLevelEditor, bInIsProjectSettings);
		}
	}
	FSlateApplication::Get().DismissAllMenus();
}

void LevelEditorActionHelpers::OnSelectHUDClassPicked(UClass* InChosenClass, TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings)
{
	if(UClass* GameModeClass = GetGameModeClass(InLevelEditor, bInIsProjectSettings))
	{
		const FScopedTransaction Transaction( NSLOCTEXT("LevelEditorCommands", "SelectHUDClassAction", "Set HUD Class") );

		AGameModeBase* ActiveGameMode = Cast<AGameModeBase>(GameModeClass->GetDefaultObject());
		ActiveGameMode->HUDClass = InChosenClass;

		UBlueprint* Blueprint = Cast<UBlueprint>(GameModeClass->ClassGeneratedBy);
		if (ensure(Blueprint))
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		}
	}

	FSlateApplication::Get().DismissAllMenus();
}

UClass* LevelEditorActionHelpers::GetPlayerControllerClass(TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings)
{
	if(UClass* GameModeClass = GetGameModeClass(InLevelEditor, bInIsProjectSettings))
	{
		AGameModeBase* ActiveGameMode = Cast<AGameModeBase>(GameModeClass->GetDefaultObject());
		if(ActiveGameMode)
		{
			return ActiveGameMode->PlayerControllerClass;
		}
	}
	return NULL;
}

FText LevelEditorActionHelpers::GetOpenPlayerControllerBlueprintLabel(TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings)
{
#define LOCTEXT_NAMESPACE "LevelToolBarViewMenu"
	if(UClass* PlayerControllerClass = GetPlayerControllerClass(InLevelEditor, bInIsProjectSettings))
	{
		FFormatNamedArguments FormatArgs;
		if (PlayerControllerClass->ClassGeneratedBy)
		{
			FormatArgs.Add(TEXT("PlayerControllerName"), FText::FromString(PlayerControllerClass->ClassGeneratedBy->GetName()));
			return FText::Format(LOCTEXT("PlayerControllerEditBlueprint", "PlayerController: Edit {PlayerControllerName}"), FormatArgs);
		}

		FormatArgs.Add(TEXT("PlayerControllerName"), FText::FromString(PlayerControllerClass->GetName()));
		return FText::Format(LOCTEXT("PlayerControllerBlueprint", "PlayerController: {PlayerControllerName}"), FormatArgs);
	}

	return LOCTEXT("PlayerControllerCreateBlueprint", "PlayerController: New...");
#undef LOCTEXT_NAMESPACE
}

void LevelEditorActionHelpers::OnCreatePlayerControllerClassPicked(UClass* InChosenClass, TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings)
{
	if(InChosenClass)
	{
		const FString NewBPName(TEXT("NewPlayerController"));
		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprintFromClass(NSLOCTEXT("LevelEditorCommands", "CreatePlayerControllerBlueprint_Title", "Create PlayerController Blueprint"), InChosenClass, NewBPName);

		if(Blueprint)
		{
			// @todo Re-enable once world centric works
			const bool bOpenWorldCentric = false;
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(
				Blueprint,
				bOpenWorldCentric ? EToolkitMode::WorldCentric : EToolkitMode::Standalone,
				InLevelEditor.Pin()  );

			OnSelectPlayerControllerClassPicked(Blueprint->GeneratedClass, InLevelEditor, bInIsProjectSettings);
		}
	}
	FSlateApplication::Get().DismissAllMenus();
}

void LevelEditorActionHelpers::OnSelectPlayerControllerClassPicked(UClass* InChosenClass, TWeakPtr< SLevelEditor > InLevelEditor, bool bInIsProjectSettings)
{
	if(UClass* GameModeClass = GetGameModeClass(InLevelEditor, bInIsProjectSettings))
	{
		const FScopedTransaction Transaction( NSLOCTEXT("LevelEditorCommands", "SelectPlayerControllerClassAction", "Set Player Controller Class") );

		AGameModeBase* ActiveGameMode = Cast<AGameModeBase>(GameModeClass->GetDefaultObject());
		ActiveGameMode->PlayerControllerClass = InChosenClass;

		UBlueprint* Blueprint = Cast<UBlueprint>(GameModeClass->ClassGeneratedBy);
		if (ensure(Blueprint))
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		}
	}
	FSlateApplication::Get().DismissAllMenus();
}

void FLevelEditorToolBar::RegisterLevelEditorToolBar( const TSharedRef<FUICommandList>& InCommandList, const TSharedRef<SLevelEditor> InLevelEditor)
{
	RegisterSourceControlMenu();
	RegisterCinematicsMenu();
	RegisterBuildMenu();
	RegisterEditorModesMenu();
#if WITH_LIVE_CODING
	RegisterCompileMenu();
#endif

	RegisterQuickSettingsMenu();
	RegisterOpenBlueprintMenu();

#define LOCTEXT_NAMESPACE "LevelEditorToolBar"

	UToolMenu* Toolbar = UToolMenus::Get()->RegisterMenu("LevelEditor.LevelEditorToolBar", NAME_None, EMultiBoxType::ToolBar);

	{
		FToolMenuSection& Section = Toolbar->AddSection("File");

		// Save All Levels
		Section.AddEntry(FToolMenuEntry::InitToolBarButton(
			FLevelEditorCommands::Get().Save,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "AssetEditor.SaveAsset"),
			NAME_None,
			"SaveAllLevels"
		));

		// Source control buttons
		{
			enum EQueryState
			{
				NotQueried,
				Querying,
				Queried,
			};

			static EQueryState QueryState = EQueryState::NotQueried;

			struct FSourceControlStatus
			{
				static void CheckSourceControlStatus()
				{
					ISourceControlModule& SourceControlModule = ISourceControlModule::Get();
					if (SourceControlModule.IsEnabled())
					{
						SourceControlModule.GetProvider().Execute(ISourceControlOperation::Create<FConnect>(),
																  EConcurrency::Asynchronous,
																  FSourceControlOperationComplete::CreateStatic(&FSourceControlStatus::OnSourceControlOperationComplete));
						QueryState = EQueryState::Querying;
					}
				}

				static void OnSourceControlOperationComplete(const FSourceControlOperationRef& InOperation, ECommandResult::Type InResult)
				{
					QueryState = EQueryState::Queried;
				}

				static FText GetSourceControlTooltip()
				{
					if (QueryState == EQueryState::Querying)
					{
						return LOCTEXT("SourceControlUnknown", "Source control status is unknown");
					}
					else
					{
						return ISourceControlModule::Get().GetProvider().GetStatusText();
					}
				}

				static FSlateIcon GetSourceControlIcon()
				{
					if (QueryState == EQueryState::Querying)
					{
						return FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.SourceControl.Unknown");
					}
					else
					{
						ISourceControlModule& SourceControlModule = ISourceControlModule::Get();
						if (SourceControlModule.IsEnabled())
						{
							if (!SourceControlModule.GetProvider().IsAvailable())
							{
								return FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.SourceControl.Problem");
							}
							else
							{
								return FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.SourceControl.On");
							}
						}
						else
						{
							return FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.SourceControl.Off");
						}
					}
				}
			};

			FSourceControlStatus::CheckSourceControlStatus();

			Section.AddEntry(FToolMenuEntry::InitComboButton(
				"SourceControl",
				FUIAction(),
				FOnGetContent::CreateStatic(&FLevelEditorToolBar::GenerateSourceControlMenu, InCommandList, TWeakPtr<SLevelEditor>(InLevelEditor)),
				LOCTEXT("SourceControl_Label", "Source Control"),
				TAttribute<FText>::Create(&FSourceControlStatus::GetSourceControlTooltip),
				TAttribute<FSlateIcon>::Create(&FSourceControlStatus::GetSourceControlIcon),
				false
				));
		}
	}

	{

		struct FEditorModesStatus
		{
			static FSlateIcon GetEditorModesIcon()
			{
				for (const FEditorModeInfo& Mode : FEditorModeRegistry::Get().GetSortedModeInfo())
				{
					if (!Mode.bVisible)
					{
						continue;
					}

					if (GLevelEditorModeTools().IsModeActive(Mode.ID))
					{
						// if its a default mode, use the default tool icon
						if (GLevelEditorModeTools().IsDefaultMode(Mode.ID))
						{
							return FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.EditorModes");
						}

						FName EditorModeCommandName = FName(*(FString("EditorMode.") + Mode.ID.ToString()));
						const FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
						const FLevelEditorModesCommands& Commands = LevelEditorModule.GetLevelEditorModesCommands();

						TSharedPtr<FUICommandInfo> EditorModeCommand =
							FInputBindingManager::Get().FindCommandInContext(Commands.GetContextName(), EditorModeCommandName);
						if (EditorModeCommand.IsValid())
						{
							return EditorModeCommand->GetIcon();
						}
					}
					
				}
				return FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.EditorModes");

			}
		};

		FToolMenuSection& Section = Toolbar->AddSection("Modes");
		Section.AddEntry(FToolMenuEntry::InitComboButton(
			"EditorModes",
			FUIAction(),
			FOnGetContent::CreateStatic(&FLevelEditorToolBar::GenerateEditorModesMenu, InCommandList, TWeakPtr<SLevelEditor>(InLevelEditor)),
			LOCTEXT("EditorModes_Label", "Modes"),
			LOCTEXT("EditorModes_Tooltip", "Displays a list of editing modes that can be toggled"),
			TAttribute<FSlateIcon>::Create(&FEditorModesStatus::GetEditorModesIcon)
		));
	}

	{
		FToolMenuSection& Section = Toolbar->AddSection("Content");
		Section.AddEntry(FToolMenuEntry::InitToolBarButton(FLevelEditorCommands::Get().OpenContentBrowser, LOCTEXT( "ContentBrowser_Override", "Content" ), TAttribute<FText>(), TAttribute<FSlateIcon>(), "LevelToolbarContent"));
		if (FLauncherPlatformModule::Get()->CanOpenLauncher(true)) 
		{
			Section.AddEntry(FToolMenuEntry::InitToolBarButton(FLevelEditorCommands::Get().OpenMarketplace, LOCTEXT("Marketplace_Override", "Marketplace"), TAttribute<FText>(), TAttribute<FSlateIcon>(), "LevelToolbarMarketplace"));
		}
	}

	FToolMenuSection& SettingsSection = Toolbar->AddSection("Settings");
	{
		SettingsSection.AddEntry(FToolMenuEntry::InitComboButton(
			"LevelToolbarQuickSettings",
			FUIAction(),
			FOnGetContent::CreateStatic(&FLevelEditorToolBar::GenerateQuickSettingsMenu, InCommandList, TWeakPtr<SLevelEditor>(InLevelEditor)),
			LOCTEXT("QuickSettingsCombo", "Settings"),
			LOCTEXT("QuickSettingsCombo_ToolTip", "Project and Editor settings"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.GameSettings"),
			false,
			"LevelToolbarQuickSettings"
			));

	}

	{
		struct FPreviewModeFunctionality
		{

			static FText GetPreviewModeText()
			{
				EShaderPlatform ShaderPlatform = ShaderFormatToLegacyShaderPlatform(GEditor->PreviewPlatform.PreviewShaderPlatformName);

				switch (ShaderPlatform)
				{
					case SP_VULKAN_ES3_1_ANDROID:
					{						
						return LOCTEXT("PreviewModeES31_Vulkan_Text", "Vulkan Preview");
					}
				}

				switch (GEditor->PreviewPlatform.PreviewFeatureLevel)
				{
					case ERHIFeatureLevel::ES2:
					{
						return LOCTEXT("PreviewModeES2_Text", "ES2 Preview");
					}
					case ERHIFeatureLevel::ES3_1:
					{
						return LOCTEXT("PreviewModeES3_1_Text", "ES3.1 Preview");
					}
					default:
					{
						return LOCTEXT("PreviewModeGeneric", "Preview Mode");
					}
				}
			}

			static FText GetPreviewModeTooltip()
			{
				EShaderPlatform PreviewShaderPlatform = GEditor->PreviewPlatform.PreviewShaderPlatformName != NAME_None ?
					ShaderFormatToLegacyShaderPlatform(GEditor->PreviewPlatform.PreviewShaderPlatformName) :
					GetFeatureLevelShaderPlatform(GEditor->PreviewPlatform.PreviewFeatureLevel);

				EShaderPlatform MaxRHIFeatureLevelPlatform = GetFeatureLevelShaderPlatform(GMaxRHIFeatureLevel);

				{
					const FText& RenderingAsPlatformName = GetFriendlyShaderPlatformName(GEditor->PreviewPlatform.bPreviewFeatureLevelActive ? PreviewShaderPlatform : MaxRHIFeatureLevelPlatform);
                    const FText& SwitchToPlatformName = GetFriendlyShaderPlatformName(GEditor->PreviewPlatform.bPreviewFeatureLevelActive ? MaxRHIFeatureLevelPlatform : PreviewShaderPlatform);
                    if (GWorld->FeatureLevel == GMaxRHIFeatureLevel)
                    {
                        return FText::Format(LOCTEXT("PreviewModeViewingAsSwitchTo", "Viewing {0}. Click to preview {1}."), RenderingAsPlatformName, SwitchToPlatformName);
                    }
                    else
                    {
                        return FText::Format(LOCTEXT("PreviewModePreviewingAsSwitchTo", "Previewing {0}. Click to view {1}."), RenderingAsPlatformName, SwitchToPlatformName);
                    }
				}
			}

			static FSlateIcon GetPreviewModeIcon()
			{
				EShaderPlatform ShaderPlatform = ShaderFormatToLegacyShaderPlatform(GEditor->PreviewPlatform.PreviewShaderPlatformName);

				if (ShaderPlatform == SP_NumPlatforms)
				{
					ShaderPlatform = GetFeatureLevelShaderPlatform(GEditor->PreviewPlatform.PreviewFeatureLevel);
				}
				switch (ShaderPlatform)
				{
					case SP_OPENGL_ES3_1_ANDROID:
					{
						return FSlateIcon(FEditorStyle::GetStyleSetName(), GEditor->IsFeatureLevelPreviewActive() ? "LevelEditor.PreviewMode.AndroidES31.Enabled" : "LevelEditor.PreviewMode.AndroidES31.Disabled");
					}
					case SP_VULKAN_ES3_1_ANDROID:
					{
						return FSlateIcon(FEditorStyle::GetStyleSetName(), GEditor->IsFeatureLevelPreviewActive() ? "LevelEditor.PreviewMode.AndroidVulkan.Enabled" : "LevelEditor.PreviewMode.AndroidVulkan.Disabled");
					}
					case SP_METAL:
					{
						return FSlateIcon(FEditorStyle::GetStyleSetName(), GEditor->IsFeatureLevelPreviewActive() ? "LevelEditor.PreviewMode.iOS.Enabled" : "LevelEditor.PreviewMode.iOS.Disabled");
					}
					case SP_VULKAN_PCES3_1:
					case SP_OPENGL_PCES2:
					case SP_PCD3D_ES2:
					case SP_METAL_MACES2:
					case SP_OPENGL_ES2_WEBGL:
					{
						return FSlateIcon(FEditorStyle::GetStyleSetName(), GEditor->IsFeatureLevelPreviewActive() ? "LevelEditor.PreviewMode.AndroidES2.Enabled" : "LevelEditor.PreviewMode.AndroidES2.Disabled");
					}
				}
				switch (GEditor->PreviewPlatform.PreviewFeatureLevel)
				{
					case ERHIFeatureLevel::ES2:
					{
						return FSlateIcon(FEditorStyle::GetStyleSetName(), GEditor->IsFeatureLevelPreviewActive() ? "LevelEditor.PreviewMode.AndroidES2.Enabled" : "LevelEditor.PreviewMode.AndroidES2.Disabled");
					}
					case ERHIFeatureLevel::ES3_1:
					{
						return FSlateIcon(FEditorStyle::GetStyleSetName(), GEditor->IsFeatureLevelPreviewActive() ? "LevelEditor.PreviewMode.Enabled" : "LevelEditor.PreviewMode.Disabled");
					}
					default:
					{
						return FSlateIcon(FEditorStyle::GetStyleSetName(), GEditor->IsFeatureLevelPreviewActive() ? "LevelEditor.PreviewMode.Enabled" : "LevelEditor.PreviewMode.Disabled");
					}
				}
			}
		};

		SettingsSection.AddEntry(FToolMenuEntry::InitToolBarButton( 
			FLevelEditorCommands::Get().ToggleFeatureLevelPreview,
			TAttribute<FText>::Create(&FPreviewModeFunctionality::GetPreviewModeText),
        	TAttribute<FText>::Create(&FPreviewModeFunctionality::GetPreviewModeTooltip),
        	TAttribute<FSlateIcon>::Create(&FPreviewModeFunctionality::GetPreviewModeIcon)
			));
	}

	{
		FToolMenuSection& Section = Toolbar->AddSection("Misc");
		Section.AddEntry(FToolMenuEntry::InitComboButton(
			"OpenBlueprint",
			FUIAction(),
			FOnGetContent::CreateStatic( &FLevelEditorToolBar::GenerateOpenBlueprintMenuContent, InCommandList, TWeakPtr<SLevelEditor>( InLevelEditor ) ),
			LOCTEXT( "OpenBlueprint_Label", "Blueprints" ),
			LOCTEXT( "OpenBlueprint_ToolTip", "List of world Blueprints available to the user for editing or creation." ),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.OpenLevelBlueprint")
			));

		Section.AddEntry(FToolMenuEntry::InitComboButton(
			"EditCinematics",
			FUIAction(),
			FOnGetContent::CreateStatic( &FLevelEditorToolBar::GenerateCinematicsMenuContent, InCommandList, TWeakPtr<SLevelEditor>( InLevelEditor ) ),
			LOCTEXT( "EditCinematics_Label", "Cinematics" ),
			LOCTEXT( "EditCinematics_Tooltip", "Displays a list of Level Sequence objects to open in their respective editors"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.EditMatinee") 
			));

		Section.AddEntry(FToolMenuEntry::InitToolBarButton( FLevelEditorCommands::Get().ToggleVR, LOCTEXT("ToggleVR", "VR Mode")) );
	}

	{
		FToolMenuSection& Section = Toolbar->AddSection("Compile");
		// Build			
		Section.AddEntry(FToolMenuEntry::InitToolBarButton( FLevelEditorCommands::Get().Build, LOCTEXT("BuildAll", "Build")) );

		// Build menu drop down
		Section.AddEntry(FToolMenuEntry::InitComboButton(
			"BuildComboButton",
			FUIAction(),
			FOnGetContent::CreateStatic( &FLevelEditorToolBar::GenerateBuildMenuContent, InCommandList, TWeakPtr<SLevelEditor>(InLevelEditor) ),
			LOCTEXT( "BuildCombo_Label", "Build Options" ),
			LOCTEXT( "BuildComboToolTip", "Build options menu" ),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Build"),
			true));

		Section.AddDynamicEntry("CompilerAvailable", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
		{
			// Only show the compile options on machines with the solution (assuming they can build it)
			if (FSourceCodeNavigation::IsCompilerAvailable())
			{
				// Since we can always add new code to the project, only hide these buttons if we haven't done so yet
				InSection.AddEntry(FToolMenuEntry::InitToolBarButton(
					"CompileButton",
					FUIAction(
						FExecuteAction::CreateStatic(&FLevelEditorActionCallbacks::RecompileGameCode_Clicked),
						FCanExecuteAction::CreateStatic(&FLevelEditorActionCallbacks::Recompile_CanExecute),
						FIsActionChecked(),
						FIsActionButtonVisible::CreateStatic(FLevelEditorActionCallbacks::CanShowSourceCodeActions)),
					LOCTEXT("CompileMenuButton", "Compile"),
					FLevelEditorCommands::Get().RecompileGameCode->GetDescription(),
					FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Recompile")
				));

#if WITH_LIVE_CODING
				InSection.AddEntry(FToolMenuEntry::InitComboButton(
					"CompileComboButton",
					FUIAction(
						FExecuteAction(),
						FCanExecuteAction(),
						FIsActionChecked(),
						FIsActionButtonVisible::CreateStatic(FLevelEditorActionCallbacks::CanShowSourceCodeActions)),
					FNewToolMenuWidgetChoice(),
					LOCTEXT("CompileCombo_Label", "Compile Options"),
					LOCTEXT("CompileComboToolTip", "Compile options menu"),
					FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Recompile"),
					true
				));
#endif
			}
		}));
	}

	{
		FToolMenuSection& Section = Toolbar->AddSection("Game");

		// Add the shared play-world commands that will be shown on the Kismet toolbar as well
		FPlayWorldCommands::BuildToolbar(Section, true);
	}

#undef LOCTEXT_NAMESPACE
}

/**
 * Static: Creates a widget for the level editor tool bar
 *
 * @return	New widget
 */
TSharedRef< SWidget > FLevelEditorToolBar::MakeLevelEditorToolBar( const TSharedRef<FUICommandList>& InCommandList, const TSharedRef<SLevelEditor> InLevelEditor )
{
#define LOCTEXT_NAMESPACE "LevelEditorToolBar"

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	FToolMenuContext MenuContext(InCommandList, LevelEditorModule.GetToolBarExtensibilityManager()->GetAllExtenders());
	ULevelEditorMenuContext* LevelEditorMenuContext = NewObject<ULevelEditorMenuContext>();
	LevelEditorMenuContext->LevelEditor = InLevelEditor;
	MenuContext.AddObject(LevelEditorMenuContext);

	// Create the tool bar!
	return
		SNew( SBorder )
		.Padding(0)
		.BorderImage( FEditorStyle::GetBrush("NoBorder") )
		.IsEnabled( FSlateApplication::Get().GetNormalExecutionAttribute() )
		[
			UToolMenus::Get()->GenerateWidget("LevelEditor.LevelEditorToolBar", MenuContext)
		];
#undef LOCTEXT_NAMESPACE
}



TSharedRef< SWidget > FLevelEditorToolBar::GenerateBuildMenuContent( TSharedRef<FUICommandList> InCommandList, TWeakPtr<SLevelEditor> InLevelEditor)
{
	// Get all menu extenders for this context menu from the level editor module
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>( TEXT("LevelEditor") );
	TSharedPtr<FExtender> MenuExtender = LevelEditorModule.AssembleExtenders(InCommandList, LevelEditorModule.GetAllLevelEditorToolbarBuildMenuExtenders());
	FToolMenuContext MenuContext(InCommandList, MenuExtender);

	ULevelEditorMenuContext* LevelEditorMenuContext = NewObject<ULevelEditorMenuContext>();
	LevelEditorMenuContext->LevelEditor = InLevelEditor;
	MenuContext.AddObject(LevelEditorMenuContext);

	return UToolMenus::Get()->GenerateWidget("LevelEditor.LevelEditorToolBar.BuildComboButton", MenuContext);
}

void FLevelEditorToolBar::RegisterBuildMenu()
{
#define LOCTEXT_NAMESPACE "LevelToolBarBuildMenu"

	static const FName BaseMenuName = "LevelEditor.LevelEditorToolBar.BuildComboButton";
	UToolMenu* Menu = UToolMenus::Get()->RegisterMenu(BaseMenuName);

	struct FLightingMenus
	{
	public:

		static void RegisterMenus(const FName InBaseMenuName)
		{
			FLightingMenus::RegisterLightingQualityMenu(InBaseMenuName);
			FLightingMenus::RegisterLightingInfoMenu(InBaseMenuName);
		}

	private:

		/** Generates a lighting quality sub-menu */
		static void RegisterLightingQualityMenu(const FName InBaseMenuName)
		{
			UToolMenu* SubMenu = UToolMenus::Get()->RegisterMenu(UToolMenus::JoinMenuPaths(InBaseMenuName, "LightingQuality"));

			{
				FToolMenuSection& Section = SubMenu->AddSection("LevelEditorBuildLightingQuality", LOCTEXT( "LightingQualityHeading", "Quality Level" ) );
				Section.AddMenuEntry( FLevelEditorCommands::Get().LightingQuality_Production );
				Section.AddMenuEntry( FLevelEditorCommands::Get().LightingQuality_High );
				Section.AddMenuEntry( FLevelEditorCommands::Get().LightingQuality_Medium );
				Section.AddMenuEntry( FLevelEditorCommands::Get().LightingQuality_Preview );
			}
		}

		/** Generates a lighting density sub-menu */
		static void RegisterLightingDensityMenu(const FName InBaseMenuName)
		{
			UToolMenu* SubMenu = UToolMenus::Get()->RegisterMenu(UToolMenus::JoinMenuPaths(InBaseMenuName, "LightingDensity"));

			{
				FToolMenuSection& Section = SubMenu->AddSection("LevelEditorBuildLightingDensity", LOCTEXT( "LightingDensityHeading", "Density Rendering" ) );
				TSharedRef<SWidget> Ideal =		SNew(SHorizontalBox)
												+SHorizontalBox::Slot()
												.Padding( FMargin( 27.0f, 0.0f, 0.0f, 0.0f ) )
												.FillWidth(1.0f)
												[
													SNew(SSpinBox<float>)
													.MinValue(0.f)
													.MaxValue(100.f)
													.Value(FLevelEditorActionCallbacks::GetLightingDensityIdeal())
													.OnValueChanged_Static(&FLevelEditorActionCallbacks::SetLightingDensityIdeal)
												];
				Section.AddEntry(FToolMenuEntry::InitWidget("Ideal", Ideal, LOCTEXT("LightingDensity_Ideal","Ideal Density")));
				
				TSharedRef<SWidget> Maximum =	SNew(SHorizontalBox)
												+SHorizontalBox::Slot()
												.FillWidth(1.0f)
												[
													SNew(SSpinBox<float>)
													.MinValue(0.01f)
													.MaxValue(100.01f)
													.Value(FLevelEditorActionCallbacks::GetLightingDensityMaximum())
													.OnValueChanged_Static(&FLevelEditorActionCallbacks::SetLightingDensityMaximum)
												];
				Section.AddEntry(FToolMenuEntry::InitWidget("Maximum", Maximum, LOCTEXT("LightingDensity_Maximum","Maximum Density")));

				TSharedRef<SWidget> ClrScale =	SNew(SHorizontalBox)
												+SHorizontalBox::Slot()
												.Padding( FMargin( 35.0f, 0.0f, 0.0f, 0.0f ) )
												.FillWidth(1.0f)
												[
													SNew(SSpinBox<float>)
													.MinValue(0.f)
													.MaxValue(10.f)
													.Value(FLevelEditorActionCallbacks::GetLightingDensityColorScale())
													.OnValueChanged_Static(&FLevelEditorActionCallbacks::SetLightingDensityColorScale)
												];
				Section.AddEntry(FToolMenuEntry::InitWidget("ColorScale", ClrScale, LOCTEXT("LightingDensity_ColorScale","Color Scale")));

				TSharedRef<SWidget> GrayScale =	SNew(SHorizontalBox)
												+SHorizontalBox::Slot()
												.Padding( FMargin( 11.0f, 0.0f, 0.0f, 0.0f ) )
												.FillWidth(1.0f)
												[
													SNew(SSpinBox<float>)
													.MinValue(0.f)
													.MaxValue(10.f)
													.Value(FLevelEditorActionCallbacks::GetLightingDensityGrayscaleScale())
													.OnValueChanged_Static(&FLevelEditorActionCallbacks::SetLightingDensityGrayscaleScale)
												];
				Section.AddEntry(FToolMenuEntry::InitWidget("GrayscaleScale", GrayScale, LOCTEXT("LightingDensity_GrayscaleScale","Grayscale Scale")));

				Section.AddMenuEntry( FLevelEditorCommands::Get().LightingDensity_RenderGrayscale );
			}
		}

		/** Generates a lighting resolution sub-menu */
		static void RegisterLightingResolutionMenu(const FName InBaseMenuName)
		{
			UToolMenu* SubMenu = UToolMenus::Get()->RegisterMenu(UToolMenus::JoinMenuPaths(InBaseMenuName, "LightingResolution"));

			{
				FToolMenuSection& Section = SubMenu->AddSection("LevelEditorBuildLightingResolution1", LOCTEXT( "LightingResolutionHeading1", "Primitive Types" ) );
				TSharedRef<SWidget> Meshes =	SNew(SHorizontalBox)
												+SHorizontalBox::Slot()
												.AutoWidth()
												[
													SNew( SCheckBox )
													.Style( FEditorStyle::Get(), "Menu.CheckBox" )
													.ToolTipText(LOCTEXT( "StaticMeshesToolTip", "Static Meshes will be adjusted if checked." ))
													.IsChecked_Static(&FLevelEditorActionCallbacks::IsLightingResolutionStaticMeshesChecked)
													.OnCheckStateChanged_Static(&FLevelEditorActionCallbacks::SetLightingResolutionStaticMeshes)
													.Content()
													[
														SNew( STextBlock )
														.Text( LOCTEXT("StaticMeshes", "Static Meshes") )
													]
												]
												+SHorizontalBox::Slot()
												.AutoWidth()
												.Padding( FMargin( 4.0f, 0.0f, 11.0f, 0.0f ) )
												[
													SNew(SSpinBox<float>)
													.MinValue(4.f)
													.MaxValue(4096.f)
													.ToolTipText(LOCTEXT( "LightingResolutionStaticMeshesMinToolTip", "The minimum lightmap resolution for static mesh adjustments. Anything outside of Min/Max range will not be touched when adjusting." ))
													.Value(FLevelEditorActionCallbacks::GetLightingResolutionMinSMs())
													.OnValueChanged_Static(&FLevelEditorActionCallbacks::SetLightingResolutionMinSMs)
												]
												+SHorizontalBox::Slot()
												.AutoWidth()
												[
													SNew(SSpinBox<float>)
													.MinValue(4.f)
													.MaxValue(4096.f)
													.ToolTipText(LOCTEXT( "LightingResolutionStaticMeshesMaxToolTip", "The maximum lightmap resolution for static mesh adjustments. Anything outside of Min/Max range will not be touched when adjusting." ))
													.Value(FLevelEditorActionCallbacks::GetLightingResolutionMaxSMs())
													.OnValueChanged_Static(&FLevelEditorActionCallbacks::SetLightingResolutionMaxSMs)
												];
				Section.AddEntry(FToolMenuEntry::InitWidget("Meshes", Meshes, FText::GetEmpty(), true));
				
				TSharedRef<SWidget> BSPs =		SNew(SHorizontalBox)
												+SHorizontalBox::Slot()
												.AutoWidth()
												[
													SNew( SCheckBox )
													.Style(FEditorStyle::Get(), "Menu.CheckBox")
													.ToolTipText(LOCTEXT( "BSPSurfacesToolTip", "BSP Surfaces will be adjusted if checked." ))
													.IsChecked_Static(&FLevelEditorActionCallbacks::IsLightingResolutionBSPSurfacesChecked)
													.OnCheckStateChanged_Static(&FLevelEditorActionCallbacks::SetLightingResolutionBSPSurfaces)
													.Content()
													[
														SNew( STextBlock )
														.Text( LOCTEXT("BSPSurfaces", "BSP Surfaces") )
													]
												]
												+SHorizontalBox::Slot()
												.AutoWidth()
												.Padding( FMargin( 6.0f, 0.0f, 4.0f, 0.0f ) )
												[
													SNew(SSpinBox<float>)
													.MinValue(1.f)
													.MaxValue(63556.f)
													.ToolTipText(LOCTEXT( "LightingResolutionBSPsMinToolTip", "The minimum lightmap resolution of a BSP surface to adjust. When outside of the Min/Max range, the BSP surface will no be altered." ))
													.Value(FLevelEditorActionCallbacks::GetLightingResolutionMinBSPs())
													.OnValueChanged_Static(&FLevelEditorActionCallbacks::SetLightingResolutionMinBSPs)
												]
												+SHorizontalBox::Slot()
												.AutoWidth()
												[
													SNew(SSpinBox<float>)
													.MinValue(1.f)
													.MaxValue(63556.f)
													.ToolTipText(LOCTEXT( "LightingResolutionBSPsMaxToolTip", "The maximum lightmap resolution of a BSP surface to adjust. When outside of the Min/Max range, the BSP surface will no be altered." ))
													.Value(FLevelEditorActionCallbacks::GetLightingResolutionMaxBSPs())
													.OnValueChanged_Static(&FLevelEditorActionCallbacks::SetLightingResolutionMaxBSPs)
												];
				Section.AddEntry(FToolMenuEntry::InitWidget("BSPs", BSPs, FText::GetEmpty(), true));
			}

			{
				FToolMenuSection& Section = SubMenu->AddSection("LevelEditorBuildLightingResolution2", LOCTEXT( "LightingResolutionHeading2", "Select Options" ) );
				Section.AddMenuEntry( FLevelEditorCommands::Get().LightingResolution_CurrentLevel );
				Section.AddMenuEntry( FLevelEditorCommands::Get().LightingResolution_SelectedLevels );
				Section.AddMenuEntry( FLevelEditorCommands::Get().LightingResolution_AllLoadedLevels );
				Section.AddMenuEntry( FLevelEditorCommands::Get().LightingResolution_SelectedObjectsOnly );
			}

			{
				FToolMenuSection& Section = SubMenu->AddSection("LevelEditorBuildLightingResolution3", LOCTEXT( "LightingResolutionHeading3", "Ratio" ) );
				TSharedRef<SWidget> Ratio =		SNew(SSpinBox<int32>)
												.MinValue(0)
												.MaxValue(400)
												.ToolTipText(LOCTEXT( "LightingResolutionRatioToolTip", "Ratio to apply (New Resolution = Ratio / 100.0f * CurrentResolution)." ))
												.Value(FLevelEditorActionCallbacks::GetLightingResolutionRatio())
												.OnEndSliderMovement_Static(&FLevelEditorActionCallbacks::SetLightingResolutionRatio)
												.OnValueCommitted_Static(&FLevelEditorActionCallbacks::SetLightingResolutionRatioCommit);
				Section.AddEntry(FToolMenuEntry::InitWidget("Ratio", Ratio, LOCTEXT( "LightingResolutionRatio", "Ratio" )));
			}
		}

		/** Generates a lighting info dialogs sub-menu */
		static void RegisterLightingInfoMenu(const FName InBaseMenuName)
		{
			RegisterLightingDensityMenu(UToolMenus::JoinMenuPaths(InBaseMenuName, "LightingInfo"));
			RegisterLightingResolutionMenu(UToolMenus::JoinMenuPaths(InBaseMenuName, "LightingInfo"));

			UToolMenu* SubMenu = UToolMenus::Get()->RegisterMenu(UToolMenus::JoinMenuPaths(InBaseMenuName, "LightingInfo"));

			{
				FToolMenuSection& Section = SubMenu->AddSection("LevelEditorBuildLightingInfo", LOCTEXT( "LightingInfoHeading", "Lighting Info Dialogs" ) );
				Section.AddSubMenu(
					"LightingDensity",
					LOCTEXT( "LightingDensityRenderingSubMenu", "LightMap Density Rendering Options" ),
					LOCTEXT( "LightingDensityRenderingSubMenu_ToolTip", "Shows the LightMap Density Rendering viewmode options." ),
					FNewToolMenuChoice() );

				Section.AddSubMenu(
					"LightingResolution",
					LOCTEXT( "LightingResolutionAdjustmentSubMenu", "LightMap Resolution Adjustment" ),
					LOCTEXT( "LightingResolutionAdjustmentSubMenu_ToolTip", "Shows the LightMap Resolution Adjustment options." ),
					FNewToolMenuChoice() );

				Section.AddMenuEntry( FLevelEditorCommands::Get().LightingStaticMeshInfo, LOCTEXT( "BuildLightingInfo_LightingStaticMeshInfo", "Lighting StaticMesh Info..." ) );
			}
		}
	};

	FLightingMenus::RegisterMenus(BaseMenuName);

	{
		FToolMenuSection& Section = Menu->AddSection( "LevelEditorLighting", LOCTEXT( "LightingHeading", "Lighting" ) );
		Section.AddMenuEntry( FLevelEditorCommands::Get().BuildLightingOnly, LOCTEXT( "BuildLightingOnlyHeading", "Build Lighting Only" ) );

		Section.AddSubMenu(
			"LightingQuality",
			LOCTEXT( "LightingQualitySubMenu", "Lighting Quality" ),
			LOCTEXT( "LightingQualitySubMenu_ToolTip", "Allows you to select the quality level for precomputed lighting" ),
			FNewToolMenuChoice() );

		Section.AddSubMenu(
			"LightingInfo",
			LOCTEXT( "BuildLightingInfoSubMenu", "Lighting Info" ),
			LOCTEXT( "BuildLightingInfoSubMenu_ToolTip", "Access the lighting info dialogs" ),
			FNewToolMenuChoice() );

		Section.AddMenuEntry( FLevelEditorCommands::Get().LightingBuildOptions_UseErrorColoring );
		Section.AddMenuEntry( FLevelEditorCommands::Get().LightingBuildOptions_ShowLightingStats );
	}

	{
		FToolMenuSection& Section = Menu->AddSection("LevelEditorReflections", LOCTEXT( "ReflectionHeading", "Reflections" ) );
		Section.AddMenuEntry( FLevelEditorCommands::Get().BuildReflectionCapturesOnly );
	}

	{
		FToolMenuSection& Section = Menu->AddSection("LevelEditorVisibility", LOCTEXT( "VisibilityHeading", "Visibility" ) );
		Section.AddMenuEntry( FLevelEditorCommands::Get().BuildLightingOnly_VisibilityOnly );
	}

	{
		FToolMenuSection& Section = Menu->AddSection("LevelEditorGeometry", LOCTEXT( "GeometryHeading", "Geometry" ) );
		Section.AddMenuEntry( FLevelEditorCommands::Get().BuildGeometryOnly );
		Section.AddMenuEntry( FLevelEditorCommands::Get().BuildGeometryOnly_OnlyCurrentLevel );
	}

	{
		FToolMenuSection& Section = Menu->AddSection("LevelEditorNavigation", LOCTEXT( "NavigationHeading", "Navigation" ) );
		Section.AddMenuEntry( FLevelEditorCommands::Get().BuildPathsOnly );
	}

	{
		FToolMenuSection& Section = Menu->AddSection("LevelEditorLOD", LOCTEXT("LODHeading", "Hierarchical LOD"));
		Section.AddMenuEntry(FLevelEditorCommands::Get().BuildLODsOnly);
	}
	
	{
		FToolMenuSection& Section = Menu->AddSection("LevelEditorTextureStreaming", LOCTEXT("TextureStreamingHeading", "Texture Streaming"));
		Section.AddDynamicEntry("BuildTextureStreamingOnly", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
		{
			if (CVarStreamingUseNewMetrics.GetValueOnAnyThread() != 0) // There is no point of in building texture streaming data with the old system.
			{
				InSection.AddMenuEntry(FLevelEditorCommands::Get().BuildTextureStreamingOnly);
			}
		}));
		Section.AddMenuEntry(FLevelEditorCommands::Get().BuildVirtualTextureOnly);
	}

	
	{
		FToolMenuSection& Section = Menu->AddSection("LevelEditorAutomation", LOCTEXT( "AutomationHeading", "Automation" ) );
		Section.AddMenuEntry( 
			FLevelEditorCommands::Get().BuildAndSubmitToSourceControl,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.BuildAndSubmit")
			);
	}

	// Map Check
	{
		FToolMenuSection& Section = Menu->AddSection("LevelEditorVerification", LOCTEXT( "VerificationHeading", "Verification" ) );
		Section.AddMenuEntry( FLevelEditorCommands::Get().MapCheck, LOCTEXT("OpenMapCheck", "Map Check") );
	}

#undef LOCTEXT_NAMESPACE
}

static void MakeMaterialQualityLevelMenu( UToolMenu* InMenu )
{
	{
		FToolMenuSection& Section = InMenu->AddSection("LevelEditorMaterialQualityLevel", NSLOCTEXT( "LevelToolBarViewMenu", "MaterialQualityLevelHeading", "Material Quality Level" ) );
		Section.AddMenuEntry(FLevelEditorCommands::Get().MaterialQualityLevel_Low);
		Section.AddMenuEntry(FLevelEditorCommands::Get().MaterialQualityLevel_Medium);
		Section.AddMenuEntry(FLevelEditorCommands::Get().MaterialQualityLevel_High);
	}
}

static void MakeShaderModelPreviewMenu( UToolMenu* InMenu )
{
#define LOCTEXT_NAMESPACE "LevelToolBarViewMenu"

	FToolMenuSection& Section = InMenu->AddSection("EditorPreviewMode", LOCTEXT("EditorPreviewModeDevices", "Preview Devices"));

	// SM5
	Section.AddMenuEntry(FLevelEditorCommands::Get().PreviewPlatformOverride_SM5);

	// Android
	bool bAndroidBuildForES31 = false;
	GConfig->GetBool(TEXT("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings"), TEXT("bBuildForES31"), bAndroidBuildForES31, GEngineIni);
	if (bAndroidBuildForES31)
	{
		Section.AddMenuEntry(FLevelEditorCommands::Get().PreviewPlatformOverride_AndroidGLES31);
	}

	bool bAndroidSupportsVulkan = false;
	GConfig->GetBool(TEXT("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings"), TEXT("bSupportsVulkan"), bAndroidSupportsVulkan, GEngineIni);
	if (bAndroidSupportsVulkan)
	{
		Section.AddMenuEntry(FLevelEditorCommands::Get().PreviewPlatformOverride_AndroidVulkanES31);
	}

	bool bAndroidBuildForES2 = false;
	GConfig->GetBool(TEXT("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings"), TEXT("bBuildForES2"), bAndroidBuildForES2, GEngineIni);
	if (bAndroidBuildForES2)
	{
    	Section.AddMenuEntry(FLevelEditorCommands::Get().PreviewPlatformOverride_AndroidGLES2);
	}

	// iOS
	bool bIOSSupportsMetal = false;
	GConfig->GetBool(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("bSupportsMetal"), bIOSSupportsMetal, GEngineIni);
	if (bIOSSupportsMetal)
	{
		Section.AddMenuEntry(FLevelEditorCommands::Get().PreviewPlatformOverride_IOSMetalES31);
	}

#undef LOCTEXT_NAMESPACE
}

static void MakeScalabilityMenu( UToolMenu* InMenu )
{
	{
		FToolMenuSection& Section = InMenu->AddSection("Section");
		Section.AddEntry(FToolMenuEntry::InitWidget("ScalabilitySettings", SNew(SScalabilitySettings), FText(), true));
	}
}

static void MakePreviewSettingsMenu( UToolMenu* InMenu )
{
#define LOCTEXT_NAMESPACE "LevelToolBarViewMenu"
	{
		FToolMenuSection& Section = InMenu->AddSection("LevelEditorPreview", LOCTEXT("PreviewHeading", "Previewing"));
		Section.AddMenuEntry(FLevelEditorCommands::Get().DrawBrushMarkerPolys);
		Section.AddMenuEntry(FLevelEditorCommands::Get().OnlyLoadVisibleInPIE);
		Section.AddMenuEntry(FLevelEditorCommands::Get().ToggleParticleSystemLOD);
		Section.AddMenuEntry(FLevelEditorCommands::Get().ToggleParticleSystemHelpers);
		Section.AddMenuEntry(FLevelEditorCommands::Get().ToggleFreezeParticleSimulation);
		Section.AddMenuEntry(FLevelEditorCommands::Get().ToggleLODViewLocking);
		Section.AddMenuEntry(FLevelEditorCommands::Get().LevelStreamingVolumePrevis);
	}
#undef LOCTEXT_NAMESPACE
}

#if WITH_LIVE_CODING
void FLevelEditorToolBar::RegisterCompileMenu()
{
#define LOCTEXT_NAMESPACE "LevelToolBarCompileMenu"

	UToolMenu* Menu = UToolMenus::Get()->RegisterMenu("LevelEditor.LevelEditorToolBar.CompileComboButton");

	{
		FToolMenuSection& Section = Menu->AddSection("LiveCodingMode", LOCTEXT( "LiveCodingMode", "General" ) );
		Section.AddMenuEntry( FLevelEditorCommands::Get().LiveCoding_Enable );
	}

	{
		FToolMenuSection& Section = Menu->AddSection("LiveCodingActions", LOCTEXT( "LiveCodingActions", "Actions" ) );
		Section.AddMenuEntry( FLevelEditorCommands::Get().LiveCoding_StartSession );
		Section.AddMenuEntry( FLevelEditorCommands::Get().LiveCoding_ShowConsole );
		Section.AddMenuEntry( FLevelEditorCommands::Get().LiveCoding_Settings );
	}

#undef LOCTEXT_NAMESPACE
}
#endif

TSharedRef< SWidget > FLevelEditorToolBar::GenerateQuickSettingsMenu(TSharedRef<FUICommandList> InCommandList, TWeakPtr<SLevelEditor> InLevelEditor)
{
	// Get all menu extenders for this context menu from the level editor module
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>( TEXT("LevelEditor") );
	TSharedPtr<FExtender> MenuExtender = LevelEditorModule.AssembleExtenders(InCommandList, LevelEditorModule.GetAllLevelEditorToolbarViewMenuExtenders());

	FToolMenuContext MenuContext(InCommandList, MenuExtender);
	ULevelEditorMenuContext* LevelEditorMenuContext = NewObject<ULevelEditorMenuContext>();
	LevelEditorMenuContext->LevelEditor = InLevelEditor;
	MenuContext.AddObject(LevelEditorMenuContext);

	return UToolMenus::Get()->GenerateWidget("LevelEditor.LevelEditorToolBar.LevelToolbarQuickSettings", MenuContext);
}

void FLevelEditorToolBar::RegisterQuickSettingsMenu()
{
#define LOCTEXT_NAMESPACE "LevelToolBarViewMenu"

	UToolMenu* Menu = UToolMenus::Get()->RegisterMenu("LevelEditor.LevelEditorToolBar.LevelToolbarQuickSettings");

	struct Local
	{
		static void OpenSettings(FName ContainerName, FName CategoryName, FName SectionName)
		{
			FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer(ContainerName, CategoryName, SectionName);
		}
	};

	{
		FToolMenuSection& Section = Menu->AddSection("ProjectSettingsSection", LOCTEXT("ProjectSettings", "Game Specific Settings"));

		Section.AddMenuEntry(FLevelEditorCommands::Get().WorldProperties);

		Section.AddMenuEntry(
			"ProjectSettings",
			LOCTEXT("ProjectSettingsMenuLabel", "Project Settings..."),
			LOCTEXT("ProjectSettingsMenuToolTip", "Change the settings of the currently loaded project"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "ProjectSettings.TabIcon"),
			FUIAction(FExecuteAction::CreateStatic(&Local::OpenSettings, FName("Project"), FName("Project"), FName("General")))
			);

		Section.AddDynamicEntry("PluginsEditor", FNewToolMenuDelegateLegacy::CreateLambda([](FMenuBuilder& InMenuBuilder, UToolMenu* InMenu)
		{
			if (IModularFeatures::Get().IsModularFeatureAvailable(EditorFeatures::PluginsEditor))
			{
				FGlobalTabmanager::Get()->PopulateTabSpawnerMenu(InMenuBuilder, "PluginsEditor");
			}
		}));
	}

	{
		FToolMenuSection& Section = Menu->AddSection("LevelEditorSelection", LOCTEXT("SelectionHeading","Selection") );
		Section.AddMenuEntry( FLevelEditorCommands::Get().AllowTranslucentSelection );
		Section.AddMenuEntry( FLevelEditorCommands::Get().AllowGroupSelection );
		Section.AddMenuEntry( FLevelEditorCommands::Get().StrictBoxSelect );
		Section.AddMenuEntry( FLevelEditorCommands::Get().TransparentBoxSelect );
		Section.AddMenuEntry( FLevelEditorCommands::Get().ShowTransformWidget );
	}

	{
		FToolMenuSection& Section = Menu->AddSection("LevelEditorScalability", LOCTEXT("ScalabilityHeading", "Scalability") );
		Section.AddSubMenu(
			"Scalability",
			LOCTEXT( "ScalabilitySubMenu", "Engine Scalability Settings" ),
			LOCTEXT( "ScalabilitySubMenu_ToolTip", "Open the engine scalability settings" ),
			FNewToolMenuDelegate::CreateStatic( &MakeScalabilityMenu ) );

		Section.AddSubMenu(
			"MaterialQualityLevel",
			LOCTEXT( "MaterialQualityLevelSubMenu", "Material Quality Level" ),
			LOCTEXT( "MaterialQualityLevelSubMenu_ToolTip", "Sets the value of the CVar \"r.MaterialQualityLevel\" (low=0, high=1, medium=2). This affects materials via the QualitySwitch material expression." ),
			FNewToolMenuDelegate::CreateStatic( &MakeMaterialQualityLevelMenu ) );

		Section.AddSubMenu(
			"FeatureLevelPreview",
			LOCTEXT("FeatureLevelPreviewSubMenu", "Preview Rendering Level"),
			LOCTEXT("FeatureLevelPreviewSubMenu_ToolTip", "Sets the rendering level used by the main editor"),
			FNewToolMenuDelegate::CreateStatic(&MakeShaderModelPreviewMenu));
	}

	{
		FToolMenuSection& Section = Menu->AddSection("LevelEditorAudio", LOCTEXT("AudioHeading", "Real Time Audio") );
		TSharedRef<SWidget> VolumeItem = SNew(SHorizontalBox)
											+SHorizontalBox::Slot()
											.FillWidth(0.9f)
											.Padding( FMargin(2.0f, 0.0f, 0.0f, 0.0f) )
											[
												SNew(SVolumeControl)
												.ToolTipText_Static(&FLevelEditorActionCallbacks::GetAudioVolumeToolTip)
												.Volume_Static(&FLevelEditorActionCallbacks::GetAudioVolume)
												.OnVolumeChanged_Static(&FLevelEditorActionCallbacks::OnAudioVolumeChanged)
												.Muted_Static(&FLevelEditorActionCallbacks::GetAudioMuted)
												.OnMuteChanged_Static(&FLevelEditorActionCallbacks::OnAudioMutedChanged)
											]
											+SHorizontalBox::Slot()
											.FillWidth(0.1f);

		Section.AddEntry(FToolMenuEntry::InitWidget("Volume", VolumeItem, LOCTEXT("VolumeControlLabel","Volume")));
	}

	{
		FToolMenuSection& Section = Menu->AddSection( "Snapping", LOCTEXT("SnappingHeading","Snapping") );
		Section.AddMenuEntry( FLevelEditorCommands::Get().EnableActorSnap );
		TSharedRef<SWidget> SnapItem = 
		SNew(SHorizontalBox)
	          +SHorizontalBox::Slot()
	          .FillWidth(0.9f)
	          [
		          SNew(SSlider)
		          .ToolTipText_Static(&FLevelEditorActionCallbacks::GetActorSnapTooltip)
		          .Value_Static(&FLevelEditorActionCallbacks::GetActorSnapSetting)
		          .OnValueChanged_Static(&FLevelEditorActionCallbacks::SetActorSnapSetting)
	          ]
	          +SHorizontalBox::Slot()
	          .FillWidth(0.1f);
		Section.AddEntry(FToolMenuEntry::InitWidget("Snap", SnapItem, LOCTEXT("ActorSnapLabel", "Distance")));

		Section.AddMenuEntry( FLevelEditorCommands::Get().ToggleSocketSnapping );
		Section.AddMenuEntry( FLevelEditorCommands::Get().EnableVertexSnap );
	}

	{
		FToolMenuSection& Section = Menu->AddSection("LevelEditorViewport", LOCTEXT("ViewportHeading", "Viewport") );
		Section.AddMenuEntry( FLevelEditorCommands::Get().ToggleHideViewportUI );

		Section.AddSubMenu( "Preview", LOCTEXT("PreviewMenu", "Previewing"), LOCTEXT("PreviewMenuTooltip","Game Preview Settings"), FNewToolMenuDelegate::CreateStatic( &MakePreviewSettingsMenu ) );
	}

#undef LOCTEXT_NAMESPACE
}


TSharedRef< SWidget > FLevelEditorToolBar::GenerateSourceControlMenu(TSharedRef<FUICommandList> InCommandList, TWeakPtr< SLevelEditor > InLevelEditor)
{
	// Get all menu extenders for this context menu from the level editor module
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
	TSharedPtr<FExtender> MenuExtender = LevelEditorModule.AssembleExtenders(InCommandList, LevelEditorModule.GetAllLevelEditorToolbarSourceControlMenuExtenders());

	FToolMenuContext MenuContext(InCommandList, MenuExtender);
	ULevelEditorMenuContext* LevelEditorMenuContext = NewObject<ULevelEditorMenuContext>();
	LevelEditorMenuContext->LevelEditor = InLevelEditor;
	MenuContext.AddObject(LevelEditorMenuContext);

	return UToolMenus::Get()->GenerateWidget("LevelEditor.LevelEditorToolBar.SourceControl", MenuContext);
}

TSharedRef< SWidget > FLevelEditorToolBar::GenerateEditorModesMenu(TSharedRef<FUICommandList> InCommandList, TWeakPtr<SLevelEditor> InLevelEditor)
{
	// Get all menu extenders for this context menu from the level editor module
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));

	FToolMenuContext MenuContext(InCommandList);
	ULevelEditorMenuContext* LevelEditorMenuContext = NewObject<ULevelEditorMenuContext>();
	LevelEditorMenuContext->LevelEditor = InLevelEditor;
	MenuContext.AddObject(LevelEditorMenuContext);

	return UToolMenus::Get()->GenerateWidget("LevelEditor.LevelEditorToolBar.EditorModes", MenuContext);
}

void FLevelEditorToolBar::RegisterSourceControlMenu()
{
#define LOCTEXT_NAMESPACE "LevelToolBarSourceControlMenu"

	UToolMenu* Menu = UToolMenus::Get()->RegisterMenu("LevelEditor.LevelEditorToolBar.SourceControl");
	Menu->bShouldCloseWindowAfterMenuSelection = true;
	FToolMenuSection& Section = Menu->AddSection("SourceControlActions", LOCTEXT("SourceControlMenuHeadingActions", "Actions"));

	Section.AddDynamicEntry("ConnectToSourceControl", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
	{
		ISourceControlModule& SourceControlModule = ISourceControlModule::Get();
		if (ISourceControlModule::Get().IsEnabled() && ISourceControlModule::Get().GetProvider().IsAvailable())
		{
			InSection.AddMenuEntry(
				FLevelEditorCommands::Get().ChangeSourceControlSettings, 
				TAttribute<FText>(),
				TAttribute<FText>(),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.Actions.ChangeSettings")
				);
		}
		else
		{
			InSection.AddMenuEntry(
				FLevelEditorCommands::Get().ConnectToSourceControl,
				TAttribute<FText>(),
				TAttribute<FText>(),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.Actions.Connect")
				);
		}
	}));

	Section.AddMenuSeparator("SourceControlConnectionSeparator");

	Section.AddMenuEntry(
		FLevelEditorCommands::Get().CheckOutModifiedFiles,
		TAttribute<FText>(),
		TAttribute<FText>(),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.Actions.CheckOut")
		);


	Section.AddMenuEntry(
		FLevelEditorCommands::Get().SubmitToSourceControl,
		TAttribute<FText>(),
		TAttribute<FText>(),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.Actions.Submit")
		);

#undef LOCTEXT_NAMESPACE
}

TSharedRef< SWidget > FLevelEditorToolBar::GenerateOpenBlueprintMenuContent( TSharedRef<FUICommandList> InCommandList, TWeakPtr< SLevelEditor > InLevelEditor )
{
#define LOCTEXT_NAMESPACE "LevelToolBarViewMenu"

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedPtr<FExtender> MenuExtender = FExtender::Combine(LevelEditorModule.GetAllLevelEditorToolbarBlueprintsMenuExtenders());

	FToolMenuContext MenuContext(InCommandList, MenuExtender);
	ULevelEditorMenuContext* LevelEditorMenuContext = NewObject<ULevelEditorMenuContext>();
	LevelEditorMenuContext->LevelEditor = InLevelEditor;
	MenuContext.AddObject(LevelEditorMenuContext);

	return UToolMenus::Get()->GenerateWidget("LevelEditor.LevelEditorToolBar.OpenBlueprint", MenuContext);
#undef LOCTEXT_NAMESPACE
}

void FLevelEditorToolBar::RegisterOpenBlueprintMenu()
{
#define LOCTEXT_NAMESPACE "LevelToolBarViewMenu"
	UToolMenu* Menu = UToolMenus::Get()->RegisterMenu("LevelEditor.LevelEditorToolBar.OpenBlueprint");

	struct FBlueprintMenus
	{
		/** Generates a sub-level Blueprints sub-menu */
		static void MakeSubLevelsMenu(UToolMenu* InMenu)
		{
			ULevelEditorMenuContext* Context = InMenu->FindContext<ULevelEditorMenuContext>();
			if (Context && Context->LevelEditor.IsValid())
			{
				FSlateIcon EditBP(FEditorStyle::Get().GetStyleSetName(), TEXT("LevelEditor.OpenLevelBlueprint"));

				{
					FToolMenuSection& Section = InMenu->AddSection("SubLevels", LOCTEXT("SubLevelsHeading", "Sub-Level Blueprints"));
					UWorld* World = Context->LevelEditor.Pin()->GetWorld();
					// Sort the levels alphabetically 
					TArray<ULevel*> SortedLevels = World->GetLevels();
					Algo::Sort(SortedLevels, LevelEditorActionHelpers::FLevelSortByName());

					for (ULevel* const Level : SortedLevels)
					{
						if (Level != NULL && Level->GetOutermost() != NULL && !Level->IsPersistentLevel())
						{
							FUIAction UIAction
							(
								FExecuteAction::CreateStatic(&FLevelEditorToolBar::OnOpenSubLevelBlueprint, Level)
							);

							FText DisplayName = FText::Format(LOCTEXT("SubLevelBlueprintItem", "Edit {0}"), FText::FromString(FPaths::GetCleanFilename(Level->GetOutermost()->GetName())));
							Section.AddMenuEntry(NAME_None, DisplayName, FText::GetEmpty(), EditBP, UIAction);
						}
					}
				}
			}
		}

		/** Handle BP being selected from popup picker */
		static void OnBPSelected(const struct FAssetData& AssetData)
		{
			UBlueprint* SelectedBP = Cast<UBlueprint>(AssetData.GetAsset());
			if(SelectedBP)
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(SelectedBP);
			}
		}


		/** Generates 'open blueprint' sub-menu */
		static void MakeOpenBPClassMenu(UToolMenu* InMenu)
		{
			FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

			// Configure filter for asset picker
			FAssetPickerConfig Config;
			Config.Filter.ClassNames.Add(UBlueprint::StaticClass()->GetFName());
			Config.InitialAssetViewType = EAssetViewType::List;
			Config.OnAssetSelected = FOnAssetSelected::CreateStatic(&FBlueprintMenus::OnBPSelected);
			Config.bAllowDragging = false;
			// Don't show stuff in Engine
			Config.Filter.PackagePaths.Add("/Game");
			Config.Filter.bRecursivePaths = true;

			TSharedRef<SWidget> Widget = 
				SNew(SBox)
				.WidthOverride(300.f)
				.HeightOverride(300.f)
				[
					ContentBrowserModule.Get().CreateAssetPicker(Config)
				];
		

			{
				FToolMenuSection& Section = InMenu->AddSection("Browse", LOCTEXT("BrowseHeader", "Browse"));
				Section.AddEntry(FToolMenuEntry::InitWidget("PickClassWidget", Widget, FText::GetEmpty()));
			}
		}
	};

	{
		FToolMenuSection& Section = Menu->AddSection("BlueprintClass", LOCTEXT("BlueprintClass", "Blueprint Class"));

		// Create a blank BP
		Section.AddMenuEntry(FLevelEditorCommands::Get().CreateBlankBlueprintClass);

		// Convert selection to BP
		Section.AddMenuEntry(FLevelEditorCommands::Get().ConvertSelectionToBlueprintViaHarvest);
		Section.AddMenuEntry(FLevelEditorCommands::Get().ConvertSelectionToBlueprintViaSubclass);

		// Open an existing Blueprint Class...
		FSlateIcon OpenBPIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.OpenClassBlueprint");
		Section.AddSubMenu(
			"OpenBlueprintClass",
			LOCTEXT("OpenBlueprintClassSubMenu", "Open Blueprint Class..."),
			LOCTEXT("OpenBlueprintClassSubMenu_ToolTip", "Open an existing Blueprint Class in this project"),
			FNewToolMenuDelegate::CreateStatic(&FBlueprintMenus::MakeOpenBPClassMenu),
			false,
			OpenBPIcon);
	}

	{
		FToolMenuSection& Section = Menu->AddSection("LevelScriptBlueprints", LOCTEXT("LevelScriptBlueprints", "Level Blueprints"));
		Section.AddMenuEntry( FLevelEditorCommands::Get().OpenLevelBlueprint );

		Section.AddDynamicEntry("SubLevels", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
		{
			ULevelEditorMenuContext* Context = InSection.FindContext<ULevelEditorMenuContext>();
			if (Context && Context->LevelEditor.IsValid())
			{
				// If there are any sub-levels, display the sub-menu. A single level means there is only the persistent level
				UWorld* World = Context->LevelEditor.Pin()->GetWorld();
				if (World->GetNumLevels() > 1)
				{
					InSection.AddSubMenu(
						"SubLevels",
						LOCTEXT("SubLevelsSubMenu", "Sub-Levels"),
						LOCTEXT("SubLevelsSubMenu_ToolTip", "Shows available sub-level Blueprints that can be edited."),
						FNewToolMenuDelegate::CreateStatic(&FBlueprintMenus::MakeSubLevelsMenu),
						FUIAction(), EUserInterfaceActionType::Button, false, FSlateIcon(FEditorStyle::Get().GetStyleSetName(), TEXT("LevelEditor.OpenLevelBlueprint")));
				}
			}
		}));
	}

	{
		FToolMenuSection& Section = Menu->AddSection("ProjectSettingsClasses", LOCTEXT("ProjectSettingsClasses", "Project Settings"));
		LevelEditorActionHelpers::CreateGameModeSubMenu(Section, "ProjectSettingsClasses", true);
	}

	{
		FToolMenuSection& Section = Menu->AddSection("WorldSettingsClasses", LOCTEXT("WorldSettingsClasses", "World Override"));
		LevelEditorActionHelpers::CreateGameModeSubMenu(Section, "WorldSettingsClasses", false);
	}

	// If source control is enabled, queue up a query to the status of the config file so it is (hopefully) ready before we get to the sub-menu
	if(ISourceControlModule::Get().IsEnabled())
	{
		FString ConfigFilePath = FPaths::ConvertRelativePathToFull(FString::Printf(TEXT("%sDefaultEngine.ini"), *FPaths::SourceConfigDir()));

		// note: calling QueueStatusUpdate often does not spam status updates as an internal timer prevents this
		ISourceControlModule::Get().QueueStatusUpdate(ConfigFilePath);
	}
#undef LOCTEXT_NAMESPACE
}

void FLevelEditorToolBar::OnOpenSubLevelBlueprint( ULevel* InLevel )
{
	ULevelScriptBlueprint* LevelScriptBlueprint = InLevel->GetLevelScriptBlueprint();

	if( LevelScriptBlueprint )
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(LevelScriptBlueprint);
	}
	else
	{
		FMessageDialog::Open( EAppMsgType::Ok, NSLOCTEXT("UnrealEd", "UnableToCreateLevelScript", "Unable to find or create a level blueprint for this level.") );
	}
}

TSharedRef< SWidget > FLevelEditorToolBar::GenerateCinematicsMenuContent(TSharedRef<FUICommandList> InCommandList, TWeakPtr<SLevelEditor> InLevelEditor)
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");

	FToolMenuContext MenuContext(InCommandList, FExtender::Combine(LevelEditorModule.GetAllLevelEditorToolbarCinematicsMenuExtenders()));
	ULevelEditorMenuContext* LevelEditorMenuContext = NewObject<ULevelEditorMenuContext>();
	LevelEditorMenuContext->LevelEditor = InLevelEditor;
	MenuContext.AddObject(LevelEditorMenuContext);

	return UToolMenus::Get()->GenerateWidget("LevelEditor.LevelEditorToolBar.Cinematics", MenuContext);
}

void FLevelEditorToolBar::RegisterCinematicsMenu()
{
#define LOCTEXT_NAMESPACE "LevelToolBarCinematicsMenu"

	UToolMenu* Menu = UToolMenus::Get()->RegisterMenu("LevelEditor.LevelEditorToolBar.Cinematics");
	Menu->bShouldCloseWindowAfterMenuSelection = true;

	Menu->AddSection("LevelEditorNewCinematics", LOCTEXT("CinematicsMenuCombo_NewHeading", "New"));

	//Add a heading to separate the existing cinematics from the 'Add New Cinematic Actor' button
	FToolMenuSection& ExistingCinematicSection = Menu->AddSection("LevelEditorExistingCinematic", LOCTEXT("CinematicMenuCombo_ExistingHeading", "Edit Existing Cinematic"));
	ExistingCinematicSection.AddDynamicEntry("LevelEditorExistingCinematic", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
	{
		ULevelEditorMenuContext* FoundContext = InSection.Context.FindContext<ULevelEditorMenuContext>();
		if (!FoundContext)
		{
			return;
		}

		const int32 bAllowMatineeActors = CVarAllowMatineeActors->GetInt();

		UWorld* World = FoundContext->LevelEditor.IsValid() ? FoundContext->LevelEditor.Pin()->GetWorld() : nullptr;
		const bool bHasAnyCinematicsActors = (bAllowMatineeActors && !!TActorIterator<AMatineeActor>(World)) || !!TActorIterator<ALevelSequenceActor>(World);
		if (!bHasAnyCinematicsActors)
		{
			return;
		}

		using namespace SceneOutliner;

		// We can't build a list of Matinees and LevelSequenceActors while the current World is a PIE world.
		FInitializationOptions InitOptions;
		{
			InitOptions.Mode = ESceneOutlinerMode::ActorPicker;

			// We hide the header row to keep the UI compact.
			// @todo: Might be useful to have this sometimes, actually.  Ideally the user could summon it.
			InitOptions.bShowHeaderRow = false;
			InitOptions.bShowSearchBox = false;
			InitOptions.bShowCreateNewFolder = false;

			InitOptions.ColumnMap.Add(FBuiltInColumnTypes::Label(), FColumnInfo(EColumnVisibility::Visible, 0));
			InitOptions.ColumnMap.Add(FBuiltInColumnTypes::ActorInfo(), FColumnInfo(EColumnVisibility::Visible, 10));

			// Only display Matinee and MovieScene actors
			auto ActorFilter = [&](const AActor* Actor) {
				return (bAllowMatineeActors && Actor->IsA(AMatineeActor::StaticClass())) || Actor->IsA(ALevelSequenceActor::StaticClass());
			};
			InitOptions.Filters->AddFilterPredicate(FActorFilterPredicate::CreateLambda(ActorFilter));
		}

		// actor selector to allow the user to choose an actor
		FSceneOutlinerModule& SceneOutlinerModule = FModuleManager::LoadModuleChecked<FSceneOutlinerModule>("SceneOutliner");
		TSharedRef< SWidget > MiniSceneOutliner =
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.MaxHeight(400.0f)
			[
				SceneOutlinerModule.CreateSceneOutliner(
					InitOptions,
					FOnActorPicked::CreateStatic(&FLevelEditorToolBar::OnCinematicsActorPicked))
			];

		InSection.AddEntry(FToolMenuEntry::InitWidget("LevelEditorExistingCinematic", MiniSceneOutliner, FText::GetEmpty(), true));
	}));

#undef LOCTEXT_NAMESPACE
}

void FLevelEditorToolBar::RegisterEditorModesMenu()
{
#define LOCTEXT_NAMESPACE "LevelToolBarEditorModesMenu"
	UToolMenu* Menu = UToolMenus::Get()->RegisterMenu("LevelEditor.LevelEditorToolBar.EditorModes");

	FToolMenuSection& Section = Menu->AddSection("EditorModes", LOCTEXT("EditorModesMenu_NewHeading", "Editor Modes"));

	Section.AddDynamicEntry("ModesList", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
	{

		const FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
		const FLevelEditorModesCommands& Commands = LevelEditorModule.GetLevelEditorModesCommands();

		TArray<FEditorModeInfo, TInlineAllocator<1>> DefaultModes;

		TArray<FEditorModeInfo, TInlineAllocator<10>> NonDefaultModes;

		for (const FEditorModeInfo& Mode : FEditorModeRegistry::Get().GetSortedModeInfo())
		{
			// If the mode isn't visible don't create a menu option for it.
			if (!Mode.bVisible)
			{
				continue;
			}

			if (GLevelEditorModeTools().IsDefaultMode(Mode.ID))
			{
				DefaultModes.Add(Mode.ID);
			}
			else
			{
				NonDefaultModes.Add(Mode.ID);
			}
			
		}

		auto BuildEditorModes = 
			[&Commands, &InSection](const TArrayView<FEditorModeInfo>& Modes)
			{
				for (const FEditorModeInfo& Mode : Modes)
				{
					FName EditorModeCommandName = FName(*(FString("EditorMode.") + Mode.ID.ToString()));

					TSharedPtr<FUICommandInfo> EditorModeCommand =
						FInputBindingManager::Get().FindCommandInContext(Commands.GetContextName(), EditorModeCommandName);

					// If a command isn't yet registered for this mode, we need to register one.
					if (!EditorModeCommand.IsValid())
					{
						continue;
					}

					InSection.AddMenuEntry(EditorModeCommand);
				}

			};

		// Build Default Modes first
		BuildEditorModes(MakeArrayView(DefaultModes));

		InSection.AddMenuSeparator(NAME_None);
			
		// Build non-default modes second
		BuildEditorModes(MakeArrayView(NonDefaultModes));

	}));

#undef LOCTEXT_NAMESPACE
}


void FLevelEditorToolBar::OnCinematicsActorPicked( AActor* Actor )
{
	//The matinee editor will not tick unless the editor viewport is in realtime mode.
	//the scene outliner eats input, so we must close any popups manually.
	FSlateApplication::Get().DismissAllMenus();

	// Make sure we dismiss the menus before we open this
	if (AMatineeActor* MatineeActor = Cast<AMatineeActor>(Actor))
	{
		// Open Matinee for editing!
		GEditor->OpenMatinee( MatineeActor );
	}
	else if (ALevelSequenceActor* LevelSequenceActor = Cast<ALevelSequenceActor>(Actor))
	{
		FScopedSlowTask SlowTask(1.f, NSLOCTEXT("LevelToolBarCinematicsMenu", "LoadSequenceSlowTask", "Loading Level Sequence..."));
		SlowTask.MakeDialog();
		SlowTask.EnterProgressFrame();
		UObject* Asset = LevelSequenceActor->LevelSequence.TryLoad();

		if (Asset != nullptr)
		{
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Asset);
		}
	}
}
