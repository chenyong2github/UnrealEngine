// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/BaseToolkit.h"
#include "InteractiveTool.h"
#include "ModelingToolsEditorMode.h"

#include "Widgets/SBoxPanel.h"

class IDetailsView;
class SButton;
class STextBlock;

class FModelingToolsEditorModeToolkit : public FModeToolkit
{
public:

	FModelingToolsEditorModeToolkit();
	
	/** FModeToolkit interface */
	virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost) override;

	/** IToolkit interface */
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual class FEdMode* GetEditorMode() const override;
	virtual TSharedPtr<class SWidget> GetInlineContent() const override { return ToolkitWidget; }

	virtual FModelingToolsEditorMode* GetToolsEditorMode() const;
	virtual UEdModeInteractiveToolsContext* GetToolsContext() const;

	// set/clear notification message area
	virtual void PostNotification(const FText& Message);
	virtual void ClearNotification();

private:

	TSharedPtr<SWidget> ToolkitWidget;
	TSharedPtr<IDetailsView> DetailsView;

	TSharedPtr<STextBlock> ToolHeaderLabel;
	TSharedPtr<STextBlock> ToolMessageArea;
	TSharedPtr<SButton> AcceptButton;
	TSharedPtr<SButton> CancelButton;
	TSharedPtr<SButton> CompletedButton;
};
