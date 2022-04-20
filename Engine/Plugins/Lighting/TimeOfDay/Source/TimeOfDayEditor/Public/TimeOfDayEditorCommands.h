// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "TimeOfDayEditorStyle.h"

class FTimeOfDayEditorCommands : public TCommands<FTimeOfDayEditorCommands>
{
public:

	FTimeOfDayEditorCommands()
		: TCommands<FTimeOfDayEditorCommands>(TEXT("TimeOfDay"), NSLOCTEXT("Contexts", "TimeOfDay", "TimeOfDay Plugin"), NAME_None, FTimeOfDayEditorStyle::GetStyleSetName())
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr<FUICommandInfo> OpenTimeOfDayEditor;
};
