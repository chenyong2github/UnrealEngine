// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigValidationTabSummoner.h"
#include "SRigHierarchy.h"
#include "ControlRigEditorStyle.h"
#include "ControlRigEditor.h"
#include "ControlRigBlueprint.h"
#include "SControlRigValidationWidget.h"

#define LOCTEXT_NAMESPACE "RigValidationTabSummoner"

const FName FRigValidationTabSummoner::TabID(TEXT("RigValidation"));

FRigValidationTabSummoner::FRigValidationTabSummoner(const TSharedRef<FControlRigEditor>& InControlRigEditor)
	: FWorkflowTabFactory(TabID, InControlRigEditor)
	, WeakControlRigEditor(InControlRigEditor)
{
	TabLabel = LOCTEXT("RigValidationTabLabel", "Rig Validation");
	TabIcon = FSlateIcon(FControlRigEditorStyle::Get().GetStyleSetName(), "ControlRig.TabIcon");

	bIsSingleton = true;

	ViewMenuDescription = LOCTEXT("RigValidation_ViewMenu_Desc", "Rig Validation");
	ViewMenuTooltip = LOCTEXT("RigValidation_ViewMenu_ToolTip", "Show the Rig Validation tab");
}

TSharedRef<SWidget> FRigValidationTabSummoner::CreateTabBody(const FWorkflowTabSpawnInfo& Info) const
{
	ensure(WeakControlRigEditor.IsValid());

	UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(WeakControlRigEditor.Pin()->GetBlueprintObj());
	check(RigBlueprint);
	
	UControlRigValidator* Validator = RigBlueprint->Validator;
	check(Validator);

	TSharedRef<SControlRigValidationWidget> ValidationWidget = SNew(SControlRigValidationWidget, Validator);
	Validator->SetControlRig(Cast<UControlRig>(RigBlueprint->GetObjectBeingDebugged()));
	return ValidationWidget;
}

#undef LOCTEXT_NAMESPACE 
