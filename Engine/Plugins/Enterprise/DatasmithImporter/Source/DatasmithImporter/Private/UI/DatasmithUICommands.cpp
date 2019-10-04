// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "UI/DatasmithUICommands.h"
#include "UI/DatasmithStyle.h"
#include "Styling/ISlateStyle.h"

#define LOCTEXT_NAMESPACE "DatasmithImporter"

FDatasmithUICommands::FDatasmithUICommands()
: TCommands<FDatasmithUICommands>(FDatasmithStyle::GetContextName(), LOCTEXT("DatasmithImporter", "Datasmith Importer"), NAME_None, FDatasmithStyle::GetStyleSetName())
{
}

void FDatasmithUICommands::RegisterCommands()
{
	UI_COMMAND(RepeatLastImport, "Import", "Repeat last import", EUserInterfaceActionType::Button, FInputChord());
}

TSharedPtr< FUICommandInfo > FDatasmithUICommands::AddMenuCommand(const FString& CommandName, const FText& Caption, const FText& Description)
{
	// Get the non-const instance so that we can add a new command to the list
	FDatasmithUICommands& DatasmithCommands = *(Instance.Pin());

	TSharedPtr<FUICommandInfo>& Command = DatasmithCommands.MenuCommands.AddDefaulted_GetRef();
	const FString DotString = FString(TEXT(".")) + CommandName;

	FUICommandInfo::MakeCommandInfo(
		DatasmithCommands.AsShared(),
		Command,
		*CommandName,
		Caption,
		Description,
		FSlateIcon(DatasmithCommands.GetStyleSetName(), ISlateStyle::Join(DatasmithCommands.GetContextName(), TCHAR_TO_ANSI(*DotString))),
		EUserInterfaceActionType::Check,
		FInputChord()
		);

	return Command;
}

void FDatasmithUICommands::RemoveMenuCommand(const TSharedPtr< FUICommandInfo >& Command)
{
	// Get the non-const instance so that we can remove the command from the list
	FDatasmithUICommands& DatasmithCommands = *(Instance.Pin());

	DatasmithCommands.MenuCommands.RemoveSingle(Command);
}

#undef LOCTEXT_NAMESPACE
