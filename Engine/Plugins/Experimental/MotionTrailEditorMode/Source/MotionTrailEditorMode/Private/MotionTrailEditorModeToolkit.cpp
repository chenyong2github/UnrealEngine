// Copyright Epic Games, Inc. All Rights Reserved.

#include "MotionTrailEditorModeToolkit.h"
#include "MotionTrailEditorMode.h"
#include "Engine/Selection.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "EditorModeManager.h"

#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "IDetailRootObjectCustomization.h"

#define LOCTEXT_NAMESPACE "FMotionTrailEditorModeEdModeToolkit"

FMotionTrailEditorModeToolkit::FMotionTrailEditorModeToolkit()
{
}

void FMotionTrailEditorModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost)
{
	FModeToolkit::Init(InitToolkitHost);
}

FName FMotionTrailEditorModeToolkit::GetToolkitFName() const
{
	return FName("MotionTrailEditorMode");
}

FText FMotionTrailEditorModeToolkit::GetBaseToolkitName() const
{
	return NSLOCTEXT("MotionTrailEditorModeToolkit", "DisplayName", "Motion Trail Editor Tool");
}

class UEdMode* FMotionTrailEditorModeToolkit::GetScriptableEditorMode() const
{
	return GLevelEditorModeTools().GetActiveScriptableMode("MotionTrailEditorMode");
}

#undef LOCTEXT_NAMESPACE
