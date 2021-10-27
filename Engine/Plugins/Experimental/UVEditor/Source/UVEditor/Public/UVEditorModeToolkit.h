// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "InteractiveTool.h"
#include "Toolkits/BaseToolkit.h"

class SBorder;
class STextBlock;
class UInteractiveToolPropertySet;

/**
 * The UV editor mode toolkit is responsible for the panel on the side in the UV editor
 * that shows mode and tool properties. Tool buttons would go in Init().
 */
class FUVEditorModeToolkit : public FModeToolkit
{
public:
	FUVEditorModeToolkit();
	~FUVEditorModeToolkit();

	/** Creates a menu where the displayed UV Channel can be changed for each asset */
	virtual TSharedRef<SWidget> CreateChannelMenu();
	
	/** Creates a widget where the background visualization can be changed. */
	virtual TSharedRef<SWidget> CreateBackgroundSettingsWidget();

	// FModeToolkit
	virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost, TWeakObjectPtr<UEdMode> InOwningMode) override;
	virtual FText GetActiveToolDisplayName() const override { return ActiveToolName; }

	// IToolkit
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual TSharedPtr<class SWidget> GetInlineContent() const override { return ToolkitWidget; }

protected:
	// FModeToolkit
	virtual void OnToolStarted(UInteractiveToolManager* Manager, UInteractiveTool* Tool) override;
	virtual void OnToolEnded(UInteractiveToolManager* Manager, UInteractiveTool* Tool) override;


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

	/** Contains the widget container for the Accept/Cancel buttons for tools. */
	TSharedPtr<SWidget> ViewportOverlayWidget;

	FText ActiveToolName;

	void UpdateActiveToolProperties();
	void InvalidateCachedDetailPanelState(UObject* ChangedObject);

};