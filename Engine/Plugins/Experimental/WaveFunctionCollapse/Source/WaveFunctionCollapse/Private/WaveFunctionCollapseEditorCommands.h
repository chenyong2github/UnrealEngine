// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorStyleSet.h"
#include "Framework/Commands/Commands.h"

class FWaveFunctionCollapseEditorCommands : public TCommands<FWaveFunctionCollapseEditorCommands>
{
public:

	FWaveFunctionCollapseEditorCommands()
		: TCommands<FWaveFunctionCollapseEditorCommands>
		(
			TEXT("WaveFunctionCollapse"),
			NSLOCTEXT("Contexts", "WaveFunctionCollapse", "WaveFunctionCollapse Plugin"),
			NAME_None,
			FEditorStyle::GetStyleSetName()
		) 
		{}

	virtual void RegisterCommands() override;

public:

	TSharedPtr<FUICommandInfo> WaveFunctionCollapseWidget;
};