// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigCurveContainerTabSummoner.h"
#include "SRigCurveContainer.h"
#include "ControlRigEditorStyle.h"
#include "ControlRigEditor.h"

#define LOCTEXT_NAMESPACE "RigCurveContainerTabSummoner"

const FName FRigCurveContainerTabSummoner::TabID(TEXT("RigCurveContainer"));

FRigCurveContainerTabSummoner::FRigCurveContainerTabSummoner(const TSharedRef<FControlRigEditor>& InControlRigEditor)
	: FWorkflowTabFactory(TabID, InControlRigEditor)
	, ControlRigEditor(InControlRigEditor)
{
	TabLabel = LOCTEXT("RigCurveContainerTabLabel", "Curve Container");
	TabIcon = FSlateIcon(FControlRigEditorStyle::Get().GetStyleSetName(), "ControlRig.TabIcon");

	bIsSingleton = true;

	ViewMenuDescription = LOCTEXT("RigCurveContainer_ViewMenu_Desc", "Curve Container");
	ViewMenuTooltip = LOCTEXT("RigCurveContainer_ViewMenu_ToolTip", "Show the Rig Curve Container tab");
}

TSharedRef<SWidget> FRigCurveContainerTabSummoner::CreateTabBody(const FWorkflowTabSpawnInfo& Info) const
{
	return SNew(SRigCurveContainer, ControlRigEditor.Pin().ToSharedRef());
}

#undef LOCTEXT_NAMESPACE 
