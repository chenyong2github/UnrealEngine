// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "UI/DatasmithUIManager.h"

#include "UI/DatasmithUICommands.h"
#include "UI/DatasmithStyle.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "DatasmithImporter"

TUniquePtr<FDatasmithUIManager> FDatasmithUIManager::Instance = NULL;

void FDatasmithUIManager::Initialize()
{
	if (!Instance.IsValid())
	{
		Instance = MakeUnique<FDatasmithUIManager>();

		FDatasmithStyle::Initialize();
		FDatasmithUICommands::Register();

		Instance->ExtendToolbar();
	}
}

void FDatasmithUIManager::Shutdown()
{
	if (Instance.IsValid())
	{
		FDatasmithUICommands::Unregister();
		FDatasmithStyle::Shutdown();

		Instance.Reset();
	}
}

FDatasmithUIManager& FDatasmithUIManager::Get()
{
	check(Instance);
	return *Instance;
}

void FDatasmithUIManager::ExtendToolbar()
{
	DatasmithActions = MakeShareable(new FUICommandList);

	// Action to repeat the last selected command
	DatasmithActions->MapAction(FDatasmithUICommands::Get().RepeatLastImport,
		FExecuteAction::CreateLambda([this]() { DatasmithActions->ExecuteAction(GetLastSelectedCommand()); })
	);

	// Add a Datasmith toolbar section after the settings section of the level editor
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");

	TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);
	ToolbarExtender->AddToolBarExtension("Settings", EExtensionHook::After, DatasmithActions, FToolBarExtensionDelegate::CreateRaw(this, &FDatasmithUIManager::FillToolbar));

	LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(ToolbarExtender);
}

TSharedRef<SWidget> GenerateDatasmithMenuContent(const TSharedPtr<FUICommandList>& InCommandList)
{
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, InCommandList);

	// Fill the Datasmith importers section in the drop-down menu with the registered importers
	MenuBuilder.BeginSection("DatasmithImporters", LOCTEXT("DatasmithImportersSection", "Datasmith Importers"));
	{
		for (const TSharedPtr<FUICommandInfo>& Command : FDatasmithUICommands::Get().MenuCommands)
		{
			MenuBuilder.AddMenuEntry(Command);
		}
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void FDatasmithUIManager::FillToolbar(FToolBarBuilder& ToolbarBuilder)
{
	ToolbarBuilder.BeginSection("Datasmith");
	{
		// Add a button to repeat the last selected import command
		// The button's text and visual content is fetched from the last selected menu entry
		ToolbarBuilder.AddToolBarButton(
			FDatasmithUICommands::Get().RepeatLastImport,
			NAME_None,
			TAttribute< FText >::Create(TAttribute< FText >::FGetter::CreateLambda([this]() { return GetLastSelectedCommand()->GetLabel(); })),
			TAttribute< FText >::Create(TAttribute< FText >::FGetter::CreateLambda([this]() { return GetLastSelectedCommand()->GetDescription(); })),
			TAttribute< FSlateIcon >::Create(TAttribute< FSlateIcon >::FGetter::CreateLambda([this]() { return GetLastSelectedCommand()->GetIcon(); }))
		);
	}
	ToolbarBuilder.EndSection();
}

TSharedPtr<FUICommandInfo> FDatasmithUIManager::AddMenuEntry(const FString& CommandName, const FText& Caption, const FText& Description, const FString& IconResourcePath, FExecuteAction ExecuteAction, UClass* FactoryClass)
{
	// Setup the icon and command. They are binded together through the CommandName
	FDatasmithStyle::SetIcon(CommandName, IconResourcePath);
	TSharedPtr<FUICommandInfo> Command = FDatasmithUICommands::AddMenuCommand(CommandName, Caption, Description);

	FactoryClassToUICommandMap.Add(FactoryClass, Command);

	// The menu entry will have a check mark next to it if it was the last selected command
	DatasmithActions->MapAction(Command,
		ExecuteAction,
		FCanExecuteAction(),
		FIsActionChecked::CreateLambda([=]() { return LastSelectedCommand == Command; })
	);
	return Command;
}

void FDatasmithUIManager::RemoveMenuEntry(const TSharedPtr<FUICommandInfo>& Command)
{
	if (LastSelectedCommand == Command)
	{
		LastSelectedCommand.Reset();
	}

	UClass* const* Class = FactoryClassToUICommandMap.FindKey(Command);
	if (Class)
	{
		FactoryClassToUICommandMap.Remove(*Class);
	}

	DatasmithActions->UnmapAction(Command);
	FDatasmithUICommands::RemoveMenuCommand(Command);
}

const TSharedRef<FUICommandInfo> FDatasmithUIManager::GetLastSelectedCommand()
{
	if (!LastSelectedCommand.IsValid())
	{
		// Initialize the last selected command with the first command, which is always the UDatasmith import since it's the first to get loaded and registered
		LastSelectedCommand = FDatasmithUICommands::Get().MenuCommands[0];
	}
	return LastSelectedCommand.ToSharedRef();
}

void FDatasmithUIManager::SetLastFactoryUsed(UClass* Class)
{
	// Internally, we only need to know the command associated with the last factory used
	LastSelectedCommand = FactoryClassToUICommandMap[Class];
}

#undef LOCTEXT_NAMESPACE
