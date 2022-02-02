// Copyright Epic Games, Inc. All Rights Reserved.

#include "LumenVisualizationMenuCommands.h"
#include "LumenVisualizationData.h"
#include "Containers/UnrealString.h"
#include "Framework/Commands/InputChord.h"
#include "Materials/Material.h"
#include "Internationalization/Text.h"
#include "Templates/Function.h"
#include "EditorStyleSet.h"
#include "EditorViewportClient.h"

#define LOCTEXT_NAMESPACE "LumenVisualizationMenuCommands"

FLumenVisualizationMenuCommands::FLumenVisualizationMenuCommands()
	: TCommands<FLumenVisualizationMenuCommands>
	(
		TEXT("LumenVisualizationMenu"), // Context name for fast lookup
		NSLOCTEXT("Contexts", "LumenVisualizationMenu", "Lumen"), // Localized context name for displaying
		NAME_None, // Parent context name.  
		FEditorStyle::GetStyleSetName() // Icon Style Set
	),
	CommandMap()
{
}

void FLumenVisualizationMenuCommands::BuildCommandMap()
{
	const FLumenVisualizationData& VisualizationData = GetLumenVisualizationData();
	const FLumenVisualizationData::TModeMap& ModeMap = VisualizationData.GetModeMap();

	CommandMap.Empty();
	for (FLumenVisualizationData::TModeMap::TConstIterator It = ModeMap.CreateConstIterator(); It; ++It)
	{
		const FLumenVisualizationData::FModeRecord& Entry = It.Value();
		FLumenVisualizationRecord& Record = CommandMap.Add(Entry.ModeName, FLumenVisualizationRecord());
		Record.Name = Entry.ModeName;
		Record.Command = FUICommandInfoDecl(
			this->AsShared(),
			Entry.ModeName,
			Entry.ModeText,
			Entry.ModeDesc)
			.UserInterfaceType(EUserInterfaceActionType::RadioButton)
			.DefaultChord(FInputChord()
		);

		switch (Entry.ModeType)
		{
		default:
		case FLumenVisualizationData::FModeType::Overview:
			Record.Type = FLumenVisualizationType::Overview;
			break;

		case FLumenVisualizationData::FModeType::Standard:
			Record.Type = FLumenVisualizationType::Standard;
			break;
		}
	}
}

void FLumenVisualizationMenuCommands::BuildVisualisationSubMenu(FMenuBuilder& Menu)
{
	const FLumenVisualizationMenuCommands& Commands = FLumenVisualizationMenuCommands::Get();
	if (Commands.IsPopulated())
	{
		Menu.BeginSection("LevelViewportLumenVisualizationMode", LOCTEXT("LumenVisualizationHeader", "Lumen Visualization Mode"));

		if (Commands.AddCommandTypeToMenu(Menu, FLumenVisualizationType::Overview))
		{
			Menu.AddMenuSeparator();
		}

		Commands.AddCommandTypeToMenu(Menu, FLumenVisualizationType::Standard);

		Menu.EndSection();
	}
}

bool FLumenVisualizationMenuCommands::AddCommandTypeToMenu(FMenuBuilder& Menu, const FLumenVisualizationType Type) const
{
	bool bAddedCommands = false;

	const TLumenVisualizationModeCommandMap& Commands = CommandMap;
	for (TCommandConstIterator It = CreateCommandConstIterator(); It; ++It)
	{
		const FLumenVisualizationRecord& Record = It.Value();
		if (Record.Type == Type)
		{
			Menu.AddMenuEntry(Record.Command, NAME_None, Record.Command->GetLabel());
			bAddedCommands = true;
		}
	}

	return bAddedCommands;
}

FLumenVisualizationMenuCommands::TCommandConstIterator FLumenVisualizationMenuCommands::CreateCommandConstIterator() const
{
	return CommandMap.CreateConstIterator();
}

void FLumenVisualizationMenuCommands::RegisterCommands()
{
	BuildCommandMap();
}

void FLumenVisualizationMenuCommands::BindCommands(FUICommandList& CommandList, const TSharedPtr<FEditorViewportClient>& Client) const
{
	// Map Lumen visualization mode actions
	for (FLumenVisualizationMenuCommands::TCommandConstIterator It = FLumenVisualizationMenuCommands::Get().CreateCommandConstIterator(); It; ++It)
	{
		const FLumenVisualizationMenuCommands::FLumenVisualizationRecord& Record = It.Value();
		CommandList.MapAction(
			Record.Command,
			FExecuteAction::CreateStatic<const TSharedPtr<FEditorViewportClient>&>(&FLumenVisualizationMenuCommands::ChangeLumenVisualizationMode, Client, Record.Name),
			FCanExecuteAction(),
			FIsActionChecked::CreateStatic<const TSharedPtr<FEditorViewportClient>&>(&FLumenVisualizationMenuCommands::IsLumenVisualizationModeSelected, Client, Record.Name)
		);
	}
}

void FLumenVisualizationMenuCommands::ChangeLumenVisualizationMode(const TSharedPtr<FEditorViewportClient>& Client, FName InName)
{
	check(Client.IsValid());
	Client->ChangeLumenVisualizationMode(InName);
}

bool FLumenVisualizationMenuCommands::IsLumenVisualizationModeSelected(const TSharedPtr<FEditorViewportClient>& Client, FName InName)
{
	check(Client.IsValid());
	return Client->IsLumenVisualizationModeSelected(InName);
}

#undef LOCTEXT_NAMESPACE
