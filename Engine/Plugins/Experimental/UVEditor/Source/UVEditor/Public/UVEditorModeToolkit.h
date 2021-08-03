// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Toolkits/BaseToolkit.h"
#include "InteractiveTool.h"

class SBorder;
class STextBlock;

/**
 * The UV editor mode toolkit is responsible for the panel on the side in the UV editor
 * that shows mode and tool properties. Tool buttons would go in Init().
 */
class FUVEditorModeToolkit : public FModeToolkit
{
public:
	FUVEditorModeToolkit();
	~FUVEditorModeToolkit();

	/** FModeToolkit interface */
	virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost, TWeakObjectPtr<UEdMode> InOwningMode) override;

	/** IToolkit interface */
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual TSharedPtr<class SWidget> GetInlineContent() const override { return ToolkitWidget; }


	virtual void SetBackgroundSettings(UObject* InSettingsObject);

protected:
	// The mode's entire toolbox, which gets returned by GetInlineContent()
	TSharedPtr<SWidget> ToolkitWidget;

	// The portion of the toolbox composed of buttons that activate tools
	TSharedPtr<SBorder> ToolButtonsContainer;

	// A place for tools to write out any warnings
	TSharedPtr<STextBlock> ToolWarningArea;

	// A container for the tool settings that is populated by the DetailsView managed
	// in FModeToolkit
	TSharedPtr<SBorder> ToolDetailsContainer;

	// A container for the editor settings
	TSharedPtr<SBorder> EditorDetailsContainer;

	// A container for the background settings
	TSharedPtr<SBorder> BackgroundDetailsContainer;
	TSharedPtr<IDetailsView> BackgroundDetailsView;

	// A place for tools to write out any instructions
	TSharedPtr<STextBlock> ToolMessageArea;
};