// Copyright Epic Games, Inc. All Rights Reserved.

#include "RCBehaviourConditionalModel.h"

#include "Behaviour/Builtin/Conditional/RCBehaviourConditional.h"
#include "Controller/RCController.h"
#include "Modules/ModuleManager.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "PropertyEditorModule.h"
#include "RCVirtualProperty.h"
#include "SRCBehaviourConditional.h"
#include "UI/Action/Conditional/RCActionConditionalModel.h"
#include "UI/Action/SRCActionPanel.h"
#include "UI/Action/SRCActionPanelList.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "FRCBehaviourConditionalModel"

FRCBehaviourConditionalModel::FRCBehaviourConditionalModel(URCBehaviourConditional* ConditionalBehaviour)
	: FRCBehaviourModel(ConditionalBehaviour)
	, ConditionalBehaviourWeakPtr(ConditionalBehaviour)
{
	FPropertyRowGeneratorArgs Args;
	Args.bShouldShowHiddenProperties = true;
	PropertyRowGenerator = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreatePropertyRowGenerator(Args);

	CreateComparandInputField();
}

URCAction* FRCBehaviourConditionalModel::AddAction(const TSharedRef<const FRemoteControlField> InRemoteControlField)
{
	URCAction* NewAction = nullptr;

	if (URCBehaviourConditional* ConditionalBehaviour = Cast<URCBehaviourConditional>(GetBehaviour()))
	{
		NewAction = ConditionalBehaviour->AddAction(InRemoteControlField, Condition, Comparand);

		OnActionAdded(NewAction);
	}

	return NewAction;
}

void FRCBehaviourConditionalModel::CreateComparandInputField()
{
	if (URCBehaviourConditional* ConditionalBehaviour = Cast<URCBehaviourConditional>(GetBehaviour()))
	{
		if (URCController* Controller = ConditionalBehaviour->ControllerWeakPtr.Get())
		{
			// Virtual Property for the Comparand
			Comparand = NewObject<URCVirtualPropertySelfContainer>(ConditionalBehaviour);
			Comparand->DuplicateProperty(FName("Comparand"), Controller->GetProperty());

			// UI widget (via Property Generator)
			PropertyRowGenerator->SetStructure(Comparand->CreateStructOnScope());
			for (const TSharedRef<IDetailTreeNode>& CategoryNode : PropertyRowGenerator->GetRootTreeNodes())
			{
				TArray<TSharedRef<IDetailTreeNode>> Children;
				CategoryNode->GetChildren(Children);
				for (const TSharedRef<IDetailTreeNode>& Child : Children)
				{
					DetailTreeNodeWeakPtr = Child;
					break;
				}
			}
		}
	}
}

TSharedRef<SWidget> FRCBehaviourConditionalModel::GetBehaviourDetailsWidget()
{
	return SAssignNew(BehaviourDetailsWidget, SRCBehaviourConditional, SharedThis(this));
}

void FRCBehaviourConditionalModel::OnActionAdded(URCAction* Action)
{
	if (URCBehaviourConditional* ConditionalBehaviour = Cast<URCBehaviourConditional>(GetBehaviour()))
	{
		ConditionalBehaviour->OnActionAdded(Action, Condition, Comparand);

		// Create a new Comparand for the user (the previous input field is already associated with the newly created action)
		CreateComparandInputField();

		BehaviourDetailsWidget->RefreshPropertyWidget();
	}
}

TSharedRef<SWidget> FRCBehaviourConditionalModel::GetComparandFieldWidget() const
{
	if (const TSharedPtr<IDetailTreeNode> DetailTreeNode = DetailTreeNodeWeakPtr.Pin())
	{
		const FNodeWidgets NodeWidgets = DetailTreeNode->CreateNodeWidgets();

		const TSharedRef<SHorizontalBox> FieldWidget = SNew(SHorizontalBox);

		if (NodeWidgets.ValueWidget)
		{
			FieldWidget->AddSlot()
				.Padding(FMargin(3.0f, 2.0f))
				.HAlign(HAlign_Right)
				.AutoWidth()
				[
					NodeWidgets.ValueWidget.ToSharedRef()
				];
		}
		else if (NodeWidgets.WholeRowWidget)
		{
			FieldWidget->AddSlot()
				.Padding(FMargin(3.0f, 2.0f))
				.AutoWidth()
				[
					NodeWidgets.WholeRowWidget.ToSharedRef()
				];
		}

		return SNew(SHorizontalBox)
			.Clipping(EWidgetClipping::OnDemand)
			// Property Value Widget
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				FieldWidget
			];
	}

	return SNullWidget::NullWidget;
}

void FRCBehaviourConditionalModel::SetSelectedConditionType(const ERCBehaviourConditionType InCondition)
{
	Condition = InCondition;
}

TSharedPtr<SRCLogicPanelListBase> FRCBehaviourConditionalModel::GetActionsListWidget(TSharedRef<SRCActionPanel> InActionPanel)
{
	return SNew(SRCActionPanelList<FRCActionConditionalModel>, InActionPanel, SharedThis(this));
}

#undef LOCTEXT_NAMESPACE