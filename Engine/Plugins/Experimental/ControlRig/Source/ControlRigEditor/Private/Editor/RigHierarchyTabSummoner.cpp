// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigHierarchyTabSummoner.h"
#include "SRigBoneHierarchy.h"
#include "ControlRigEditorStyle.h"
#include "ControlRigEditor.h"

#define LOCTEXT_NAMESPACE "RigBoneHierarchyTabSummoner"

const FName FRigBoneHierarchyTabSummoner::TabID(TEXT("RigHierarchy"));

FRigBoneHierarchyTabSummoner::FRigBoneHierarchyTabSummoner(const TSharedRef<FControlRigEditor>& InControlRigEditor)
	: FWorkflowTabFactory(TabID, InControlRigEditor)
	, ControlRigEditor(InControlRigEditor)
{
	TabLabel = LOCTEXT("RigBoneHierarchyTabLabel", "Bone Hierarchy");
	TabIcon = FSlateIcon(FControlRigEditorStyle::Get().GetStyleSetName(), "ControlRig.TabIcon");

	bIsSingleton = true;

	ViewMenuDescription = LOCTEXT("RigBoneHierarchy_ViewMenu_Desc", "Bone Hierarchy");
	ViewMenuTooltip = LOCTEXT("RigBoneHierarchy_ViewMenu_ToolTip", "Show the Rig Bone Hierarchy tab");
}

TSharedRef<SWidget> FRigBoneHierarchyTabSummoner::CreateTabBody(const FWorkflowTabSpawnInfo& Info) const
{
	return SNew(SRigBoneHierarchy, ControlRigEditor.Pin().ToSharedRef());
}

#undef LOCTEXT_NAMESPACE 
