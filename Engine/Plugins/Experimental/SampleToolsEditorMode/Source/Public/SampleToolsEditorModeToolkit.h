// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/BaseToolkit.h"
#include "InteractiveTool.h"
#include "SampleToolsEditorMode.h"

class IDetailsView;

/**
 * This FModeToolkit just creates a basic UI panel that allows various InteractiveTools to
 * be initialized, and a DetailsView used to show properties of the active Tool.
 */
class FSampleToolsEditorModeToolkit : public FModeToolkit
{
public:
	FSampleToolsEditorModeToolkit();
	
	// FModeToolkit interface 
	virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost) override;

	// IToolkit interface 
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual class FEdMode* GetEditorMode() const override;
	virtual TSharedPtr<class SWidget> GetInlineContent() const override { return ToolkitWidget; }

	virtual FSampleToolsEditorMode* GetToolsEditorMode() const;

private:

	TSharedPtr<SWidget> ToolkitWidget;
	TSharedPtr<IDetailsView> DetailsView;

	// these functions just forward calls to the ToolsContext / ToolManager

	bool CanStartTool(const FString& ToolTypeIdentifier);
	bool CanAcceptActiveTool();
	bool CanCancelActiveTool();
	bool CanCompleteActiveTool();

	FReply StartTool(const FString& ToolTypeIdentifier);
	FReply EndTool(EToolShutdownType ShutdownType);
};
