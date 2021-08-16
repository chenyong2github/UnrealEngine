// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FractureTool.h"

#include "FractureToolEditing.generated.h"

UCLASS(DisplayName = "Delete Branch", Category = "FractureTools")
class UFractureToolDeleteBranch : public UFractureActionTool
{
public:
	GENERATED_BODY()

	UFractureToolDeleteBranch(const FObjectInitializer& ObjInit) : Super(ObjInit) {}

	// UFractureActionTool Interface
	virtual FText GetDisplayText() const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetToolIcon() const override;
	virtual void RegisterUICommand(FFractureEditorCommands* BindingContext) override;
	virtual void Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit) override;
};


UCLASS(DisplayName = "Hide", Category = "FractureTools")
class UFractureToolHide : public UFractureActionTool
{
public:
	GENERATED_BODY()

	UFractureToolHide(const FObjectInitializer& ObjInit) : Super(ObjInit) {}

	// UFractureActionTool Interface
	virtual FText GetDisplayText() const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetToolIcon() const override;
	virtual void RegisterUICommand(FFractureEditorCommands* BindingContext) override;
	virtual void Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit) override;
};


UCLASS(DisplayName = "Unhide", Category = "FractureTools")
class UFractureToolUnhide : public UFractureActionTool
{
public:
	GENERATED_BODY()

	UFractureToolUnhide(const FObjectInitializer& ObjInit) : Super(ObjInit) {}

	// UFractureActionTool Interface
	virtual FText GetDisplayText() const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetToolIcon() const override;
	virtual void RegisterUICommand(FFractureEditorCommands* BindingContext) override;
	virtual void Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit) override;
};



UCLASS(DisplayName = "Validate", Category = "FractureTools")
class UFractureToolValidate : public UFractureActionTool
{
public:
	GENERATED_BODY()

	UFractureToolValidate(const FObjectInitializer& ObjInit) : Super(ObjInit) {}

	// UFractureActionTool Interface
	virtual FText GetDisplayText() const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetToolIcon() const override;
	virtual void RegisterUICommand(FFractureEditorCommands* BindingContext) override;
	virtual void Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit) override;
};
