// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"

class UVEDITOR_API FUVEditorCommands : public TCommands<FUVEditorCommands>
{
public:

	FUVEditorCommands();

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr<FUICommandInfo> OpenUVEditor;
	TSharedPtr<FUICommandInfo> ApplyChanges;

	TSharedPtr<FUICommandInfo> BeginSelectTool;
	TSharedPtr<FUICommandInfo> BeginTransformTool;
	TSharedPtr<FUICommandInfo> BeginLayoutTool;

	TSharedPtr<FUICommandInfo> AcceptActiveTool;
	TSharedPtr<FUICommandInfo> CancelActiveTool;
};