// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "EditorModes.h"
#include "Toolkits/BaseToolkit.h"
#include "EditorModeManager.h"
#include "SControlRigEditModeTools.h"
#include "ControlRigEditMode.h"

class FControlRigEditModeToolkit : public FModeToolkit
{
public:

	FControlRigEditModeToolkit(FControlRigEditMode& InEditMode)
		: EditMode(InEditMode)
	{
		SAssignNew(ModeTools, SControlRigEditModeTools, EditMode, EditMode.GetWorld());
	}

	/** IToolkit interface */
	virtual FName GetToolkitFName() const override { return FName("AnimationMode"); }
	virtual FText GetBaseToolkitName() const override { return NSLOCTEXT("AnimationModeToolkit", "DisplayName", "Animation"); }
	virtual class FEdMode* GetEditorMode() const override { return &EditMode; }
	virtual TSharedPtr<class SWidget> GetInlineContent() const override { return ModeTools; }
	virtual bool ProcessCommandBindings(const FKeyEvent& InKeyEvent) const override
	{
		if (EditMode.GetCommandBindings() && EditMode.GetCommandBindings()->ProcessCommandBindings(InKeyEvent))
		{
			return true;
		}
		return false;
	}
	virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost) override;

	/** Mode Toolbar Palettes **/
	virtual void GetToolPaletteNames(TArray<FName>& InPaletteName) const override;
	virtual FText GetToolPaletteDisplayName(FName PaletteName) const override;
	virtual void BuildToolPalette(FName PaletteName, class FToolBarBuilder& ToolbarBuilder) override;

	/** Modes Panel Header Information **/
	virtual FText GetActiveToolDisplayName() const override;
	virtual FText GetActiveToolMessage() const override;
	virtual void OnToolPaletteChanged(FName PaletteName) override;

	virtual void RegisterTabSpawners(const TSharedRef<class FTabManager>& TabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<class FTabManager>& TabManager) override;

private:
	/** The edit mode we are bound to */
	FControlRigEditMode& EditMode;

	/** The tools widget */
	TSharedPtr<SControlRigEditModeTools> ModeTools;
};
