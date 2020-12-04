// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FractureTool.h"

#include "FractureToolSelectors.generated.h"


namespace GeometryCollection
{
	enum class ESelectionMode : uint8;
}

class FFractureEditorModeToolkit;


UCLASS(DisplayName = "Select All", Category = "FractureTools")
class UFractureToolSelectAll : public UFractureActionTool
{
public:
	GENERATED_BODY()

	UFractureToolSelectAll(const FObjectInitializer& ObjInit) : Super(ObjInit) {}

	// UFractureActionTool Interface
	virtual FText GetDisplayText() const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetToolIcon() const override;
	virtual void RegisterUICommand(FFractureEditorCommands* BindingContext) override;
	virtual void Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit) override;

protected:
	void SelectByMode(FFractureEditorModeToolkit* InToolkit, GeometryCollection::ESelectionMode SelectionMode);
};


UCLASS(DisplayName = "Select None", Category = "FractureTools")
class UFractureToolSelectNone : public UFractureToolSelectAll
{
public:
	GENERATED_BODY()

	UFractureToolSelectNone(const FObjectInitializer& ObjInit) : Super(ObjInit) {}

	// UFractureActionTool Interface
	virtual FText GetDisplayText() const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetToolIcon() const override;
	virtual void RegisterUICommand(FFractureEditorCommands* BindingContext) override;
	virtual void Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit) override;
};


UCLASS(DisplayName = "Select Neighbors", Category = "FractureTools")
class UFractureToolSelectNeighbors : public UFractureToolSelectAll
{
public:
	GENERATED_BODY()

	UFractureToolSelectNeighbors(const FObjectInitializer& ObjInit) : Super(ObjInit) {}

	// UFractureActionTool Interface
	virtual FText GetDisplayText() const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetToolIcon() const override;
	virtual void RegisterUICommand(FFractureEditorCommands* BindingContext) override;
	virtual void Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit) override;
};


UCLASS(DisplayName = "Select Siblings", Category = "FractureTools")
class UFractureToolSelectSiblings : public UFractureToolSelectAll
{
public:
	GENERATED_BODY()

	UFractureToolSelectSiblings(const FObjectInitializer& ObjInit) : Super(ObjInit) {}

	// UFractureActionTool Interface
	virtual FText GetDisplayText() const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetToolIcon() const override;
	virtual void RegisterUICommand(FFractureEditorCommands* BindingContext) override;
	virtual void Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit) override;
};


UCLASS(DisplayName = "Select Neighbors", Category = "FractureTools")
class UFractureToolSelectAllInCluster : public UFractureToolSelectAll
{
public:
	GENERATED_BODY()

	UFractureToolSelectAllInCluster(const FObjectInitializer& ObjInit) : Super(ObjInit) {}

	// UFractureActionTool Interface
	virtual FText GetDisplayText() const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetToolIcon() const override;
	virtual void RegisterUICommand(FFractureEditorCommands* BindingContext) override;
	virtual void Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit) override;
};


UCLASS(DisplayName = "Select Neighbors", Category = "FractureTools")
class UFractureToolSelectInvert : public UFractureToolSelectAll
{
public:
	GENERATED_BODY()

	UFractureToolSelectInvert(const FObjectInitializer& ObjInit) : Super(ObjInit) {}

	// UFractureActionTool Interface
	virtual FText GetDisplayText() const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetToolIcon() const override;
	virtual void RegisterUICommand(FFractureEditorCommands* BindingContext) override;
	virtual void Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit) override;
};
