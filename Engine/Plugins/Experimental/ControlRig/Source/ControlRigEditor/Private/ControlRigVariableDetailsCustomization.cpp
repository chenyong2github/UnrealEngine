// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ControlRigVariableDetailsCustomization.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ControlRig.h"
#include "DetailLayoutBuilder.h"
#include "BlueprintEditorModule.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SCheckBox.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "ControlRigBlueprint.h"
#include "ControlRigController.h"

#define LOCTEXT_NAMESPACE "ControlRigVariableDetailsCustomization"

TSharedPtr<IDetailCustomization> FControlRigVariableDetailsCustomization::MakeInstance(TSharedPtr<IBlueprintEditor> InBlueprintEditor)
{
	const TArray<UObject*>* Objects = (InBlueprintEditor.IsValid() ? InBlueprintEditor->GetObjectsCurrentlyBeingEdited() : nullptr);
	if (Objects && Objects->Num() == 1)
	{
		if (UBlueprint* Blueprint = Cast<UBlueprint>((*Objects)[0]))
		{
			if (Blueprint->ParentClass->IsChildOf(UControlRig::StaticClass()))
			{
				return MakeShareable(new FControlRigVariableDetailsCustomization(InBlueprintEditor, Blueprint));
			}
		}
	}

	return nullptr;
}

void FControlRigVariableDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	DetailLayout.GetObjectsBeingCustomized(ObjectsBeingCustomized);
	if (ObjectsBeingCustomized.Num() > 0)
	{
		TWeakObjectPtr<UProperty> PropertyBeingCustomized(Cast<UProperty>(ObjectsBeingCustomized[0].Get()));
		if (PropertyBeingCustomized.IsValid())
		{
			const FText AnimationInputText = LOCTEXT("AnimationInput", "Animation Input");
			const FText AnimationOutputText = LOCTEXT("AnimationOutput", "Animation Output");
			const FText AnimationInputTooltipText = LOCTEXT("AnimationInputTooltip", "Whether this variable acts as an input to this animation controller.\nSelecting this allow it to be exposed as an input pin on Evaluation nodes.");
			const FText AnimationOutputTooltipText = LOCTEXT("AnimationOutputTooltip", "Whether this variable acts as an output from this animation controller.\nSelecting this will add a pin to the Animation Output node.");
			
			DetailLayout.EditCategory("Variable")
				.AddCustomRow(AnimationInputText)
				.NameContent()
				[
					SNew(STextBlock)
					.Visibility(IsAnimationFlagEnabled(PropertyBeingCustomized) ? EVisibility::Visible : EVisibility::Hidden)
					.Font(DetailLayout.GetDetailFont())
					.Text(AnimationOutputText)
					.ToolTipText(AnimationOutputTooltipText)
				]
				.ValueContent()
				[
					SNew(SCheckBox)
					.Visibility(IsAnimationFlagEnabled(PropertyBeingCustomized) ? EVisibility::Visible : EVisibility::Hidden)
					.IsChecked_Raw(this, &FControlRigVariableDetailsCustomization::IsAnimationOutputChecked, PropertyBeingCustomized)
					.OnCheckStateChanged_Raw(this, &FControlRigVariableDetailsCustomization::HandleAnimationOutputCheckStateChanged, PropertyBeingCustomized)
					.ToolTipText(AnimationOutputTooltipText)
				];

			DetailLayout.EditCategory("Variable")
				.AddCustomRow(AnimationOutputText)
				.NameContent()
				[
					SNew(STextBlock)
					.Visibility(IsAnimationFlagEnabled(PropertyBeingCustomized) ? EVisibility::Visible : EVisibility::Hidden)
					.Font(DetailLayout.GetDetailFont())
					.Text(AnimationInputText)
					.ToolTipText(AnimationInputTooltipText)
				]
				.ValueContent()
				[
					SNew(SCheckBox)
					.Visibility(IsAnimationFlagEnabled(PropertyBeingCustomized) ? EVisibility::Visible : EVisibility::Hidden)
					.IsChecked_Raw(this, &FControlRigVariableDetailsCustomization::IsAnimationInputChecked, PropertyBeingCustomized)
					.OnCheckStateChanged_Raw(this, &FControlRigVariableDetailsCustomization::HandleAnimationInputCheckStateChanged, PropertyBeingCustomized)
					.ToolTipText(AnimationInputTooltipText)
				];
		}
	}
}

bool FControlRigVariableDetailsCustomization::IsAnimationFlagEnabled(TWeakObjectPtr<UProperty> PropertyBeingCustomized) const
{
	if (PropertyBeingCustomized.IsValid() && BlueprintPtr.IsValid())
	{
		UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(BlueprintPtr.Get());
		if (RigBlueprint)
		{
			if (RigBlueprint->Model)
			{
				UProperty* PropertyPtrBeingCustomized = PropertyBeingCustomized.Get();
				const FControlRigModelNode* Node = RigBlueprint->Model->FindNode(PropertyPtrBeingCustomized->GetFName());
				if (Node)
				{
					return Node->IsParameter();
				}
			}
		}
	}
	return false;
}

ECheckBoxState FControlRigVariableDetailsCustomization::IsAnimationOutputChecked(TWeakObjectPtr<UProperty> PropertyBeingCustomized) const
{
	if (PropertyBeingCustomized.IsValid() && BlueprintPtr.IsValid())
	{
		UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(BlueprintPtr.Get());
		if (RigBlueprint)
		{
			if (RigBlueprint->Model)
			{
				UProperty* PropertyPtrBeingCustomized = PropertyBeingCustomized.Get();
				const FControlRigModelNode* Node = RigBlueprint->Model->FindNode(PropertyPtrBeingCustomized->GetFName());
				if (Node)
				{
					if (Node->ParameterType == EControlRigModelParameterType::Output)
					{
						return ECheckBoxState::Checked;
					}
				}
			}
		}
	}

	return ECheckBoxState::Unchecked;
}

void FControlRigVariableDetailsCustomization::HandleAnimationOutputCheckStateChanged(ECheckBoxState CheckBoxState, TWeakObjectPtr<UProperty> PropertyBeingCustomized)
{
	if (PropertyBeingCustomized.IsValid() && BlueprintPtr.IsValid())
	{
		UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(BlueprintPtr.Get());
		if (RigBlueprint)
		{
			if (RigBlueprint->ModelController)
			{
				UProperty* PropertyPtrBeingCustomized = PropertyBeingCustomized.Get();
				RigBlueprint->ModelController->SetParameterType(PropertyPtrBeingCustomized->GetFName(), CheckBoxState == ECheckBoxState::Checked ? EControlRigModelParameterType::Output : EControlRigModelParameterType::Hidden);
				FBlueprintEditorUtils::ReconstructAllNodes(BlueprintPtr.Get());
			}
		}
	}
}

ECheckBoxState FControlRigVariableDetailsCustomization::IsAnimationInputChecked(TWeakObjectPtr<UProperty> PropertyBeingCustomized) const
{
	if (PropertyBeingCustomized.IsValid() && BlueprintPtr.IsValid())
	{
		UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(BlueprintPtr.Get());
		if (RigBlueprint)
		{
			if (RigBlueprint->Model)
			{
				UProperty* PropertyPtrBeingCustomized = PropertyBeingCustomized.Get();
				const FControlRigModelNode* Node = RigBlueprint->Model->FindNode(PropertyPtrBeingCustomized->GetFName());
				if (Node)
				{
					if (Node->ParameterType == EControlRigModelParameterType::Input)
					{
						return ECheckBoxState::Checked;
					}
				}
			}
		}
	}

	return ECheckBoxState::Unchecked;
}

void FControlRigVariableDetailsCustomization::HandleAnimationInputCheckStateChanged(ECheckBoxState CheckBoxState, TWeakObjectPtr<UProperty> PropertyBeingCustomized)
{
	if (PropertyBeingCustomized.IsValid() && BlueprintPtr.IsValid())
	{
		UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(BlueprintPtr.Get());
		if (RigBlueprint)
		{
			if (RigBlueprint->ModelController)
			{
				UProperty* PropertyPtrBeingCustomized = PropertyBeingCustomized.Get();
				RigBlueprint->ModelController->SetParameterType(PropertyPtrBeingCustomized->GetFName(), CheckBoxState == ECheckBoxState::Checked ? EControlRigModelParameterType::Input : EControlRigModelParameterType::Hidden);
				FBlueprintEditorUtils::ReconstructAllNodes(BlueprintPtr.Get());
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
