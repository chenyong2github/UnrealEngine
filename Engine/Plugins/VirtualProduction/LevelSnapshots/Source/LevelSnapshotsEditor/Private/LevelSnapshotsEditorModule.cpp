// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSnapshotsEditorModule.h"

#include "Settings/LevelSnapshotsEditorProjectSettings.h"
#include "NegatableFilter.h"
#include "NegatableFilterDetailsCustomization.h"
#include "LevelSnapshotsEditorCommands.h"
#include "LevelSnapshotsEditorStyle.h"
#include "LevelSnapshotsEditorData.h"
#include "LevelSnapshotsFunctionLibrary.h"
#include "LevelSnapshotsUserSettings.h"
#include "SLevelSnapshotsEditorCreationForm.h"
#include "Toolkits/LevelSnapshotsEditorToolkit.h"

#include "AssetToolsModule.h"
#include "AssetTypeActions/AssetTypeActions_LevelSnapshot.h"
#include "IAssetTools.h"
#include "ISettingsModule.h"
#include "LevelEditor.h"
#include "LevelSnapshotsEditorFunctionLibrary.h"
#include "Editor/MainFrame/Private/Menus/SettingsMenu.h"
#include "ToolMenus.h"
#include "ToolMenuSection.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/ScopeExit.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "FLevelSnapshotsEditorModule"

FLevelSnapshotsEditorModule& FLevelSnapshotsEditorModule::Get()
{
	return FModuleManager::GetModuleChecked<FLevelSnapshotsEditorModule>("LevelSnapshotsEditor");
}

void FLevelSnapshotsEditorModule::StartupModule()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	AssetTools.RegisterAssetTypeActions(MakeShared<FAssetTypeActions_LevelSnapshot>());

	FLevelSnapshotsEditorStyle::Initialize();
	FLevelSnapshotsEditorCommands::Register();
	
	RegisterMenus();
	
	if (RegisterProjectSettings() && ProjectSettingsObjectPtr->bEnableLevelSnapshotsToolbarButton)
	{
		RegisterEditorToolbar();
	}

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout( UNegatableFilter::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateLambda( []()
	{
		return MakeShared<FNegatableFilterDetailsCustomization>();
	}));
}

void FLevelSnapshotsEditorModule::ShutdownModule()
{
	UToolMenus::UnregisterOwner(this);

	FLevelSnapshotsEditorStyle::Shutdown();

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.UnregisterCustomClassLayout(UNegatableFilter::StaticClass()->GetFName());
	
	FLevelSnapshotsEditorCommands::Unregister();

	// Unregister project settings
	ISettingsModule& SettingsModule = FModuleManager::LoadModuleChecked<ISettingsModule>("Settings");
	{
		SettingsModule.UnregisterSettings("Project", "Plugins", "Level Snapshots");
	}
}

bool FLevelSnapshotsEditorModule::GetUseCreationForm() const
{
	if (ensureMsgf(ProjectSettingsObjectPtr.IsValid(), 
		TEXT("ProjectSettingsObjectPtr was not valid. Returning false for bUseCreationForm. Check to ensure that Project Settings have been registered for LevelSnapshots.")))
	{
		return ProjectSettingsObjectPtr.Get()->bUseCreationForm;
	}
	
	return false;
}

void FLevelSnapshotsEditorModule::SetUseCreationForm(bool bInUseCreationForm)
{
	if (ensureMsgf(ProjectSettingsObjectPtr.IsValid(),
		TEXT("ProjectSettingsObjectPtr was not valid. Returning false for bUseCreationForm. Check to ensure that Project Settings have been registered for LevelSnapshots.")))
	{
		ProjectSettingsObjectPtr.Get()->bUseCreationForm = bInUseCreationForm;
	}
}

void FLevelSnapshotsEditorModule::RegisterMenus()
{
	if (FSlateApplication::IsInitialized())
	{
		FToolMenuOwnerScoped OwnerScoped(this);
		UToolMenu* Menu = UToolMenus::Get()->RegisterMenu("MainFrame.MainMenu.Window");
		FToolMenuSection& Section = Menu->AddSection("ExperimentalTabSpawners", NSLOCTEXT("LevelSnapshots", "ExperimentalTabSpawnersHeading", "Experimental"), FToolMenuInsert("WindowGlobalTabSpawners", EToolMenuInsertType::After));
		Section.AddMenuEntry("OpenLevelSnapshotsEditor", NSLOCTEXT("LevelSnapshots", "LevelSnapshotsEditor", "Level Snapshots Editor"), FText(), FSlateIcon(), FUIAction(FExecuteAction::CreateRaw(this, &FLevelSnapshotsEditorModule::OpenSnapshotsEditor)));
	}
}

bool FLevelSnapshotsEditorModule::RegisterProjectSettings()
{
	ISettingsModule& SettingsModule = FModuleManager::LoadModuleChecked<ISettingsModule>("Settings");
	{
		ProjectSettingsSectionPtr = SettingsModule.RegisterSettings("Project", "Plugins", "Level Snapshots",
			NSLOCTEXT("LevelSnapshots", "LevelSnapshotsSettingsCategoryDisplayName", "Level Snapshots"),
			NSLOCTEXT("LevelSnapshots", "LevelSnapshotsSettingsDescription", "Configure the Level Snapshots settings"),
			GetMutableDefault<ULevelSnapshotsEditorProjectSettings>());

		if (ProjectSettingsSectionPtr.IsValid() && ProjectSettingsSectionPtr->GetSettingsObject().IsValid())
		{
			ProjectSettingsObjectPtr = Cast<ULevelSnapshotsEditorProjectSettings>(ProjectSettingsSectionPtr->GetSettingsObject());

			ProjectSettingsSectionPtr->OnModified().BindRaw(this, &FLevelSnapshotsEditorModule::HandleModifiedProjectSettings);
		}
	}

	return ProjectSettingsObjectPtr.IsValid();
}

bool FLevelSnapshotsEditorModule::HandleModifiedProjectSettings()
{
	if (ensureMsgf(ProjectSettingsObjectPtr.IsValid(),
		TEXT("ProjectSettingsObjectPtr was not valid. Check to ensure that Project Settings have been registered for LevelSnapshots.")))
	{
		ProjectSettingsObjectPtr->ValidateRootLevelSnapshotSaveDirAsGameContentRelative();
		ProjectSettingsObjectPtr->SanitizeAllProjectSettingsPaths(true);
	}
	
	return true;
}

void FLevelSnapshotsEditorModule::RegisterEditorToolbar()
{
	if (IsRunningGame())
	{
		return;
	}
	
	// Get the Level Editor so we can insert our combo button into the Level Editor's toolbar
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	
	MapEditorToolbarActions();

	// Create a toolbar extension instance that will insert our toolbar button after the 'Settings' horizontal box in the toolbar
	TSharedPtr<FExtender> NewToolbarExtender = MakeShareable(new FExtender);
	NewToolbarExtender->AddToolBarExtension("Settings",
		EExtensionHook::After,
		EditorToolbarButtonCommandList,
		FToolBarExtensionDelegate::CreateRaw(this, &FLevelSnapshotsEditorModule::CreateEditorToolbarButton));
	
	// Now insert the button into the main toolbar using the extension
	LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(NewToolbarExtender);
}

void FLevelSnapshotsEditorModule::MapEditorToolbarActions()
{
	EditorToolbarButtonCommandList = MakeShareable(new FUICommandList);

	EditorToolbarButtonCommandList->MapAction(
		FLevelSnapshotsEditorCommands::Get().UseCreationFormToggle,
		FUIAction(
			FExecuteAction::CreateRaw(this, &FLevelSnapshotsEditorModule::ToggleUseCreationForm),
			FCanExecuteAction(),
			FIsActionChecked::CreateRaw(this, &FLevelSnapshotsEditorModule::GetUseCreationForm)
		)
	);

	EditorToolbarButtonCommandList->MapAction(
		FLevelSnapshotsEditorCommands::Get().OpenLevelSnapshotsEditor,
		FExecuteAction::CreateRaw(this, &FLevelSnapshotsEditorModule::OpenSnapshotsEditor)
	);

	EditorToolbarButtonCommandList->MapAction(
		FLevelSnapshotsEditorCommands::Get().LevelSnapshotsSettings,
		FExecuteAction::CreateStatic(&FLevelSnapshotsEditorModule::OpenLevelSnapshotsSettings)
	);
}

void FLevelSnapshotsEditorModule::CreateEditorToolbarButton(FToolBarBuilder& Builder)
{
	Builder.AddToolBarButton(
		FUIAction(FExecuteAction::CreateRaw(this, &FLevelSnapshotsEditorModule::BuildPathsToSaveSnapshotWithOptionalForm)),
		NAME_None,
		NSLOCTEXT("LevelSnapshots", "LevelSnapshots", "Level Snapshots"), // Set Text under image
		NSLOCTEXT("LevelSnapshots", "LevelSnapshotsToolbarButtonTooltip", "Take snapshot with optional form"), //  Set tooltip
		FSlateIcon(FLevelSnapshotsEditorStyle::GetStyleSetName(), "LevelSnapshots.ToolbarButton") // Set image
	);
	
	Builder.AddComboButton(
		FUIAction(),
		FOnGetContent::CreateRaw(this, &FLevelSnapshotsEditorModule::FillEditorToolbarComboButtonMenuOptions, EditorToolbarButtonCommandList), // Add combo button subcommands menu
		FText::GetEmpty(),
		NSLOCTEXT("LevelSnapshots", "LevelSnapshotsToolbarComboButtonTooltip", "Open Level Snapshots Options") //  Set tooltip
	); 
}

TSharedRef<SWidget> FLevelSnapshotsEditorModule::FillEditorToolbarComboButtonMenuOptions(TSharedPtr<class FUICommandList> Commands)
{
	// Create FMenuBuilder instance for the commands we created
	FMenuBuilder MenuBuilder(true, Commands);

	// Then use it to add entries to the submenu of the combo button
	MenuBuilder.BeginSection("Creation", NSLOCTEXT("LevelSnapshots", "Creation", "Creation"));
	MenuBuilder.AddMenuEntry(FLevelSnapshotsEditorCommands::Get().UseCreationFormToggle);
	MenuBuilder.EndSection();
	MenuBuilder.AddMenuSeparator();
	MenuBuilder.AddMenuEntry(FLevelSnapshotsEditorCommands::Get().OpenLevelSnapshotsEditor);
	MenuBuilder.AddMenuEntry(FLevelSnapshotsEditorCommands::Get().LevelSnapshotsSettings);

	// Create the widget so it can be attached to the combo button
	return MenuBuilder.MakeWidget();
}

void FLevelSnapshotsEditorModule::BuildPathsToSaveSnapshotWithOptionalForm() const
{	
	check(ProjectSettingsObjectPtr.IsValid());

	// Creation Form

	if (ProjectSettingsObjectPtr.Get()->bUseCreationForm)
	{
		TSharedRef<SWidget> CreationForm = 
			FLevelSnapshotsEditorCreationForm::MakeAndShowCreationWindow(
				FCloseCreationFormDelegate::CreateRaw(this, &FLevelSnapshotsEditorModule::HandleFormReply), ProjectSettingsObjectPtr.Get());
	}
	else
	{
		TakeAndSaveSnapshot(FText::GetEmpty());
	}
}

void FLevelSnapshotsEditorModule::HandleFormReply(bool bWasCreateSnapshotPressed, FText InDescription) const
{
	if (bWasCreateSnapshotPressed)
	{
		TakeAndSaveSnapshot(InDescription, true);
	}
}

void FLevelSnapshotsEditorModule::TakeAndSaveSnapshot(const FText& InDescription, const bool bShouldUseOverrides) const
{
	if (!ensure(GEditor))
	{
		return;
	}
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!ensure(World && ProjectSettingsObjectPtr.IsValid()))
	{
		return;
	}
	ULevelSnapshotsEditorProjectSettings* ProjectSettings = ProjectSettingsObjectPtr.Get();


	// Notify the user that a snapshot is being created
	FNotificationInfo Notification(NSLOCTEXT("LevelSnapshots", "NotificationFormatText_CreatingSnapshot", "Creating Level Snapshot"));
	Notification.Image = FLevelSnapshotsEditorStyle::GetBrush(TEXT("LevelSnapshots.ToolbarButton"));
	Notification.bUseThrobber = true;
	Notification.bUseSuccessFailIcons = true;
	Notification.ExpireDuration = 2.f;
	Notification.bFireAndForget = false;

	TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(Notification);
	NotificationItem->SetCompletionState(SNotificationItem::CS_Pending);
	ON_SCOPE_EXIT
	{
		NotificationItem->ExpireAndFadeout();
	};

	
	ProjectSettings->ValidateRootLevelSnapshotSaveDirAsGameContentRelative();
	ProjectSettings->SanitizeAllProjectSettingsPaths(true);

	const FFormatNamedArguments& FormatArguments = ULevelSnapshotsEditorProjectSettings::GetFormatNamedArguments(World->GetName());
	const FText& NewSnapshotDir = FText::Format(FText::FromString(
		bShouldUseOverrides && ProjectSettings->IsPathOverridden() ? 
		ProjectSettings->GetSaveDirOverride() : ProjectSettings->LevelSnapshotSaveDir), FormatArguments);
	const FText& NewSnapshotName = FText::Format(FText::FromString(
		bShouldUseOverrides && ProjectSettings->IsNameOverridden() ?
		ProjectSettings->GetNameOverride() : ProjectSettings->DefaultLevelSnapshotName), FormatArguments);

	const FString& ValidatedName = FPaths::MakeValidFileName(NewSnapshotName.ToString());
	const FString& PathToSavePackage = FPaths::Combine(ProjectSettings->RootLevelSnapshotSaveDir.Path, NewSnapshotDir.ToString(), ValidatedName);


	// Take snapshot
	UPackage* Package = CreatePackage(*PathToSavePackage);
	ULevelSnapshot* Snapshot = ULevelSnapshotsFunctionLibrary::TakeLevelSnapshot_Internal(World, *ValidatedName, Package, InDescription.ToString());
	if (!ensure(Snapshot))
	{
		return;
	}

	
	// Take screenshot before we save
	const FString& PackageFileName = FPackageName::LongPackageNameToFilename(PathToSavePackage, FPackageName::GetAssetPackageExtension());
	const bool bPathIsValid = FPaths::ValidatePath(PackageFileName);
	if (bPathIsValid)
	{
		ULevelSnapshotsEditorFunctionLibrary::GenerateThumbnailForSnapshotAsset(Snapshot);
	}

	
	// Notify the user of the outcome
	if (bPathIsValid && UPackage::SavePackage(Package, Snapshot, RF_Public | RF_Standalone, *PackageFileName))
	{
		// If successful
		NotificationItem->SetText(
			FText::Format(
				NSLOCTEXT("LevelSnapshots", "NotificationFormatText_CreateSnapshotSuccess", "Successfully created Level Snapshot \"{0}\""), NewSnapshotName));
		NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
	}
	else
	{
		NotificationItem->SetText(
			FText::Format(
				NSLOCTEXT("LevelSnapshots", "NotificationFormatText_CreateSnapshotSuccess", "Failed to create Level Snapshot \"{0}\". Check the file name."), NewSnapshotName));
		NotificationItem->SetCompletionState(SNotificationItem::CS_Fail);
	}
}

void FLevelSnapshotsEditorModule::OpenSnapshotsEditor()
{
	if (SnapshotEditorToolkit.IsValid())
	{
		SnapshotEditorToolkit.Pin()->BringToolkitToFront();
	}
	else
	{
		ULevelSnapshotsEditorData* EditingObject = AllocateTransientPreset();
		SnapshotEditorToolkit = FLevelSnapshotsEditorToolkit::CreateSnapshotEditor(EditingObject);
	}
}

void FLevelSnapshotsEditorModule::OpenLevelSnapshotsSettings()
{
	FSettingsMenu::OpenSettings("Project", "Plugins", "Level Snapshots");
}

ULevelSnapshotsEditorData* FLevelSnapshotsEditorModule::AllocateTransientPreset()
{
	static const TCHAR* PackageName = TEXT("/Temp/LevelSnapshots/PendingSnapshots");

	ULevelSnapshotsEditorData* ExistingPreset = FindObject<ULevelSnapshotsEditorData>(nullptr, TEXT("/Temp/LevelSnapshots/PendingSnapshots.PendingSnapshots"));
	if (ExistingPreset)
	{
		return ExistingPreset;
	}

	ULevelSnapshotsEditorData* TemplatePreset = GetDefault<ULevelSnapshotsUserSettings>()->LastEditorData.Get();

	static FName DesiredName = "PendingSnapshots";

	UPackage* NewPackage = CreatePackage(PackageName);
	NewPackage->SetFlags(RF_Transient);
	NewPackage->AddToRoot();

	ULevelSnapshotsEditorData* NewPreset = nullptr;

	if (TemplatePreset)
	{
		NewPreset = DuplicateObject<ULevelSnapshotsEditorData>(TemplatePreset, NewPackage, DesiredName);
		NewPreset->SetFlags(RF_Transient | RF_Transactional | RF_Standalone);
	}
	else
	{
		NewPreset = NewObject<ULevelSnapshotsEditorData>(NewPackage, DesiredName, RF_Transient | RF_Transactional | RF_Standalone);
	}

	return NewPreset;
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FLevelSnapshotsEditorModule, LevelSnapshotsEditor)
