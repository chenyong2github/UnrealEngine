// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WorkflowOrientedApp/WorkflowTabManager.h"
#include "WidgetBlueprintEditor.h"
#include "BlueprintEditorModes.h"

class UWidgetBlueprint;

/////////////////////////////////////////////////////
// FWidgetBlueprintApplicationMode

class UMGEDITOR_API FWidgetBlueprintApplicationMode : public FBlueprintEditorApplicationMode
{
public:
	FWidgetBlueprintApplicationMode(TSharedPtr<class FWidgetBlueprintEditor> InWidgetEditor, FName InModeName);

public:
	TSharedPtr<class FWidgetBlueprintEditor> GetBlueprintEditor() const;
	UWidgetBlueprint* GetBlueprint() const;

protected:
	TWeakPtr<class FWidgetBlueprintEditor> MyWidgetBlueprintEditor;

	// Set of spawnable tabs in the mode
	FWorkflowAllowedTabSet TabFactories;
};
