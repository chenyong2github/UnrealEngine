// Copyright Epic Games, Inc. All Rights Reserved.

#include "TimeOfDayEditor.h"
#include "TimeOfDayEditorStyle.h"
#include "TimeOfDayEditorCommands.h"
#include "TimeOfDayActorDetails.h"
#include "STimeOfDaySettings.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "ToolMenus.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "LevelEditor.h"
#include "PropertyEditorModule.h"

#define LOCTEXT_NAMESPACE "TimeOfDayEditor"

static const FName TimeOfDayTabName("TimeOfDayEditor");
static const FName LevelEditorModuleName("LevelEditor");
static const FName PropertyEditorModuleName("PropertyEditor");
static const FName TimeOfDayActorClassName("TimeOfDayActor");

void FTimeOfDayEditorModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	
	FTimeOfDayEditorStyle::Initialize();
	FTimeOfDayEditorCommands::Register();
	
	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FTimeOfDayEditorCommands::Get().OpenTimeOfDayEditor,
		FExecuteAction::CreateRaw(this, &FTimeOfDayEditorModule::OnOpenTimeOfDayEditor),
		FCanExecuteAction());
	{
		const IWorkspaceMenuStructure& MenuStructure = WorkspaceMenu::GetMenuStructure();
		TSharedRef<FWorkspaceItem> LevelEditorGroup = MenuStructure.GetLevelEditorCategory();

		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(LevelEditorModuleName);
		
		auto RegisterTabSpawner = [this, LevelEditorGroup]()
		{
			FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(LevelEditorModuleName);
			TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();

			LevelEditorTabManager->RegisterTabSpawner(TimeOfDayTabName, FOnSpawnTab::CreateRaw(this, &FTimeOfDayEditorModule::CreateTimeOfDayTab))
				.SetDisplayName(LOCTEXT("TabTitle", "Time of Day"))
				.SetTooltipText(LOCTEXT("TooltipText", "Opens the Time of Day editor"))
				.SetGroup(LevelEditorGroup)
				.SetIcon(FSlateIcon(FTimeOfDayEditorStyle::GetStyleSetName(), "TimeOfDay.OpenTimeOfDayEditor"));

			RegisterMenus();
		};

		if (TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager())
		{
			RegisterTabSpawner();
		}
		else
		{
			LevelEditorModule.OnTabManagerChanged().AddLambda(RegisterTabSpawner);
		}
	}

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FTimeOfDayEditorModule::RegisterMenus));

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(PropertyEditorModuleName);
	PropertyModule.RegisterCustomClassLayout(TimeOfDayActorClassName, FOnGetDetailCustomizationInstance::CreateStatic(&FTimeOfDayActorDetails::MakeInstance));

	RegisterModulePropertySections();
}

void FTimeOfDayEditorModule::ShutdownModule()
{
	if (FSlateApplication::IsInitialized() && FModuleManager::Get().IsModuleLoaded(LevelEditorModuleName))
	{
		FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(LevelEditorModuleName);
		TSharedPtr<FTabManager> LevelEditorTabManager;
		if (LevelEditorModule)
		{
			LevelEditorTabManager = LevelEditorModule->GetLevelEditorTabManager();
			LevelEditorModule->OnTabManagerChanged().RemoveAll(this);
		}

		if (LevelEditorTabManager.IsValid())
		{
			LevelEditorTabManager->UnregisterTabSpawner(TimeOfDayTabName);
		}
	}

	if (FPropertyEditorModule* PropertyModule = FModuleManager::GetModulePtr<FPropertyEditorModule>(PropertyEditorModuleName))
	{
		PropertyModule->UnregisterCustomClassLayout(TimeOfDayActorClassName);
	}

	DeregisterModulePropertySections();

	UToolMenus::UnRegisterStartupCallback(this);

	UToolMenus::UnregisterOwner(this);

	FTimeOfDayEditorStyle::Shutdown();

	FTimeOfDayEditorCommands::Unregister();
}

void FTimeOfDayEditorModule::OnOpenTimeOfDayEditor()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(LevelEditorModuleName);

	if (TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager())
	{
		LevelEditorTabManager->TryInvokeTab(TimeOfDayTabName);
	}
}

TSharedPtr<FPropertySection> FTimeOfDayEditorModule::RegisterPropertySection(FPropertyEditorModule& PropertyModule, FName ClassName, FName SectionName, FText DisplayName)
{
	TSharedRef<FPropertySection> PropertySection = PropertyModule.FindOrCreateSection(ClassName, SectionName, DisplayName);
	RegisteredPropertySections.Add(ClassName, SectionName);
	return PropertySection;
}

void FTimeOfDayEditorModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);
	{
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.ModesToolBar");
		{
			FToolMenuSection& Section = ToolbarMenu->AddSection("TimeOfDay", TAttribute<FText>(), FToolMenuInsert("EditorModes", EToolMenuInsertType::After));
			{
				FToolMenuEntry& Entry = Section.AddEntry(
					FToolMenuEntry::InitToolBarButton(FTimeOfDayEditorCommands::Get().OpenTimeOfDayEditor));
				Entry.SetCommandList(PluginCommands);
			}
		}
	}
}

TSharedRef<SDockTab> FTimeOfDayEditorModule::CreateTimeOfDayTab(const FSpawnTabArgs& Args)
{
	return
		SNew(SDockTab)
		[
			SNew(STimeOfDaySettings)
		];
}

void FTimeOfDayEditorModule::RegisterModulePropertySections()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>(PropertyEditorModuleName);
	{
		TSharedPtr<FPropertySection> Section = RegisterPropertySection(PropertyModule, "TimeOfDayActor", "General", LOCTEXT("General", "General"));
		Section->AddCategory("General");
		Section->AddCategory("TimeOfDay");
		Section->AddCategory("RuntimeDayCycle");
		Section->AddCategory("BindingOverrides");
	}
}

void FTimeOfDayEditorModule::DeregisterModulePropertySections()
{
	FPropertyEditorModule* PropertyModule = FModuleManager::GetModulePtr<FPropertyEditorModule>(PropertyEditorModuleName);
	if (!PropertyModule)
	{
		return;
	}

	for (TMultiMap<FName, FName>::TIterator PropertySectionIterator = RegisteredPropertySections.CreateIterator(); PropertySectionIterator; ++PropertySectionIterator)
	{
		PropertyModule->RemoveSection(PropertySectionIterator->Key, PropertySectionIterator->Value);
		PropertySectionIterator.RemoveCurrent();
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FTimeOfDayEditorModule, TimeOfDayEditor)