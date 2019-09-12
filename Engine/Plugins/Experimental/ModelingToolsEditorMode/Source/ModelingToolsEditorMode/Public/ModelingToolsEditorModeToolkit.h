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

private:

	TSharedPtr<SWidget> ToolkitWidget;
	TSharedPtr<IDetailsView> DetailsView;

	TSharedRef<SButton> MakeToolButton(const FText& ButtonLabel, const FString& ToolIdentifier);
	SVerticalBox::FSlot& MakeToolButtonSlotV(const FText& ButtonLabel, const FString& ToolIdentifier);
	SHorizontalBox::FSlot& MakeToolButtonSlotH(const FText& ButtonLabel, const FString& ToolIdentifier);

	SVerticalBox::FSlot& MakeSetToolLabelV(const FText& LabelText);

	TSharedPtr<STextBlock> ToolHeaderLabel;
	TSharedPtr<SButton> AcceptButton;
	TSharedPtr<SButton> CancelButton;
	TSharedPtr<SButton> CompletedButton;
};
