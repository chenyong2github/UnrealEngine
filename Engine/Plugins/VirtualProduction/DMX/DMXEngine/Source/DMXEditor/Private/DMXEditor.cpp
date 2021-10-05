// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXEditor.h"
#include "DMXEditorLog.h"
#include "DMXEditorModule.h"
#include "DMXEditorTabNames.h"
#include "DMXEditorUtils.h"
#include "DMXFixtureTypeSharedData.h"
#include "DMXFixturePatchSharedData.h"
#include "Library/DMXLibrary.h"
#include "Library/DMXEntityFixtureType.h"
#include "Library/DMXEntityFixturePatch.h"
#include "LibraryEditorTab/SDMXLibraryEditorTab.h"
#include "Modes/DMXEditorApplicationMode.h"
#include "Toolbars/DMXEditorToolbar.h"
#include "Commands/DMXEditorCommands.h"

#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Docking/SDockTab.h"

#include "Widgets/SDMXEntityEditor.h"
#include "Widgets/SDMXEntityInspector.h"
#include "Widgets/FixturePatch/SDMXFixturePatchEditor.h"
#include "Widgets/FixtureType/SDMXFixtureTypeEditor.h"
#include "Widgets/OutputConsole/SDMXOutputConsole.h"

#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"


#define LOCTEXT_NAMESPACE "FDMXEditor"

const FName FDMXEditor::ToolkitFName(TEXT("DMXEditor"));

FName FDMXEditor::GetToolkitFName() const
{
	return ToolkitFName;
}

FText FDMXEditor::GetBaseToolkitName() const
{
	return LOCTEXT("DMXEditor", "DMX Editor");
}

FString FDMXEditor::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix_LevelScript", "Script ").ToString();
}

FLinearColor FDMXEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.0f, 0.0f, 0.3f, 0.5f);
}

void FDMXEditor::InitEditor(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost, UDMXLibrary* DMXLibrary)
{
	if (!Toolbar.IsValid())
	{
		Toolbar = MakeShared<FDMXEditorToolbar>(SharedThis(this));
	}

	if (!FixtureTypeSharedData.IsValid())
	{
		FixtureTypeSharedData = MakeShared<FDMXFixtureTypeSharedData>(SharedThis(this));
	}
	
	if (!FixturePatchSharedData.IsValid())
	{
		FixturePatchSharedData = MakeShared<FDMXFixturePatchSharedData>(SharedThis(this));
	}

	// Initialize the asset editor and spawn nothing (dummy layout)
	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = true;
	const TSharedRef<FTabManager::FLayout> DummyLayout = FTabManager::NewLayout("NullLayout")->AddArea(FTabManager::NewPrimaryArea());
	FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, FDMXEditorModule::DMXEditorAppIdentifier, DummyLayout, bCreateDefaultStandaloneMenu, bCreateDefaultToolbar, (UObject*)DMXLibrary);

	CommonInitialization(DMXLibrary);

	InitalizeExtenders();

	RegenerateMenusAndToolbars();

	const bool bShouldOpenInDefaultsMode = true;
	bool bNewlyCreated = true;
	RegisterApplicationModes(DMXLibrary, bShouldOpenInDefaultsMode, bNewlyCreated);
}

void FDMXEditor::CommonInitialization(UDMXLibrary* DMXLibrary)
{
	CreateDefaultCommands();
	CreateDefaultTabContents(DMXLibrary);
}

void FDMXEditor::InitalizeExtenders()
{
	FDMXEditorModule* DMXEditorModule = &FDMXEditorModule::Get();
	TSharedPtr<FExtender> MenuExtender = DMXEditorModule->GetMenuExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects());
	AddMenuExtender(MenuExtender);

	TSharedPtr<FExtender> ToolbarExtender = DMXEditorModule->GetToolBarExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects());
	AddToolbarExtender(ToolbarExtender);
}

void FDMXEditor::RegisterApplicationModes(UDMXLibrary* DMXLibrary, bool bShouldOpenInDefaultsMode, bool bNewlyCreated)
{
	// Only one for now
	FWorkflowCentricApplication::AddApplicationMode(
		FDMXEditorApplicationMode::DefaultsMode,
		MakeShared<FDMXEditorDefaultApplicationMode>(SharedThis(this)));
	FWorkflowCentricApplication::SetCurrentMode(FDMXEditorApplicationMode::DefaultsMode);
}

UDMXLibrary * FDMXEditor::GetDMXLibrary() const
{
	return GetEditableDMXLibrary();
}


void FDMXEditor::RegisterToolbarTab(const TSharedRef<class FTabManager>& InTabManager)
{
	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);
}

void FDMXEditor::CreateDefaultTabContents(UDMXLibrary* DMXLibrary)
{
	DMXLibraryEditorTab = CreateDMXLibraryEditorTab();
	FixtureTypeEditor = CreateFixtureTypeEditor();
	FixturePatchEditor = CreateFixturePatchEditor();
}

void FDMXEditor::CreateDefaultCommands()
{
	FDMXEditorCommands::Register();

	FDMXEditorModule& DMXEditorModule = FModuleManager::LoadModuleChecked<FDMXEditorModule>("DMXEditor");

	ToolkitCommands->MapAction(
		FDMXEditorCommands::Get().AddNewEntityFixtureType,
		FExecuteAction::CreateLambda([this]() { OnAddNewEntity(UDMXEntityFixtureType::StaticClass()); }),
		FCanExecuteAction::CreateLambda([this]()->bool { return CanAddNewEntity(UDMXEntityFixtureType::StaticClass()); })
	);
	ToolkitCommands->MapAction(
		FDMXEditorCommands::Get().AddNewEntityFixturePatch,
		FExecuteAction::CreateLambda([this]() { OnAddNewEntity(UDMXEntityFixturePatch::StaticClass()); }),
		FCanExecuteAction::CreateLambda([this]()->bool { return CanAddNewEntity(UDMXEntityFixturePatch::StaticClass()); })
	);
}

void FDMXEditor::OnAddNewEntity(TSubclassOf<UDMXEntity> InEntityClass)
{
	if (!InvokeEditorTabFromEntityType(InEntityClass))
	{
		return; 
	}

	// Create the new entity with a unique name
	FString BaseName;
	OnGetBaseNameForNewEntity.Broadcast(InEntityClass, BaseName);
	FString EntityName = FDMXEditorUtils::FindUniqueEntityName(GetDMXLibrary(), InEntityClass, BaseName);

	UDMXEntity* NewEntity = nullptr;
	if (FDMXEditorUtils::AddEntity(GetDMXLibrary(), EntityName, InEntityClass, &NewEntity))
	{
		OnSetupNewEntity.Broadcast(NewEntity);
		RenameNewlyAddedEntity(NewEntity, InEntityClass);
	}
	else
	{
		UE_LOG_DMXEDITOR(Error, TEXT("Add Entity error!"));
	}
}

bool FDMXEditor::InvokeEditorTabFromEntityType(TSubclassOf<UDMXEntity> InEntityClass)
{
	// Make sure we're in the right tab for the current type
	FName TargetTabId = NAME_None;
	if (InEntityClass->IsChildOf(UDMXEntityFixtureType::StaticClass()))
	{
		TargetTabId = FDMXEditorTabNames::DMXFixtureTypesEditor;
	}
	else if (InEntityClass->IsChildOf(UDMXEntityFixturePatch::StaticClass()))
	{
		TargetTabId = FDMXEditorTabNames::DMXFixturePatchEditor;
	}
	else
	{
		UE_LOG_DMXEDITOR(Error, TEXT("%S: Unimplemented Entity type. Can't set currect Tab."), __FUNCTION__);
	}

	if (!TargetTabId.IsNone())
	{
		FName CurrentTab = FGlobalTabmanager::Get()->GetActiveTab()->GetLayoutIdentifier().TabType;
		if (!CurrentTab.IsEqual(TargetTabId))
		{
			TabManager->TryInvokeTab(MoveTemp(TargetTabId));
		}
		
		return true;
	}

	return false;
}

bool FDMXEditor::CanAddNewEntity(TSubclassOf<UDMXEntity> InEntityClass) const
{
	return true;
}

bool FDMXEditor::NewEntity_IsVisibleForType(TSubclassOf<UDMXEntity> InEntityClass) const
{
	return true;
}

void FDMXEditor::RenameNewlyAddedEntity(UDMXEntity* InEntity, TSubclassOf<UDMXEntity> InEntityClass)
{
	TSharedPtr<SDMXEntityEditor> EntityEditor = GetEditorWidgetForEntityType(InEntityClass);
	if (!EntityEditor.IsValid())
	{
		return;
	}

	// if this check ever fails, something is really wrong!
	// How can an Entity be created without the button in the editor?!
	check(EntityEditor.IsValid());
	
	EntityEditor->RequestRenameOnNewEntity(InEntity, ESelectInfo::OnMouseClick);
}

TSharedPtr<SDMXEntityEditor> FDMXEditor::GetEditorWidgetForEntityType(TSubclassOf<UDMXEntity> InEntityClass) const
{
	TSharedPtr<SDMXEntityEditor> EntityEditor = nullptr;

	if (InEntityClass->IsChildOf(UDMXEntityFixtureType::StaticClass()))
	{
		return FixtureTypeEditor;
	}
	else if (InEntityClass->IsChildOf(UDMXEntityFixturePatch::StaticClass()))
	{
		return FixturePatchEditor;
	}
	else
	{
		UE_LOG_DMXEDITOR(Error, TEXT("%S not implemented for %s"), __FUNCTION__, *InEntityClass->GetFName().ToString());
	}

	return FixtureTypeEditor;
}

void FDMXEditor::SelectEntityInItsTypeTab(UDMXEntity* InEntity, ESelectInfo::Type InSelectionType /*= ESelectInfo::Type::Direct*/)
{
	check(InEntity != nullptr);

	if (!InvokeEditorTabFromEntityType(InEntity->GetClass()))
	{
		return;
	}

	if (TSharedPtr<SDMXEntityEditor> EntityEditor = GetEditorWidgetForEntityType(InEntity->GetClass()))
	{
		EntityEditor->SelectEntity(InEntity, InSelectionType);
	}
}

void FDMXEditor::SelectEntitiesInTypeTab(const TArray<UDMXEntity*>& InEntities, ESelectInfo::Type InSelectionType /*= ESelectInfo::Type::Direct*/)
{
	if (InEntities.Num() == 0 || InEntities[0] == nullptr)
	{
		return; 
	}

	if (!InvokeEditorTabFromEntityType(InEntities[0]->GetClass()))
	{
		return;
	}

	if (TSharedPtr<SDMXEntityEditor> EntityEditor = GetEditorWidgetForEntityType(InEntities[0]->GetClass()))
	{
		EntityEditor->SelectEntities(InEntities, InSelectionType);
	}
}

TArray<UDMXEntity*> FDMXEditor::GetSelectedEntitiesFromTypeTab(TSubclassOf<UDMXEntity> InEntityClass) const
{
	if (TSharedPtr<SDMXEntityEditor> EntityEditor = GetEditorWidgetForEntityType(InEntityClass))
	{
		return EntityEditor->GetSelectedEntities();
	}

	return TArray<UDMXEntity*>();
}

UDMXLibrary* FDMXEditor::GetEditableDMXLibrary() const
{
	return Cast<UDMXLibrary>(GetEditingObject());
}

TSharedRef<SDMXLibraryEditorTab> FDMXEditor::CreateDMXLibraryEditorTab()
{
	UDMXLibrary* DMXLibrary = GetEditableDMXLibrary();
	check(DMXLibrary);

	return SNew(SDMXLibraryEditorTab)
		.DMXLibrary(DMXLibrary)
		.DMXEditor(SharedThis(this));
}

TSharedRef<SDMXFixtureTypeEditor> FDMXEditor::CreateFixtureTypeEditor()
{
	return SNew(SDMXFixtureTypeEditor)
		.DMXEditor(SharedThis(this));
}

TSharedRef<SDMXFixturePatchEditor> FDMXEditor::CreateFixturePatchEditor()
{
	return SNew(SDMXFixturePatchEditor)
		.DMXEditor(SharedThis(this));
}

TSharedPtr<FDMXFixtureTypeSharedData> FDMXEditor::GetFixtureTypeSharedData() const
{
	return FixtureTypeSharedData;
}

const TSharedPtr<FDMXFixturePatchSharedData>& FDMXEditor::GetFixturePatchSharedData() const
{
	return FixturePatchSharedData;
}

#undef LOCTEXT_NAMESPACE
