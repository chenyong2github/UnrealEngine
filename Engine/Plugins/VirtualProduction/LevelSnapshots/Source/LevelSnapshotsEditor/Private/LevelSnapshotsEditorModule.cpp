// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSnapshotsEditorModule.h"

#include "LevelSnapshotsEditorCommands.h"
#include "LevelSnapshotsEditorStyle.h"
#include "LevelSnapshotsEditorData.h"
#include "LevelSnapshotsUserSettings.h"
#include "Toolkits/LevelSnapshotsEditorToolkit.h"

#include "AssetToolsModule.h"
#include "AssetTypeActions/AssetTypeActions_LevelSnapshot.h"
#include "IAssetTools.h"
#include "ToolMenus.h"
#include "ToolMenuSection.h"

#define LOCTEXT_NAMESPACE "FLevelSnapshotsEditorModule"

void FLevelSnapshotsEditorModule::StartupModule()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	AssetTools.RegisterAssetTypeActions(MakeShared<FAssetTypeActions_LevelSnapshot>());

	RegisterMenus();

	FLevelSnapshotsEditorStyle::Initialize();
	FLevelSnapshotsEditorCommands::Register();
}

void FLevelSnapshotsEditorModule::ShutdownModule()
{
	UToolMenus::UnregisterOwner(this);

	FLevelSnapshotsEditorStyle::Shutdown();
}

void FLevelSnapshotsEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);
	UToolMenu* Menu = UToolMenus::Get()->RegisterMenu("MainFrame.MainMenu.Window");
	FToolMenuSection& Section = Menu->AddSection("ExperimentalTabSpawners", LOCTEXT("ExperimentalTabSpawnersHeading", "Experimental"), FToolMenuInsert("WindowGlobalTabSpawners", EToolMenuInsertType::After));
	Section.AddMenuEntry("CreateNDisplayEditor", LOCTEXT("LevelSnapshotsEditor", "Level Snapshots Editor"), FText(), FSlateIcon(), FUIAction(FExecuteAction::CreateRaw(this, &FLevelSnapshotsEditorModule::CreateLevelSnapshotsEditor)));
}

void FLevelSnapshotsEditorModule::CreateLevelSnapshotsEditor()
{
	ULevelSnapshotsEditorData* EditingObject = AllocateTransientPreset();
	TSharedRef<FLevelSnapshotsEditorToolkit> Toolkit = MakeShared<FLevelSnapshotsEditorToolkit>();
	Toolkit->Initialize(EToolkitMode::Standalone, TSharedPtr<IToolkitHost>(), EditingObject);
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