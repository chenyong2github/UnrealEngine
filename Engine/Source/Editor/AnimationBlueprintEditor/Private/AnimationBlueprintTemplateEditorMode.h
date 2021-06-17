// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WorkflowOrientedApp/WorkflowTabManager.h"
#include "AnimationBlueprintEditor.h"
#include "Animation/AnimBlueprint.h"
#include "BlueprintEditorModes.h"

class IPersonaPreviewScene;

class FAnimationBlueprintTemplateEditorMode : public FBlueprintEditorApplicationMode
{
protected:
	// Set of spawnable tabs in persona mode (@TODO: Multiple lists!)
	FWorkflowAllowedTabSet TabFactories;

public:
	FAnimationBlueprintTemplateEditorMode(const TSharedRef<FAnimationBlueprintEditor>& InAnimationBlueprintEditor);

	// FApplicationMode interface
	virtual void RegisterTabFactories(TSharedPtr<FTabManager> InTabManager) override;
	// End of FApplicationMode interface

private:
	TWeakObjectPtr<class UAnimBlueprint> AnimBlueprintPtr;
};
