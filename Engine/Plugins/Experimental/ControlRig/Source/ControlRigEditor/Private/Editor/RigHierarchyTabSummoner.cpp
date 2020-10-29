// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigHierarchyTabSummoner.h"
#include "SRigHierarchy.h"
#include "ControlRigEditorStyle.h"
#include "ControlRigEditor.h"

#define LOCTEXT_NAMESPACE "RigHierarchyTabSummoner"

const FName FRigHierarchyTabSummoner::TabID(TEXT("RigHierarchy"));

FRigHierarchyTabSummoner::FRigHierarchyTabSummoner(const TSharedRef<FControlRigEditor>& InControlRigEditor)
	: FWorkflowTabFactory(TabID, InControlRigEditor)
	, ControlRigEditor(InControlRigEditor)
{
	TabLabel = LOCTEXT("RigHierarchyTabLabel", "Rig Hierarchy");
	TabIcon = FSlateIcon(FControlRigEditorStyle::Get().GetStyleSetName(), "ControlRig.TabIcon");

	ViewMenuDescription = LOCTEXT("RigHierarchy_ViewMenu_Desc", "Rig Hierarchy");
	ViewMenuTooltip = LOCTEXT("RigHierarchy_ViewMenu_ToolTip", "Show the Rig Hierarchy tab");
}

FTabSpawnerEntry& FRigHierarchyTabSummoner::RegisterTabSpawner(TSharedRef<FTabManager> InTabManager, const FApplicationMode* CurrentApplicationMode) const
{
	FTabSpawnerEntry& SpawnerEntry = FWorkflowTabFactory::RegisterTabSpawner(InTabManager, CurrentApplicationMode);

	SpawnerEntry.SetReuseTabMethod(FOnFindTabToReuse::CreateLambda([](const FTabId& InTabId) ->TSharedPtr<SDockTab> {
	
		return TSharedPtr<SDockTab>();

	}));

	return SpawnerEntry;
}

TSharedRef<SWidget> FRigHierarchyTabSummoner::CreateTabBody(const FWorkflowTabSpawnInfo& Info) const
{
	return SNew(SRigHierarchy, ControlRigEditor.Pin().ToSharedRef());
}

#undef LOCTEXT_NAMESPACE 
